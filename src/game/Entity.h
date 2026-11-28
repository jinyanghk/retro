#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include "MapFormat.h"
#include "MapLoader.h"

class World;

struct EntityConnection {
    std::string output;
    std::string target;
    std::string input;
    std::string param;
    float delay = 0.0f;
    bool once = false;
    bool fired = false;
};

struct EntitySpawnData {
    const MapData::EntityKV* record = nullptr;
    nlohmann::json properties = nlohmann::json::object();
    nlohmann::json customs = nlohmann::json::object();

    std::string GetString(const std::string& key, const std::string& def = "") const;
    float GetFloat(const std::string& key, float def = 0.0f) const;
    int GetInt(const std::string& key, int def = 0) const;
    glm::vec3 GetVec3(const std::string& key, const glm::vec3& def = glm::vec3(0.0f)) const;
};

class Entity {
public:
    virtual ~Entity() = default;

    // Lifecycle
    virtual void Spawn(World& world, const EntitySpawnData& data);
    virtual void Think(World& world, float dt) {}
    virtual void Destroy(World& world);

    // Interactions
    virtual void Use(World& world, Entity* activator) {}
    virtual void Touch(World& world, Entity* other) {}
    virtual void TakeDamage(World& world, float amount, Entity* attacker, const glm::vec3& force = glm::vec3(0.0f));

    // Source-style Input/Output system
    void FireOutput(World& world, const std::string& outputName, Entity* activator = nullptr, const std::string& overrideParam = "");
    virtual void AcceptInput(World& world, const std::string& inputName, Entity* activator, const std::string& param);

    // Identity & Transform
    uint32_t GetId() const { return m_id; }
    const std::string& GetClassname() const { return m_classname; }
    const std::string& GetTargetname() const { return m_targetname; }

    glm::vec3 GetPosition() const { return m_position; }
    void SetPosition(const glm::vec3& pos) { m_position = pos; }

    glm::vec3 GetAngles() const { return m_angles; }
    void SetAngles(const glm::vec3& angles) { m_angles = angles; }

    glm::vec3 GetBBoxMins() const { return m_bboxMins; }
    glm::vec3 GetBBoxMaxs() const { return m_bboxMaxs; }

    // Physics link
    JPH::BodyID GetBodyId() const { return m_bodyId; }
    bool HasBody() const { return !m_bodyId.IsInvalid(); }

    // Health & Removal
    float GetHealth() const { return m_health; }
    void SetHealth(float hp) { m_health = hp; }
    bool IsAlive() const { return m_health > 0.0f; }

    void MarkForKill() { m_pendingKill = true; }
    bool IsPendingKill() const { return m_pendingKill; }

    virtual bool CanBePickedUp() const { return false; }

protected:
    friend class World;

    uint32_t m_id = 0;
    std::string m_classname;
    std::string m_targetname;

    glm::vec3 m_position{0.0f};
    glm::vec3 m_angles{0.0f};
    glm::vec3 m_bboxMins{0.0f};
    glm::vec3 m_bboxMaxs{0.0f};

    JPH::BodyID m_bodyId; // Invalid by default
    float m_health = 0.0f;
    bool m_pendingKill = false;

    std::vector<EntityConnection> m_connections;
};
