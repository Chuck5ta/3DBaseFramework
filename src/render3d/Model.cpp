#include "render3d/Model.h"

#include "core/Log.h"
#include "gfx/VulkanContext.h"

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <stb_image.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>

namespace render3d {

glm::mat4 NodeTransform::matrix() const
{
    return glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation) *
           glm::scale(glm::mat4(1.0f), scale);
}

namespace {

const char* resultText(cgltf_result r)
{
    switch (r) {
    case cgltf_result_file_not_found: return "file not found";
    case cgltf_result_io_error: return "couldn't read the file";
    case cgltf_result_out_of_memory: return "out of memory";
    case cgltf_result_data_too_short:
    case cgltf_result_unknown_format:
    case cgltf_result_invalid_json:
    case cgltf_result_invalid_gltf:
    case cgltf_result_invalid_options: return "not a valid glTF 2.0 file";
    case cgltf_result_legacy_gltf: return "glTF 1.0 isn't supported (re-export as glTF 2.0)";
    default: return "couldn't be loaded";
    }
}

std::string folderOf(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

std::string nameOr(const char* name, const char* fallback, size_t index)
{
    return name && *name ? std::string(name) : std::string(fallback) + " " + std::to_string(index);
}

// Reads an accessor as floats (any component type, normalised or sparse).
bool readFloats(const cgltf_accessor* acc, size_t components, std::vector<float>& out)
{
    if (!acc || cgltf_num_components(acc->type) != components)
        return false;
    out.resize(acc->count * components);
    return cgltf_accessor_unpack_floats(acc, out.data(), out.size()) == out.size();
}

const cgltf_accessor* findAttribute(const cgltf_primitive& p, cgltf_attribute_type type, int index = 0)
{
    for (cgltf_size i = 0; i < p.attributes_count; ++i)
        if (p.attributes[i].type == type && p.attributes[i].index == index)
            return p.attributes[i].data;
    return nullptr;
}

// Image bytes (PNG/JPG...) from a buffer view, a data: URI or a file next to the model.
bool imageBytes(const cgltf_image& img, const cgltf_options& options, const std::string& folder,
                std::vector<uint8_t>& out, std::string& error)
{
    if (img.buffer_view) {
        const cgltf_buffer_view& bv = *img.buffer_view;
        if (!bv.buffer || !bv.buffer->data) {
            error = "its data is missing";
            return false;
        }
        const uint8_t* p = static_cast<const uint8_t*>(bv.buffer->data) + bv.offset;
        out.assign(p, p + bv.size);
        return true;
    }
    if (!img.uri) {
        error = "it has no data";
        return false;
    }
    if (std::strncmp(img.uri, "data:", 5) == 0) {
        const char* comma = std::strchr(img.uri, ',');
        if (!comma || !std::strstr(img.uri, ";base64,")) {
            error = "unsupported data URI";
            return false;
        }
        const size_t len = std::strlen(comma + 1);
        size_t size = len / 4 * 3;
        if (len >= 1 && comma[len] == '=')
            --size;
        if (len >= 2 && comma[len - 1] == '=')
            --size;
        void* bytes = nullptr;
        if (cgltf_load_buffer_base64(&options, size, comma + 1, &bytes) != cgltf_result_success) {
            error = "bad base64 data";
            return false;
        }
        out.assign(static_cast<uint8_t*>(bytes), static_cast<uint8_t*>(bytes) + size);
        options.memory.free_func ? options.memory.free_func(options.memory.user_data, bytes) : std::free(bytes);
        return true;
    }
    std::string uri = img.uri; // "my%20texture.png" -> "my texture.png"
    uri.resize(cgltf_decode_uri(uri.data()));
    const std::string path = folder + uri;
    size_t size = 0;
    void* bytes = SDL_LoadFile(path.c_str(), &size);
    if (!bytes) {
        error = "can't read " + path;
        return false;
    }
    out.assign(static_cast<uint8_t*>(bytes), static_cast<uint8_t*>(bytes) + size);
    SDL_free(bytes);
    return true;
}

struct Loader {
    const cgltf_data& gltf;
    const cgltf_options& options;
    std::string folder;
    ModelData& out;
    std::set<std::string> warnedOnce;
    std::map<const cgltf_node*, int> nodeIndex; // glTF node -> index in out.nodes

    void warn(const std::string& message)
    {
        if (warnedOnce.insert(message).second)
            out.warnings.push_back(message);
    }

    template <class T>
    static int indexOf(const T* item, const T* first)
    {
        return item ? int(item - first) : -1;
    }

    void loadImages()
    {
        for (cgltf_size i = 0; i < gltf.images_count; ++i) {
            const cgltf_image& img = gltf.images[i];
            ModelImage mi;
            mi.name = nameOr(img.name ? img.name : img.uri && std::strncmp(img.uri, "data:", 5) != 0 ? img.uri : nullptr,
                             "image", i);
            std::vector<uint8_t> bytes;
            std::string error;
            if (!imageBytes(img, options, folder, bytes, error)) {
                warn("Image '" + mi.name + "' skipped: " + error);
            } else {
                int w = 0, h = 0, n = 0;
                stbi_uc* px = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &n, STBI_rgb_alpha);
                if (!px) {
                    warn("Image '" + mi.name + "' skipped: " + stbi_failure_reason() +
                         " (PNG and JPG work; KTX2/WebP don't yet)");
                } else {
                    mi.width = uint32_t(w);
                    mi.height = uint32_t(h);
                    mi.rgba.assign(px, px + size_t(w) * size_t(h) * 4);
                    stbi_image_free(px);
                }
            }
            out.images.push_back(std::move(mi));
        }
    }

    void loadTextures()
    {
        for (cgltf_size i = 0; i < gltf.textures_count; ++i) {
            const cgltf_texture& t = gltf.textures[i];
            ModelTexture mt;
            mt.image = indexOf(t.image, gltf.images);
            if (mt.image < 0)
                warn("Texture " + std::to_string(i) + " has no PNG/JPG image (KTX2/WebP aren't supported yet)");
            if (const cgltf_sampler* s = t.sampler) {
                if (s->mag_filter == cgltf_filter_type_nearest)
                    mt.filter = VK_FILTER_NEAREST;
                mt.wrapU = addressMode(s->wrap_s);
                mt.wrapV = addressMode(s->wrap_t);
            }
            out.textures.push_back(mt);
        }
    }

    static VkSamplerAddressMode addressMode(cgltf_wrap_mode mode)
    {
        switch (mode) {
        case cgltf_wrap_mode_clamp_to_edge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case cgltf_wrap_mode_mirrored_repeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }
    }

    int textureFor(const cgltf_texture_view& view, const std::string& material, const char* what)
    {
        if (!view.texture)
            return -1;
        if (view.texcoord != 0)
            warn("Material '" + material + "': the " + what + " texture uses UV map " +
                 std::to_string(view.texcoord) + "; only the first UV map is used");
        if (view.has_transform)
            warn("Material '" + material + "': texture offset/scale/rotation (KHR_texture_transform) is ignored");
        return indexOf(view.texture, gltf.textures);
    }

    void loadMaterials()
    {
        for (cgltf_size i = 0; i < gltf.materials_count; ++i) {
            const cgltf_material& m = gltf.materials[i];
            ModelMaterial mm;
            mm.name = nameOr(m.name, "material", i);
            if (m.has_pbr_metallic_roughness) {
                const cgltf_pbr_metallic_roughness& pbr = m.pbr_metallic_roughness;
                mm.baseColor = glm::make_vec4(pbr.base_color_factor);
                mm.baseColorTexture = textureFor(pbr.base_color_texture, mm.name, "base colour");
                mm.metallic = pbr.metallic_factor;
                mm.roughness = pbr.roughness_factor;
                if (pbr.metallic_roughness_texture.texture) {
                    // Not sampled yet (that comes with PBR materials): shade as a
                    // non-metal of medium roughness rather than as solid metal.
                    mm.metallic = 0.0f;
                    mm.roughness = std::min(mm.roughness, 0.6f);
                    warn("Material '" + mm.name + "': metallic/roughness textures aren't used yet");
                }
            } else if (m.has_pbr_specular_glossiness) {
                const cgltf_pbr_specular_glossiness& sg = m.pbr_specular_glossiness;
                mm.baseColor = glm::make_vec4(sg.diffuse_factor);
                mm.baseColorTexture = textureFor(sg.diffuse_texture, mm.name, "diffuse");
                mm.metallic = 0.0f;
                mm.roughness = 1.0f - sg.glossiness_factor;
            } else {
                mm.metallic = 0.0f;
            }
            mm.emissive = glm::make_vec3(m.emissive_factor) *
                          (m.has_emissive_strength ? m.emissive_strength.emissive_strength : 1.0f);
            mm.emissiveTexture = textureFor(m.emissive_texture, mm.name, "emission");
            if (m.normal_texture.texture)
                warn("Material '" + mm.name + "': normal maps aren't used yet");
            mm.alphaMode = m.alpha_mode == cgltf_alpha_mode_mask    ? AlphaMode::Mask
                           : m.alpha_mode == cgltf_alpha_mode_blend ? AlphaMode::Blend
                                                                    : AlphaMode::Opaque;
            mm.alphaCutoff = m.alpha_cutoff;
            mm.doubleSided = m.double_sided;
            mm.unlit = m.unlit;
            out.materials.push_back(mm);
        }
    }

    // Converts one primitive to an indexed triangle list in out.vertices/out.indices.
    bool loadPrimitive(const cgltf_primitive& p, const std::string& meshName, ModelPrimitive& prim)
    {
        if (p.type != cgltf_primitive_type_triangles && p.type != cgltf_primitive_type_triangle_strip &&
            p.type != cgltf_primitive_type_triangle_fan) {
            warn("Mesh '" + meshName + "': points and lines are skipped (only triangles are drawn)");
            return false;
        }
        if (p.targets_count > 0)
            warn("Mesh '" + meshName + "': shape keys (morph targets) aren't supported yet; the base shape is used");

        std::vector<float> pos, nrm, uv, col;
        const cgltf_accessor* posAcc = findAttribute(p, cgltf_attribute_type_position);
        if (!readFloats(posAcc, 3, pos)) {
            warn("Mesh '" + meshName + "': a part without positions was skipped");
            return false;
        }
        const size_t count = posAcc->count;
        const cgltf_accessor* nAcc = findAttribute(p, cgltf_attribute_type_normal);
        const bool haveNormals = nAcc && nAcc->count == count && readFloats(nAcc, 3, nrm);
        const cgltf_accessor* uvAcc = findAttribute(p, cgltf_attribute_type_texcoord);
        const bool haveUV = uvAcc && uvAcc->count == count && readFloats(uvAcc, 2, uv);
        const cgltf_accessor* cAcc = findAttribute(p, cgltf_attribute_type_color);
        size_t colorComponents = cAcc ? cgltf_num_components(cAcc->type) : 0;
        const bool haveColor = cAcc && cAcc->count == count && (colorComponents == 3 || colorComponents == 4) &&
                               readFloats(cAcc, colorComponents, col);

        // The corners in order, then as a triangle list.
        std::vector<uint32_t> order;
        if (p.indices) {
            order.resize(p.indices->count);
            for (size_t i = 0; i < order.size(); ++i) {
                order[i] = uint32_t(cgltf_accessor_read_index(p.indices, i));
                if (order[i] >= count) {
                    warn("Mesh '" + meshName + "': a part with broken indices was skipped");
                    return false;
                }
            }
        } else {
            order.resize(count);
            for (size_t i = 0; i < count; ++i)
                order[i] = uint32_t(i);
        }
        std::vector<uint32_t> tris;
        if (p.type == cgltf_primitive_type_triangles) {
            tris.assign(order.begin(), order.begin() + ptrdiff_t(order.size() / 3 * 3));
        } else if (p.type == cgltf_primitive_type_triangle_strip) {
            for (size_t i = 0; i + 2 < order.size(); ++i) { // every other one flips to keep the winding
                const size_t b = i + 1 + (i % 2), c = i + 2 - (i % 2);
                tris.insert(tris.end(), {order[i], order[b], order[c]});
            }
        } else {
            for (size_t i = 1; i + 1 < order.size(); ++i)
                tris.insert(tris.end(), {order[i], order[i + 1], order[0]});
        }
        if (tris.empty())
            return false;

        const auto vertex = [&](uint32_t i) {
            Vertex v{};
            v.position = glm::make_vec3(&pos[size_t(i) * 3]);
            v.normal = haveNormals ? glm::make_vec3(&nrm[size_t(i) * 3]) : glm::vec3(0.0f, 1.0f, 0.0f);
            v.uv = haveUV ? glm::make_vec2(&uv[size_t(i) * 2]) : glm::vec2(0.0f);
            v.color = glm::vec4(1.0f);
            if (haveColor)
                for (size_t c = 0; c < colorComponents; ++c)
                    v.color[int(c)] = col[size_t(i) * colorComponents + c];
            return v;
        };

        prim.firstIndex = uint32_t(out.indices.size());
        prim.vertexOffset = int32_t(out.vertices.size());
        if (haveNormals) {
            for (size_t i = 0; i < count; ++i)
                out.vertices.push_back(vertex(uint32_t(i)));
            out.indices.insert(out.indices.end(), tris.begin(), tris.end());
        } else {
            // No normals: glTF says to shade flat, so every triangle gets its own corners.
            for (size_t t = 0; t < tris.size(); t += 3) {
                Vertex a = vertex(tris[t]), b = vertex(tris[t + 1]), c = vertex(tris[t + 2]);
                const glm::vec3 n = glm::cross(b.position - a.position, c.position - a.position);
                const float len = glm::length(n);
                a.normal = b.normal = c.normal = len > 0.0f ? n / len : glm::vec3(0.0f, 1.0f, 0.0f);
                const uint32_t base = uint32_t(out.vertices.size()) - uint32_t(prim.vertexOffset);
                out.vertices.insert(out.vertices.end(), {a, b, c});
                out.indices.insert(out.indices.end(), {base, base + 1, base + 2});
            }
        }
        prim.indexCount = uint32_t(out.indices.size()) - prim.firstIndex;

        prim.boundsMin = glm::vec3(std::numeric_limits<float>::max());
        prim.boundsMax = glm::vec3(-std::numeric_limits<float>::max());
        for (size_t i = 0; i < count; ++i) {
            const glm::vec3 v = glm::make_vec3(&pos[i * 3]);
            prim.boundsMin = glm::min(prim.boundsMin, v);
            prim.boundsMax = glm::max(prim.boundsMax, v);
        }
        prim.material = indexOf(p.material, gltf.materials);
        return true;
    }

    void loadMeshes()
    {
        for (cgltf_size i = 0; i < gltf.meshes_count; ++i) {
            const cgltf_mesh& m = gltf.meshes[i];
            ModelMesh mm;
            mm.name = nameOr(m.name, "mesh", i);
            for (cgltf_size j = 0; j < m.primitives_count; ++j) {
                ModelPrimitive prim;
                if (loadPrimitive(m.primitives[j], mm.name, prim))
                    mm.primitives.push_back(prim);
            }
            out.meshes.push_back(std::move(mm));
        }
    }

    void addNode(const cgltf_node& n, int parent)
    {
        ModelNode node;
        node.name = nameOr(n.name, "node", size_t(&n - gltf.nodes));
        node.parent = parent;
        node.mesh = indexOf(n.mesh, gltf.meshes);
        if (n.skin)
            warn("Node '" + node.name + "': skinned (armature) deformation isn't supported yet; drawn in its rest "
                 "pose. For rigid parts, parent the objects to bones instead (Ctrl+P > Bone)");
        if (n.has_matrix) {
            glm::vec3 skew;
            glm::vec4 perspective;
            glm::decompose(glm::make_mat4(n.matrix), node.transform.scale, node.transform.rotation,
                           node.transform.translation, skew, perspective);
        } else {
            if (n.has_translation)
                node.transform.translation = glm::make_vec3(n.translation);
            if (n.has_rotation) // glTF stores x, y, z, w; glm::quat takes w first
                node.transform.rotation = glm::normalize(glm::quat(n.rotation[3], n.rotation[0], n.rotation[1], n.rotation[2]));
            if (n.has_scale)
                node.transform.scale = glm::make_vec3(n.scale);
        }
        const int index = int(out.nodes.size());
        nodeIndex[&n] = index;
        out.nodes.push_back(std::move(node));
        for (cgltf_size i = 0; i < n.children_count; ++i)
            addNode(*n.children[i], index);
    }

    void loadNodes()
    {
        const cgltf_scene* scene = gltf.scene ? gltf.scene : (gltf.scenes_count ? &gltf.scenes[0] : nullptr);
        if (gltf.scenes_count > 1)
            warn("The file has " + std::to_string(gltf.scenes_count) + " scenes; only the default one is loaded");
        if (scene) {
            for (cgltf_size i = 0; i < scene->nodes_count; ++i)
                addNode(*scene->nodes[i], -1);
        } else {
            for (cgltf_size i = 0; i < gltf.nodes_count; ++i)
                if (!gltf.nodes[i].parent)
                    addNode(gltf.nodes[i], -1);
        }
        if (out.nodes.empty()) { // a file with meshes but no nodes: show every mesh
            for (size_t i = 0; i < out.meshes.size(); ++i) {
                ModelNode node;
                node.name = out.meshes[i].name;
                node.mesh = int(i);
                out.nodes.push_back(node);
            }
        }
    }

    void loadAnimations()
    {
        for (cgltf_size a = 0; a < gltf.animations_count; ++a) {
            const cgltf_animation& anim = gltf.animations[a];
            ModelAnimation ma;
            ma.name = nameOr(anim.name, "animation", a);
            for (cgltf_size c = 0; c < anim.channels_count; ++c) {
                const cgltf_animation_channel& ch = anim.channels[c];
                if (!ch.target_node || !ch.sampler || !ch.sampler->input || !ch.sampler->output)
                    continue;
                AnimationChannel mc;
                switch (ch.target_path) {
                case cgltf_animation_path_type_translation: mc.path = AnimationPath::Translation; break;
                case cgltf_animation_path_type_rotation: mc.path = AnimationPath::Rotation; break;
                case cgltf_animation_path_type_scale: mc.path = AnimationPath::Scale; break;
                default:
                    warn("Animation '" + ma.name + "': shape key (morph target) animation isn't supported; skipped");
                    continue;
                }
                const auto found = nodeIndex.find(ch.target_node);
                if (found == nodeIndex.end())
                    continue; // a node outside the loaded scene
                mc.node = found->second;
                switch (ch.sampler->interpolation) {
                case cgltf_interpolation_type_step: mc.interpolation = AnimationInterpolation::Step; break;
                case cgltf_interpolation_type_cubic_spline: mc.interpolation = AnimationInterpolation::CubicSpline; break;
                default: mc.interpolation = AnimationInterpolation::Linear; break;
                }
                const cgltf_accessor& in = *ch.sampler->input;
                const cgltf_accessor& outAcc = *ch.sampler->output;
                const cgltf_size perKey = mc.interpolation == AnimationInterpolation::CubicSpline ? 3 : 1;
                if (in.count == 0 || outAcc.count != in.count * perKey) {
                    warn("Animation '" + ma.name + "': a channel with mismatched keyframes was skipped");
                    continue;
                }
                const cgltf_size components = mc.path == AnimationPath::Rotation ? 4 : 3;
                mc.times.resize(in.count);
                bool ok = true;
                for (cgltf_size k = 0; k < in.count; ++k) {
                    ok = ok && cgltf_accessor_read_float(&in, k, &mc.times[k], 1);
                    if (k > 0 && mc.times[k] < mc.times[k - 1])
                        ok = false;
                }
                mc.values.resize(outAcc.count, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
                for (cgltf_size k = 0; k < outAcc.count; ++k) {
                    float v[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                    ok = ok && cgltf_accessor_read_float(&outAcc, k, v, components);
                    mc.values[k] = glm::vec4(v[0], v[1], v[2], v[3]);
                }
                if (!ok) {
                    warn("Animation '" + ma.name + "': a channel with unreadable keyframes was skipped");
                    continue;
                }
                ma.duration = std::max(ma.duration, mc.times.back());
                ma.channels.push_back(std::move(mc));
            }
            if (!ma.channels.empty())
                out.animations.push_back(std::move(ma));
        }
    }

    void computeBounds()
    {
        glm::vec3 lo(std::numeric_limits<float>::max()), hi(-std::numeric_limits<float>::max());
        std::vector<glm::mat4> world(out.nodes.size());
        for (size_t i = 0; i < out.nodes.size(); ++i) {
            const ModelNode& n = out.nodes[i];
            world[i] = (n.parent < 0 ? glm::mat4(1.0f) : world[size_t(n.parent)]) * n.transform.matrix();
            if (n.mesh < 0)
                continue;
            for (const ModelPrimitive& p : out.meshes[size_t(n.mesh)].primitives)
                for (int c = 0; c < 8; ++c) {
                    const glm::vec3 corner((c & 1) ? p.boundsMax.x : p.boundsMin.x,
                                           (c & 2) ? p.boundsMax.y : p.boundsMin.y,
                                           (c & 4) ? p.boundsMax.z : p.boundsMin.z);
                    const glm::vec3 w = glm::vec3(world[i] * glm::vec4(corner, 1.0f));
                    lo = glm::min(lo, w);
                    hi = glm::max(hi, w);
                }
        }
        if (lo.x > hi.x)
            lo = hi = glm::vec3(0.0f);
        out.boundsMin = lo;
        out.boundsMax = hi;
    }
};

} // namespace

bool loadModelData(const std::string& path, ModelData& out, std::string* error)
{
    out = ModelData{};
    const auto fail = [&](const std::string& message) {
        if (error)
            *error = message;
        return false;
    };

    cgltf_options options{};
    cgltf_data* gltf = nullptr;
    cgltf_result r = cgltf_parse_file(&options, path.c_str(), &gltf);
    if (r != cgltf_result_success)
        return fail(resultText(r));
    struct Free { cgltf_data* d; ~Free() { cgltf_free(d); } } freeLater{gltf};

    for (cgltf_size i = 0; i < gltf->extensions_required_count; ++i) {
        const std::string ext = gltf->extensions_required[i];
        if (ext == "KHR_draco_mesh_compression" || ext == "EXT_meshopt_compression")
            return fail("it's compressed with " + ext + ", which isn't supported. In Blender's glTF "
                        "export, untick Data > Compression (Draco)");
        static const char* const known[] = {"KHR_mesh_quantization", "KHR_materials_emissive_strength",
                                            "KHR_materials_unlit", "KHR_texture_transform",
                                            "KHR_materials_pbrSpecularGlossiness", "KHR_texture_basisu",
                                            "EXT_texture_webp"};
        if (std::find(std::begin(known), std::end(known), ext) == std::end(known))
            return fail("it needs the glTF extension " + ext + ", which isn't supported");
    }

    r = cgltf_load_buffers(&options, gltf, path.c_str());
    if (r != cgltf_result_success)
        return fail(std::string("its data (.bin) couldn't be loaded: ") + resultText(r) +
                    " (keep the .bin and textures next to the .gltf, or export as .glb)");
    r = cgltf_validate(gltf);
    if (r != cgltf_result_success)
        return fail("the file is damaged or invalid (it failed glTF validation)");

    Loader loader{*gltf, options, folderOf(path), out, {}};
    loader.loadImages();
    loader.loadTextures();
    loader.loadMaterials();
    loader.loadMeshes();
    loader.loadNodes();
    loader.loadAnimations();
    loader.computeBounds();
    if (out.indices.empty())
        return fail("it has no triangles to draw");
    return true;
}

// --- ModelAnimation ----------------------------------------------------------------------

namespace {

glm::quat toQuat(const glm::vec4& v) { return glm::quat(v.w, v.x, v.y, v.z); }

// The value of a channel at `time`, as a vec4 (xyz, or a quaternion as x, y, z, w).
glm::vec4 sample(const AnimationChannel& ch, float time)
{
    const std::vector<float>& t = ch.times;
    const bool cubic = ch.interpolation == AnimationInterpolation::CubicSpline;
    const auto value = [&](size_t key) { return ch.values[cubic ? key * 3 + 1 : key]; };
    if (time <= t.front())
        return value(0);
    if (time >= t.back())
        return value(t.size() - 1);
    const size_t next = size_t(std::upper_bound(t.begin(), t.end(), time) - t.begin());
    const size_t prev = next - 1;
    const float span = t[next] - t[prev];
    const float u = span > 0.0f ? (time - t[prev]) / span : 0.0f;
    const bool rotation = ch.path == AnimationPath::Rotation;
    switch (ch.interpolation) {
    case AnimationInterpolation::Step:
        return value(prev);
    case AnimationInterpolation::CubicSpline: {
        // Hermite spline between the two keys, with glTF's tangents (scaled by the span).
        const glm::vec4 p0 = value(prev), p1 = value(next);
        const glm::vec4 m0 = ch.values[prev * 3 + 2] * span, m1 = ch.values[next * 3] * span;
        const float u2 = u * u, u3 = u2 * u;
        glm::vec4 r = (2 * u3 - 3 * u2 + 1) * p0 + (u3 - 2 * u2 + u) * m0 + (-2 * u3 + 3 * u2) * p1 + (u3 - u2) * m1;
        if (rotation) {
            const glm::quat q = glm::normalize(toQuat(r));
            r = glm::vec4(q.x, q.y, q.z, q.w);
        }
        return r;
    }
    case AnimationInterpolation::Linear:
    default:
        if (rotation) {
            const glm::quat q = glm::slerp(toQuat(value(prev)), toQuat(value(next)), u); // the short way round
            return glm::vec4(q.x, q.y, q.z, q.w);
        }
        return glm::mix(value(prev), value(next), u);
    }
}

} // namespace

void ModelAnimation::apply(float time, ModelPose& pose) const
{
    for (const AnimationChannel& ch : channels) {
        if (ch.times.empty())
            continue;
        const glm::vec4 v = sample(ch, time);
        NodeTransform& node = pose[ch.node];
        switch (ch.path) {
        case AnimationPath::Translation: node.translation = glm::vec3(v); break;
        case AnimationPath::Rotation: node.rotation = glm::normalize(toQuat(v)); break;
        case AnimationPath::Scale: node.scale = glm::vec3(v); break;
        }
    }
}

// --- ModelPose ---------------------------------------------------------------------------

NodeTransform& ModelPose::operator[](int node)
{
    if (node < 0 || size_t(node) >= nodes_.size()) {
        ignored_ = NodeTransform{};
        return ignored_;
    }
    return nodes_[size_t(node)];
}

const NodeTransform& ModelPose::operator[](int node) const
{
    return node < 0 || size_t(node) >= nodes_.size() ? ignored_ : nodes_[size_t(node)];
}

// --- Model -------------------------------------------------------------------------------

Model Model::load(gfx::VulkanContext& gpu, const std::string& path)
{
    ModelData data;
    std::string error;
    if (!loadModelData(path, data, &error))
        core::fatal("Failed to load model '%s': %s", path.c_str(), error.c_str());
    for (const std::string& w : data.warnings)
        LOG_WARN("Model '%s': %s", path.c_str(), w.c_str());
    return fromData(gpu, data, path);
}

Model Model::fromData(gfx::VulkanContext& gpu, const ModelData& data, const std::string& debugName)
{
    Model m;
    m.name_ = debugName;
    MeshData geometry;
    geometry.vertices = data.vertices;
    geometry.indices = data.indices;
    m.geometry_ = Mesh(gpu, geometry, debugName.c_str());

    for (const ModelTexture& t : data.textures) {
        const ModelImage* img = t.image >= 0 ? &data.images[size_t(t.image)] : nullptr;
        if (!img || img->rgba.empty()) {
            m.textures_.emplace_back(); // drawn as plain white
            continue;
        }
        gfx::TextureDesc desc;
        desc.srgb = true; // colour images
        desc.mipmaps = true;
        desc.filter = t.filter;
        desc.wrap = t.wrapU;
        desc.wrapV = t.wrapV;
        desc.debugName = img->name.c_str();
        m.textures_.push_back(gfx::Texture::fromPixels(gpu, img->width, img->height, img->rgba.data(), desc));
    }
    m.materials_ = data.materials;
    m.meshes_ = data.meshes;
    m.nodes_ = data.nodes;
    m.animations_ = data.animations;
    m.boundsMin_ = data.boundsMin;
    m.boundsMax_ = data.boundsMax;
    return m;
}

int Model::findNode(std::string_view name) const
{
    for (size_t i = 0; i < nodes_.size(); ++i)
        if (nodes_[i].name == name)
            return int(i);
    if (warned_.insert(std::string(name)).second)
        LOG_WARN("Model '%s' has no node called '%.*s'", name_.c_str(), int(name.size()), name.data());
    return -1;
}

int Model::findAnimation(std::string_view name) const
{
    for (size_t i = 0; i < animations_.size(); ++i)
        if (animations_[i].name == name)
            return int(i);
    if (warnedAnimations_.insert(std::string(name)).second) {
        std::string names;
        for (const ModelAnimation& a : animations_)
            names += (names.empty() ? "" : ", ") + a.name;
        LOG_WARN("Model '%s' has no animation called '%.*s' (it has: %s)", name_.c_str(), int(name.size()), name.data(),
                 names.empty() ? "none" : names.c_str());
    }
    return -1;
}

glm::mat4 Model::nodeMatrix(int node, const glm::mat4& transform, const ModelPose* pose) const
{
    const bool usePose = pose && pose->size() == nodes_.size();
    glm::mat4 m(1.0f);
    for (int i = node; i >= 0 && size_t(i) < nodes_.size(); i = nodes_[size_t(i)].parent)
        m = (usePose ? (*pose)[i] : nodes_[size_t(i)].transform).matrix() * m;
    return transform * m;
}

ModelPose Model::defaultPose() const
{
    ModelPose pose;
    pose.nodes_.reserve(nodes_.size());
    for (const ModelNode& n : nodes_)
        pose.nodes_.push_back(n.transform);
    return pose;
}

const gfx::Texture* Model::texture(int index) const
{
    if (index < 0 || size_t(index) >= textures_.size() || !textures_[size_t(index)].valid())
        return nullptr;
    return &textures_[size_t(index)];
}

void Model::nodeMatrices(const glm::mat4& transform, const ModelPose* pose, std::vector<glm::mat4>& out) const
{
    out.resize(nodes_.size());
    const bool usePose = pose && pose->size() == nodes_.size();
    for (size_t i = 0; i < nodes_.size(); ++i) {
        const NodeTransform& t = usePose ? (*pose)[int(i)] : nodes_[i].transform;
        const int parent = nodes_[i].parent;
        out[i] = (parent < 0 ? transform : out[size_t(parent)]) * t.matrix();
    }
}

} // namespace render3d
