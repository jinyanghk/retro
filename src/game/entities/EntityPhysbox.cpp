#include "EntityPhysbox.h"
#include "../EntityFactory.h"
#include "../World.h"
#include "Physics.h"
#include <algorithm>

REGISTER_ENTITY("func_physbox", EntityPhysbox);

void PhysicsPropEntity::Think(World& world, float dt) {
    if (!HasBody()) return;

    float x, y, z;
    if (GetBodyPosition(m_bodyId, x, y, z)) {
        m_position = glm::vec3(x, y, z);
    }
}

void PhysicsPropEntity::Destroy(World& world) {
    if (HasBody()) {
        DestroyBodySafe(m_bodyId);
    }
    Entity::Destroy(world);
}

void PhysicsPropEntity::TakeDamage(World& world, float amount, Entity* attacker, const glm::vec3& force) {
    if (HasBody() && glm::length(force) > 0.001f) {
        SetBodyVelocity(m_bodyId, force.x, force.y, force.z);
    }
    Entity::TakeDamage(world, amount, attacker, force);
}

void EntityPhysbox::Spawn(World& world, const EntitySpawnData& data) {
    Entity::Spawn(world, data);

    glm::vec3 center = (m_bboxMins + m_bboxMaxs) * 0.5f;
    glm::vec3 size = m_bboxMaxs - m_bboxMins;
    m_halfExtents = glm::max(size * 0.5f, glm::vec3(2.0f));
    m_position = center;

    // Spawnflag 1024 or 4096 in HL2 = Prevent pickup / Motion disabled
    int spawnflags = data.GetInt("spawnflags", 0);
    m_canPickup = (spawnflags & 1024) == 0;

    JPH::BodyID id = CreatePhysBox(center.x, center.y, center.z,
                                   m_halfExtents.x, m_halfExtents.y, m_halfExtents.z);
    world.RegisterBody(id, this);
}
