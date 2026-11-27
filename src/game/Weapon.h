#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

class World;

enum class WeaponType {
    Pistol = 0,
    Shotgun = 1,
    GravityGun = 2
};

struct WeaponState {
    WeaponType current = WeaponType::GravityGun;
    float fireCooldown = 0.0f;

    // Gravity Gun hold state (tracks Entity ID safely in case entity is destroyed while held)
    uint32_t heldEntityId = 0;
    JPH::BodyID heldBodyId;
    float holdDistance = 90.0f;
};

void UpdateWeapon(WeaponState& weapon, World& world, const glm::vec3& eyePos, const glm::vec3& viewDir,
                  bool primaryPressed, bool primaryJustPressed, bool secondaryJustPressed, float dt);

void ReleaseHeldObject(WeaponState& weapon, World& world);
