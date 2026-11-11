#include "render3d/MeshRenderer.h"

#include "core/Log.h"
#include "core/Paths.h"
#include "gfx/VulkanContext.h"

#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace render3d {

namespace {

// Must match the std140 block in shaders/mesh.vert / mesh.frag.
struct FrameUniforms {
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 viewProjection;
    glm::vec4 cameraPosition;  // xyz
    glm::vec4 sunDirection;    // xyz = direction towards the sun
    glm::vec4 sunColor;        // rgb = linear colour * intensity
    glm::vec4 skyColor;        // rgb linear * ambient intensity
    glm::vec4 groundColor;     // rgb linear * ambient intensity
    glm::vec4 fog;             // rgb = linear colour, a = density
};

// Must match the push_constant block in shaders/common.glsl. At most 128 bytes: the
// minimum every GPU guarantees.
struct PushConstants {
    glm::mat4 model;
    glm::vec4 color;    // linear base colour and alpha
    glm::vec4 emissive; // rgb = linear glow; a = alpha cutoff (0 = none)
    glm::vec4 material; // x = roughness, y = metallic, z = 1 if unlit
};
static_assert(sizeof(PushConstants) <= 128);

constexpr VkShaderStageFlags kPushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

float srgbToLinear(float c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

glm::vec3 srgbToLinear(const glm::vec3& c)
{
    return {srgbToLinear(c.r), srgbToLinear(c.g), srgbToLinear(c.b)};
}

} // namespace

MeshRenderer::MeshRenderer(gfx::VulkanContext& ctx, gfx::Renderer& renderer)
    : ctx_(ctx)
{
    // Without a depth buffer, whatever is drawn last ends up in front.
    if (renderer.depthFormat() == VK_FORMAT_UNDEFINED)
        core::fatal("MeshRenderer needs a depth buffer: set config.renderer.depthBuffer = true in main.cpp");
    frameSetLayout_ = gfx::DescriptorLayoutBuilder()
                          .add(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
                          .build(ctx.device());
    descriptors_.init(ctx.device(), renderer.framesInFlight(), {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1.0f}},
                      /*allowFree*/ false);
    materialSetLayout_ = gfx::DescriptorLayoutBuilder()
                             .add(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
                             .add(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
                             .build(ctx.device());

    auto builder = gfx::PipelineBuilder()
                       .shaders(core::shaderPath("mesh.vert.spv"), core::shaderPath("mesh.frag.spv"))
                       .vertexBinding(0, sizeof(Vertex))
                       .vertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position))
                       .vertexAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal))
                       .vertexAttribute(2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv))
                       .vertexAttribute(3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color))
                       .cullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE)
                       .dynamicCulling() // per draw: double-sided materials, mirrored nodes
                       .depthTest(true, true, VK_COMPARE_OP_LESS)
                       .colorFormat(renderer.colorFormat())
                       .depthFormat(renderer.depthFormat())
                       .descriptorSetLayout(frameSetLayout_)        // set 0
                       .descriptorSetLayout(materialSetLayout_)     // set 1
                       .pushConstants(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                      sizeof(PushConstants));
    pipeline_ = builder.build(ctx);
    blendPipeline_ = gfx::PipelineBuilder(builder).blend(gfx::BlendMode::Alpha).depthTest(true, false).build(ctx);

    if (ctx.wireframeSupported())
        wireframePipeline_ = builder.polygonMode(VK_POLYGON_MODE_LINE).cullMode(VK_CULL_MODE_NONE).build(ctx);

    for (uint32_t i = 0; i < renderer.framesInFlight(); ++i) {
        PerFrame f;
        f.uniforms = gfx::Buffer(ctx, sizeof(FrameUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                 gfx::MemoryUsage::CpuToGpu, "frame uniforms");
        f.set = descriptors_.allocate(frameSetLayout_).set;
        f.materialSets = std::make_unique<gfx::DescriptorAllocator>();
        f.materialSets->init(ctx.device(), 64, {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2.0f}}, /*allowFree*/ false);
        gfx::DescriptorWriter()
            .buffer(0, f.uniforms.handle(), sizeof(FrameUniforms), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            .update(ctx.device(), f.set);
        frames_.push_back(std::move(f));
    }

    white_ = gfx::Texture::solid(ctx, 255, 255, 255);
}

MeshRenderer::~MeshRenderer()
{
    for (PerFrame& f : frames_)
        f.materialSets->destroy();
    descriptors_.destroy();
    vkDestroyDescriptorSetLayout(ctx_.device(), frameSetLayout_, nullptr);
    vkDestroyDescriptorSetLayout(ctx_.device(), materialSetLayout_, nullptr);
}

void MeshRenderer::begin(const gfx::FrameContext& frame, const Camera3D& camera, const SceneLighting& light)
{
    cmd_ = frame.cmd;
    drawCalls_ = 0;
    boundTexture_ = VK_NULL_HANDLE;
    boundGeometry_ = nullptr;
    cullMode_ = VK_CULL_MODE_FLAG_BITS_MAX_ENUM;
    frontFace_ = VK_FRONT_FACE_MAX_ENUM;
    cameraPosition_ = camera.position;
    blended_.clear();

    FrameUniforms u;
    u.view = camera.view();
    u.projection = camera.projection();
    u.viewProjection = u.projection * u.view;
    u.cameraPosition = glm::vec4(camera.position, 1.0f);
    u.sunDirection = glm::vec4(-glm::normalize(light.sunDirection), 0.0f);
    u.sunColor = glm::vec4(srgbToLinear(light.sunColor) * light.sunIntensity, 0.0f);
    u.skyColor = glm::vec4(srgbToLinear(light.skyColor) * light.ambientIntensity, 0.0f);
    u.groundColor = glm::vec4(srgbToLinear(light.groundColor) * light.ambientIntensity, 0.0f);
    u.fog = glm::vec4(srgbToLinear(light.fogColor), light.fogDensity);

    PerFrame& f = frames_[frame.frameIndex];
    f.uniforms.write(&u, sizeof(u));
    f.materialSets->reset(); // this frame slot's previous use has finished on the GPU
    f.cache.clear();
    frame_ = &f;

    active_ = wireframe_ ? &wireframePipeline_ : &pipeline_;
    active_->bind(cmd_);
    vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, active_->layout(), 0, 1, &f.set, 0, nullptr);
}

void MeshRenderer::bindTextures(const gfx::Texture* baseColor, const gfx::Texture* emission)
{
    const gfx::Texture* a = baseColor ? baseColor : &white_;
    const gfx::Texture* e = emission ? emission : &white_;
    VkDescriptorSet set = VK_NULL_HANDLE;
    for (const PerFrame::Cached& c : frame_->cache)
        if (c.baseColor == a && c.emission == e) {
            set = c.set;
            break;
        }
    if (!set) {
        set = frame_->materialSets->allocate(materialSetLayout_).set;
        gfx::DescriptorWriter()
            .image(0, a->view(), a->sampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            .image(1, e->view(), e->sampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            .update(ctx_.device(), set);
        frame_->cache.push_back({a, e, set});
    }
    if (set != boundTexture_) {
        vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, active_->layout(), 1, 1, &set, 0, nullptr);
        boundTexture_ = set;
    }
}

void MeshRenderer::setCulling(bool doubleSided, bool mirrored)
{
    // A mirrored (negative scale) object turns its triangles inside out, so its front
    // faces wind the other way. Wireframe always shows both sides.
    const VkCullModeFlags cull = doubleSided || wireframe_ ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
    const VkFrontFace front = mirrored ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    if (cull != cullMode_) {
        vkCmdSetCullMode(cmd_, cull);
        cullMode_ = cull;
    }
    if (front != frontFace_) {
        vkCmdSetFrontFace(cmd_, front);
        frontFace_ = front;
    }
}

void MeshRenderer::draw(const Mesh& mesh, const glm::mat4& model, const Material& material)
{
    bindTextures(material.texture, material.emissiveTexture);
    setCulling(material.doubleSided, glm::determinant(glm::mat3(model)) < 0.0f);

    PushConstants pc;
    pc.model = model;
    pc.color = glm::vec4(srgbToLinear(glm::vec3(material.color)), material.color.a);
    pc.emissive = glm::vec4(srgbToLinear(material.emissive) * material.emissiveStrength, 0.0f);
    pc.material = glm::vec4(material.roughness, material.metallic, 0.0f, 0.0f);
    vkCmdPushConstants(cmd_, active_->layout(), kPushStages, 0, sizeof(pc), &pc);

    mesh.draw(cmd_);
    boundGeometry_ = &mesh;
    ++drawCalls_;
}

void MeshRenderer::draw(const Model& model, const glm::mat4& transform, const ModelPose* pose)
{
    if (!model.valid())
        return;
    model.nodeMatrices(transform, pose, nodeMatrices_);
    for (int n = 0; n < model.nodeCount(); ++n) {
        const int mesh = model.node(n).mesh;
        if (mesh < 0)
            continue;
        const glm::mat4& matrix = nodeMatrices_[size_t(n)];
        for (const ModelPrimitive& prim : model.meshes()[size_t(mesh)].primitives) {
            const bool blend = prim.material >= 0 &&
                               model.materials()[size_t(prim.material)].alphaMode == AlphaMode::Blend;
            if (blend) {
                // Later, farthest first: see-through parts must go over what's behind them.
                const glm::vec3 centre = glm::vec3(matrix * glm::vec4((prim.boundsMin + prim.boundsMax) * 0.5f, 1.0f));
                blended_.push_back({&model, &prim, matrix, glm::length(centre - cameraPosition_)});
            } else {
                drawPrimitive(model, prim, matrix);
            }
        }
    }
}

void MeshRenderer::drawPrimitive(const Model& model, const ModelPrimitive& prim, const glm::mat4& matrix)
{
    static const ModelMaterial plain = [] {
        ModelMaterial m;
        m.metallic = 0.0f;
        m.roughness = 0.5f;
        return m;
    }();
    const ModelMaterial& mat = prim.material >= 0 ? model.materials()[size_t(prim.material)] : plain;

    bindTextures(model.texture(mat.baseColorTexture), model.texture(mat.emissiveTexture));
    setCulling(mat.doubleSided, glm::determinant(glm::mat3(matrix)) < 0.0f);
    if (boundGeometry_ != &model.geometry()) {
        model.geometry().bind(cmd_);
        boundGeometry_ = &model.geometry();
    }

    PushConstants pc;
    pc.model = matrix;
    pc.color = mat.baseColor; // glTF colours are already linear
    pc.emissive = glm::vec4(mat.emissive, mat.alphaMode == AlphaMode::Mask ? mat.alphaCutoff : 0.0f);
    pc.material = glm::vec4(mat.roughness, mat.metallic, mat.unlit ? 1.0f : 0.0f, 0.0f);
    vkCmdPushConstants(cmd_, active_->layout(), kPushStages, 0, sizeof(pc), &pc);

    model.geometry().drawRange(cmd_, prim.firstIndex, prim.indexCount, prim.vertexOffset);
    ++drawCalls_;
}

void MeshRenderer::end()
{
    if (!blended_.empty()) {
        std::sort(blended_.begin(), blended_.end(),
                  [](const BlendedDraw& a, const BlendedDraw& b) { return a.distance > b.distance; });
        if (!wireframe_) {
            // Same layout, so the frame and texture sets stay bound; dynamic culling state
            // carries over to the new pipeline too, but set it again to be safe.
            active_ = &blendPipeline_;
            active_->bind(cmd_);
            cullMode_ = VK_CULL_MODE_FLAG_BITS_MAX_ENUM;
            frontFace_ = VK_FRONT_FACE_MAX_ENUM;
        }
        for (const BlendedDraw& d : blended_)
            drawPrimitive(*d.model, *d.primitive, d.matrix);
        blended_.clear();
    }
    cmd_ = VK_NULL_HANDLE;
    active_ = nullptr;
    frame_ = nullptr;
}

} // namespace render3d
