#include "render3d/Raycast.h"

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace render3d {

std::optional<float> raycastPlane(const Ray& ray, const glm::vec3& point, const glm::vec3& normal)
{
    const float facing = glm::dot(ray.direction, normal);
    if (std::abs(facing) < 1e-6f)
        return std::nullopt; // parallel to the plane
    const float t = glm::dot(point - ray.origin, normal) / facing;
    if (t < 0.0f)
        return std::nullopt; // behind the ray
    return t;
}

std::optional<float> raycastSphere(const Ray& ray, const glm::vec3& centre, float radius)
{
    const glm::vec3 toCentre = centre - ray.origin;
    const float c = glm::dot(toCentre, toCentre) - radius * radius;
    if (c <= 0.0f)
        return std::nullopt; // starts inside
    const float b = glm::dot(toCentre, ray.direction); // distance to the closest approach
    const float d = b * b - c;
    if (b <= 0.0f || d < 0.0f)
        return std::nullopt; // pointing away, or passing by
    return b - std::sqrt(d);
}

std::optional<float> raycastBox(const Ray& ray, const glm::mat4& transform, const glm::vec3& halfExtents)
{
    // Move the ray into the box's own space, where the box is axis-aligned. The same t
    // gives the same point in both spaces (the map is linear), so distances stay in
    // world units even when the box is scaled.
    const glm::mat4 toLocal = glm::inverse(transform);
    const glm::vec3 o(toLocal * glm::vec4(ray.origin, 1.0f));
    const glm::vec3 d(toLocal * glm::vec4(ray.direction, 0.0f));

    // Slab test: the ray is inside the box between where it enters all three pairs of
    // faces and where it leaves the first one.
    float enter = -std::numeric_limits<float>::infinity();
    float leave = std::numeric_limits<float>::infinity();
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(d[axis]) < 1e-8f) {
            if (std::abs(o[axis]) > halfExtents[axis])
                return std::nullopt; // parallel to these faces and outside them
            continue;
        }
        float t0 = (-halfExtents[axis] - o[axis]) / d[axis];
        float t1 = (halfExtents[axis] - o[axis]) / d[axis];
        if (t0 > t1)
            std::swap(t0, t1);
        enter = std::max(enter, t0);
        leave = std::min(leave, t1);
        if (enter > leave)
            return std::nullopt;
    }
    if (enter < 0.0f)
        return std::nullopt; // starts inside, or the box is behind the ray
    return enter;
}

} // namespace render3d
