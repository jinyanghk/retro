#include "EntityBarrel.h"
#include "../EntityFactory.h"
#include "../World.h"
#include "Physics.h"
#include <algorithm>
#include <cctype>

REGISTER_ENTITY("prop_physics", EntityBarrel);
REGISTER_ENTITY("prop_physics_multiplayer", EntityBarrel);
REGISTER_ENTITY("prop_physics_override", EntityBarrel);

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

void EntityBarrel::Spawn(World& world, const EntitySpawnData& data) {
    Entity::Spawn(world, data);

    m_modelName = data.GetString("model", "");
    std::string lowerModel = ToLower(m_modelName);

    float customDamage = data.GetFloat("ExplodeDamage", 0.0f);
    float customRadius = data.GetFloat("ExplodeRadius", 0.0f);

    m_isExplosive = (customDamage > 0.0f) ||
                    (lowerModel.find("oildrum001_explosive") != std::string::npos) ||
                    (lowerModel.find("barrel") != std::string::npos && lowerModel.find("explosive") != std::string::npos);

    if (m_isExplosive) {
        m_explodeDamage = (customDamage > 0.0f) ? customDamage : 120.0f;
        m_explodeRadius = (customRadius > 0.0f) ? customRadius : 250.0f;
        if (m_health <= 0.0f) m_health = 30.0f;
    }

    // Standard barrel cylinder dimensions (radius ~14, half-height ~22)
    float radius = 14.0f;
    float halfHeight = 22.0f;
    m_halfExtents = glm::vec3(radius, halfHeight, radius);

    // Offset spawn center up by halfHeight so origin at floor doesn't clip
    glm::vec3 spawnCenter = m_position + glm::vec3(0.0f, halfHeight, 0.0f);
    m_position = spawnCenter;

    JPH::BodyID id = CreateBarrelBody(spawnCenter.x, spawnCenter.y, spawnCenter.z, radius, halfHeight);
    world.RegisterBody(id, this);
}

void EntityBarrel::TakeDamage(World& world, float amount, Entity* attacker, const glm::vec3& force) {
    if (m_exploded) return;

    if (HasBody() && glm::length(force) > 0.001f) {
        SetBodyVelocity(m_bodyId, force.x, force.y, force.z);
    }

    if (!m_isExplosive) {
        Entity::TakeDamage(world, amount, attacker, force);
        return;
    }

    m_health -= amount;
    if (m_health <= 0.0f) {
        Explode(world, attacker);
    }
}

void EntityBarrel::Explode(World& world, Entity* attacker) {
    if (m_exploded) return;
    m_exploded = true;

    glm::vec3 blastPos = m_position;
    FireOutput(world, "OnBreak", attacker);
    MarkForKill();

    // Damages nearby enemies, props, and triggers chain-reaction barrels
    world.RadiusDamage(blastPos, m_explodeRadius, m_explodeDamage, 650.0f, this);
}
