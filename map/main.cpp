#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <algorithm>

#include "version.h"

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

    std::vector<float>    verts;
    std::vector<uint32_t> indices;
    std::vector<float> triggerBounds;   // 6 float / 个
    std::vector<float> physboxBounds;   // 6 float / 个
    std::vector<float> doorBounds;      // 12 float / 个: mins[3], maxs[3], dir[3], dist, speed, wait
    std::vector<float> zombieSpawns;    // 每个僵尸3个 float (x, y, zs)

    auto push_vertex = [&](const Vec3& p, const Vec3& n) -> uint32_t {
        uint32_t id = (uint32_t)(verts.size() / 6);
        verts.insert(verts.end(), {p.x, p.y, p.z, n.x, n.y, n.z});
        return id;
    };
    auto to_vec3 = [](const json& j) -> Vec3 {
        return Vec3{ j[0].get<float>(), j[1].get<float>(), j[2].get<float>() };
    };

    // 1. Brush 渲染几何
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

                uint32_t v0 = push_vertex(bverts[i0], n);
                uint32_t v1 = push_vertex(bverts[i1], n);
                uint32_t v2 = push_vertex(bverts[i2], n);
                uint32_t v3 = push_vertex(bverts[i3], n);

                indices.push_back(v0); indices.push_back(v1); indices.push_back(v2);
                indices.push_back(v0); indices.push_back(v2); indices.push_back(v3);
            }
        }
    }

    // 2. 解析实体
    float spawnPos[3] = { 0.0f, 32.0f, 0.0f };
    float spawnAngles[3] = { 0.0f, 0.0f, 0.0f };
    int nTriggers = 0, nPhysboxes = 0, nDoors = 0;

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
                std::cout << "Trigger [" << classname << "] AABB: ("
                          << mn.x << "," << mn.y << "," << mn.z << ") - ("
                          << mx.x << "," << mx.y << "," << mx.z << ")\n";
                nTriggers++;
            }
            else if (classname == "func_physbox") {
                Vec3 mn = GetVec3(je, "bbox_mins", {-32, -32, -32});
                Vec3 mx = GetVec3(je, "bbox_maxs", { 32,  32,  32});
                physboxBounds.insert(physboxBounds.end(), { mn.x, mn.y, mn.z, mx.x, mx.y, mx.z });
                std::cout << "Physbox AABB: ("
                          << mn.x << "," << mn.y << "," << mn.z << ") - ("
                          << mx.x << "," << mx.y << "," << mx.z << ")\n";
                nPhysboxes++;
            }
            else if (classname == "npc_zombie") {
                Vec3 p = GetVec3(je, "origin", {0, 36, 0});
                zombieSpawns.insert(zombieSpawns.end(), {p.x, p.y, p.z});
                std::cout << "Zombie spawn at (" << p.x << ", " << p.y << ", " << p.z << ")\n";
            }            
            else if (classname == "func_door") {
                Vec3 mn = GetVec3(je, "bbox_mins", {-32, -64, -8});
                Vec3 mx = GetVec3(je, "bbox_maxs", { 32,  64,  8});

                // 打开方向：angle KV，0=+X, 90=+Z (Y-up 下水平面)
                float angleDeg = 0.0f;
                float openDist = 128.0f;
                float speed    = 100.0f;
                float waitTime = 4.0f;
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
                float dirX = std::cos(rad);
                float dirY = 0.0f;
                float dirZ = std::sin(rad);

                doorBounds.insert(doorBounds.end(), {
                    mn.x, mn.y, mn.z, mx.x, mx.y, mx.z,
                    dirX, dirY, dirZ,
                    openDist, speed, waitTime
                });
                std::cout << "Door AABB: (" << mn.x << "," << mn.y << "," << mn.z
                          << ") - (" << mx.x << "," << mx.y << "," << mx.z
                          << ") dir=(" << dirX << "," << dirY << "," << dirZ
                          << ") dist=" << openDist << "\n";
                nDoors++;
            }
        }
    }

    // 3. 写 MBVH
    std::ofstream out("map.bvh", std::ios::binary);
    if (!out.is_open()) { std::cerr << "Cannot write map.bvh\n"; return 1; }
    out.write("MBVH", 4);
    uint32_t nv = (uint32_t)(verts.size() / 6);
    uint32_t ni = (uint32_t)indices.size();
    out.write((char*)&nv, 4);
    out.write((char*)&ni, 4);
    out.write((char*)verts.data(), verts.size() * sizeof(float));
    out.write((char*)indices.data(), indices.size() * sizeof(uint32_t));

    out.write((char*)spawnPos, sizeof(spawnPos));
    out.write((char*)spawnAngles, sizeof(spawnAngles));

    uint32_t numTriggers = triggerBounds.size() / 6;
    out.write((char*)&numTriggers, 4);
    out.write((char*)triggerBounds.data(), triggerBounds.size() * sizeof(float));

    uint32_t numPhysboxes = physboxBounds.size() / 6;
    out.write((char*)&numPhysboxes, 4);
    out.write((char*)physboxBounds.data(), physboxBounds.size() * sizeof(float));

    uint32_t numDoors = doorBounds.size() / 12;
    out.write((char*)&numDoors, 4);
    out.write((char*)doorBounds.data(), doorBounds.size() * sizeof(float));

    // npc_zombie
    uint32_t numZombies = zombieSpawns.size() / 3;
    out.write((char *)&numZombies, 4);
    out.write((char *)zombieSpawns.data(), zombieSpawns.size() * sizeof(float));

    std::cout << "Compiled map.bvh: " << nv << " verts, " << ni << " indices\n";
    std::cout << "Spawn: (" << spawnPos[0] << ", " << spawnPos[1] << ", " << spawnPos[2] << ")\n";
    std::cout << "Triggers: " << numTriggers
              << "  Physboxes: " << numPhysboxes
              << "  Doors: " << numDoors << "\n";
    return 0;
}