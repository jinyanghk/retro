#include "World.h"
#include "EntityFactory.h"
#include "Physics.h"
#include <iostream>
#include <algorithm>
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

void World::LoadFromMap(const BvhMapData& mapData) {
    Clear();

    int spawnedCount = 0;
    for (const auto& rec : mapData.entities) {
        std::unique_ptr<Entity> ent = EntityFactory::Instance().Create(rec.classname);
        if (!ent) {
            // Unregistered entity classes (e.g., info_node, light, info_player_start) are safely skipped
            continue;
        }

        EntitySpawnData spawnData;
        spawnData.record = &rec;

        if (!rec.kvJson.empty()) {
            try {
                nlohmann::json j = nlohmann::json::parse(rec.kvJson);
                if (j.contains("properties") && j["properties"].is_object()) {
                    spawnData.properties = j["properties"];
                }
                if (j.contains("customs") && j["customs"].is_object()) {
                    spawnData.customs = j["customs"];
                }
                if (j.contains("connections") && j["connections"].is_array()) {
                    for (const auto& c : j["connections"]) {
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
            } catch (const std::exception& e) {
                std::cerr m_id = m_nextEntityId++;
        m_idLookup[rawPtr->m_id] = rawPtr;
        m_entities.push_back(std::move(ent));

        rawPtr->Spawn(*this, spawnData);
        spawnedCount++;
    }

    std::cout << "[World] Spawned " << spawnedCount << " entities from LUMP_ENTITIES.\n";
}

void World::Update(float dt) {
    // 1. Fire any due Hammer I/O outputs
    ProcessIOEvents(dt);

    // 2. Run Think() across all active entities (index-based loop allows safe spawning during Think)
    size_t count = m_entities.size();
    for (size_t i = 0; i < count; ++i) {
        Entity* ent = m_entities[i].get();
        if (ent && !ent->IsPendingKill()) {
            ent->Think(*this, dt);
        }
    }

    // 3. Clean up killed entities and free their Jolt bodies
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

    // Uses your existing Physics::RaycastWorld helper (can be expanded to return hit BodyID in Step 2)
    float hitDist = Physics::RaycastWorld(origin, dir, maxDist, ignoreBody, &out.bodyId, &out.hitNormal);
    if (hitDist >= 0.0f && hitDist IsPendingKill() || ent == attacker) continue;

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
