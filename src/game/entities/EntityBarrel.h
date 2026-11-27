#pragma once

#include "EntityPhysbox.h"

class EntityBarrel : public PhysicsPropEntity {
public:
    void Spawn(World& world, const EntitySpawnData& data) override;
    void TakeDamage(World& world, float amount, Entity* attacker, const glm::vec3& force) override;

    bool IsExplosive() const { return m_isExplosive; }
    bool HasExploded() const { return m_exploded; }

private:
    void Explode(World& world, Entity* attacker);

    bool m_isExplosive = false;
    bool m_exploded = false;
    float m_explodeDamage = 120.0f;
    float m_explodeRadius = 250.0f;
};
