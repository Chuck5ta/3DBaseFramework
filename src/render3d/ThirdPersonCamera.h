#pragma once

// A camera that follows a character from behind and orbits around it: the view of most
// third-person action games.
//
//     render3d::ThirdPersonCamera follow;           // a member of your game
//
//     // every frame, after moving the player:
//     follow.orbit(mouseDelta.x * 0.0025f, -mouseDelta.y * 0.0025f);
//     follow.zoom(in.mouseWheel());
//     follow.update(playerPosition, dt, blockedFn);  // blockedFn may be left out
//     follow.apply(camera_);                         // then draw with camera_ as usual
//
// Move the player relative to the view with flatForward() and flatRight(), so "up" on
// the stick always walks away from the camera.
//
// - It looks at `target + targetOffset` (the head or shoulders, not the feet), shifted
//   sideways by `shoulder` for an over-the-shoulder view (0 = centred).
// - The target is followed with a little lag (`followSharpness`), which hides small
//   jerks in the movement; the zoom eases the same way.
// - Collision: give update() a function that casts a ray into your world (see Raycast.h)
//   and returns the distance to the first hit. When something comes between the target
//   and the camera, the camera jumps in front of it at once (so the view is never
//   blocked), and eases back out once the way is clear.
//
// yaw and pitch use Camera3D's convention, in radians: they are the direction the camera
// looks. yaw = -pi/2 looks along -Z; a negative pitch looks down on the target.

#include "render3d/Camera3D.h"
#include "render3d/Raycast.h"

#include <glm/vec3.hpp>

#include <functional>
#include <optional>

namespace render3d {

class ThirdPersonCamera {
public:
    // Casts a ray into the world; returns the distance to the first hit, or nothing.
    using RaycastFn = std::function<std::optional<float>(const Ray& ray)>;

    float yaw = -1.5707963f;  // radians: -pi/2 = looking along -Z (behind a player facing -Z)
    float pitch = -0.3f;      // radians: negative looks down
    float distance = 4.5f;    // wanted distance from the look-at point; zoom() changes it

    glm::vec3 targetOffset{0.0f, 1.55f, 0.0f}; // from the target (the feet) to the look-at point
    float shoulder = 0.45f;   // sideways shift of the look-at point (+ = right); 0 = centred
    float minDistance = 1.5f;
    float maxDistance = 12.0f;
    float minPitch = -1.25f;  // looking down at most ~72 degrees
    float maxPitch = 0.6f;    // looking up at most ~34 degrees

    float followSharpness = 12.0f; // how tightly it follows the target (higher = tighter, 0 = rigid)
    float zoomSharpness = 10.0f;   // how quickly zoom() takes effect
    float returnSharpness = 4.0f;  // how quickly it moves back out after a collision
    float collisionRadius = 0.25f; // the camera keeps this far from what it hits

    // Turn around the target (radians); pitch is kept between minPitch and maxPitch.
    void orbit(float yawDelta, float pitchDelta);
    // Positive steps move in (each step is 15% closer), negative out: pass the mouse wheel.
    void zoom(float steps);

    // Follows `target` (call once per frame, after the target moved). The first call,
    // and snapTo(), place the camera without any easing.
    void update(const glm::vec3& target, float dt, const RaycastFn& blocked = {});
    void snapTo(const glm::vec3& target);

    // Copies the position and direction into a Camera3D (its fov and planes are kept).
    void apply(Camera3D& camera) const;

    glm::vec3 position() const { return lookAt_ - forward() * current_; }
    glm::vec3 lookAtPoint() const { return lookAt_; }
    float currentDistance() const { return current_; } // shorter than `distance` while blocked
    bool blocked() const { return blocked_; }

    glm::vec3 forward() const;     // the view direction
    glm::vec3 flatForward() const; // the view direction along the ground (for movement)
    glm::vec3 flatRight() const;   // to the right of the view, along the ground

private:
    glm::vec3 lookPoint(const RaycastFn& blocked) const;

    bool started_ = false;
    glm::vec3 pivot_{0.0f};  // the smoothed target
    glm::vec3 lookAt_{0.0f};
    float zoom_ = 4.5f;      // the smoothed `distance`
    float current_ = 4.5f;   // the actual distance, after collisions
    bool blocked_ = false;
};

} // namespace render3d
