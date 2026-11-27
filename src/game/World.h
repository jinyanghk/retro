#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include "Entity.h"
#include "MapFormat.h"

struct MapData;

struct DelayedIOEvent {
    std::string targetname;
    std::string inputName;
    std::string param;
    float timeRemaining = 0.0f;
    uint32_t activatorId = 0;
};

struct WorldRaycastHit {
    bool hit = false;
    float distance = 0.0f;
    float fraction = 1.0f;
    glm::vec3 hitPos{0.0f};
    glm::vec3 hitNormal{0.0f};
    JPH::BodyID bodyId;
    Entity* entity = nullptr;
};

class World {
public:
    World() = default;
    ~World() { Clear(); }

    // Lifecycle
    void Clear();
    void LoadFromMap(const MapData& mapData);
    void Update(float dt);

    // Entity management
    template <typename T, typename... Args>
    T* SpawnEntity(Args&&... args) {
        auto ent = std::make_unique<T>(std::forward<Args>(args)...);
        T* rawPtr = ent.get();
        ent->m_id = m_nextEntityId++;
        m_entities.push_back(std::move(ent));
        m_idLookup[rawPtr->m_id] = rawPtr;
        return rawPtr;
    }

    Entity* GetEntityById(uint32_t id) const;
    std::vector<Entity*> FindEntitiesByName(const std::string& targetname) const;
    std::vector<Entity*> FindEntitiesByClass(const std::string& classname) const;
    const std::vector<std::unique_ptr<Entity>>& GetEntities() const { return m_entities; }

    // Physics body <-> Entity mapping
    void RegisterBody(JPH::BodyID bodyId, Entity* entity);
    void UnregisterBody(JPH::BodyID bodyId);
    Entity* GetEntityFromBody(JPH::BodyID bodyId) const;

    // Gameplay helpers
    WorldRaycastHit Raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDist, Entity* ignoreEntity = nullptr) const;
    void RadiusDamage(const glm::vec3& center, float radius, float maxDamage, float maxForce, Entity* attacker = nullptr);

    // Player & NavGraph state
    void SetPlayerState(const glm::vec3& pos, float hp) { m_playerPos = pos; m_playerHealth = hp; }
    glm::vec3 GetPlayerPosition() const { return m_playerPos; }
    float GetPlayerHealth() const { return m_playerHealth; }

    void DamagePlayer(float dmg) { m_pendingPlayerDamage += dmg; }
    float ConsumePlayerDamage() {
        float d = m_pendingPlayerDamage;
        m_pendingPlayerDamage = 0.0f;
        return d;
    }

    void SetMapData(const MapData* map) { m_mapData = map; }
    const MapData* GetMapData() const { return m_mapData; }

    // Source-style I/O queue
    void QueueEvent(const std::string& targetname, const std::string& inputName, const std::string& param, float delay, Entity* activator = nullptr);

    // Level transition request
    void RequestLevelChange(const std::string& nextMapName) { m_pendingLevelChange = nextMapName; }
    bool HasPendingLevelChange() const { return !m_pendingLevelChange.empty(); }
    std::string ConsumePendingLevelChange() {
        std::string next = m_pendingLevelChange;
        m_pendingLevelChange.clear();
        return next;
    }

private:
    void ProcessIOEvents(float dt);
    void SweepDestroyedEntities();

    uint32_t m_nextEntityId = 1;
    std::vector<std::unique_ptr<Entity>> m_entities;
    std::unordered_map<uint32_t, Entity*> m_idLookup;
    std::unordered_map<uint32_t, Entity*> m_bodyLookup;
    std::vector<DelayedIOEvent> m_ioQueue;
    std::string m_pendingLevelChange;

    glm::vec3 m_playerPos{0.0f};
    float m_playerHealth = 100.0f;
    float m_pendingPlayerDamage = 0.0f;
    const MapData* m_mapData = nullptr;
};
