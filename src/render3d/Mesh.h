#pragma once

#include "gfx/Buffer.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

namespace render3d {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
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

    // Binds the buffers and issues one indexed draw.
    void draw(VkCommandBuffer cmd) const;

    uint32_t indexCount() const { return indexCount_; }
    bool valid() const { return vertices_.valid(); }

private:
    gfx::Buffer vertices_;
    gfx::Buffer indices_;
    uint32_t indexCount_ = 0;
};

} // namespace render3d
