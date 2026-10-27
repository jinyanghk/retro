#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstring>

#include "version.h"
#include "MapFormat.h" // 🔥 引入共享格式

using json = nlohmann::json;

struct Vec3 { float x, y, z; };

static Vec3 GetVec3(const json& je, const char* key, Vec3 fallback) {
    if (!je.contains(key) || je[key].size() < 3) return fallback;
    return Vec3{ je[key][0].get<float>(), je[key][1].get<float>(), je[key][2].get<float>() };
}

int main() {
    std::cout << "branch: " << Version::GIT_BRANCH << ", commit: " << Version::GIT_HASH << std::endl;
    std::ifstream f("map.json");
    if (!f.is_open()) { std::cerr << "Cannot open map.json\n"; return 1; }
    json data;
    try { f >> data; } catch (const std::exception& e) {
        std::cerr << "json parse error: " << e.what() << "\n"; return 1;
    }

    // 渲染用的顶点
    std::vector<float>    verts;
    std::vector<uint32_t> indices;
    
    // 纯碰撞用的顶点
    std::vector<float>    collisionVerts;
    std::vector<uint32_t> collisionIndices;

    std::vector<float> triggerBounds;   // 6 float / 个
    std::vector<float> physboxBounds;   // 6 float / 个
    std::vector<float> doorBounds;      // 12 float / 个
    std::vector<float> zombieSpawns;    
    std::vector<float> barrelSpawns;    

    std::vector<std::string> triggerTargets; // 🔥 收集 Trigger 的 target
    std::vector<std::string> doorNames;      // 🔥 收集 Door 的 targetname

    auto push_vertex = [&](const Vec3& p, const Vec3& n) -> uint32_t {
        uint32_t id = (uint32_t)(verts.size() / 6);
        verts.insert(verts.end(), {p.x, p.y, p.z, n.x, n.y, n.z});
        return id;
    };
    auto to_vec3 = [](const json& j) -> Vec3 {
        return Vec3{ j[0].get<float>(), j[1].get<float>(), j[2].get<float>() };
    };

    // 1. Brush 渲染几何 + 碰撞几何
    if (data.contains("brushes")) {
        for (const auto& jb : data["brushes"]) {
            if (!jb.contains("vertices") || !jb.contains("faces")) continue;
            std::vector<Vec3> bverts;
            for (const auto& jv : jb["vertices"]) bverts.push_back(to_vec3(jv));
            if (bverts.size() < 8) continue;

            for (const auto& jf : jb["faces"]) {
                if (!jf.contains("indices") || jf["indices"].size() < 3) continue;
                Vec3 n = jf.contains("normal") ? to_vec3(jf["normal"]) : Vec3{0,0,1};

                int i0 = jf["indices"][0].get<int>();
                int i1 = jf["indices"][1].get<int>();
                int i2 = jf["indices"][2].get<int>();
                int i3 = jf["indices"].size() >= 4 ? jf["indices"][3].get<int>() : i2;

                if (i0 < 0 || i0 >= (int)bverts.size() ||
                    i1 < 0 || i1 >= (int)bverts.size() ||
                    i2 < 0 || i2 >= (int)bverts.size() ||
                    i3 < 0 || i3 >= (int)bverts.size()) continue;

                // 渲染
                uint32_t v0 = push_vertex(bverts[i0], n);
                uint32_t v1 = push_vertex(bverts[i1], n);
                uint32_t v2 = push_vertex(bverts[i2], n);
                uint32_t v3 = push_vertex(bverts[i3], n);
                indices.push_back(v0); indices.push_back(v1); indices.push_back(v2);
                indices.push_back(v0); indices.push_back(v2); indices.push_back(v3);

                // 碰撞
                uint32_t cv0 = (uint32_t)(collisionVerts.size() / 3);
                collisionVerts.insert(collisionVerts.end(), {bverts[i0].x, bverts[i0].y, bverts[i0].z});
                uint32_t cv1 = (uint32_t)(collisionVerts.size() / 3);
                collisionVerts.insert(collisionVerts.end(), {bverts[i1].x, bverts[i1].y, bverts[i1].z});
                uint32_t cv2 = (uint32_t)(collisionVerts.size() / 3);
                collisionVerts.insert(collisionVerts.end(), {bverts[i2].x, bverts[i2].y, bverts[i2].z});
                uint32_t cv3 = (uint32_t)(collisionVerts.size() / 3);
                collisionVerts.insert(collisionVerts.end(), {bverts[i3].x, bverts[i3].y, bverts[i3].z});

                collisionIndices.push_back(cv0); collisionIndices.push_back(cv1); collisionIndices.push_back(cv2);
                collisionIndices.push_back(cv0); collisionIndices.push_back(cv2); collisionIndices.push_back(cv3);
            }
        }
    }

    // 2. 解析实体
    float spawnPos[3] = { 0.0f, 32.0f, 0.0f };
    float spawnAngles[3] = { 0.0f, 0.0f, 0.0f };

    if (data.contains("entities")) {
        for (const auto& je : data["entities"]) {
            std::string classname = je.value("classname", "");

            if (classname == "info_player_start") {
                Vec3 p = GetVec3(je, "origin", {0, 32, 0});
                Vec3 a = GetVec3(je, "angles", {0, 0, 0});
                spawnPos[0] = p.x; spawnPos[1] = p.y; spawnPos[2] = p.z;
                spawnAngles[0] = a.x; spawnAngles[1] = a.y; spawnAngles[2] = a.z;
            }
            else if (classname == "trigger_multiple" || classname == "trigger_once") {
                Vec3 mn = GetVec3(je, "bbox_mins", {-32, -32, -32});
                Vec3 mx = GetVec3(je, "bbox_maxs", { 32,  32,  32});
                triggerBounds.insert(triggerBounds.end(), { mn.x, mn.y, mn.z, mx.x, mx.y, mx.z });
                
                std::string tgt = "";
                
                // 🔥 优先从 outputs 数组里找 target
                if (je.contains("outputs") && je["outputs"].is_array() && !je["outputs"].empty()) {
                    tgt = je["outputs"][0].value("target", "");
                } 
                // 兼容旧数据
                else if (je.contains("properties") && je["properties"].contains("target")) {
                    tgt = je["properties"]["target"].get<std::string>();
                }
                
                triggerTargets.push_back(tgt);
                printf("Trigger loaded, target = %s\n", tgt.c_str()); // 👈 加上这行打印，看看终端有没有输出 door1
            }
            else if (classname == "func_physbox") {
                Vec3 mn = GetVec3(je, "bbox_mins", {-32, -32, -32});
                Vec3 mx = GetVec3(je, "bbox_maxs", { 32,  32,  32});
                physboxBounds.insert(physboxBounds.end(), { mn.x, mn.y, mn.z, mx.x, mx.y, mx.z });
            }
            else if (classname == "npc_zombie") {
                Vec3 p = GetVec3(je, "origin", {0, 36, 0});
                zombieSpawns.insert(zombieSpawns.end(), {p.x, p.y, p.z});
            } 
            else if (classname == "prop_physics") {
                Vec3 p = GetVec3(je, "origin", {0, 32, 0});
                barrelSpawns.insert(barrelSpawns.end(), {p.x, p.y, p.z});
            }                       
            else if (classname == "func_door") {
                Vec3 mn = GetVec3(je, "bbox_mins", {-32, -64, -8});
                Vec3 mx = GetVec3(je, "bbox_maxs", { 32,  64,  8});

                float angleDeg = 0.0f, openDist = 128.0f, speed = 100.0f, waitTime = 4.0f;
                if (je.contains("properties") && je["properties"].is_object()) {
                    auto& p = je["properties"];
                    auto readFloat = [&](const char* k, float& dst) {
                        if (p.contains(k) && p[k].is_string()) {
                            try { dst = std::stof(p[k].get<std::string>()); } catch (...) {}
                        }
                    };
                    readFloat("angle", angleDeg);
                    readFloat("lip",   openDist);
                    readFloat("speed", speed);
                    readFloat("wait",  waitTime);
                }
                float rad = angleDeg * 3.14159265f / 180.0f;
                float dirX = std::cos(rad), dirY = 0.0f, dirZ = std::sin(rad);

                doorBounds.insert(doorBounds.end(), {
                    mn.x, mn.y, mn.z, mx.x, mx.y, mx.z,
                    dirX, dirY, dirZ, openDist, speed, waitTime
                });

                std::string name = "";
                if (je.contains("properties") && je["properties"].contains("targetname"))
                    name = je["properties"]["targetname"].get<std::string>();
                doorNames.push_back(name);
            }
        }
    }

    // 3. 组装 Lump 内存块
    std::vector<char> outBuffer;
    std::vector<LumpDir> dirs;

    auto AppendLumpRaw = [&](uint32_t type, const std::vector<char>& raw) {
        LumpDir d;
        d.type   = type;
        d.offset = (uint32_t)outBuffer.size();
        d.size   = (uint32_t)raw.size();
        outBuffer.insert(outBuffer.end(), raw.begin(), raw.end());
        while (outBuffer.size() % 4) outBuffer.push_back(0); // 4字节对齐
        dirs.push_back(d);
    };

    // 预分配头部空间 (Magic 4 + Ver 4 + Count 4 + Dirs)
    // 我们先记录当前位置，最后回填
    size_t headerStart = outBuffer.size();
    outBuffer.resize(12 + 8 * sizeof(LumpDir), 0); // 假设最多 8 个 Lump

    // LUMP_GEOMETRY
    {
        std::vector<char> buf;
        auto PushU32 = [&](uint32_t v){ buf.insert(buf.end(), (char*)&v, (char*)&v + 4); };
        auto PushFloatArr = [&](const std::vector<float>& arr){ buf.insert(buf.end(), (const char*)arr.data(), (const char*)arr.data() + arr.size()*4); };
        auto PushU32Arr = [&](const std::vector<uint32_t>& arr){ buf.insert(buf.end(), (const char*)arr.data(), (const char*)arr.data() + arr.size()*4); };
        uint32_t nv = (uint32_t)(verts.size() / 6);
        uint32_t ni = (uint32_t)indices.size();
        PushU32(nv); PushU32(ni); PushFloatArr(verts); PushU32Arr(indices);
        AppendLumpRaw(LUMP_GEOMETRY, buf);
    }

    // LUMP_COLLISION
    {
        std::vector<char> buf;
        auto PushU32 = [&](uint32_t v){ buf.insert(buf.end(), (char*)&v, (char*)&v + 4); };
        auto PushFloatArr = [&](const std::vector<float>& arr){ buf.insert(buf.end(), (const char*)arr.data(), (const char*)arr.data() + arr.size()*4); };
        auto PushU32Arr = [&](const std::vector<uint32_t>& arr){ buf.insert(buf.end(), (const char*)arr.data(), (const char*)arr.data() + arr.size()*4); };
        uint32_t cnv = (uint32_t)(collisionVerts.size() / 3);
        uint32_t cni = (uint32_t)collisionIndices.size();
        PushU32(cnv); PushU32(cni); PushFloatArr(collisionVerts); PushU32Arr(collisionIndices);
        AppendLumpRaw(LUMP_COLLISION, buf);
    }

    // LUMP_SPAWN
    {
        std::vector<char> buf;
        buf.insert(buf.end(), (char*)spawnPos, (char*)spawnPos + 12);
        buf.insert(buf.end(), (char*)spawnAngles, (char*)spawnAngles + 12);
        AppendLumpRaw(LUMP_SPAWN, buf);
    }

    // LUMP_TRIGGERS
    {
        std::vector<TriggerRecord> records;
        for (size_t i = 0; i < triggerBounds.size() / 6; i++) {
            TriggerRecord r{};
            r.mins[0] = triggerBounds[i*6+0]; r.mins[1] = triggerBounds[i*6+1]; r.mins[2] = triggerBounds[i*6+2];
            r.maxs[0] = triggerBounds[i*6+3]; r.maxs[1] = triggerBounds[i*6+4]; r.maxs[2] = triggerBounds[i*6+5];
            std::string t = (i < triggerTargets.size()) ? triggerTargets[i] : "";
            strncpy(r.target, t.c_str(), 63);
            records.push_back(r);
        }
        std::vector<char> buf;
        uint32_t count = (uint32_t)records.size();
        buf.insert(buf.end(), (char*)&count, (char*)&count + 4);
        buf.insert(buf.end(), (char*)records.data(), (char*)records.data() + records.size() * sizeof(TriggerRecord));
        AppendLumpRaw(LUMP_TRIGGERS, buf);
    }

    // LUMP_PHYSBOX
    {
        std::vector<char> buf;
        uint32_t count = (uint32_t)(physboxBounds.size() / 6);
        buf.insert(buf.end(), (char*)&count, (char*)&count + 4);
        buf.insert(buf.end(), (char*)physboxBounds.data(), (char*)physboxBounds.data() + physboxBounds.size() * 4);
        AppendLumpRaw(LUMP_PHYSBOX, buf);
    }

    // LUMP_DOORS
    {
        std::vector<DoorRecord> records;
        for (size_t i = 0; i < doorBounds.size() / 12; i++) {
            DoorRecord r{};
            r.mins[0] = doorBounds[i*12+0]; r.mins[1] = doorBounds[i*12+1]; r.mins[2] = doorBounds[i*12+2];
            r.maxs[0] = doorBounds[i*12+3]; r.maxs[1] = doorBounds[i*12+4]; r.maxs[2] = doorBounds[i*12+5];
            r.dir[0]  = doorBounds[i*12+6]; r.dir[1]  = doorBounds[i*12+7]; r.dir[2]  = doorBounds[i*12+8];
            r.dist    = doorBounds[i*12+9];
            r.speed   = doorBounds[i*12+10];
            r.wait    = doorBounds[i*12+11];
            std::string n = (i < doorNames.size()) ? doorNames[i] : "";
            strncpy(r.targetname, n.c_str(), 63);
            records.push_back(r);
        }
        std::vector<char> buf;
        uint32_t count = (uint32_t)records.size();
        buf.insert(buf.end(), (char*)&count, (char*)&count + 4);
        buf.insert(buf.end(), (char*)records.data(), (char*)records.data() + records.size() * sizeof(DoorRecord));
        AppendLumpRaw(LUMP_DOORS, buf);
    }

    // LUMP_ZOMBIES
    {
        std::vector<char> buf;
        uint32_t count = (uint32_t)(zombieSpawns.size() / 3);
        buf.insert(buf.end(), (char*)&count, (char*)&count + 4);
        buf.insert(buf.end(), (char*)zombieSpawns.data(), (char*)zombieSpawns.data() + zombieSpawns.size() * 4);
        AppendLumpRaw(LUMP_ZOMBIES, buf);
    }

    // LUMP_BARRELS
    {
        std::vector<char> buf;
        uint32_t count = (uint32_t)(barrelSpawns.size() / 3);
        buf.insert(buf.end(), (char*)&count, (char*)&count + 4);
        buf.insert(buf.end(), (char*)barrelSpawns.data(), (char*)barrelSpawns.data() + barrelSpawns.size() * 4);
        AppendLumpRaw(LUMP_BARRELS, buf);
    }

    // 回填头部
    uint32_t lumpCount = (uint32_t)dirs.size();
    memcpy(&outBuffer[headerStart + 0], "MBVH", 4);
    uint32_t version = MAP_VERSION;
    memcpy(&outBuffer[headerStart + 4], &version, 4);
    memcpy(&outBuffer[headerStart + 8], &lumpCount, 4);
    memcpy(&outBuffer[headerStart + 12], dirs.data(), lumpCount * sizeof(LumpDir));

    // 写到文件
    std::ofstream out("map.bvh", std::ios::binary);
    if (!out.is_open()) { std::cerr << "Cannot write map.bvh\n"; return 1; }
    out.write(outBuffer.data(), outBuffer.size());
    out.close();

    printf("Compiled map.bvh: Version=%u, Lumps=%u\n", version, lumpCount);
    return 0;
}