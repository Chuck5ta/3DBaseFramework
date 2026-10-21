#pragma once

#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace render3d {

// Perspective camera. Right-handed world, +Y up, default looking down -Z.
// yaw/pitch are radians; yaw = -pi/2 looks along -Z.
class Camera3D {
public:
    glm::vec3 position{0.0f, 2.0f, 8.0f};
    float yaw = -glm::half_pi<float>();
    float pitch = 0.0f;
    float fovY = glm::radians(60.0f);
    float nearPlane = 0.1f;
    float farPlane = 500.0f;

    void setAspect(float aspect) { aspect_ = aspect; }
    float aspect() const { return aspect_; }

    glm::vec3 forward() const;
    glm::vec3 right() const;
    glm::vec3 up() const;

    void lookAt(const glm::vec3& target);

    glm::mat4 view() const;
    // Vulkan conventions: depth [0, 1], Y flipped so +Y is up on screen.
    glm::mat4 projection() const;
    glm::mat4 viewProjection() const { return projection() * view(); }

private:
    float aspect_ = 16.0f / 9.0f;
};

} // namespace render3d
