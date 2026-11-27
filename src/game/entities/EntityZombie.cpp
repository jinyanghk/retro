#include "EntityZombie.h"
#include "../EntityFactory.h"
#include "../World.h"
#include "../AStar.h"
#include "Physics.h"
#include <cmath>
#include <algorithm>

REGISTER_ENTITY("npc_zombie", EntityZombie);

void EntityZombie::Spawn(World& world, const EntitySpawnData& data) {
    Entity::Spawn(world, data);

    if (m_health <= 0.0f) m_health = 100.0f;
    m_yaw = m_angles.y;

    // Create Jolt CharacterVirtual capsule + raycast hitbox body
    m_charId = CreateCharacter(m_position.x, m_position.y + 36.0f, m_position.z, 16.0f, 36.0f);
    JPH::BodyID hitboxId = CreateEnemyHitbox(m_position.x, m_position.y + 36.0f, m_position.z, 16.0f, 36.0f);
    world.RegisterBody(hitboxId, this);
}

void EntityZombie::Think(World& world, float dt) {
    if (m_state == AIState::Dead) return;

    if (m_attackCooldown > 0.0f) {
        m_attackCooldown -= dt;
    }

    glm::vec3 playerPos = world.GetPlayerPosition();
    glm::vec3 toPlayer = playerPos - m_position;
    toPlayer.y = 0.0f;
    float dist = glm::length(toPlayer);

    if (dist > m_aggroRange) {
        m_state = AIState::Idle;
        UpdateCharacter(m_charId, 0.0f, 0.0f, dt);
    } else if (dist <= m_attackRange) {
        m_state = AIState::Attack;
        UpdateCharacter(m_charId, 0.0f, 0.0f, dt);

        if (dist > 0.01f) {
            m_yaw = glm::degrees(std::atan2(toPlayer.z, toPlayer.x));
        }

        if (m_attackCooldown <= 0.0f) {
            m_attackCooldown = 1.2f;
            world.DamagePlayer(m_attackDamage);
        }
    } else {
        m_state = AIState::Chase;
        UpdateNavigation(world, playerPos, dt);
    }

    // Sync position & hitbox from Jolt character
    float cx, cy, cz;
    if (GetCharacterPosition(m_charId, cx, cy, cz)) {
        m_position = glm::vec3(cx, cy - 36.0f, cz);
        if (HasBody()) {
            MoveKinematicBody(m_bodyId, cx, cy, cz);
        }
    }
}

void EntityZombie::UpdateNavigation(World& world, const glm::vec3& targetPos, float dt) {
    m_repathTimer -= dt;
    if (m_repathTimer <= 0.0f) {
        m_repathTimer = 0.5f;
        const auto* nav = world.GetNavGraph();
        if (nav && !nav->nodes.empty()) {
            m_path = FindPathAStar(*nav, m_position, targetPos);
        } else {
            m_path = { targetPos };
        }
    }

    glm::vec3 moveTarget = targetPos;
    if (!m_path.empty()) {
        moveTarget = m_path.front();
        glm::vec3 flatDiff = moveTarget - m_position;
        flatDiff.y = 0.0f;
        if (glm::length(flatDiff) < 28.0f && m_path.size() > 1) {
            m_path.erase(m_path.begin());
            moveTarget = m_path.front();
        }
    }

    glm::vec3 dir = moveTarget - m_position;
    dir.y = 0.0f;
    float len = glm::length(dir);
    if (len > 0.01f) {
        dir /= len;
        m_yaw = glm::degrees(std::atan2(dir.z, dir.x));
        UpdateCharacter(m_charId, dir.x * m_moveSpeed, dir.z * m_moveSpeed, dt);
    } else {
        UpdateCharacter(m_charId, 0.0f, 0.0f, dt);
    }
}

void EntityZombie::TakeDamage(World& world, float amount, Entity* attacker, const glm::vec3& force) {
    if (m_state == AIState::Dead) return;

    m_health -= amount;
    if (m_health <= 0.0f) {
        m_health = 0.0f;
        m_state = AIState::Dead;
        FireOutput(world, "OnDeath", attacker);
        MarkForKill();
    }
}

void EntityZombie::Destroy(World& world) {
    if (HasBody()) {
        DestroyBodySafe(m_bodyId);
    }
    if (m_charId >= 0) {
        DestroyCharacter(m_charId);
        m_charId = -1;
    }
    Entity::Destroy(world);
}
