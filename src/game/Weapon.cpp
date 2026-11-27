#include "Weapon.h"
#include "World.h"
#include "Entity.h"
#include "Physics.h"
#include <algorithm>

void ReleaseHeldObject(WeaponState& weapon, World& world) {
    if (!weapon.heldBodyId.IsInvalid()) {
        SetBodyGravityFactor(weapon.heldBodyId, 1.0f);
        weapon.heldBodyId = JPH::BodyID();
    }
    weapon.heldEntityId = 0;
}

static void FireHitscan(World& world, const glm::vec3& eyePos, const glm::vec3& viewDir, float damage, float impulse) {
    WorldRaycastHit hit = world.Raycast(eyePos, viewDir, 4000.0f);
    if (!hit.hit) return;

    if (hit.entity) {
        hit.entity->TakeDamage(world, damage, nullptr, viewDir * impulse);
    } else if (!hit.bodyId.IsInvalid()) {
        // Fallback for non-entity dynamic bodies
        SetBodyVelocity(hit.bodyId, viewDir.x * impulse, viewDir.y * impulse, viewDir.z * impulse);
    }
}

static void UpdateGravityGun(WeaponState& weapon, World& world, const glm::vec3& eyePos, const glm::vec3& viewDir,
                             bool primaryJustPressed, bool secondaryJustPressed, float dt) {
    // Validate currently held entity still exists
    Entity* heldEnt = (weapon.heldEntityId != 0) ? world.GetEntityById(weapon.heldEntityId) : nullptr;
    if (!heldEnt || heldEnt->IsPendingKill() || !heldEnt->HasBody()) {
        if (weapon.heldEntityId != 0) {
            ReleaseHeldObject(weapon, world);
        }
    }

    // Secondary Fire (Right Click): Grab or Drop
    if (secondaryJustPressed) {
        if (heldEnt) {
            ReleaseHeldObject(weapon, world);
            return;
        }

        WorldRaycastHit hit = world.Raycast(eyePos, viewDir, 320.0f);
        if (hit.hit && hit.entity && hit.entity->CanBePickedUp()) {
            weapon.heldEntityId = hit.entity->GetId();
            weapon.heldBodyId = hit.entity->GetBodyId();
            weapon.holdDistance = std::clamp(hit.distance, 65.0f, 120.0f);
            SetBodyGravityFactor(weapon.heldBodyId, 0.0f);
        }
        return;
    }

    // Primary Fire (Left Click): Punt held object OR blast target in front
    if (primaryJustPressed) {
        if (heldEnt) {
            JPH::BodyID body = heldEnt->GetBodyId();
            ReleaseHeldObject(weapon, world);
            glm::vec3 launchVel = viewDir * 1400.0f;
            SetBodyVelocity(body, launchVel.x, launchVel.y, launchVel.z);
            heldEnt->TakeDamage(world, 25.0f, nullptr, launchVel);
            weapon.fireCooldown = 0.25f;
            return;
        }

        WorldRaycastHit hit = world.Raycast(eyePos, viewDir, 380.0f);
        if (hit.hit && hit.entity) {
            glm::vec3 puntForce = viewDir * 1100.0f + glm::vec3(0.0f, 180.0f, 0.0f);
            hit.entity->TakeDamage(world, 20.0f, nullptr, puntForce);
            weapon.fireCooldown = 0.35f;
        }
        return;
    }

    // Smooth spring-velocity pull while holding an object
    if (heldEnt && !weapon.heldBodyId.IsInvalid()) {
        glm::vec3 targetPos = eyePos + viewDir * weapon.holdDistance;
        glm::vec3 currentPos = heldEnt->GetPosition();
        glm::vec3 pullVel = (targetPos - currentPos) * 14.0f;
        SetBodyVelocity(weapon.heldBodyId, pullVel.x, pullVel.y, pullVel.z);
    }
}

void UpdateWeapon(WeaponState& weapon, World& world, const glm::vec3& eyePos, const glm::vec3& viewDir,
                  bool primaryPressed, bool primaryJustPressed, bool secondaryJustPressed, float dt) {
    if (weapon.fireCooldown > 0.0f) {
        weapon.fireCooldown = std::max(0.0f, weapon.fireCooldown - dt);
    }

    if (weapon.current == WeaponType::GravityGun) {
        UpdateGravityGun(weapon, world, eyePos, viewDir, primaryJustPressed, secondaryJustPressed, dt);
        return;
    }

    // Drop held Gravity Gun object if player switched weapons
    if (weapon.heldEntityId != 0) {
        ReleaseHeldObject(weapon, world);
    }

    if (weapon.fireCooldown > 0.0f) return;

    if (weapon.current == WeaponType::Pistol && primaryJustPressed) {
        FireHitscan(world, eyePos, viewDir, 25.0f, 350.0f);
        weapon.fireCooldown = 0.18f;
    } else if (weapon.current == WeaponType::Shotgun && primaryJustPressed) {
        // 6-pellet spread
        for (int i = 0; i < 6; ++i) {
            glm::vec3 jitter(
                ((i % 3) - 1) * 0.03f,
                ((i / 3) - 0.5f) * 0.03f,
                (((i + 1) % 3) - 1) * 0.03f
            );
            FireHitscan(world, eyePos, glm::normalize(viewDir + jitter), 14.0f, 220.0f);
        }
        weapon.fireCooldown = 0.75f;
    }
}
