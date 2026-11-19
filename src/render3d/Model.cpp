#include "render3d/Model.h"

#include "render3d/Tangents.h"

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

const cgltf_accessor* findAttribute(const cgltf_attribute* attributes, cgltf_size count, cgltf_attribute_type type,
                                    int index = 0)
{
    for (cgltf_size i = 0; i < count; ++i)
        if (attributes[i].type == type && attributes[i].index == index)
            return attributes[i].data;
    return nullptr;
}

const cgltf_accessor* findAttribute(const cgltf_primitive& p, cgltf_attribute_type type, int index = 0)
{
    return findAttribute(p.attributes, p.attributes_count, type, index);
}

// Grows lo..hi by how far a mesh's morph targets can move a vertex at these weights: each
// target adds its own box times its weight (a negative weight turns the box round).
void growByMorphs(const ModelMesh& mesh, const float* weights, size_t count, glm::vec3& lo, glm::vec3& hi)
{
    for (size_t t = 0; t < mesh.targetMin.size() && t < count; ++t) {
        const float w = weights[t];
        if (w == 0.0f)
            continue;
        const glm::vec3 a = mesh.targetMin[t] * w, b = mesh.targetMax[t] * w;
        lo += glm::min(a, b);
        hi += glm::max(a, b);
    }
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
    std::vector<const cgltf_node*> sourceNodes; // index in out.nodes -> glTF node (or nullptr)

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
            mm.normalTexture = textureFor(m.normal_texture, mm.name, "normal");
            mm.normalScale = m.normal_texture.scale;
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
    bool loadPrimitive(const cgltf_primitive& p, ModelMesh& mesh, ModelPrimitive& prim)
    {
        const std::string& meshName = mesh.name;
        if (p.type != cgltf_primitive_type_triangles && p.type != cgltf_primitive_type_triangle_strip &&
            p.type != cgltf_primitive_type_triangle_fan) {
            warn("Mesh '" + meshName + "': points and lines are skipped (only triangles are drawn)");
            return false;
        }
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
        std::vector<float> tan; // the file's own tangents, if it has them (xyz + handedness)
        const cgltf_accessor* tAcc = findAttribute(p, cgltf_attribute_type_tangent);
        const bool haveTangents = haveNormals && tAcc && tAcc->count == count &&
                                  cgltf_num_components(tAcc->type) == 4 && readFloats(tAcc, 4, tan);
        const cgltf_accessor* cAcc = findAttribute(p, cgltf_attribute_type_color);
        size_t colorComponents = cAcc ? cgltf_num_components(cAcc->type) : 0;
        const bool haveColor = cAcc && cAcc->count == count && (colorComponents == 3 || colorComponents == 4) &&
                               readFloats(cAcc, colorComponents, col);

        // Skinning: which joints move each vertex (any integer type), and how much.
        std::vector<float> weights;
        std::vector<glm::u16vec4> joints;
        const cgltf_accessor* jAcc = findAttribute(p, cgltf_attribute_type_joints);
        const cgltf_accessor* wAcc = findAttribute(p, cgltf_attribute_type_weights);
        bool haveSkin = jAcc && wAcc && jAcc->count == count && cgltf_num_components(jAcc->type) == 4 &&
                        wAcc->count == count && readFloats(wAcc, 4, weights);
        if (haveSkin) {
            joints.resize(count);
            for (size_t i = 0; i < count && haveSkin; ++i) {
                cgltf_uint j[4] = {};
                haveSkin = cgltf_accessor_read_uint(jAcc, i, j, 4) && j[0] <= 0xFFFF && j[1] <= 0xFFFF &&
                           j[2] <= 0xFFFF && j[3] <= 0xFFFF;
                joints[i] = glm::u16vec4(j[0], j[1], j[2], j[3]);
            }
            if (!haveSkin)
                warn("Mesh '" + meshName + "': unreadable joints; drawn without skinning");
        }
        if (findAttribute(p, cgltf_attribute_type_joints, 1))
            warn("Mesh '" + meshName + "': only 4 bones per vertex are used (Blender: Limit Total, 4)");

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

        const auto skinVertex = [&](uint32_t i) {
            SkinVertex v;
            v.joints = joints[i];
            v.weights = glm::make_vec4(&weights[size_t(i) * 4]);
            const float sum = v.weights.x + v.weights.y + v.weights.z + v.weights.w;
            if (sum > 0.0f)
                v.weights /= sum; // exporters round; the weights must add up to 1
            else
                v.weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            return v;
        };
        // Kept one per vertex, so start the joint data where this primitive's vertices start.
        if (haveSkin)
            out.skinVertices.resize(out.vertices.size());

        const auto vertex = [&](uint32_t i) {
            Vertex v{};
            v.position = glm::make_vec3(&pos[size_t(i) * 3]);
            v.normal = haveNormals ? glm::make_vec3(&nrm[size_t(i) * 3]) : glm::vec3(0.0f, 1.0f, 0.0f);
            v.uv = haveUV ? glm::make_vec2(&uv[size_t(i) * 2]) : glm::vec2(0.0f);
            if (haveTangents) {
                v.tangent = glm::make_vec4(&tan[size_t(i) * 4]);
                v.tangent.w = v.tangent.w < 0.0f ? -1.0f : 1.0f;
            }
            v.color = glm::vec4(1.0f);
            if (haveColor)
                for (size_t c = 0; c < colorComponents; ++c)
                    v.color[int(c)] = col[size_t(i) * colorComponents + c];
            return v;
        };

        prim.firstIndex = uint32_t(out.indices.size());
        prim.vertexOffset = int32_t(out.vertices.size());
        std::vector<uint32_t> source; // for each vertex made, the file's vertex it came from (morph targets)
        if (haveNormals) {
            for (size_t i = 0; i < count; ++i) {
                source.push_back(uint32_t(i));
                out.vertices.push_back(vertex(uint32_t(i)));
                if (haveSkin)
                    out.skinVertices.push_back(skinVertex(uint32_t(i)));
            }
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
                source.insert(source.end(), {tris[t], tris[t + 1], tris[t + 2]});
                out.indices.insert(out.indices.end(), {base, base + 1, base + 2});
                if (haveSkin)
                    out.skinVertices.insert(out.skinVertices.end(),
                                            {skinVertex(tris[t]), skinVertex(tris[t + 1]), skinVertex(tris[t + 2])});
            }
        }
        prim.indexCount = uint32_t(out.indices.size()) - prim.firstIndex;
        prim.material = indexOf(p.material, gltf.materials);

        // A normal map needs tangents: the file's, or MikkTSpace's (as Blender would make them).
        const bool normalMapped = prim.material >= 0 && out.materials[size_t(prim.material)].normalTexture >= 0;
        if (normalMapped && !haveUV) {
            warn("Mesh '" + meshName + "': a part with a normal map but no UV map; drawn without the normal map");
        } else if (normalMapped && !haveTangents) {
            generateTangents(out.vertices, out.indices, prim.firstIndex, prim.indexCount, uint32_t(prim.vertexOffset),
                             [&](uint32_t from) {
                                 if (haveSkin) {
                                     const SkinVertex copy = out.skinVertices[from];
                                     out.skinVertices.push_back(copy);
                                 }
                                 source.push_back(source[from - uint32_t(prim.vertexOffset)]);
                             });
        }
        if (p.targets_count > 0)
            loadMorphTargets(p, mesh, prim, source, count, haveNormals);

        prim.boundsMin = glm::vec3(std::numeric_limits<float>::max());
        prim.boundsMax = glm::vec3(-std::numeric_limits<float>::max());
        for (size_t i = 0; i < count; ++i) {
            const glm::vec3 v = glm::make_vec3(&pos[i * 3]);
            prim.boundsMin = glm::min(prim.boundsMin, v);
            prim.boundsMax = glm::max(prim.boundsMax, v);
        }
        prim.skinned = haveSkin;
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
                if (loadPrimitive(m.primitives[j], mm, prim))
                    mm.primitives.push_back(prim);
            }
            const size_t targets = mm.targetMin.size();
            if (targets > 0) {
                // Blender writes the shape key names into the mesh's extras ("targetNames").
                for (size_t t = 0; t < targets; ++t)
                    mm.targetNames.push_back(t < m.target_names_count && m.target_names[t] && *m.target_names[t]
                                                 ? std::string(m.target_names[t])
                                                 : "target " + std::to_string(t));
                mm.defaultWeights.assign(targets, 0.0f);
                if (m.weights_count == targets)
                    mm.defaultWeights.assign(m.weights, m.weights + targets);
            }
            out.meshes.push_back(std::move(mm));
        }
    }

    // Reads a primitive's morph targets: each made vertex gets the offsets of the file's
    // vertex it came from (`source`), all its targets together.
    void loadMorphTargets(const cgltf_primitive& p, ModelMesh& mesh, ModelPrimitive& prim,
                          const std::vector<uint32_t>& source, size_t count, bool useNormals)
    {
        const size_t targets = p.targets_count;
        if (!mesh.targetMin.empty() && mesh.targetMin.size() != targets) {
            warn("Mesh '" + mesh.name + "': its parts have different numbers of shape keys; a part's are ignored");
            return;
        }
        std::vector<std::vector<float>> pos(targets), nrm(targets);
        for (size_t t = 0; t < targets; ++t) {
            const cgltf_morph_target& target = p.targets[t];
            const cgltf_accessor* pa = findAttribute(target.attributes, target.attributes_count,
                                                     cgltf_attribute_type_position);
            const cgltf_accessor* na = findAttribute(target.attributes, target.attributes_count,
                                                     cgltf_attribute_type_normal);
            if (pa && (pa->count != count || !readFloats(pa, 3, pos[t]))) {
                warn("Mesh '" + mesh.name + "': unreadable shape key positions; that shape key does nothing");
                pos[t].clear();
            }
            if (useNormals && na && (na->count != count || !readFloats(na, 3, nrm[t]))) {
                warn("Mesh '" + mesh.name + "': unreadable shape key normals; they're left as they are");
                nrm[t].clear();
            }
            if (findAttribute(target.attributes, target.attributes_count, cgltf_attribute_type_tangent))
                warn("Mesh '" + mesh.name + "': shape key tangents are ignored (the normals and positions are used)");
        }
        if (mesh.targetMin.empty()) {
            mesh.targetMin.assign(targets, glm::vec3(0.0f));
            mesh.targetMax.assign(targets, glm::vec3(0.0f));
        }
        prim.morphTargets = uint32_t(targets);
        prim.morphFirst = uint32_t(out.morphDeltas.size());
        out.morphDeltas.resize(out.morphDeltas.size() + source.size() * targets);
        MorphDelta* d = out.morphDeltas.data() + prim.morphFirst;
        for (size_t v = 0; v < source.size(); ++v)
            for (size_t t = 0; t < targets; ++t, ++d) {
                const size_t s = size_t(source[v]) * 3;
                if (!pos[t].empty())
                    d->position = glm::make_vec3(&pos[t][s]);
                if (!nrm[t].empty())
                    d->normal = glm::make_vec3(&nrm[t][s]);
                mesh.targetMin[t] = glm::min(mesh.targetMin[t], d->position);
                mesh.targetMax[t] = glm::max(mesh.targetMax[t], d->position);
            }
    }

    // Each node with a morphed mesh gets its run of the model's weight list, at the file's
    // weights: the node's own, else its mesh's (glTF's rule).
    void assignMorphWeights()
    {
        for (size_t i = 0; i < out.nodes.size(); ++i) {
            ModelNode& node = out.nodes[i];
            if (node.mesh < 0 || out.meshes[size_t(node.mesh)].targetMin.empty())
                continue;
            const ModelMesh& mesh = out.meshes[size_t(node.mesh)];
            node.firstMorphWeight = int(out.morphWeights.size());
            const cgltf_node* src = i < sourceNodes.size() ? sourceNodes[i] : nullptr;
            if (src && src->weights_count == mesh.defaultWeights.size())
                out.morphWeights.insert(out.morphWeights.end(), src->weights, src->weights + src->weights_count);
            else
                out.morphWeights.insert(out.morphWeights.end(), mesh.defaultWeights.begin(), mesh.defaultWeights.end());
        }
    }

    void addNode(const cgltf_node& n, int parent)
    {
        ModelNode node;
        node.name = nameOr(n.name, "node", size_t(&n - gltf.nodes));
        node.parent = parent;
        node.mesh = indexOf(n.mesh, gltf.meshes);
        node.skin = n.mesh ? indexOf(n.skin, gltf.skins) : -1; // the glTF skin, until loadSkins()
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
        sourceNodes.push_back(&n);
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

    void loadSkins()
    {
        std::vector<int> skinIndex(gltf.skins_count, -1); // glTF skin -> index in out.skins
        for (cgltf_size s = 0; s < gltf.skins_count; ++s) {
            const cgltf_skin& skin = gltf.skins[s];
            ModelSkin ms;
            ms.name = nameOr(skin.name, "skin", s);
            bool ok = skin.joints_count > 0;
            for (cgltf_size j = 0; j < skin.joints_count && ok; ++j) {
                const auto found = nodeIndex.find(skin.joints[j]);
                ok = found != nodeIndex.end();
                if (ok)
                    ms.joints.push_back(found->second);
            }
            ms.inverseBind.assign(ms.joints.size(), glm::mat4(1.0f)); // glTF's default
            if (ok && skin.inverse_bind_matrices) {
                ok = skin.inverse_bind_matrices->count >= skin.joints_count;
                for (size_t j = 0; j < ms.joints.size() && ok; ++j)
                    ok = cgltf_accessor_read_float(skin.inverse_bind_matrices, j, &ms.inverseBind[j][0][0], 16);
            }
            if (!ok) {
                warn("Skin '" + ms.name + "' can't be used (its bones are outside the scene, or unreadable); "
                     "its mesh is drawn unbent");
                continue;
            }
            skinIndex[s] = int(out.skins.size());
            out.skins.push_back(std::move(ms));
        }

        for (ModelNode& node : out.nodes) {
            if (node.skin < 0)
                continue;
            node.skin = skinIndex[size_t(node.skin)];
            if (node.skin < 0)
                continue;
            // Every part of the mesh must have joints within this skin's list.
            const size_t jointCount = out.skins[size_t(node.skin)].joints.size();
            bool usable = true;
            for (const ModelPrimitive& prim : out.meshes[size_t(node.mesh)].primitives) {
                usable = usable && prim.skinned;
                for (uint32_t i = prim.firstIndex; usable && i < prim.firstIndex + prim.indexCount; ++i) {
                    const SkinVertex& v = out.skinVertices[size_t(prim.vertexOffset) + out.indices[i]];
                    for (int k = 0; k < 4; ++k)
                        usable = usable && (v.weights[k] == 0.0f || v.joints[k] < jointCount);
                }
            }
            if (!usable) {
                warn("Node '" + node.name + "': its mesh doesn't match its skin (missing or wrong joints); "
                     "drawn unbent");
                node.skin = -1;
            }
        }
        if (!out.skinVertices.empty())
            out.skinVertices.resize(out.vertices.size()); // one per vertex, unskinned ones too
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
                    warn("Animation '" + ma.name + "': shape key (morph target) animation isn't supported yet; "
                         "skipped (set the weights in MeshRenderer::draw instead)");
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
        }
        for (size_t i = 0; i < out.nodes.size(); ++i) {
            const ModelNode& n = out.nodes[i];
            if (n.mesh >= 0 && n.skin >= 0) {
                // Bend every vertex into the default pose, as the GPU will.
                const ModelSkin& skin = out.skins[size_t(n.skin)];
                std::vector<glm::mat4> joints(skin.joints.size());
                for (size_t j = 0; j < joints.size(); ++j)
                    joints[j] = world[size_t(skin.joints[j])] * skin.inverseBind[j];
                for (const ModelPrimitive& p : out.meshes[size_t(n.mesh)].primitives)
                    for (uint32_t k = p.firstIndex; k < p.firstIndex + p.indexCount; ++k) {
                        const size_t v = size_t(p.vertexOffset) + out.indices[k];
                        const SkinVertex& sv = out.skinVertices[v];
                        glm::mat4 m(0.0f);
                        for (int c = 0; c < 4; ++c)
                            if (sv.weights[c] > 0.0f)
                                m += joints[sv.joints[c]] * sv.weights[c];
                        glm::vec3 position = out.vertices[v].position; // morphed first, at the file's weights
                        for (uint32_t t = 0; t < p.morphTargets && n.firstMorphWeight >= 0; ++t)
                            position += out.morphWeights[size_t(n.firstMorphWeight) + t] *
                                        out.morphDeltas[p.morphFirst + size_t(out.indices[k]) * p.morphTargets + t].position;
                        const glm::vec3 w = glm::vec3(m * glm::vec4(position, 1.0f));
                        lo = glm::min(lo, w);
                        hi = glm::max(hi, w);
                    }
                continue;
            }
            if (n.mesh < 0)
                continue;
            const ModelMesh& mesh = out.meshes[size_t(n.mesh)];
            for (const ModelPrimitive& p : mesh.primitives) {
                glm::vec3 pMin = p.boundsMin, pMax = p.boundsMax;
                if (p.morphTargets > 0 && n.firstMorphWeight >= 0)
                    growByMorphs(mesh, &out.morphWeights[size_t(n.firstMorphWeight)], p.morphTargets, pMin, pMax);
                for (int c = 0; c < 8; ++c) {
                    const glm::vec3 corner((c & 1) ? pMax.x : pMin.x, (c & 2) ? pMax.y : pMin.y,
                                           (c & 4) ? pMax.z : pMin.z);
                    const glm::vec3 w = glm::vec3(world[i] * glm::vec4(corner, 1.0f));
                    lo = glm::min(lo, w);
                    hi = glm::max(hi, w);
                }
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
    loader.loadSkins();
    loader.assignMorphWeights();
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
    if (!data.skinVertices.empty())
        m.skinVertices_ = gfx::Buffer::createWithData(gpu, data.skinVertices.data(),
                                                      data.skinVertices.size() * sizeof(SkinVertex),
                                                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, (debugName + " joints").c_str());
    if (!data.morphDeltas.empty())
        m.morphDeltas_ = gfx::Buffer::createWithData(gpu, data.morphDeltas.data(),
                                                     data.morphDeltas.size() * sizeof(MorphDelta),
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                     (debugName + " morph targets").c_str());
    m.defaultMorphWeights_ = data.morphWeights;
    m.morphWeightNode_.assign(data.morphWeights.size(), -1);
    for (size_t i = 0; i < data.nodes.size(); ++i) {
        const ModelNode& n = data.nodes[i];
        if (n.firstMorphWeight < 0)
            continue;
        const size_t count = data.meshes[size_t(n.mesh)].targetNames.size();
        for (size_t t = 0; t < count && size_t(n.firstMorphWeight) + t < m.morphWeightNode_.size(); ++t)
            m.morphWeightNode_[size_t(n.firstMorphWeight) + t] = int(i);
    }

    std::vector<bool> isNormalMap(data.textures.size(), false); // data, not colour: no sRGB decoding
    for (const ModelMaterial& mat : data.materials)
        if (mat.normalTexture >= 0 && size_t(mat.normalTexture) < isNormalMap.size())
            isNormalMap[size_t(mat.normalTexture)] = true;
    for (const ModelTexture& t : data.textures) {
        const ModelImage* img = t.image >= 0 ? &data.images[size_t(t.image)] : nullptr;
        if (!img || img->rgba.empty()) {
            m.textures_.emplace_back(); // drawn as plain white
            continue;
        }
        gfx::TextureDesc desc;
        desc.srgb = !isNormalMap[m.textures_.size()]; // colour images are sRGB; normal maps are data
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
    m.skins_ = data.skins;
    m.animations_ = data.animations;
    m.boundsMin_ = data.boundsMin;
    m.boundsMax_ = data.boundsMax;

    // Each joint's box: every vertex it has a say in, for skinBounds().
    m.jointBounds_.resize(data.skins.size());
    for (size_t s = 0; s < data.skins.size(); ++s)
        m.jointBounds_[s].assign(data.skins[s].joints.size(), {glm::vec3(std::numeric_limits<float>::max()),
                                                                glm::vec3(-std::numeric_limits<float>::max())});
    for (const ModelNode& n : data.nodes) {
        if (n.mesh < 0 || n.skin < 0)
            continue;
        std::vector<JointBounds>& boxes = m.jointBounds_[size_t(n.skin)];
        for (const ModelPrimitive& p : data.meshes[size_t(n.mesh)].primitives)
            for (uint32_t k = p.firstIndex; k < p.firstIndex + p.indexCount; ++k) {
                const size_t v = size_t(p.vertexOffset) + data.indices[k];
                const SkinVertex& sv = data.skinVertices[v];
                for (int c = 0; c < 4; ++c)
                    if (sv.weights[c] > 0.0f && size_t(sv.joints[c]) < boxes.size()) {
                        JointBounds& b = boxes[size_t(sv.joints[c])];
                        b.min = glm::min(b.min, data.vertices[v].position);
                        b.max = glm::max(b.max, data.vertices[v].position);
                    }
            }
    }
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

int Model::findMorphTarget(std::string_view name) const
{
    for (size_t w = 0; w < morphWeightNode_.size(); ++w)
        if (morphTargetName(int(w)) == name)
            return int(w);
    if (warnedTargets_.insert(std::string(name)).second) {
        std::string names;
        for (size_t w = 0; w < morphWeightNode_.size() && names.size() < 400; ++w)
            names += (names.empty() ? "" : ", ") + morphTargetName(int(w));
        LOG_WARN("Model '%s' has no shape key (morph target) called '%.*s' (it has: %s)", name_.c_str(),
                 int(name.size()), name.data(), names.empty() ? "none" : names.c_str());
    }
    return -1;
}

int Model::findMorphTarget(int node, std::string_view name) const
{
    if (node >= 0 && size_t(node) < nodes_.size() && nodes_[size_t(node)].firstMorphWeight >= 0) {
        const ModelNode& n = nodes_[size_t(node)];
        const std::vector<std::string>& names = meshes_[size_t(n.mesh)].targetNames;
        for (size_t t = 0; t < names.size(); ++t)
            if (names[t] == name)
                return n.firstMorphWeight + int(t);
    }
    const std::string key = std::to_string(node) + ":" + std::string(name);
    if (warnedTargets_.insert(key).second)
        LOG_WARN("Model '%s': node %d has no shape key (morph target) called '%.*s'", name_.c_str(), node,
                 int(name.size()), name.data());
    return -1;
}

const std::string& Model::morphTargetName(int weight) const
{
    static const std::string none;
    if (weight < 0 || size_t(weight) >= morphWeightNode_.size() || morphWeightNode_[size_t(weight)] < 0)
        return none;
    const ModelNode& n = nodes_[size_t(morphWeightNode_[size_t(weight)])];
    return meshes_[size_t(n.mesh)].targetNames[size_t(weight - n.firstMorphWeight)];
}

void Model::morphReach(int mesh, const float* weights, size_t count, glm::vec3& lo, glm::vec3& hi) const
{
    growByMorphs(meshes_[size_t(mesh)], weights, count, lo, hi);
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

void Model::jointMatrices(int skin, const std::vector<glm::mat4>& nodeMatrices, std::vector<glm::mat4>& out) const
{
    const ModelSkin& s = skins_[size_t(skin)];
    out.resize(s.joints.size());
    for (size_t j = 0; j < s.joints.size(); ++j)
        out[j] = nodeMatrices[size_t(s.joints[j])] * s.inverseBind[j];
}

bool Model::skinBounds(int skin, const std::vector<glm::mat4>& joints, glm::vec3& min, glm::vec3& max,
                       const glm::vec3& morphLo, const glm::vec3& morphHi) const
{
    // A bent vertex is a weighted average of where each of its joints would put it, so it
    // lies within the boxes those joints carry: the box around them all holds it.
    min = glm::vec3(std::numeric_limits<float>::max());
    max = -min;
    const std::vector<JointBounds>& boxes = jointBounds_[size_t(skin)];
    for (size_t j = 0; j < boxes.size() && j < joints.size(); ++j) {
        const JointBounds& b = boxes[j];
        if (b.min.x > b.max.x)
            continue; // this joint moves nothing
        // The box as the joint turns it, then the upright box around that.
        // Morph targets move the vertices before the joints do: grow the box by their reach.
        const glm::vec3 bMin = b.min + morphLo, bMax = b.max + morphHi;
        const glm::mat4& m = joints[j];
        const glm::vec3 centre = glm::vec3(m * glm::vec4((bMin + bMax) * 0.5f, 1.0f));
        const glm::vec3 half = (bMax - bMin) * 0.5f;
        const glm::vec3 reach = glm::abs(glm::vec3(m[0])) * half.x + glm::abs(glm::vec3(m[1])) * half.y +
                                glm::abs(glm::vec3(m[2])) * half.z;
        min = glm::min(min, centre - reach);
        max = glm::max(max, centre + reach);
    }
    return min.x <= max.x;
}

} // namespace render3d
