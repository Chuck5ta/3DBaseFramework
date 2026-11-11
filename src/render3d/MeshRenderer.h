#pragma once

#include "gfx/Buffer.h"
#include "gfx/Descriptors.h"
#include "gfx/Pipeline.h"
#include "gfx/Renderer.h"
#include "gfx/Texture.h"
#include "render3d/Camera3D.h"
#include "render3d/Mesh.h"
#include "render3d/Model.h"

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <memory>
#include <vector>

namespace render3d {

// For drawing a Mesh. (A Model brings its own materials from the glTF file.)
struct Material {
    const gfx::Texture* texture = nullptr; // nullptr = plain colour
    glm::vec4 color{1.0f};                 // sRGB tint, multiplied with the texture
    glm::vec3 emissive{0.0f};              // sRGB glow colour (lights, LEDs), unaffected by lighting
    float emissiveStrength = 1.0f;         // above 1 for a brighter glow
    const gfx::Texture* emissiveTexture = nullptr; // where it glows (times `emissive`); nullptr = everywhere
    float roughness = 0.5f;                // 0 = glossy, small sharp highlight; 1 = matte
    float metallic = 0.0f;                 // 1 = metal: highlights take the surface colour
    bool doubleSided = false;              // visible from behind too (thin planes)
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

// Forward renderer for lit, textured meshes and glTF models.
//
//   meshes.begin(frame, camera, lighting);
//   meshes.draw(cube, modelMatrix, {&crateTexture});
//   meshes.draw(drone, transform);              // a Model: all its parts and materials
//   meshes.draw(drone, transform, &pose);       // ...with its nodes posed (spinning rotors)
//   meshes.end();                               // see-through parts are drawn here
//
// Per-frame data (camera, lights) lives in a uniform buffer (set 0); the base colour and
// emission textures are set 1; the model matrix and material values are push constants.
// Opaque and alpha-masked parts are drawn straight away; see-through (Blend) parts are
// collected and drawn in end(), farthest first, so they blend over everything behind them.
// Needs a depth buffer (config.renderer.depthBuffer = true).
class MeshRenderer {
public:
    MeshRenderer(gfx::VulkanContext& ctx, gfx::Renderer& renderer);
    ~MeshRenderer();

    void begin(const gfx::FrameContext& frame, const Camera3D& camera, const SceneLighting& lighting);
    void draw(const Mesh& mesh, const glm::mat4& model, const Material& material = {});
    void draw(const Model& model, const glm::mat4& transform, const ModelPose* pose = nullptr);
    void end();

    void setWireframe(bool enabled) { wireframe_ = enabled && wireframePipeline_.valid(); }
    bool wireframe() const { return wireframe_; }
    bool wireframeSupported() const { return wireframePipeline_.valid(); }

    uint32_t drawCalls() const { return drawCalls_; }

private:
    gfx::VulkanContext& ctx_;
    VkDescriptorSetLayout frameSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout materialSetLayout_ = VK_NULL_HANDLE; // binding 0 base colour, 1 emission
    gfx::DescriptorAllocator descriptors_;
    gfx::Pipeline pipeline_;
    gfx::Pipeline blendPipeline_;     // see-through parts: blended, no depth writes
    gfx::Pipeline wireframePipeline_;
    gfx::Texture white_;

    void bindTextures(const gfx::Texture* baseColor, const gfx::Texture* emission);
    void setCulling(bool doubleSided, bool mirrored);
    void drawPrimitive(const Model& model, const ModelPrimitive& prim, const glm::mat4& matrix);

    struct BlendedDraw {
        const Model* model;
        const ModelPrimitive* primitive;
        glm::mat4 matrix;
        float distance;
    };
    std::vector<BlendedDraw> blended_;
    std::vector<glm::mat4> nodeMatrices_; // scratch

    struct PerFrame {
        gfx::Buffer uniforms;
        VkDescriptorSet set = VK_NULL_HANDLE;
        // Texture pairs used this frame, each with its set. Rebuilt every frame (the pool
        // is reset when the frame comes round again), so no set outlives its textures.
        std::unique_ptr<gfx::DescriptorAllocator> materialSets;
        struct Cached {
            const gfx::Texture* baseColor;
            const gfx::Texture* emission;
            VkDescriptorSet set;
        };
        std::vector<Cached> cache;
    };
    PerFrame* frame_ = nullptr;
    std::vector<PerFrame> frames_;

    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    const gfx::Pipeline* active_ = nullptr;
    VkDescriptorSet boundTexture_ = VK_NULL_HANDLE;
    const Mesh* boundGeometry_ = nullptr;
    VkCullModeFlags cullMode_ = VK_CULL_MODE_FLAG_BITS_MAX_ENUM;
    VkFrontFace frontFace_ = VK_FRONT_FACE_MAX_ENUM;
    glm::vec3 cameraPosition_{0.0f};
    bool wireframe_ = false;
    uint32_t drawCalls_ = 0;
};

} // namespace render3d
