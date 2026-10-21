#include "render3d/MeshRenderer.h"

#include "core/Paths.h"
#include "gfx/VulkanContext.h"

#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/matrix.hpp>

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

// Exactly 128 bytes: the minimum push constant size every GPU guarantees.
struct PushConstants {
    glm::mat4 model;
    glm::vec4 normalMatrix[3]; // mat3 as three vec4 columns (std430 padding)
    glm::vec4 color;           // linear
};
static_assert(sizeof(PushConstants) == 128);

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
    frameSetLayout_ = gfx::DescriptorLayoutBuilder()
                          .add(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
                          .build(ctx.device());
    descriptors_.init(ctx.device(), renderer.framesInFlight(), {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1.0f}},
                      /*allowFree*/ false);

    auto builder = gfx::PipelineBuilder()
                       .shaders(core::shaderPath("mesh.vert.spv"), core::shaderPath("mesh.frag.spv"))
                       .vertexBinding(0, sizeof(Vertex))
                       .vertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position))
                       .vertexAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal))
                       .vertexAttribute(2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv))
                       .cullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE)
                       .depthTest(true, true, VK_COMPARE_OP_LESS)
                       .colorFormat(renderer.colorFormat())
                       .depthFormat(renderer.depthFormat())
                       .descriptorSetLayout(frameSetLayout_)        // set 0
                       .descriptorSetLayout(ctx.textureSetLayout()) // set 1
                       .pushConstants(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                      sizeof(PushConstants));
    pipeline_ = builder.build(ctx);

    if (ctx.wireframeSupported())
        wireframePipeline_ = builder.polygonMode(VK_POLYGON_MODE_LINE).cullMode(VK_CULL_MODE_NONE).build(ctx);

    for (uint32_t i = 0; i < renderer.framesInFlight(); ++i) {
        PerFrame f;
        f.uniforms = gfx::Buffer(ctx, sizeof(FrameUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                 gfx::MemoryUsage::CpuToGpu, "frame uniforms");
        f.set = descriptors_.allocate(frameSetLayout_).set;
        gfx::DescriptorWriter()
            .buffer(0, f.uniforms.handle(), sizeof(FrameUniforms), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            .update(ctx.device(), f.set);
        frames_.push_back(std::move(f));
    }

    white_ = gfx::Texture::solid(ctx, 255, 255, 255);
}

MeshRenderer::~MeshRenderer()
{
    descriptors_.destroy();
    vkDestroyDescriptorSetLayout(ctx_.device(), frameSetLayout_, nullptr);
}

void MeshRenderer::begin(const gfx::FrameContext& frame, const Camera3D& camera, const SceneLighting& light)
{
    cmd_ = frame.cmd;
    drawCalls_ = 0;
    boundTexture_ = VK_NULL_HANDLE;

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

    active_ = wireframe_ ? &wireframePipeline_ : &pipeline_;
    active_->bind(cmd_);
    vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, active_->layout(), 0, 1, &f.set, 0, nullptr);
}

void MeshRenderer::draw(const Mesh& mesh, const glm::mat4& model, const Material& material)
{
    const gfx::Texture& tex = material.texture ? *material.texture : white_;
    const VkDescriptorSet texSet = tex.descriptorSet();
    if (texSet != boundTexture_) {
        vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, active_->layout(), 1, 1, &texSet, 0,
                                nullptr);
        boundTexture_ = texSet;
    }

    PushConstants pc;
    pc.model = model;
    // Inverse-transpose keeps normals correct under non-uniform scaling.
    const glm::mat3 normal = glm::transpose(glm::inverse(glm::mat3(model)));
    for (int i = 0; i < 3; ++i)
        pc.normalMatrix[i] = glm::vec4(normal[i], 0.0f);
    pc.color = glm::vec4(srgbToLinear(glm::vec3(material.color)), material.color.a);

    vkCmdPushConstants(cmd_, active_->layout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(pc), &pc);
    mesh.draw(cmd_);
    ++drawCalls_;
}

void MeshRenderer::end()
{
    cmd_ = VK_NULL_HANDLE;
    active_ = nullptr;
}

} // namespace render3d
