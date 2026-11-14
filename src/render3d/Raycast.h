#pragma once

// Ray tests against simple shapes: "how far along this line until it hits something?"
// Used by ThirdPersonCamera to keep walls out of the view; also handy for shooting,
// line of sight and picking things with the mouse.
//
//     render3d::Ray ray{eye, glm::normalize(target - eye)};
//     if (auto d = render3d::raycastBox(ray, crateMatrix))   // the crate's model matrix
//         hitPoint = ray.at(*d);
//
// Each test returns the distance along the ray to the first hit, or nothing. A ray that
// starts inside a box or sphere doesn't hit it (it is on its way out), so a camera whose
// target stands inside an object isn't blocked by it.

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <optional>

namespace render3d {

struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f}; // must be normalised: distances are in world units

    glm::vec3 at(float distance) const { return origin + direction * distance; }
};

// An infinite plane through `point`, facing `normal`; hit from either side.
std::optional<float> raycastPlane(const Ray& ray, const glm::vec3& point, const glm::vec3& normal);

std::optional<float> raycastSphere(const Ray& ray, const glm::vec3& centre, float radius);

// A box drawn with `transform` (a model matrix: position, rotation, scale), with
// half-size `halfExtents` before that transform. The default fits primitives::cube(1.0f),
// so the same matrix you draw a crate with tests against that crate.
std::optional<float> raycastBox(const Ray& ray, const glm::mat4& transform,
                                const glm::vec3& halfExtents = glm::vec3(0.5f));

} // namespace render3d
