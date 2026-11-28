#pragma once

#include "../Entity.h"

class EntityDoor : public Entity {
public:
    enum class DoorState { Closed, Opening, Open, Closing };

    void Spawn(World& world, const EntitySpawnData& data) override;
    void Think(World& world, float dt) override;
    void Destroy(World& world) override;
    void Use(World& world, Entity* activator) override;
    void AcceptInput(World& world, const std::string& inputName, Entity* activator, const std::string& param) override;

    void Open(World& world);
    void Close(World& world);
    void Toggle(World& world);

    glm::vec3 GetCurrentMins() const { return m_closedMins + m_moveDir * (m_moveDist * m_openFraction); }
    glm::vec3 GetCurrentMaxs() const { return m_closedMaxs + m_moveDir * (m_moveDist * m_openFraction); }
    float GetOpenFraction() const { return m_openFraction; }

private:
    glm::vec3 m_closedCenter{0.0f};
    glm::vec3 m_closedMins{0.0f};
    glm::vec3 m_closedMaxs{0.0f};
    glm::vec3 m_moveDir{0.0f, 1.0f, 0.0f};

    float m_moveDist = 64.0f;
    float m_speed = 100.0f;
    float m_waitTime = 3.0f; // -1 = stay open forever
    float m_openTimer = 0.0f;
    float m_openFraction = 0.0f;
    bool m_useOpens = true;

    DoorState m_state = DoorState::Closed;
};
