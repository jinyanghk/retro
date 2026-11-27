#include "EntityZombie.h"
#include "../EntityFactory.h"
#include "../World.h"
#include "../AStar.h"
#include "MapFormat.h"
#include "Physics.h"
#include <cmath>
#include <algorithm>

REGISTER_ENTITY("npc_zombie", EntityZombie);

void EntityZombie::Spawn(World& world, const EntitySpawnData& data) {
    Entity::Spawn(world, data);

    if (m_health <= 0.0f) m_health = 100.0f;
    m_yaw = m_angles.y;

    m_charId = CreateEnemyCharacter(m_position.x, m_position.y + 36.0f, m_position.z);
    JPH::BodyID hitboxId = CreateDoorBody(m_position.x, m_position.y + 36.0f, m_position.z, 17.0f, 36.0f, 17.0f);
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

    glm::vec3 moveVel(0.0f);

    if (dist > m_aggroRange) {
        m_state = AIState::Idle;
    } else if (dist <= m_attackRange) {
        m_state = AIState::Attack;

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

        glm::vec3 moveTarget = !m_path.empty() ? m_path.front() : playerPos;
        glm::vec3 dir = moveTarget - m_position;
        dir.y = 0.0f;
        float len = glm::length(dir);
        if (len > 0.01f) {
            dir /= len;
            m_yaw = glm::degrees(std::atan2(dir.z, dir.x));
            moveVel = glm::vec3(dir.x * m_moveSpeed, 0.0f, dir.z * m_moveSpeed);
        }
    }

    bool grounded = false;
    glm::vec3 newPos = UpdateEnemyCharacter(m_charId, moveVel, dt, grounded);
    m_position = glm::vec3(newPos.x, newPos.y - 36.0f, newPos.z);
    if (HasBody()) {
        MoveKinematicBody(m_bodyId, newPos.x, newPos.y, newPos.z);
    }
}

void EntityZombie::UpdateNavigation(World& world, const glm::vec3& targetPos, float dt) {
    m_repathTimer -= dt;
    if (m_repathTimer <= 0.0f) {
        m_repathTimer = 0.5f;
        const MapData* map = world.GetMapData();
        if (map && !map->navNodes.empty()) {
            m_path = FindPath(*map, m_position, targetPos);
        } else {
            m_path = { targetPos };
        }
    }

    if (!m_path.empty()) {
        glm::vec3 flatDiff = m_path.front() - m_position;
        flatDiff.y = 0.0f;
        if (glm::length(flatDiff) < 28.0f && m_path.size() > 1) {
            m_path.erase(m_path.begin());
        }
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
        DestroyEnemyCharacter(m_charId);
        m_charId = -1;
    }
    Entity::Destroy(world);
}
