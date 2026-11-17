#pragma once

// 3D models from glTF 2.0 files: .gltf (with its .bin and images) or a single .glb.
// Blender exports them (File > Export > glTF 2.0), as do most free asset packs.
//
//   // onInit:
//   drone_ = render3d::Model::load(app.gpu(), core::assetPath("models/drone.glb"));
//
//   // onRender, between meshes.begin() and meshes.end():
//   meshes.draw(drone_, transform);
//
// Moving parts. A model's nodes are the objects in Blender's Outliner, each with its own
// position, rotation and scale relative to its parent. A ModelPose holds those for one
// copy of the model, so every drone on screen can have its rotors at a different angle:
//
//   rotor_ = drone_.findNode("RotorFL");          // once
//   pose_ = drone_.defaultPose();                 // one per drone
//   pose_[rotor_].rotation = glm::angleAxis(spin, glm::vec3(0, 1, 0));
//   meshes.draw(drone_, transform, &pose_);
//
// Animations made in Blender (one per Action) are played by a ModelAnimator, which
// fills a ModelPose for you (see ModelAnimator.h). Sockets: an empty in Blender, say
// "Socket_Hand_R", marks where to attach another model, which then follows the animation:
//
//   const int hand = soldier_.findNode("Socket_Hand_R");              // once
//   const glm::mat4 at = soldier_.nodeMatrix(hand, transform, &animator_.pose());
//   meshes.draw(rifle_, at);
//
// Skinned meshes (Blender: a mesh with an Armature modifier) bend instead of moving in
// one piece: each vertex follows up to four bones (joints), weighted, so an elbow or knee
// bends smoothly. The bones are nodes like any other, so animations, poses and sockets
// work the same way, and MeshRenderer bends the mesh on the GPU as it draws it.
//
// Units are metres and +Y is up. A model's front faces +Z (Blender's -Y after export).

#include "gfx/Texture.h"
#include "render3d/Mesh.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_precision.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace gfx { class VulkanContext; }

namespace render3d {

enum class AlphaMode {
    Opaque, // alpha ignored
    Mask,   // pixels with alpha below alphaCutoff are cut out (leaves, fences, decals)
    Blend,  // see-through (glass, smoke): drawn after everything else, back to front
};

struct ModelMaterial {
    std::string name;
    glm::vec4 baseColor{1.0f};   // linear RGBA, multiplied with the texture and vertex colours
    int baseColorTexture = -1;   // index into ModelData::textures, -1 = none
    glm::vec3 emissive{0.0f};    // linear glow colour, already times the emission strength
    int emissiveTexture = -1;    // multiplied with `emissive` (glowing windows, screens, LEDs)
    float metallic = 1.0f;       // glTF's defaults; Blender writes its own values
    float roughness = 1.0f;
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;    // drawn from both sides (thin planes, leaves)
    bool unlit = false;          // KHR_materials_unlit: flat colour, no lighting
};

struct ModelImage {
    std::string name;
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba; // width * height * 4
};

struct ModelTexture {
    int image = -1;
    VkFilter filter = VK_FILTER_LINEAR;
    VkSamplerAddressMode wrapU = VK_SAMPLER_ADDRESS_MODE_REPEAT; // across (glTF's S)
    VkSamplerAddressMode wrapV = VK_SAMPLER_ADDRESS_MODE_REPEAT; // down (glTF's T)
};

// A run of triangles with one material, inside the model's shared vertex/index arrays.
struct ModelPrimitive {
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    int32_t vertexOffset = 0;
    int material = -1; // -1 = plain white
    bool skinned = false; // has joints and weights (in ModelData::skinVertices)
    glm::vec3 boundsMin{0.0f}, boundsMax{0.0f}; // before any skinning
};

struct ModelMesh {
    std::string name;
    std::vector<ModelPrimitive> primitives;
};

// Which joints move a vertex of a skinned mesh, and how much (glTF JOINTS_0 / WEIGHTS_0).
// One per vertex in ModelData::vertices, kept in a second vertex buffer.
struct SkinVertex {
    glm::u16vec4 joints{0};  // indices into the node's ModelSkin::joints
    glm::vec4 weights{0.0f}; // add up to 1; 0 = unused
};

// A skin (Blender: an armature) bends the mesh of the node that uses it. Its joints are
// nodes (Blender: bones). inverseBind takes a vertex from the mesh into each joint's own
// frame as it was when the mesh was made (the bind pose): it then moves with that joint.
struct ModelSkin {
    std::string name;
    std::vector<int> joints;            // node indices
    std::vector<glm::mat4> inverseBind; // one per joint
};

// A node's transform relative to its parent.
struct NodeTransform {
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    glm::mat4 matrix() const;
};

struct ModelNode {
    std::string name;
    int parent = -1; // always lower than this node's index
    int mesh = -1;   // -1 = an empty (a pivot or group)
    int skin = -1;   // >= 0: the mesh is bent by this skin (and the node's own transform is ignored)
    NodeTransform transform;
};

class ModelPose;

// --- Animations ------------------------------------------------------------------------------

enum class AnimationPath { Translation, Rotation, Scale };
enum class AnimationInterpolation { Linear, Step, CubicSpline };

// The keyframes of one property (position, rotation or scale) of one node.
struct AnimationChannel {
    int node = -1;
    AnimationPath path = AnimationPath::Translation;
    AnimationInterpolation interpolation = AnimationInterpolation::Linear;
    std::vector<float> times;      // seconds, increasing
    // xyz for translation and scale; a quaternion as (x, y, z, w) for rotation.
    // CubicSpline has three per key: in-tangent, value, out-tangent.
    std::vector<glm::vec4> values;
};

// One animation: a Blender Action ("Idle", "Fire", "Load"), as glTF stores it.
struct ModelAnimation {
    std::string name;
    float duration = 0.0f; // seconds (the last keyframe's time)
    std::vector<AnimationChannel> channels;

    // Sets the transforms of the nodes this animation moves to their values at `time`
    // (seconds, clamped to 0..duration). Nodes it doesn't animate are left alone.
    void apply(float time, ModelPose& pose) const;
};

// Everything in a glTF file, on the CPU. Model::load uses it; it's also handy for tools
// and tests (no GPU needed).
struct ModelData {
    std::vector<Vertex> vertices;
    std::vector<SkinVertex> skinVertices; // empty, or one per vertex if anything is skinned
    std::vector<uint32_t> indices;
    std::vector<ModelMesh> meshes;
    std::vector<ModelMaterial> materials;
    std::vector<ModelImage> images;
    std::vector<ModelTexture> textures;
    std::vector<ModelNode> nodes;       // parents before children
    std::vector<ModelSkin> skins;
    std::vector<ModelAnimation> animations;
    glm::vec3 boundsMin{0.0f}, boundsMax{0.0f}; // of the whole model, in its default pose
    std::vector<std::string> warnings;  // things that were skipped (also logged)
};

// Reads a .gltf or .glb. Returns false with `error` set if the file can't be used.
bool loadModelData(const std::string& path, ModelData& out, std::string* error = nullptr);

// Per-copy node transforms for drawing a model in a different pose.
class ModelPose {
public:
    // pose[node].rotation = ...; node -1 (from a failed findNode) is accepted and ignored.
    NodeTransform& operator[](int node);
    const NodeTransform& operator[](int node) const;
    size_t size() const { return nodes_.size(); }

private:
    friend class Model;
    friend class ModelAnimator;
    std::vector<NodeTransform> nodes_;
    NodeTransform ignored_;
};

class Model {
public:
    Model() = default;

    // Loads a .gltf/.glb and uploads it to the GPU. Aborts with a message if the file is
    // missing or can't be read (like textures and sounds do).
    static Model load(gfx::VulkanContext& gpu, const std::string& path);
    static Model fromData(gfx::VulkanContext& gpu, const ModelData& data, const std::string& debugName = "model");

    bool valid() const { return geometry_.valid(); }

    // Nodes: the objects in Blender's Outliner. findNode returns -1 (and warns once)
    // if there's no node with that name.
    int nodeCount() const { return int(nodes_.size()); }
    int findNode(std::string_view name) const;
    const ModelNode& node(int index) const { return nodes_[size_t(index)]; }
    ModelPose defaultPose() const;

    // Animations: one per Blender Action. findAnimation returns -1 (and warns once) if
    // there's none with that name. ModelAnimator plays them by name.
    const std::vector<ModelAnimation>& animations() const { return animations_; }
    int findAnimation(std::string_view name) const;

    const std::vector<ModelMaterial>& materials() const { return materials_; }
    const std::vector<ModelMesh>& meshes() const { return meshes_; }
    const std::vector<ModelSkin>& skins() const { return skins_; }

    // The model's size in its default pose (metres): for placing it on the ground,
    // scaling it to fit, or collision.
    glm::vec3 boundsMin() const { return boundsMin_; }
    glm::vec3 boundsMax() const { return boundsMax_; }
    glm::vec3 size() const { return boundsMax_ - boundsMin_; }
    glm::vec3 center() const { return (boundsMin_ + boundsMax_) * 0.5f; }

    // --- For MeshRenderer ---------------------------------------------------------------
    const Mesh& geometry() const { return geometry_; }
    const gfx::Texture* texture(int index) const;
    // World matrix of one node (a socket, a muzzle, a hand) for a pose (nullptr = default
    // pose): draw an attached model with it, or take its position for effects and bullets.
    // Node -1 gives `transform` itself.
    glm::mat4 nodeMatrix(int node, const glm::mat4& transform, const ModelPose* pose = nullptr) const;
    // World matrix of every node for a pose (nullptr = default pose).
    void nodeMatrices(const glm::mat4& transform, const ModelPose* pose, std::vector<glm::mat4>& out) const;
    // The joint vertex data (an invalid buffer if nothing is skinned).
    const gfx::Buffer& skinVertices() const { return skinVertices_; }
    // For each joint of a skin: the matrix taking a bind-pose vertex to where that joint
    // has moved it, in model space, given the node matrices from nodeMatrices() with an
    // identity transform.
    void jointMatrices(int skin, const std::vector<glm::mat4>& nodeMatrices, std::vector<glm::mat4>& out) const;
    // The box around everything a skin bends, as `joints` (from jointMatrices) bend it, in
    // model space: for frustum culling. False if the skin moves no vertex.
    bool skinBounds(int skin, const std::vector<glm::mat4>& joints, glm::vec3& min, glm::vec3& max) const;

private:
    // For each skin, for each of its joints: the box around the vertices it moves at all,
    // in the bind pose (min > max if none). Bent by that joint's matrix, each box holds
    // where its vertices can be, so together they hold the whole bent mesh.
    struct JointBounds {
        glm::vec3 min, max;
    };
    std::vector<std::vector<JointBounds>> jointBounds_;
    Mesh geometry_;
    gfx::Buffer skinVertices_;
    std::vector<gfx::Texture> textures_;
    std::vector<ModelMaterial> materials_;
    std::vector<ModelMesh> meshes_;
    std::vector<ModelNode> nodes_;
    std::vector<ModelSkin> skins_;
    std::vector<ModelAnimation> animations_;
    glm::vec3 boundsMin_{0.0f}, boundsMax_{0.0f};
    std::string name_;
    mutable std::set<std::string, std::less<>> warned_;           // missing node names reported
    mutable std::set<std::string, std::less<>> warnedAnimations_; // missing animation names reported
};

} // namespace render3d
