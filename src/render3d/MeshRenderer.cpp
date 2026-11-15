#include "render3d/MeshRenderer.h"

#include "core/Log.h"
#include "core/Paths.h"
#include "gfx/VulkanContext.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
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
    glm::mat4 shadowMatrix;    // world -> the sun's clip space (the shadow map)
    glm::vec4 shadow;          // x = strength (0 = off), y = normal offset (m), z = depth bias, w = texel (uv)
    glm::vec4 shadowFade;      // x, y = fade from/to this distance from the camera; z = softness (texels)
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

// How far towards the sun, beyond the shadowed area, things still cast shadows into it:
// a drone 50 m up still shades the ground below.
constexpr float kCasterReach = 100.0f;

// A depth format the GPU can render to and sample with filtering. D32 float everywhere
// that matters; D16 as a fallback.
VkFormat pickShadowFormat(VkPhysicalDevice gpu, bool& linear)
{
    for (VkFormat format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM}) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(gpu, format, &props);
        const VkFormatFeatureFlags f = props.optimalTilingFeatures;
        if ((f & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) && (f & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
            linear = (f & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
            return format;
        }
    }
    core::fatal("No depth format can be used for a shadow map");
}

float srgbToLinear(float c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

glm::vec3 srgbToLinear(const glm::vec3& c)
{
    return {srgbToLinear(c.r), srgbToLinear(c.g), srgbToLinear(c.b)};
}

} // namespace

MeshRenderer::MeshRenderer(gfx::VulkanContext& ctx, gfx::Renderer& renderer, uint32_t shadowMapSize)
    : ctx_(ctx)
{
    // Without a depth buffer, whatever is drawn last ends up in front.
    if (renderer.depthFormat() == VK_FORMAT_UNDEFINED)
        core::fatal("MeshRenderer needs a depth buffer: set config.renderer.depthBuffer = true in main.cpp");
    frameSetLayout_ = gfx::DescriptorLayoutBuilder()
                          .add(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
                          .add(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT) // shadow map
                          .build(ctx.device());
    descriptors_.init(ctx.device(), renderer.framesInFlight(),
                      {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1.0f}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1.0f}},
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

    // --- Shadow map ---
    bool linear = false;
    const VkFormat shadowFormat = pickShadowFormat(ctx.physicalDevice(), linear);
    gfx::ImageDesc mapDesc;
    mapDesc.width = mapDesc.height = std::max(shadowMapSize, 64u);
    mapDesc.format = shadowFormat;
    mapDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    mapDesc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    mapDesc.debugName = "shadow map";
    shadowMap_ = gfx::Image(ctx, mapDesc);
    // Ready to be sampled from the start, so frames without a shadow pass are valid too.
    ctx.immediateSubmit([&](VkCommandBuffer cmd) {
        gfx::imageBarrier(cmd, shadowMap_.handle(), VK_IMAGE_ASPECT_DEPTH_BIT,
                          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_PIPELINE_STAGE_2_NONE, 0,
                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    });

    // A comparison sampler: each lookup answers "is this point nearer the sun than what
    // the map saw?" (1 = lit, 0 = in shadow). With linear filtering the GPU blends four
    // such answers, which softens the edges for free.
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = samplerInfo.minFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; // outside the map: lit
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VK_CHECK(vkCreateSampler(ctx.device(), &samplerInfo, nullptr, &shadowSampler_));

    // Depth only (no colour attachment), and no culling: a crate's back faces cast its
    // shadow just as well, and open meshes (planes, leaves) cast from both sides.
    shadowPipeline_ = gfx::PipelineBuilder()
                          .shaders(core::shaderPath("shadow.vert.spv"), core::shaderPath("shadow.frag.spv"))
                          .vertexBinding(0, sizeof(Vertex))
                          .vertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position))
                          .vertexAttribute(2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv))
                          .vertexAttribute(3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color))
                          .cullMode(VK_CULL_MODE_NONE)
                          .depthTest(true, true, VK_COMPARE_OP_LESS)
                          .colorFormat(VK_FORMAT_UNDEFINED)
                          .depthFormat(shadowFormat)
                          .descriptorSetLayout(frameSetLayout_)     // same layout as the main pass,
                          .descriptorSetLayout(materialSetLayout_)  // so the same sets bind
                          .pushConstants(kPushStages, sizeof(PushConstants))
                          .build(ctx);

    for (uint32_t i = 0; i < renderer.framesInFlight(); ++i) {
        PerFrame f;
        f.uniforms = gfx::Buffer(ctx, sizeof(FrameUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                 gfx::MemoryUsage::CpuToGpu, "frame uniforms");
        f.set = descriptors_.allocate(frameSetLayout_).set;
        f.materialSets = std::make_unique<gfx::DescriptorAllocator>();
        f.materialSets->init(ctx.device(), 64, {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2.0f}}, /*allowFree*/ false);
        gfx::DescriptorWriter()
            .buffer(0, f.uniforms.handle(), sizeof(FrameUniforms), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            .image(1, shadowMap_.view(), shadowSampler_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
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
    vkDestroySampler(ctx_.device(), shadowSampler_, nullptr);
    vkDestroyDescriptorSetLayout(ctx_.device(), frameSetLayout_, nullptr);
    vkDestroyDescriptorSetLayout(ctx_.device(), materialSetLayout_, nullptr);
}

void MeshRenderer::fitShadowMap(const Camera3D& camera, const SceneLighting& light)
{
    // The shadow map covers a sphere around the part of the view within `distance`: the
    // smallest sphere holding that slice of the camera's frustum. Its size depends only on
    // the field of view and distances, never on where the camera looks, so turning the
    // camera doesn't change how big a texel is (which would make edges crawl).
    const float n = camera.nearPlane;
    const float f = std::max(n + 0.01f, std::min(light.shadows.distance, camera.farPlane));
    const float tanHalf = std::tan(camera.fovY * 0.5f);
    const float k2 = (1.0f + camera.aspect() * camera.aspect()) * tanHalf * tanHalf; // corner slope, squared
    float along = (f + n) * (1.0f + k2) * 0.5f; // the centre, this far in front of the camera
    float radius;
    if (along >= f) {
        along = f;
        radius = f * std::sqrt(k2);
    } else {
        radius = std::sqrt((f - along) * (f - along) + f * f * k2);
    }
    const glm::vec3 centre = camera.position + camera.forward() * along;

    // Look along the sunlight. Then move the centre in whole texels only, so as the
    // camera moves, the map's texels stay put on the ground and edges don't shimmer.
    const glm::vec3 dir = glm::normalize(light.sunDirection);
    const glm::vec3 up = std::abs(dir.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), dir, up);
    const float size = float(shadowMap_.extent().width);
    const float texel = 2.0f * radius / size;
    glm::vec3 c = glm::vec3(view * glm::vec4(centre, 1.0f));
    c.x = std::floor(c.x / texel) * texel;
    c.y = std::floor(c.y / texel) * texel;

    // The view looks down -z: the near plane is towards the sun, out past the sphere so
    // taller things (and the drone above) still cast into it.
    const glm::mat4 proj = glm::ortho(c.x - radius, c.x + radius, c.y - radius, c.y + radius,
                                      -c.z - radius - kCasterReach, -c.z + radius);
    shadowMatrix_ = proj * view;
    shadowTexel_ = texel;
    shadowDepthRange_ = 2.0f * radius + kCasterReach;
}

void MeshRenderer::writeFrameUniforms(uint32_t frameIndex, const Camera3D& camera, const SceneLighting& light,
                                      bool shadows)
{
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

    // Biases in texels become metres (normal offset) and the map's depth units (0..1).
    const ShadowSettings& s = light.shadows;
    const float distance = std::min(s.distance, camera.farPlane);
    u.shadowMatrix = shadowMatrix_;
    u.shadow = glm::vec4(shadows ? std::clamp(s.strength, 0.0f, 1.0f) : 0.0f,
                         std::max(s.normalBias, 0.0f) * shadowTexel_,
                         std::max(s.depthBias, 0.0f) * shadowTexel_ / shadowDepthRange_,
                         1.0f / float(shadowMap_.extent().width));
    u.shadowFade = glm::vec4(distance * 0.8f, distance, std::max(s.softness, 0.0f), 0.0f);

    frames_[frameIndex].uniforms.write(&u, sizeof(u));
}

void MeshRenderer::beginShadows(const gfx::FrameContext& frame, const Camera3D& camera, const SceneLighting& light)
{
    shadowDrawCalls_ = 0;
    if (!light.shadows.enabled || light.shadows.strength <= 0.0f || light.shadows.distance <= 0.0f) {
        pass_ = Pass::SkippedShadow; // draw() calls do nothing until endShadows
        return;
    }
    pass_ = Pass::Shadow;
    cmd_ = frame.cmd;
    boundTexture_ = VK_NULL_HANDLE;
    boundGeometry_ = nullptr;
    blended_.clear();

    fitShadowMap(camera, light);
    shadowFrame_ = frame.frameNumber;
    writeFrameUniforms(frame.frameIndex, camera, light, true);

    PerFrame& f = frames_[frame.frameIndex];
    f.materialSets->reset(); // this frame slot's previous use has finished on the GPU
    f.cache.clear();
    frame_ = &f;

    // Wait for earlier frames to finish reading the map, then render into it. Its old
    // contents don't matter (UNDEFINED): every texel gets cleared.
    gfx::imageBarrier(cmd_, shadowMap_.handle(), VK_IMAGE_ASPECT_DEPTH_BIT,
                      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                      VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    // The pipeline declares one colour attachment of format UNDEFINED, matched here by an
    // attachment with no image: nothing but depth is written.
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = VK_NULL_HANDLE;
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = shadowMap_.view();
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.clearValue.depthStencil = {1.0f, 0};

    const VkExtent2D size = shadowMap_.extent();
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea = {{0, 0}, size};
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &color;
    info.pDepthAttachment = &depth;
    vkCmdBeginRendering(cmd_, &info);

    VkViewport viewport{0.0f, 0.0f, float(size.width), float(size.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, size};
    vkCmdSetViewport(cmd_, 0, 1, &viewport);
    vkCmdSetScissor(cmd_, 0, 1, &scissor);

    active_ = &shadowPipeline_;
    active_->bind(cmd_);
    vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, active_->layout(), 0, 1, &f.set, 0, nullptr);
}

void MeshRenderer::endShadows()
{
    if (pass_ == Pass::Shadow) {
        vkCmdEndRendering(cmd_);
        // The main pass samples the map in its fragment shader.
        gfx::imageBarrier(cmd_, shadowMap_.handle(), VK_IMAGE_ASPECT_DEPTH_BIT,
                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        blended_.clear();
    }
    pass_ = Pass::None;
    cmd_ = VK_NULL_HANDLE;
    active_ = nullptr;
    frame_ = nullptr;
}

void MeshRenderer::begin(const gfx::FrameContext& frame, const Camera3D& camera, const SceneLighting& light)
{
    pass_ = Pass::Main;
    cmd_ = frame.cmd;
    drawCalls_ = 0;
    boundTexture_ = VK_NULL_HANDLE;
    boundGeometry_ = nullptr;
    cullMode_ = VK_CULL_MODE_FLAG_BITS_MAX_ENUM;
    frontFace_ = VK_FRONT_FACE_MAX_ENUM;
    cameraPosition_ = camera.position;
    blended_.clear();

    // Shadows only if this frame had a shadow pass (and they're still switched on).
    const bool shadows = shadowFrame_ == frame.frameNumber && light.shadows.enabled;
    writeFrameUniforms(frame.frameIndex, camera, light, shadows);

    PerFrame& f = frames_[frame.frameIndex];
    if (shadowFrame_ != frame.frameNumber) {
        f.materialSets->reset(); // (beginShadows did this already otherwise)
        f.cache.clear();
    }
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
    if (pass_ == Pass::Shadow)
        return; // the shadow pipeline draws both sides, always
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
    if (pass_ == Pass::SkippedShadow || (pass_ == Pass::Shadow && !material.castShadows))
        return;
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
    ++(pass_ == Pass::Shadow ? shadowDrawCalls_ : drawCalls_);
}

void MeshRenderer::draw(const Model& model, const glm::mat4& transform, const ModelPose* pose)
{
    if (!model.valid() || pass_ == Pass::SkippedShadow)
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
            if (blend && pass_ == Pass::Shadow) {
                continue; // see-through parts cast no shadow
            } else if (blend) {
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
    ++(pass_ == Pass::Shadow ? shadowDrawCalls_ : drawCalls_);
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
    pass_ = Pass::None;
    cmd_ = VK_NULL_HANDLE;
    active_ = nullptr;
    frame_ = nullptr;
}

} // namespace render3d
