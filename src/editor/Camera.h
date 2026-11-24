#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

struct Camera {
    glm::vec3 target = glm::vec3(0, 100, 0);
    float yaw   = 0.0f;
    float pitch = 0.0f;
    float dist  = 200.0f;

    glm::vec3 GetEye() const {
        float rp = glm::radians(pitch), ry = glm::radians(yaw);
        return target + glm::vec3(std::cos(rp)*std::cos(ry), std::sin(rp), std::cos(rp)*std::sin(ry)) * dist;
    }
    glm::mat4 GetView() const { return glm::lookAt(GetEye(), target, glm::vec3(0,1,0)); }
};

extern Camera g_cam;