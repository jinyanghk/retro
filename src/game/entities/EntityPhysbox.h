#pragma once

#include "../Entity.h"

class PhysicsPropEntity : public Entity {
public:
    void Think(World& world, float dt) override;
    void Destroy(World& world) override;
    void TakeDamage(World& world, float amount, Entity* attacker, const glm::vec3& force) override;

    bool CanBePickedUp() const override { return m_canPickup && HasBody(); }
    glm::vec3 GetHalfExtents() const { return m_halfExtents; }
    const std::string& GetModelName() const { return m_modelName; }

protected:
    glm::vec3 m_halfExtents{16.0f};
    std::string m_modelName;
    bool m_canPickup = true;
};

class EntityPhysbox : public PhysicsPropEntity {
public:
    void Spawn(World& world, const EntitySpawnData& data) override;
};
