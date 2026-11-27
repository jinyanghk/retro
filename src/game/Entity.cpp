#include "Entity.h"
#include "World.h"
#include <sstream>

std::string EntitySpawnData::GetString(const std::string& key, const std::string& def) const {
    if (properties.contains(key)) {
        const auto& val = properties[key];
        if (val.is_string()) return val.get<std::string>();
        if (val.is_number_integer()) return std::to_string(val.get<int>());
        if (val.is_number_float()) return std::to_string(val.get<float>());
    }
    if (customs.contains(key) && customs[key].is_string()) {
        return customs[key].get<std::string>();
    }
    return def;
}

float EntitySpawnData::GetFloat(const std::string& key, float def) const {
    if (properties.contains(key)) {
        const auto& val = properties[key];
        if (val.is_number()) return val.get<float>();
        if (val.is_string()) {
            try { return std::stof(val.get<std::string>()); } catch (...) {}
        }
    }
    if (customs.contains(key) && customs[key].is_number()) {
        return customs[key].get<float>();
    }
    return def;
}

int EntitySpawnData::GetInt(const std::string& key, int def) const {
    if (properties.contains(key)) {
        const auto& val = properties[key];
        if (val.is_number()) return val.get<int>();
        if (val.is_string()) {
            try { return std::stoi(val.get<std::string>()); } catch (...) {}
        }
    }
    if (customs.contains(key) && customs[key].is_number()) {
        return customs[key].get<int>();
    }
    return def;
}

glm::vec3 EntitySpawnData::GetVec3(const std::string& key, const glm::vec3& def) const {
    std::string str = GetString(key, "");
    if (str.empty()) return def;

    glm::vec3 out = def;
    std::istringstream iss(str);
    if (iss >> out.x >> out.y >> out.z) {
        return out;
    }
    return def;
}

void Entity::Spawn(World& world, const EntitySpawnData& data) {
    if (!data.record) return;

    m_classname = data.record->classname;
    m_position  = data.record->origin;
    m_angles    = data.record->angles;
    m_bboxMins  = data.record->bboxMins;
    m_bboxMaxs  = data.record->bboxMaxs;

    m_targetname = data.GetString("targetname", "");
    m_health     = data.GetFloat("health", 0.0f);
}

void Entity::Destroy(World& world) {
    if (!m_bodyId.IsInvalid()) {
        world.UnregisterBody(m_bodyId);
        m_bodyId = JPH::BodyID();
    }
}

void Entity::TakeDamage(World& world, float amount, Entity* attacker, const glm::vec3& force) {
    if (m_health <= 0.0f) return;

    m_health -= amount;
    if (m_health <= 0.0f) {
        m_health = 0.0f;
        FireOutput(world, "OnBreak", attacker);
        FireOutput(world, "OnDeath", attacker);
        MarkForKill();
    }
}

void Entity::FireOutput(World& world, const std::string& outputName, Entity* activator, const std::string& overrideParam) {
    for (auto& conn : m_connections) {
        if (conn.output != outputName) continue;
        if (conn.once && conn.fired) continue;

        conn.fired = true;
        const std::string& paramToUse = overrideParam.empty() ? conn.param : overrideParam;
        world.QueueEvent(conn.target, conn.input, paramToUse, conn.delay, activator);
    }
}

void Entity::AcceptInput(World& world, const std::string& inputName, Entity* activator, const std::string& param) {
    if (inputName == "Kill") {
        MarkForKill();
    }
}
