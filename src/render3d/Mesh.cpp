#include "render3d/Mesh.h"

#include "render3d/Tangents.h"

#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>

namespace render3d {

namespace primitives {

MeshData cube(float size)
{
    const float h = size * 0.5f;
    MeshData m;
    // One quad per face so each face gets its own normal and full UVs.
    struct Face { glm::vec3 normal, u, v; };
    const Face faces[6] = {
        {{ 1, 0, 0}, { 0, 0,-1}, {0, 1, 0}},
        {{-1, 0, 0}, { 0, 0, 1}, {0, 1, 0}},
        {{ 0, 1, 0}, { 1, 0, 0}, {0, 0,-1}},
        {{ 0,-1, 0}, { 1, 0, 0}, {0, 0, 1}},
        {{ 0, 0, 1}, { 1, 0, 0}, {0, 1, 0}},
        {{ 0, 0,-1}, {-1, 0, 0}, {0, 1, 0}},
    };
    for (const Face& f : faces) {
        const uint32_t base = uint32_t(m.vertices.size());
        const glm::vec3 c = f.normal * h;
        // bottom-left, bottom-right, top-right, top-left (CCW seen from outside)
        m.vertices.push_back({c - f.u * h - f.v * h, f.normal, {0.0f, 1.0f}});
        m.vertices.push_back({c + f.u * h - f.v * h, f.normal, {1.0f, 1.0f}});
        m.vertices.push_back({c + f.u * h + f.v * h, f.normal, {1.0f, 0.0f}});
        m.vertices.push_back({c - f.u * h + f.v * h, f.normal, {0.0f, 0.0f}});
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base + 2, base + 3, base});
    }
    generateTangents(m); // ready for a normal map
    return m;
}

MeshData plane(float size, float uvRepeat)
{
    const float h = size * 0.5f;
    const glm::vec3 n(0.0f, 1.0f, 0.0f);
    MeshData m;
    m.vertices = {
        {{-h, 0.0f,  h}, n, {0.0f, uvRepeat}},
        {{ h, 0.0f,  h}, n, {uvRepeat, uvRepeat}},
        {{ h, 0.0f, -h}, n, {uvRepeat, 0.0f}},
        {{-h, 0.0f, -h}, n, {0.0f, 0.0f}},
    };
    m.indices = {0, 1, 2, 2, 3, 0};
    generateTangents(m); // ready for a normal map
    return m;
}

MeshData sphere(float radius, uint32_t segments, uint32_t rings)
{
    MeshData m;
    for (uint32_t r = 0; r <= rings; ++r) {
        const float v = float(r) / float(rings);
        const float phi = v * glm::pi<float>(); // 0 at the north pole
        for (uint32_t s = 0; s <= segments; ++s) {
            const float u = float(s) / float(segments);
            const float theta = u * glm::two_pi<float>();
            const glm::vec3 n(std::sin(phi) * std::cos(theta), std::cos(phi), -std::sin(phi) * std::sin(theta));
            m.vertices.push_back({n * radius, n, {u, v}});
        }
    }
    const uint32_t stride = segments + 1;
    for (uint32_t r = 0; r < rings; ++r)
        for (uint32_t s = 0; s < segments; ++s) {
            const uint32_t a = r * stride + s;       // top-left
            const uint32_t b = a + stride;           // bottom-left
            m.indices.insert(m.indices.end(), {a, b, b + 1, b + 1, a + 1, a});
        }
    generateTangents(m); // ready for a normal map
    return m;
}

} // namespace primitives

Mesh::Mesh(gfx::VulkanContext& ctx, const MeshData& data, const char* debugName)
    : indexCount_(uint32_t(data.indices.size()))
{
    vertices_ = gfx::Buffer::createWithData(ctx, data.vertices.data(), data.vertices.size() * sizeof(Vertex),
                                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, debugName);
    indices_ = gfx::Buffer::createWithData(ctx, data.indices.data(), data.indices.size() * sizeof(uint32_t),
                                           VK_BUFFER_USAGE_INDEX_BUFFER_BIT, debugName);
    if (!data.vertices.empty()) {
        boundsMin_ = boundsMax_ = data.vertices[0].position;
        for (const Vertex& v : data.vertices) {
            boundsMin_ = glm::min(boundsMin_, v.position);
            boundsMax_ = glm::max(boundsMax_, v.position);
        }
    }
}

void Mesh::draw(VkCommandBuffer cmd, uint32_t instanceCount, uint32_t firstInstance) const
{
    bind(cmd);
    vkCmdDrawIndexed(cmd, indexCount_, instanceCount, 0, 0, firstInstance);
}

void Mesh::bind(VkCommandBuffer cmd) const
{
    const VkBuffer vb = vertices_.handle();
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
    vkCmdBindIndexBuffer(cmd, indices_.handle(), 0, VK_INDEX_TYPE_UINT32);
}

void Mesh::drawRange(VkCommandBuffer cmd, uint32_t firstIndex, uint32_t indexCount, int32_t vertexOffset,
                     uint32_t instanceCount, uint32_t firstInstance) const
{
    vkCmdDrawIndexed(cmd, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

} // namespace render3d
