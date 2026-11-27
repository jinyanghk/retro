#include "World.h"
#include "EntityFactory.h"
#include "Physics.h"
#include <iostream>
#include <algorithm>
#include <string_view>
#include <nlohmann/json.hpp>

void World::Clear() {
    for (auto& ent : m_entities) {
        if (ent) {
            ent->Destroy(*this);
        }
    }
    m_entities.clear();
    m_idLookup.clear();
    m_bodyLookup.clear();
    m_ioQueue.clear();
    m_pendingLevelChange.clear();
    m_nextEntityId = 1;
}

// In World.cpp:
void World::LoadFromMap(const MapData& mapData) {
    Clear();
    m_mapData = &mapData;

    int spawnedCount = 0;
    for (const auto& rec : mapData.entities) {
        std::unique_ptr<Entity> ent = EntityFactory::Instance().Create(rec.classname);
        if (!ent) continue;

        EntitySpawnData spawnData;
        spawnData.record = &rec;

        if (rec.kv.is_object()) {
            if (rec.kv.contains("properties") && rec.kv["properties"].is_object()) {
                spawnData.properties = rec.kv["properties"];
            }
            if (rec.kv.contains("customs") && rec.kv["customs"].is_object()) {
                spawnData.customs = rec.kv["customs"];
            }
            if (rec.kv.contains("connections") && rec.kv["connections"].is_array()) {
                for (const auto& c : rec.kv["connections"]) {
                    EntityConnection conn;
                    conn.output = c.value("output", "");
                    conn.target = c.value("target", "");
                    conn.input  = c.value("input", "");
                    conn.param  = c.value("param", "");
                    conn.delay  = c.value("delay", 0.0f);
                    conn.once   = c.value("once", false);
                    if (!conn.output.empty() && !conn.target.empty()) {
                        ent->m_connections.push_back(std::move(conn));
                    }
                }
            }
        }

        Entity* rawPtr = ent.get();
        rawPtr->m_id = m_nextEntityId++;
        m_idLookup[rawPtr->m_id] = rawPtr;
        m_entities.push_back(std::move(ent));

        rawPtr->Spawn(*this, spawnData);
        spawnedCount++;
    }

    std::printf("[World] Spawned %d entities from mapData.entities.\n", spawnedCount);
}

void World::Update(float dt) {
    ProcessIOEvents(dt);

    size_t count = m_entities.size();
    for (size_t i = 0; i < count; ++i) {
        Entity* ent = m_entities[i].get();
        if (ent && !ent->IsPendingKill()) {
            ent->Think(*this, dt);
        }
    }

    SweepDestroyedEntities();
}

void World::ProcessIOEvents(float dt) {
    for (size_t i = 0; i < m_ioQueue.size(); ) {
        m_ioQueue[i].timeRemaining -= dt;
        if (m_ioQueue[i].timeRemaining <= 0.0f) {
            DelayedIOEvent ev = std::move(m_ioQueue[i]);
            m_ioQueue.erase(m_ioQueue.begin() + i);

            Entity* activator = GetEntityById(ev.activatorId);
            auto targets = FindEntitiesByName(ev.targetname);
            for (Entity* target : targets) {
                if (target && !target->IsPendingKill()) {
                    target->AcceptInput(*this, ev.inputName, activator, ev.param);
                }
            }
        } else {
            ++i;
        }
    }
}

void World::SweepDestroyedEntities() {
    for (size_t i = 0; i < m_entities.size(); ) {
        if (m_entities[i]->IsPendingKill()) {
            Entity* ent = m_entities[i].get();
            ent->Destroy(*this);
            m_idLookup.erase(ent->GetId());
            m_entities.erase(m_entities.begin() + i);
        } else {
            ++i;
        }
    }
}

Entity* World::GetEntityById(uint32_t id) const {
    auto it = m_idLookup.find(id);
    return (it != m_idLookup.end()) ? it->second : nullptr;
}

std::vector<Entity*> World::FindEntitiesByName(const std::string& targetname) const {
    std::vector<Entity*> result;
    if (targetname.empty()) return result;

    for (const auto& ent : m_entities) {
        if (!ent->IsPendingKill() && ent->GetTargetname() == targetname) {
            result.push_back(ent.get());
        }
    }
    return result;
}

std::vector<Entity*> World::FindEntitiesByClass(const std::string& classname) const {
    std::vector<Entity*> result;
    for (const auto& ent : m_entities) {
        if (!ent->IsPendingKill() && ent->GetClassname() == classname) {
            result.push_back(ent.get());
        }
    }
    return result;
}

void World::RegisterBody(JPH::BodyID bodyId, Entity* entity) {
    if (bodyId.IsInvalid() || !entity) return;
    entity->m_bodyId = bodyId;
    m_bodyLookup[bodyId.GetIndexAndSequenceNumber()] = entity;
}

void World::UnregisterBody(JPH::BodyID bodyId) {
    if (bodyId.IsInvalid()) return;
    m_bodyLookup.erase(bodyId.GetIndexAndSequenceNumber());
}

Entity* World::GetEntityFromBody(JPH::BodyID bodyId) const {
    if (bodyId.IsInvalid()) return nullptr;
    auto it = m_bodyLookup.find(bodyId.GetIndexAndSequenceNumber());
    return (it != m_bodyLookup.end()) ? it->second : nullptr;
}

WorldRaycastHit World::Raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDist, Entity* ignoreEntity) const {
    WorldRaycastHit out;
    JPH::BodyID ignoreBody = ignoreEntity ? ignoreEntity->GetBodyId() : JPH::BodyID();

    PhysicsRayHit hit = RaycastWorld(origin, dir, maxDist, ignoreBody);
    if (hit.hit) {
        out.hit = true;
        out.distance = hit.distance;
        out.fraction = (maxDist > 0.0f) ? (hit.distance / maxDist) : 0.0f;
        out.hitPos = hit.point;
        out.hitNormal = hit.normal;
        out.bodyId = hit.bodyId;
        out.entity = GetEntityFromBody(hit.bodyId);
    }
    return out;
}

void World::RadiusDamage(const glm::vec3& center, float radius, float maxDamage, float maxForce, Entity* attacker) {
    if (radius <= 0.0f) return;

    for (size_t i = 0; i < m_entities.size(); ++i) {
        Entity* ent = m_entities[i].get();
        if (!ent || ent->IsPendingKill() || ent == attacker) continue;

        glm::vec3 diff = ent->GetPosition() - center;
        float dist = glm::length(diff);
        if (dist <= radius) {
            float falloff = 1.0f - (dist / radius);
            glm::vec3 dir = (dist > 0.001f) ? (diff / dist) : glm::vec3(0.0f, 1.0f, 0.0f);
            ent->TakeDamage(*this, maxDamage * falloff, attacker, dir * (maxForce * falloff));
        }
    }
}

void World::QueueEvent(const std::string& targetname, const std::string& inputName, const std::string& param, float delay, Entity* activator) {
    DelayedIOEvent ev;
    ev.targetname = targetname;
    ev.inputName = inputName;
    ev.param = param;
    ev.timeRemaining = std::max(0.0f, delay);
    ev.activatorId = activator ? activator->GetId() : 0;
    m_ioQueue.push_back(std::move(ev));
}
