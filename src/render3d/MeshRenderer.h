#pragma once

#include "gfx/Buffer.h"
#include "gfx/Descriptors.h"
#include "gfx/Pipeline.h"
#include "gfx/Renderer.h"
#include "gfx/Texture.h"
#include "render3d/Camera3D.h"
#include "render3d/Mesh.h"

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <vector>

namespace render3d {

struct Material {
    const gfx::Texture* texture = nullptr; // nullptr = plain colour
    glm::vec4 color{1.0f};                 // sRGB tint, multiplied with the texture
};

// Simple outdoor lighting: one sun + hemisphere ambient + distance fog.
// Colours are sRGB.
struct SceneLighting {
    glm::vec3 sunDirection{-0.4f, -1.0f, -0.3f}; // direction the light travels
    glm::vec3 sunColor{1.0f, 0.96f, 0.88f};
    float sunIntensity = 2.5f;
    glm::vec3 skyColor{0.55f, 0.65f, 0.80f};     // ambient from above
    glm::vec3 groundColor{0.30f, 0.27f, 0.22f};  // ambient from below
    float ambientIntensity = 0.5f;
    glm::vec3 fogColor{0.62f, 0.70f, 0.80f};     // match the renderer's clear colour
    float fogDensity = 0.015f;                   // 0 disables fog
};

// Forward renderer for lit, textured meshes.
//
//   meshes.begin(frame, camera, lighting);
//   meshes.draw(cube, modelMatrix, {&crateTexture});
//   meshes.end();
//
// Per-frame data (camera, lights) lives in a uniform buffer (set 0); the
// texture is set 1; model matrix and colour are push constants.
class MeshRenderer {
public:
    MeshRenderer(gfx::VulkanContext& ctx, gfx::Renderer& renderer);
    ~MeshRenderer();

    void begin(const gfx::FrameContext& frame, const Camera3D& camera, const SceneLighting& lighting);
    void draw(const Mesh& mesh, const glm::mat4& model, const Material& material = {});
    void end();

    void setWireframe(bool enabled) { wireframe_ = enabled && wireframePipeline_.valid(); }
    bool wireframe() const { return wireframe_; }
    bool wireframeSupported() const { return wireframePipeline_.valid(); }

    uint32_t drawCalls() const { return drawCalls_; }

private:
    gfx::VulkanContext& ctx_;
    VkDescriptorSetLayout frameSetLayout_ = VK_NULL_HANDLE;
    gfx::DescriptorAllocator descriptors_;
    gfx::Pipeline pipeline_;
    gfx::Pipeline wireframePipeline_;
    gfx::Texture white_;

    struct PerFrame {
        gfx::Buffer uniforms;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };
    std::vector<PerFrame> frames_;

    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    const gfx::Pipeline* active_ = nullptr;
    VkDescriptorSet boundTexture_ = VK_NULL_HANDLE;
    bool wireframe_ = false;
    uint32_t drawCalls_ = 0;
};

} // namespace render3d
