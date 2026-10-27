#include "MapCompiler.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

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

    // 1. brushes
    if (data.contains("brushes")) {
        for (const auto& jb : data["brushes"]) {
            Brush b;
            if (jb.contains("vertices")) {
                for (const auto& jv : jb["vertices"])
                    b.verts.push_back({ jv[0].get<float>(), jv[1].get<float>(), jv[2].get<float>() });
            }
            if (jb.contains("faces")) {
                for (const auto& jf : jb["faces"]) {
                    BrushFace face;
                    face.normal = jf.contains("normal")
                        ? Vec3{ jf["normal"][0].get<float>(), jf["normal"][1].get<float>(), jf["normal"][2].get<float>() }
                        : Vec3{0,0,1};
                    if (jf.contains("indices"))
                        for (const auto& ji : jf["indices"]) face.indices.push_back(ji.get<int>());
                    b.faces.push_back(std::move(face));
                }
            }
            out.brushes.push_back(std::move(b));
        }
    }

    // 2. entities
    if (data.contains("entities")) {
        for (const auto& je : data["entities"]) {
            Entity e;
            e.classname = je.value("classname", "");
            e.origin = GetVec3(je, "origin", {0,0,0});
            e.angles = GetVec3(je, "angles", {0,0,0});
            e.bboxMins = GetVec3(je, "bbox_mins", {-32,-32,-32});
            e.bboxMaxs = GetVec3(je, "bbox_maxs", { 32, 32, 32});

            if (e.classname == "info_player_start") {
                out.spawnPos[0]    = e.origin.x;
                out.spawnPos[1]    = e.origin.y;
                out.spawnPos[2]    = e.origin.z;
                out.spawnAngles[0] = e.angles.x;
                out.spawnAngles[1] = e.angles.y;
                out.spawnAngles[2] = e.angles.z;
            }

            // trigger target 解析（兼容 outputs 与旧 properties）
            if (e.classname == "trigger_multiple" || e.classname == "trigger_once") {
                if (je.contains("outputs") && je["outputs"].is_array() && !je["outputs"].empty())
                    e.target = je["outputs"][0].value("target", "");
                else if (je.contains("properties") && je["properties"].contains("target"))
                    e.target = je["properties"]["target"].get<std::string>();
            }

            // door 属性
            if (e.classname == "func_door" && je.contains("properties") && je["properties"].is_object()) {
                const auto& p = je["properties"];
                auto readFloat = [&](const char* k, float& dst) {
                    if (p.contains(k) && p[k].is_string()) {
                        try { dst = std::stof(p[k].get<std::string>()); } catch (...) {}
                    }
                };
                readFloat("angle", e.angleDeg);
                readFloat("lip",   e.lip);
                readFloat("speed", e.speed);
                readFloat("wait",  e.wait);
                if (p.contains("targetname"))
                    e.targetname = p["targetname"].get<std::string>();
            }

            // TODO(属性透传): 把 je["properties"] 全部扫进 e.properties

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
            printf("Trigger loaded, target = %s\n", e.target.c_str());
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