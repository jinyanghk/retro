#include "MapCompiler.h"
#include <fstream>
#include <iostream>
#include <cmath>

using json = nlohmann::json;

static Vec3 GetVec3(const json& je, const char* key, Vec3 fallback) {
    if (!je.contains(key) || je[key].size() < 3) return fallback;
    return Vec3{ je[key][0].get<float>(), je[key][1].get<float>(), je[key][2].get<float>() };
}

bool ParseJson(const char* path, MapData& out) {
    std::ifstream f(path);
    if (!f.is_open()) { std::cerr << "Cannot open " << path << "\n"; return false; }
    json data;
    try { f >> data; } catch (const std::exception& e) {
        std::cerr << "json parse error: " << e.what() << "\n"; return false;
    }

    // ================= 1. 解析 Brushes =================
    if (data.contains("brushes")) {
        int brushCount = 0;
        for (const auto& jb : data["brushes"]) {
            Brush b;
            
            // 👈 新增：优先解析真实的多边形面
            if (jb.contains("faces") && jb["faces"].is_array()) {
                for (const auto& jf : jb["faces"]) {
                    BrushFace face;
                    face.material = jf.value("material", "DEV/DEV_MEASUREGENERIC01");
                    face.uaxis    = jf.value("uaxis", "[1 0 0 0] 1");
                    face.vaxis    = jf.value("vaxis", "[0 1 0 0] 1");
                    
                    // 保存面法线
                    if (jf.contains("normal") && jf["normal"].size() >= 3) {
                        face.normal.x = jf["normal"][0].get<float>();
                        face.normal.y = jf["normal"][1].get<float>();
                        face.normal.z = jf["normal"][2].get<float>();
                    }

                    // 解析该面的顶点
                    if (jf.contains("vertices")) {
                        for (const auto& jv : jf["vertices"]) {
                            face.verts.push_back({ 
                                jv[0].get<float>(), 
                                jv[1].get<float>(), 
                                jv[2].get<float>() 
                            });
                        }
                    }
                    if (!face.verts.empty()) {
                        b.faces.push_back(face);
                    }
                }
            }

            // 原有的 AABB 兜底（如果没有 faces，或者编辑器生成的小方块）
            if (jb.contains("vertices") && jb["vertices"].size() >= 8) {
                for (const auto& jv : jb["vertices"])
                    b.verts.push_back({ jv[0].get<float>(), jv[1].get<float>(), jv[2].get<float>() });
            } else if (jb.contains("position") && jb.contains("halfSize")) {
                float px = jb["position"][0], py = jb["position"][1], pz = jb["position"][2];
                float hx = jb["halfSize"][0], hy = jb["halfSize"][1], hz = jb["halfSize"][2];

                b.verts = {
                    { px - hx, py - hy, pz - hz },
                    { px + hx, py - hy, pz - hz },
                    { px + hx, py + hy, pz - hz },
                    { px - hx, py + hy, pz - hz },
                    { px - hx, py - hy, pz + hz },
                    { px + hx, py - hy, pz + hz },
                    { px + hx, py + hy, pz + hz },
                    { px - hx, py + hy, pz + hz }
                };
            }

            // 处理包围盒（用于编辑器里的框选）
            if (jb.contains("position") && jb.contains("halfSize")) {
                float px = jb["position"][0], py = jb["position"][1], pz = jb["position"][2];
                float hx = jb["halfSize"][0], hy = jb["halfSize"][1], hz = jb["halfSize"][2];
                b.mins = { px - hx, py - hy, pz - hz };
                b.maxs = { px + hx, py + hy, pz + hz };
            } else if (!b.verts.empty()) {
                for (auto& v : b.verts) {
                    b.mins.x = std::min(b.mins.x, v.x); b.maxs.x = std::max(b.maxs.x, v.x);
                    b.mins.y = std::min(b.mins.y, v.y); b.maxs.y = std::max(b.maxs.y, v.y);
                    b.mins.z = std::min(b.mins.z, v.z); b.maxs.z = std::max(b.maxs.z, v.z);
                }
            }

            // 只要有顶点或者面，就收进刷子
            if (!b.verts.empty() || !b.faces.empty()) {
                out.brushes.push_back(std::move(b));
                brushCount++;
            }
        }
        printf("[JsonParser] Loaded %d brushes.\n", brushCount);
    }

    // ================= 2. 解析 Entities =================
    if (data.contains("entities")) {
        for (const auto& je : data["entities"]) {
            Entity e;
            e.classname = je.value("classname", "");
            e.origin    = GetVec3(je, "origin",    {0,0,0});
            e.angles    = GetVec3(je, "angles",    {0,0,0});
            e.bboxMins  = GetVec3(je, "bbox_mins", {-32,-32,-32});
            e.bboxMaxs  = GetVec3(je, "bbox_maxs", { 32, 32, 32});

            if (je.contains("properties") && je["properties"].is_object())
                e.properties = je["properties"];
            if (je.contains("customs") && je["customs"].is_object())
                e.customs = je["customs"];                
            if (je.contains("connections") && je["connections"].is_array())
                e.connections = je["connections"];

            if (e.classname == "info_player_start") {
                out.spawnPos[0]    = e.origin.x;
                out.spawnPos[1]    = e.origin.y;
                out.spawnPos[2]    = e.origin.z;
                out.spawnAngles[0] = e.angles.x;
                out.spawnAngles[1] = e.angles.y;
                out.spawnAngles[2] = e.angles.z;
            }

            if (e.classname == "trigger_multiple" || e.classname == "trigger_once") {
                if (e.connections.is_array() && !e.connections.empty())
                    e.target = e.connections[0].value("target", "");
                else if (e.properties.contains("target"))
                    e.target = e.properties["target"].get<std::string>();
            }

            if (e.classname == "func_door" && e.properties.is_object()) {
                auto readFloat = [&](const char* k, float& dst) {
                    if (e.properties.contains(k) && e.properties[k].is_string()) {
                        try { dst = std::stof(e.properties[k].get<std::string>()); } catch (...) {}
                    }
                };
                readFloat("angle", e.angleDeg);
                readFloat("lip",   e.lip);
                readFloat("speed", e.speed);
                readFloat("wait",  e.wait);
                if (e.properties.contains("targetname"))
                    e.targetname = e.properties["targetname"].get<std::string>();
            }

            out.entities.push_back(std::move(e));
        }
    }
    return true;
}

void CollectEntities(const MapData& map,
                     std::vector<float>& triggerBounds,
                     std::vector<std::string>& triggerTargets,
                     std::vector<float>& physboxBounds,
                     std::vector<float>& doorBounds,
                     std::vector<std::string>& doorNames,
                     std::vector<float>& zombieSpawns,
                     std::vector<float>& barrelSpawns)
{
    for (const auto& e : map.entities) {
        if (e.classname == "trigger_multiple" || e.classname == "trigger_once") {
            triggerBounds.insert(triggerBounds.end(),
                { e.bboxMins.x, e.bboxMins.y, e.bboxMins.z,
                  e.bboxMaxs.x, e.bboxMaxs.y, e.bboxMaxs.z });
            triggerTargets.push_back(e.target);
        }
        else if (e.classname == "func_physbox") {
            physboxBounds.insert(physboxBounds.end(),
                { e.bboxMins.x, e.bboxMins.y, e.bboxMins.z,
                  e.bboxMaxs.x, e.bboxMaxs.y, e.bboxMaxs.z });
        }
        else if (e.classname == "npc_zombie") {
            zombieSpawns.insert(zombieSpawns.end(), { e.origin.x, e.origin.y, e.origin.z });
        }
        else if (e.classname == "prop_physics") {
            barrelSpawns.insert(barrelSpawns.end(), { e.origin.x, e.origin.y, e.origin.z });
        }
        else if (e.classname == "func_door") {
            float rad = e.angleDeg * 3.14159265f / 180.0f;
            float dirX = std::cos(rad), dirY = 0.0f, dirZ = std::sin(rad);
            doorBounds.insert(doorBounds.end(),
                { e.bboxMins.x, e.bboxMins.y, e.bboxMins.z,
                  e.bboxMaxs.x, e.bboxMaxs.y, e.bboxMaxs.z,
                  dirX, dirY, dirZ, e.lip, e.speed, e.wait });
            doorNames.push_back(e.targetname);
        }
    }
}