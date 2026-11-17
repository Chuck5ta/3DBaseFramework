#pragma once

#include "gfx/Buffer.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <vector>

namespace render3d {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 color{1.0f}; // linear RGBA, multiplied with the material (glTF COLOR_0)
};

// CPU-side geometry. Triangles are counter-clockwise when seen from the front.
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

namespace primitives {
MeshData cube(float size = 1.0f);
// Flat XZ plane facing +Y, centred on the origin. uvRepeat tiles the texture.
MeshData plane(float size = 10.0f, float uvRepeat = 1.0f);
MeshData sphere(float radius = 0.5f, uint32_t segments = 32, uint32_t rings = 16);
} // namespace primitives

// GPU mesh: device-local vertex + index buffers. Move-only.
class Mesh {
public:
    Mesh() = default;
    Mesh(gfx::VulkanContext& ctx, const MeshData& data, const char* debugName = nullptr);

    // Binds the buffers and issues one indexed draw. instanceCount > 1 draws that many
    // copies at once (instancing), numbered from firstInstance in the vertex shader's
    // gl_InstanceIndex.
    void draw(VkCommandBuffer cmd, uint32_t instanceCount = 1, uint32_t firstInstance = 0) const;
    // For drawing parts of the buffers (a Model's pieces): bind once, then draw ranges.
    void bind(VkCommandBuffer cmd) const;
    void drawRange(VkCommandBuffer cmd, uint32_t firstIndex, uint32_t indexCount, int32_t vertexOffset,
                   uint32_t instanceCount = 1, uint32_t firstInstance = 0) const;

    uint32_t indexCount() const { return indexCount_; }
    bool valid() const { return vertices_.valid(); }
    // The box around its vertices (for frustum culling).
    glm::vec3 boundsMin() const { return boundsMin_; }
    glm::vec3 boundsMax() const { return boundsMax_; }

private:
    gfx::Buffer vertices_;
    gfx::Buffer indices_;
    uint32_t indexCount_ = 0;
    glm::vec3 boundsMin_{0.0f}, boundsMax_{0.0f};
};

} // namespace render3d
