#pragma once

// The space a camera can see: six planes (left, right, bottom, top, near, far) around a
// truncated pyramid. Things wholly outside it can't be on screen, so they needn't be drawn
// ("frustum culling"). MeshRenderer culls with one for you; use your own to skip work
// for things off screen (animating, updating effects, playing footsteps...).
//
//     render3d::Frustum view(camera.viewProjection());
//     if (view.intersects(model.boundsMin(), model.boundsMax(), transform))
//         animate(...);
//
// It works for any projection: the sun's shadow map is a box-shaped "frustum" too.

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>

namespace render3d {

class Frustum {
public:
    // Contains everything: nothing is culled.
    Frustum();
    // The volume a view-projection matrix maps into the screen (Vulkan's clip space:
    // -w <= x, y <= w and 0 <= z <= w).
    explicit Frustum(const glm::mat4& viewProjection);

    // Could any part of the box min..max, placed by `transform`, be inside? The box is
    // tested as placed (turned and scaled with the object, not a box around it), against
    // one plane at a time: a box just outside a corner can still answer true, never the
    // other way round. That's the usual trade: drawing a few extra is harmless.
    bool intersects(const glm::vec3& min, const glm::vec3& max, const glm::mat4& transform = glm::mat4(1.0f)) const;
    bool intersectsSphere(const glm::vec3& centre, float radius) const;

    // Each plane as (normal, d): a point p is on the inside when dot(normal, p) + d >= 0.
    // Normals are unit length, so that's also the distance from the plane in world units.
    const std::array<glm::vec4, 6>& planes() const { return planes_; }

private:
    std::array<glm::vec4, 6> planes_;
};

} // namespace render3d
