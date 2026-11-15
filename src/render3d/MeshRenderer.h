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
#include <span>
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
    bool castShadows = true;               // false for the ground, glows and effects
};

// Shadows cast by the sun. One shadow map covers everything within `distance` of the
// camera; shadows fade out towards that edge. Sizes are in shadow-map texels, so they
// keep working when you change `distance` or the map's size.
struct ShadowSettings {
    bool enabled = true;
    float distance = 30.0f;  // metres from the camera that get shadows
    float strength = 1.0f;   // 0 = none .. 1 = only the ambient light reaches shadowed places
    float softness = 1.0f;   // blur radius in texels (0 = hard, pixelated edges)
    float normalBias = 1.0f; // texels: looks up the shadow a little off the surface...
    float depthBias = 1.5f;  // ...and lets it be this much in front: together they stop
                             // surfaces shadowing themselves in stripes ("shadow acne")
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
    ShadowSettings shadows;                      // the sun's shadows
};

// Forward renderer for lit, textured meshes and glTF models, with sun shadows.
//
//   meshes.begin(frame, camera, lighting);
//   meshes.draw(cube, modelMatrix, {&crateTexture});
//   meshes.draw(drone, transform);              // a Model: all its parts and materials
//   meshes.draw(drone, transform, &pose);       // ...with its nodes posed (spinning rotors)
//   meshes.end();                               // see-through parts are drawn here
//
// Shadows take a second pass over the scene, from the sun, before the main pass begins
// (in Game::onPreRender). Make the same draw() calls there: between beginShadows and
// endShadows they draw depth into the shadow map instead of colour onto the screen.
//
//   void onPreRender(...) { meshes.beginShadows(frame, camera, lighting); drawScene(); meshes.endShadows(); }
//   void onRender(...)    { meshes.begin(frame, camera, lighting);        drawScene(); meshes.end(); }
//
// Skip the shadow pass (or set lighting.shadows.enabled = false) and the scene is simply
// drawn without shadows.
//
// Skinned models bend on the GPU: draw(model, ...) works out each joint's matrix for the
// pose, appends them to this frame's joint buffer (set 0, binding 2), and draws the mesh
// with a vertex shader that blends the matrices of each vertex's joints.
//
// Many copies of the same thing (trees, rocks, crates) are quicker drawn together: one
// draw call per part instead of one per copy, however many copies there are.
//
//   std::vector<glm::mat4> trees = ...;          // one matrix per tree
//   meshes.drawInstanced(treeModel, trees);      // all of them, in one draw per part
//   meshes.drawInstanced(cube, crates, {&crateTexture});
//
// The matrices go into this frame's instance buffer (set 0, binding 3); the vertex shader
// picks each copy's matrix by its gl_InstanceIndex. Every copy shares the material and
// the pose, and every copy is drawn, on screen or not.
//
// Per-frame data (camera, lights) lives in a uniform buffer (set 0); the base colour and
// emission textures are set 1; the model matrix and material values are push constants.
// Opaque and alpha-masked parts are drawn straight away; see-through (Blend) parts are
// collected and drawn in end(), farthest first, so they blend over everything behind them.
// The shadow map is the depth of the scene as the sun sees it (set 0, binding 1).
// Needs a depth buffer (config.renderer.depthBuffer = true).
class MeshRenderer {
public:
    // shadowMapSize: the shadow map's width and height in texels. Bigger is sharper and
    // slower; 2048 (16 MB) suits a few dozen metres around the camera.
    // maxJoints: how many joint matrices a frame can hold, shadow pass included (each
    // skinned model drawn uses one per bone, twice with shadows). 8192 is 512 KB per frame.
    // maxInstances: how many instance matrices a frame can hold, shadow pass included (each
    // drawInstanced uses one per copy, twice with shadows). 16384 is 1 MB per frame.
    MeshRenderer(gfx::VulkanContext& ctx, gfx::Renderer& renderer, uint32_t shadowMapSize = 2048,
                 uint32_t maxJoints = 8192, uint32_t maxInstances = 16384);
    ~MeshRenderer();

    // The shadow pass: call before the main pass begins (onPreRender), with the camera
    // and lighting that begin() will get this frame.
    void beginShadows(const gfx::FrameContext& frame, const Camera3D& camera, const SceneLighting& lighting);
    void endShadows();

    void begin(const gfx::FrameContext& frame, const Camera3D& camera, const SceneLighting& lighting);
    void draw(const Mesh& mesh, const glm::mat4& model, const Material& material = {});
    void draw(const Model& model, const glm::mat4& transform, const ModelPose* pose = nullptr);
    // Instancing: a copy for each matrix, all with the same material (or, for a Model, the
    // same pose), in one draw call per part. A std::vector<glm::mat4> or array works here.
    void drawInstanced(const Mesh& mesh, std::span<const glm::mat4> models, const Material& material = {});
    void drawInstanced(const Model& model, std::span<const glm::mat4> transforms, const ModelPose* pose = nullptr);
    void end();

    void setWireframe(bool enabled) { wireframe_ = enabled && wireframePipeline_.valid(); }
    bool wireframe() const { return wireframe_; }
    bool wireframeSupported() const { return wireframePipeline_.valid(); }

    uint32_t drawCalls() const { return drawCalls_; }
    uint32_t shadowDrawCalls() const { return shadowDrawCalls_; }
    // Joint matrices used so far this frame (both passes), and how many fit.
    uint32_t jointsUsed() const { return jointsUsed_; }
    uint32_t maxJoints() const { return maxJoints_; }
    // The same for instance matrices (entry 0 is kept for single copies, so this starts at 1).
    uint32_t instancesUsed() const { return instancesUsed_; }
    uint32_t maxInstances() const { return maxInstances_; }
    uint32_t shadowMapSize() const { return shadowMap_.extent().width; }
    // How much ground one shadow-map texel covers, in metres, at the last beginShadows.
    float shadowTexelSize() const { return shadowTexel_; }

private:
    gfx::VulkanContext& ctx_;
    VkDescriptorSetLayout frameSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout materialSetLayout_ = VK_NULL_HANDLE; // binding 0 base colour, 1 emission
    gfx::DescriptorAllocator descriptors_;
    gfx::Pipeline pipeline_;
    gfx::Pipeline blendPipeline_;     // see-through parts: blended, no depth writes
    gfx::Pipeline wireframePipeline_;
    // The same four for skinned meshes: a vertex shader that bends them, and their joint
    // data in a second vertex buffer.
    gfx::Pipeline skinnedPipeline_, skinnedBlendPipeline_, skinnedWireframePipeline_, skinnedShadowPipeline_;
    gfx::Texture white_;

    // Shadows: a depth image the sun "sees", and a sampler that compares against it.
    gfx::Image shadowMap_;
    VkSampler shadowSampler_ = VK_NULL_HANDLE;
    gfx::Pipeline shadowPipeline_;   // depth only, both sides of every triangle
    glm::mat4 shadowMatrix_{1.0f};   // world -> the sun's clip space
    float shadowTexel_ = 0.0f;       // metres per texel
    float shadowDepthRange_ = 1.0f;  // metres from the map's near to far plane
    uint64_t shadowFrame_ = ~uint64_t(0); // frameNumber of the last shadow pass
    uint32_t shadowDrawCalls_ = 0;

    enum class Pass { None, Shadow, SkippedShadow, Main };

    void writeFrameUniforms(uint32_t frameIndex, const Camera3D& camera, const SceneLighting& lighting,
                            bool shadows);
    void fitShadowMap(const Camera3D& camera, const SceneLighting& lighting);
    void bindTextures(const gfx::Texture* baseColor, const gfx::Texture* emission);
    void setCulling(bool doubleSided, bool mirrored);
    // Which copies a draw makes: `count` matrices from `first` in the instance buffer, the
    // last `mirrored` of them mirrored (a negative scale, so drawn with the other winding).
    // The default is entry 0, the identity: a single copy placed by the push constants.
    struct Instances {
        uint32_t first = 0;
        uint32_t count = 1;
        uint32_t mirrored = 0;
    };
    bool uploadInstances(std::span<const glm::mat4> matrices, Instances& out); // false if out of room
    // Draws a whole mesh (prim == nullptr) or one primitive of `geometry`, once per group
    // of copies that wind their triangles the same way.
    void drawCopies(const Mesh& geometry, const ModelPrimitive* prim, const Instances& copies, bool doubleSided,
                    bool mirrored);
    void drawMesh(const Mesh& mesh, const glm::mat4& model, const Material& material, const Instances& copies);
    // firstJoint: where its joint matrices start in the joint buffer; -1 = not skinned.
    void drawPrimitive(const Model& model, const ModelPrimitive& prim, const glm::mat4& matrix, int firstJoint,
                       const Instances& copies);
    void usePipeline(bool skinned);
    int uploadJoints(const Model& model, int skin); // returns the first joint's index, or -1

    struct BlendedDraw {
        const Model* model;
        const ModelPrimitive* primitive;
        glm::mat4 matrix;
        float distance;
        int firstJoint;
    };
    std::vector<BlendedDraw> blended_;
    std::vector<glm::mat4> nodeMatrices_; // scratch
    std::vector<glm::mat4> modelSpace_;   // scratch: node matrices without the transform
    std::vector<glm::mat4> joints_;       // scratch
    std::vector<glm::mat4> instances_;    // scratch: matrices sorted, the mirrored ones last

    struct PerFrame {
        gfx::Buffer uniforms;
        gfx::Buffer joints;    // joint matrices of the skinned models drawn this frame
        gfx::Buffer instances; // matrices of the copies drawn by drawInstanced; [0] = identity
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
    const gfx::Pipeline* active_ = nullptr;        // for this pass: rigid meshes...
    const gfx::Pipeline* activeSkinned_ = nullptr; // ...and skinned ones
    const gfx::Pipeline* bound_ = nullptr;
    const gfx::Buffer* boundSkin_ = nullptr;
    uint32_t maxJoints_ = 0;
    uint32_t jointsUsed_ = 0;
    bool warnedJoints_ = false;
    uint32_t maxInstances_ = 0;
    uint32_t instancesUsed_ = 1;
    bool warnedInstances_ = false;
    VkDescriptorSet boundTexture_ = VK_NULL_HANDLE;
    const Mesh* boundGeometry_ = nullptr;
    VkCullModeFlags cullMode_ = VK_CULL_MODE_FLAG_BITS_MAX_ENUM;
    VkFrontFace frontFace_ = VK_FRONT_FACE_MAX_ENUM;
    glm::vec3 cameraPosition_{0.0f};
    Pass pass_ = Pass::None;
    bool wireframe_ = false;
    uint32_t drawCalls_ = 0;
};

} // namespace render3d
