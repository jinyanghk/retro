#pragma once

#include <glm/glm.hpp>
#include <cmath>
#include <vector>

struct PlayerState {
    glm::vec3 pos{0.0f};
    glm::vec3 vel{0.0f};
    float camYaw = 0.0f;
    float camPitch = 0.0f;
    float radius = 16.0f;
    float height = 72.0f;
    float halfHeight = 36.0f;
    float eyeHeight = 28.0f;

    float moveSpeed = 240.0f;
    float jumpSpeed = 260.0f;
    float mouseSensitivity = 0.15f;

    float health = 100.0f;
    bool isGrounded = false;

    float damageTimer = 0.0f;
    float shakeTimer = 0.0f;
    float shakeAmount = 0.0f;

    glm::vec3 GetEyePos() const {
        return pos + glm::vec3(0.0f, eyeHeight, 0.0f);
    }

    glm::vec3 GetForward() const {
        float y = glm::radians(camYaw);
        float p = glm::radians(camPitch);
        return glm::normalize(glm::vec3(
            std::cos(y) * std::cos(p),
            std::sin(p),
            std::sin(y) * std::cos(p)
        ));
    }
};

struct BulletHole {
    glm::vec3 pos{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float life = 15.0f;
};

struct GameRenderBox {
    glm::vec3 center{0.0f};
    glm::vec3 halfExtents{0.0f};
    glm::vec3 color{1.0f};
    float yaw = 0.0f;
    int entityType = 0; // 0 = physbox, 1 = door, 2 = barrel, 3 = zombie
    int animState = 0;  // 0 = IDLE, 1 = CHASE, 2 = ATTACK, 3 = STUNNED, 4 = DEAD
};

struct GameRenderSnapshot {
    const GameRenderBox* boxes = nullptr;
    int boxCount = 0;

    int activeWeapon = 2; // 0 = Pistol, 1 = Shotgun, 2 = GravityGun
    bool holdingObject = false;
    glm::vec3 heldCenter{0.0f};

    float recoilTimer = 0.0f;
    float recoilDuration = 0.25f;
    float crosshairHitTimer = 0.0f;
};
