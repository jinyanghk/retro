#include "World.h"
#include "EntityFactory.h"
#include "entities/EntityZombie.h" // 👈 必须包含
#include "Physics.h"
#include "BVH.h" 

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

    printf("[World] Total entities in mapData: %zu\n", mapData.entities.size());

    int spawnedCount = 0;
    for (const auto& rec : mapData.entities) {

        std::unique_ptr<Entity> ent = EntityFactory::Instance().Create(rec.classname);
        
        if (!ent) {
            continue;
        }

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

        // 3. 调用 Spawn
        rawPtr->Spawn(*this, spawnData);
        
        spawnedCount++;
    }

    std::printf("[World] Spawned %d entities from mapData.entities.\n", spawnedCount);

    // 👇 过滤 NavNodes 并重建连线（从旧版 main.cpp 移植）
    std::vector<MapData::NodeData> validNodes;
    std::vector<uint32_t> remapIndex(mapData.navNodes.size(), -1);
    
    // 不能修改 const mapData，得用我们的 m_mapData 拷贝或让 MapData 非 const
    // 这里为了不影响外部，我们拷贝一份节点数据来处理
    std::vector<MapData::NodeData> tempNodes = mapData.navNodes; // MapData 是 const 引用，我们只操作副本

    for (size_t i = 0; i < tempNodes.size(); ++i) {
        const auto& node = tempNodes[i];
        glm::vec3 dummyN;
        
        // 1. 向下探测地面
        float distToFloor = TraceBVH(node.pos + glm::vec3(0, 64, 0), glm::vec3(0, -1, 0), dummyN);
        if (distToFloor < 0.0f || distToFloor > 144.0f) continue; 
        
        // 2. 4向探墙
        bool valid = true;
        glm::vec3 dirs[4] = {{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}};
        int blockedCount = 0;
        for (auto& d : dirs) {
            float wallDist = TraceBVH(node.pos + glm::vec3(0, 32, 0), d, dummyN);
            if (wallDist > 0.0f && wallDist < 16.0f) blockedCount++;
        }
        if (blockedCount >= 3) continue; 

        remapIndex[i] = (uint32_t)validNodes.size();
        validNodes.push_back(node);
    }

    // 3. 剔除穿墙连线
    for (auto& node : validNodes) {
        std::vector<uint32_t> newLinks;
        for (auto oldLink : node.links) {
            if (oldLink < remapIndex.size() && remapIndex[oldLink] != -1) {
                glm::vec3 targetPos = tempNodes[oldLink].pos;
                glm::vec3 dir = targetPos - node.pos;
                float dist = glm::length(dir);
                if (dist < 0.1f) continue;
                dir = glm::normalize(dir);
                
                glm::vec3 hitNormal;
                float hitDist = TraceBVH(node.pos + glm::vec3(0, 32, 0), dir, hitNormal);
                
                if (hitDist < 0.0f || hitDist > dist - 10.0f) {
                    newLinks.push_back(remapIndex[oldLink]);
                }
            }
        }
        node.links = newLinks;
    }

    // 👇 关键：通过 mutable 指针或重新 setMapData 把清理后的节点传回 A* 引擎
    // 因为 LoadFromMap 接收的是 const MapData&，所以我们需要一个地方存过滤后的数据。
    m_filteredNavNodes = std::move(validNodes);    
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
    out.hit = false;
    out.distance = maxDist;

    // 👈 1. 第一优先级：疯狂遍历所有丧尸，只要有丧尸在射线上，直接返回！
    for (const auto& ent : m_entities) {
        if (ent->GetClassname() == "npc_zombie") {
            if (ent.get() == ignoreEntity) continue;
            
            glm::vec3 ePos = ent->GetPosition(); 
            // 稍微放大一点点，防止边缘蹭不到
            glm::vec3 eMin = ePos + glm::vec3(-20.0f, -5.0f, -20.0f);
            glm::vec3 eMax = ePos + glm::vec3( 20.0f, 75.0f,  20.0f); 
            
            float tmin, tmax;
            if (RayAABB(origin, dir, eMin, eMax, tmin, tmax) && tmin >= 0.0f && tmin < maxDist) {
                out.hit = true;
                out.distance = tmin;
                out.fraction = tmin / maxDist;
                out.hitPos = origin + dir * tmin;
                out.hitNormal = -dir;
                out.entity = ent.get(); // 👈 只要命中僵尸，就立刻把这个 Entity 返回！
                out.bodyId = JPH::BodyID();
                
                // Force HIT Zombie at distance %.1f!\n", tmin);
                return out; // 👈 直接 return！管你什么墙，哪怕隔着墙我也算打中了！
            }
        }
    }

    // 👈 2. 第二优先级：如果没打中任何丧尸，再去查物理世界（墙、门、木箱）
    JPH::BodyID ignoreBody = ignoreEntity ? ignoreEntity->GetBodyId() : JPH::BodyID();
    PhysicsRayHit physHit = RaycastWorld(origin, dir, maxDist, ignoreBody);
    if (physHit.hit) {
        out.hit = true;
        out.distance = physHit.distance;
        out.fraction = physHit.distance / maxDist;
        out.hitPos = physHit.point;
        out.hitNormal = physHit.normal;
        out.bodyId = physHit.bodyId;
        out.entity = GetEntityFromBody(physHit.bodyId);
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
