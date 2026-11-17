#include "render3d/Frustum.h"

#include <glm/geometric.hpp>

#include <cmath>

namespace render3d {

Frustum::Frustum()
{
    planes_.fill(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)); // 0 * p + 1 >= 0 everywhere
}

Frustum::Frustum(const glm::mat4& m)
{
    // A point p is on screen when its clip-space position c = m * p has -w <= x <= w,
    // -w <= y <= w and 0 <= z <= w. Each of those is a plane in world space, read straight
    // out of the matrix's rows (Gribb & Hartmann): for example x >= -w is
    // dot(row3 + row0, p) >= 0. glm stores columns, so row i is (m[0][i], .., m[3][i]).
    const auto row = [&](int i) { return glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]); };
    planes_ = {row(3) + row(0), row(3) - row(0),  // left, right
               row(3) + row(1), row(3) - row(1),  // bottom, top (in clip space; Vulkan's y points down)
               row(2), row(3) - row(2)};          // near (z >= 0), far (z <= w)
    for (glm::vec4& p : planes_) {
        const float length = glm::length(glm::vec3(p));
        if (length > 0.0f)
            p /= length;
    }
}

bool Frustum::intersects(const glm::vec3& min, const glm::vec3& max, const glm::mat4& transform) const
{
    // The box as placed: a centre and three half-size axes (the transform's columns, scaled).
    const glm::vec3 half = (max - min) * 0.5f;
    const glm::vec3 centre = glm::vec3(transform * glm::vec4((min + max) * 0.5f, 1.0f));
    const glm::vec3 ax = glm::vec3(transform[0]) * half.x;
    const glm::vec3 ay = glm::vec3(transform[1]) * half.y;
    const glm::vec3 az = glm::vec3(transform[2]) * half.z;
    for (const glm::vec4& p : planes_) {
        const glm::vec3 n(p);
        // How far the box reaches towards the plane's inside, from its centre...
        const float reach = std::abs(glm::dot(n, ax)) + std::abs(glm::dot(n, ay)) + std::abs(glm::dot(n, az));
        // ...isn't enough to get there: the whole box is outside this plane.
        if (glm::dot(n, centre) + p.w < -reach)
            return false;
    }
    return true;
}

bool Frustum::intersectsSphere(const glm::vec3& centre, float radius) const
{
    for (const glm::vec4& p : planes_)
        if (glm::dot(glm::vec3(p), centre) + p.w < -radius)
            return false;
    return true;
}

} // namespace render3d
