#pragma once

#include "../Entity.h"

class EntityTrigger : public Entity {
public:
    void Spawn(World& world, const EntitySpawnData& data) override;
    void Think(World& world, float dt) override;
    void AcceptInput(World& world, const std::string& inputName, Entity* activator, const std::string& param) override;

protected:
    bool IsPointInside(const glm::vec3& pt) const;

    bool m_disabled = false;
    bool m_once = false;
    bool m_playerWasInside = false;
    float m_waitTime = 0.5f;
    float m_cooldown = 0.0f;
};

class EntityChangeLevel : public EntityTrigger {
public:
    void Spawn(World& world, const EntitySpawnData& data) override;
    void Think(World& world, float dt) override;

private:
    std::string m_nextMap;
};
