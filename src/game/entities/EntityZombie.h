#pragma once

#include "../Entity.h"
#include <vector>

class EntityZombie : public Entity {
public:
    enum class AIState { Idle, Chase, Attack, Dead };

    void Spawn(World& world, const EntitySpawnData& data) override;
    void Think(World& world, float dt) override;
    void Destroy(World& world) override;
    void TakeDamage(World& world, float amount, Entity* attacker, const glm::vec3& force) override;

    AIState GetAIState() const { return m_state; }
    float GetYaw() const { return m_yaw; }

    int GetRenderState() const {
        if (m_state == AIState::Idle) return 0;
        if (m_state == AIState::Chase) return 1;
        if (m_state == AIState::Attack) return 2;
        if (m_state == AIState::Dead) return 4;
        return 0;
    }

    bool IsZombie() const { return true; } // 👈 加上这句！

private:
    void UpdateNavigation(World& world, const glm::vec3& targetPos, float dt);

    int m_charId = -1;
    AIState m_state = AIState::Idle;
    float m_yaw = 0.0f;

    float m_moveSpeed = 95.0f;
    float m_aggroRange = 850.0f;
    float m_attackRange = 58.0f;
    float m_attackDamage = 15.0f;
    float m_attackCooldown = 0.0f;

    std::vector<glm::vec3> m_path;
    float m_repathTimer = 0.0f;
};
