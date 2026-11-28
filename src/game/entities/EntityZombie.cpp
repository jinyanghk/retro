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
    if (data.record) m_position = data.record->origin;

    if (m_health <= 0.0f) m_health = 100.0f;
    m_yaw = m_angles.y;

    // 1. 创建虚体角色（负责移动和寻路）
    m_charId = CreateEnemyCharacter(m_position.x, m_position.y + 36.0f, m_position.z);
    
    // 2. 创建 Hitbox（负责吃子弹，不挡路）
    //JPH::BodyID hitboxId = CreateTrapBody(m_position.x, m_position.y + 36.0f, m_position.z, 16.0f, 36.0f, 16.0f);
    //world.RegisterBody(hitboxId, this);
}

void EntityZombie::Think(World& world, float dt) {
    if (m_state == AIState::Dead) return;

    if (m_attackCooldown > 0.0f) m_attackCooldown -= dt;

    glm::vec3 playerPos = world.GetPlayerPosition();
    glm::vec3 toPlayer = playerPos - m_position;
    toPlayer.y = 0.0f;
    float dist = glm::length(toPlayer);

    glm::vec3 moveVel(0.0f);

    // ==========================================
    // 1. 状态切换
    // ==========================================
    if (dist > m_aggroRange) {
        m_state = AIState::Idle;
    } 
    else if (dist <= m_attackRange) {
        m_state = AIState::Attack;
        if (m_attackCooldown <= 0.0f) {
            m_attackCooldown = 1.2f;
            world.DamagePlayer(m_attackDamage);
        }
    } 
    else {
        m_state = AIState::Chase;
    }

    // ==========================================
    // 2. 动作执行
    // ==========================================
    if (m_state == AIState::Chase) {
        // 更新寻路 (假设你用的是世界过滤后的 NavNodes)
        UpdateNavigation(world, playerPos, dt);

        glm::vec3 moveTarget = !m_path.empty() ? m_path.front() : playerPos;
        glm::vec3 dir = moveTarget - m_position;
        dir.y = 0.0f;
        float len = glm::length(dir);
        
        if (len > 0.1f) {
            dir = glm::normalize(dir);
            
            // 👈 修复转向：让模型面朝移动方向
            m_yaw = glm::degrees(std::atan2(dir.z, dir.x));
            
            moveVel = glm::vec3(dir.x * m_moveSpeed, 0.0f, dir.z * m_moveSpeed);
        }
    } else if (m_state == AIState::Attack) {
        // 👈 修复朝向：攻击时，面朝玩家
        if (dist > 0.1f) {
            glm::vec3 dirToPlayer = glm::normalize(toPlayer);
            m_yaw = glm::degrees(std::atan2(dirToPlayer.z, dirToPlayer.x));
        }
    }

    // ==========================================
    // 3. 物理移动与 Hitbox 同步
    // ==========================================
    bool grounded = false;
    // 👈 注意：如果 UpdateEnemyCharacter 返回的是胶囊体中心（Y+36），需要减去 36
    glm::vec3 newPos = UpdateEnemyCharacter(m_charId, moveVel, dt, grounded);
    m_position = glm::vec3(newPos.x, newPos.y - 36.0f, newPos.z);
    
    //if (HasBody()) {
    //    MoveKinematicBody(m_bodyId, newPos.x, newPos.y, newPos.z);
    //}

    // 👇 把打印放在这里，打印真实发生后的状态！
    //printf("[Zombie] state:%d | pos:(%.1f, %.1f, %.1f) | vel:(%.1f, %.1f, %.1f) | grounded:%s\n",
    //       (int)m_state, m_position.x, m_position.y, m_position.z,
    //       moveVel.x, moveVel.y, moveVel.z, grounded ? "YES" : "NO");
}

void EntityZombie::UpdateNavigation(World& world, const glm::vec3& targetPos, float dt) {
    m_repathTimer -= dt;
    if (m_repathTimer <= 0.0f) {
        m_repathTimer = 0.5f; // 每 0.5 秒重新计算一次路径
        const MapData* map = world.GetMapData();
        if (map) {
            m_path = FindPath(*map, m_position, targetPos);
        } else {
            m_path = { targetPos };
        }
    }

    // 如果路径前方有节点，且已经到了，就删除它
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
