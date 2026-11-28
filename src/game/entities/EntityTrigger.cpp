#include "EntityTrigger.h"
#include "../EntityFactory.h"
#include "../World.h"

REGISTER_ENTITY("trigger_once", EntityTrigger);
REGISTER_ENTITY("trigger_multiple", EntityTrigger);
REGISTER_ENTITY("trigger_changelevel", EntityChangeLevel);

void EntityTrigger::Spawn(World& world, const EntitySpawnData& data) {
    Entity::Spawn(world, data);
    m_once = (m_classname == "trigger_once");
    m_waitTime = data.GetFloat("wait", 0.5f);
    m_disabled = (data.GetInt("StartDisabled", 0) != 0);
}

bool EntityTrigger::IsPointInside(const glm::vec3& pt) const {
    return (pt.x >= m_bboxMins.x && pt.x <= m_bboxMaxs.x &&
            pt.y >= m_bboxMins.y && pt.y <= m_bboxMaxs.y &&
            pt.z >= m_bboxMins.z && pt.z <= m_bboxMaxs.z);
}

void EntityTrigger::Think(World& world, float dt) {
    if (m_disabled) return;
    if (m_cooldown > 0.0f) {
        m_cooldown -= dt;
        return;
    }

    // Check player AABB center/feet against trigger volume
    bool inside = IsPointInside(world.GetPlayerPosition());
    if (inside && !m_playerWasInside) {
        FireOutput(world, "OnStartTouch");
        FireOutput(world, "OnTrigger");
        m_cooldown = m_waitTime;

        if (m_once) {
            MarkForKill();
        }
    } else if (!inside && m_playerWasInside) {
        FireOutput(world, "OnEndTouch");
    }
    m_playerWasInside = inside;
}

void EntityTrigger::AcceptInput(World& world, const std::string& inputName, Entity* activator, const std::string& param) {
    if (inputName == "Enable") {
        m_disabled = false;
    } else if (inputName == "Disable") {
        m_disabled = true;
    } else if (inputName == "Toggle") {
        m_disabled = !m_disabled;
    } else {
        Entity::AcceptInput(world, inputName, activator, param);
    }
}

void EntityChangeLevel::Spawn(World& world, const EntitySpawnData& data) {
    EntityTrigger::Spawn(world, data);
    m_nextMap = data.GetString("map", "");
}

void EntityChangeLevel::Think(World& world, float dt) {
    if (m_disabled || m_nextMap.empty()) return;

    if (IsPointInside(world.GetPlayerPosition())) {
        FireOutput(world, "OnChangeLevel");
        world.RequestLevelChange(m_nextMap);
        m_disabled = true;
    }
}
