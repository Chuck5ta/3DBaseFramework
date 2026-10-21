#include "render3d/Camera3D.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace render3d {

glm::vec3 Camera3D::forward() const
{
    return glm::normalize(glm::vec3(std::cos(pitch) * std::cos(yaw), std::sin(pitch),
                                    std::cos(pitch) * std::sin(yaw)));
}

glm::vec3 Camera3D::right() const
{
    return glm::normalize(glm::cross(forward(), glm::vec3(0.0f, 1.0f, 0.0f)));
}

glm::vec3 Camera3D::up() const
{
    return glm::cross(right(), forward());
}

void Camera3D::lookAt(const glm::vec3& target)
{
    const glm::vec3 d = glm::normalize(target - position);
    pitch = std::asin(std::clamp(d.y, -1.0f, 1.0f));
    yaw = std::atan2(d.z, d.x);
}

glm::mat4 Camera3D::view() const
{
    return glm::lookAt(position, position + forward(), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera3D::projection() const
{
    glm::mat4 p = glm::perspective(fovY, aspect_, nearPlane, farPlane);
    p[1][1] *= -1.0f; // Vulkan clip space Y points down
    return p;
}

} // namespace render3d
