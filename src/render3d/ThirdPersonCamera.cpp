#include "render3d/ThirdPersonCamera.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace render3d {

namespace {

// Moves `value` towards `target` by a fraction that depends on dt the right way:
// the same motion at 30 or 300 fps. sharpness <= 0 means "jump there".
template <typename T>
T follow(const T& value, const T& target, float sharpness, float dt)
{
    if (sharpness <= 0.0f)
        return target;
    return value + (target - value) * (1.0f - std::exp(-sharpness * dt));
}

} // namespace

void ThirdPersonCamera::orbit(float yawDelta, float pitchDelta)
{
    yaw = std::remainder(yaw + yawDelta, 6.2831853f); // keep it in -pi..pi
    pitch = std::clamp(pitch + pitchDelta, minPitch, maxPitch);
}

void ThirdPersonCamera::zoom(float steps)
{
    distance = std::clamp(distance * std::pow(0.85f, steps), minDistance, maxDistance);
}

glm::vec3 ThirdPersonCamera::forward() const
{
    return {std::cos(pitch) * std::cos(yaw), std::sin(pitch), std::cos(pitch) * std::sin(yaw)};
}

glm::vec3 ThirdPersonCamera::flatForward() const
{
    return {std::cos(yaw), 0.0f, std::sin(yaw)};
}

glm::vec3 ThirdPersonCamera::flatRight() const
{
    return {-std::sin(yaw), 0.0f, std::cos(yaw)};
}

// The point the camera looks at: above the target, shifted towards the shoulder (but
// not into a wall beside the player).
glm::vec3 ThirdPersonCamera::lookPoint(const RaycastFn& blocked) const
{
    const glm::vec3 head = pivot_ + targetOffset;
    float side = shoulder;
    if (blocked && std::abs(side) > 1e-4f) {
        const Ray ray{head, flatRight() * (side > 0.0f ? 1.0f : -1.0f)};
        if (const auto hit = blocked(ray); hit && *hit < std::abs(side) + collisionRadius)
            side = std::copysign(std::max(0.0f, *hit - collisionRadius), side);
    }
    return head + flatRight() * side;
}

void ThirdPersonCamera::snapTo(const glm::vec3& target)
{
    started_ = true;
    pivot_ = target;
    distance = std::clamp(distance, minDistance, maxDistance);
    zoom_ = current_ = distance;
    lookAt_ = pivot_ + targetOffset + flatRight() * shoulder;
    blocked_ = false;
}

void ThirdPersonCamera::update(const glm::vec3& target, float dt, const RaycastFn& blocked)
{
    pitch = std::clamp(pitch, minPitch, maxPitch); // in case they were set directly
    distance = std::clamp(distance, minDistance, maxDistance);
    if (!started_)
        snapTo(target);

    pivot_ = follow(pivot_, target, followSharpness, dt);
    zoom_ = follow(zoom_, distance, zoomSharpness, dt);
    lookAt_ = lookPoint(blocked);

    // How far back the camera can go: cast five rays backwards, from the look-at point
    // and from points around it as wide as the camera, so it also stops short of
    // corners and edges that a single thin ray would slip past.
    float room = zoom_;
    if (blocked) {
        const glm::vec3 back = -forward();
        const glm::vec3 side = glm::normalize(glm::cross(back, glm::vec3(0.0f, 1.0f, 0.0f))) * collisionRadius;
        const glm::vec3 up = glm::cross(side, back);
        const glm::vec3 offsets[5] = {glm::vec3(0.0f), side, -side, up, -up};
        for (const glm::vec3& offset : offsets)
            if (const auto hit = blocked(Ray{lookAt_ + offset, back}); hit && *hit < room + collisionRadius)
                room = std::min(room, std::max(0.0f, *hit - collisionRadius));
    }
    blocked_ = room < zoom_ - 1e-3f;

    // Blocked: jump in front of the obstacle now. Clear: ease back out.
    current_ = room < current_ ? room : follow(current_, room, returnSharpness, dt);
}

void ThirdPersonCamera::apply(Camera3D& camera) const
{
    camera.position = position();
    camera.yaw = yaw;
    camera.pitch = pitch;
}

} // namespace render3d
