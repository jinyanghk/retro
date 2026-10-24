#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <algorithm> // std::min, std::max

using json = nlohmann::json;

struct Vec3 { float x, y, z; };

int main() {
    std::ifstream f("map.json");
    if (!f.is_open()) { std::cerr << "Cannot open map.json\n"; return 1; }
    json data;
    try { f >> data; } catch (const std::exception& e) {
        std::cerr << "json parse error: " << e.what() << "\n"; return 1;
    }

    std::vector<float>    verts;    // x,y,z, nx,ny,nz  (6 floats/顶点)
    std::vector<uint32_t> indices;
    std::vector<float> triggerBounds; // 每个触发器 6 个 float: minX, minY, minZ, maxX, maxY, maxZ

    auto push_vertex = [&](const Vec3& p, const Vec3& n) -> uint32_t {
        uint32_t id = (uint32_t)(verts.size() / 6);
        verts.insert(verts.end(), {p.x, p.y, p.z, n.x, n.y, n.z});
        return id;
    };
    auto to_vec3 = [](const json& j) -> Vec3 {
        return Vec3{ j[0].get<float>(), j[1].get<float>(), j[2].get<float>() };
    };

    // 1. 只处理普通 Brush 渲染
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

    // 2. 解析实体：出生点 + 触发器
    float spawnPos[3] = { 0.0f, 32.0f, 0.0f };
    float spawnAngles[3] = { 0.0f, 0.0f, 0.0f };

    if (data.contains("entities")) {
        for (const auto& je : data["entities"]) {
            std::string classname = je.value("classname", "");
            
            // 解析出生点
            if (classname == "info_player_start") {
                if (je.contains("origin")) {
                    spawnPos[0] = je["origin"][0].get<float>();
                    spawnPos[1] = je["origin"][1].get<float>();
                    spawnPos[2] = je["origin"][2].get<float>();
                }
                if (je.contains("angles")) {
                    spawnAngles[0] = je["angles"][0].get<float>();
                    spawnAngles[1] = je["angles"][1].get<float>();
                    spawnAngles[2] = je["angles"][2].get<float>();
                }
            }
            // ★ 解析触发器实体 (trigger_multiple)
            else if (classname == "trigger_multiple") {
                if (je.contains("origin")) {
                    float cx = je["origin"][0].get<float>();
                    float cy = je["origin"][1].get<float>();
                    float cz = je["origin"][2].get<float>();
                    
                    // 默认给触发器 32x32x32 的半边长（即 64x64x64 的触发空间）
                    float halfSize = 32.0f; 
                    
                    triggerBounds.insert(triggerBounds.end(), { 
                        cx - halfSize, cy - halfSize, cz - halfSize,
                        cx + halfSize, cy + halfSize, cz + halfSize 
                    });
                    std::cout << "Trigger entity at (" << cx << ", " << cy << ", " << cz << ") with size 64x64x64\n";
                }
            }
        }
    }

    // 写 MYSB
    std::ofstream out("map.bsp", std::ios::binary);
    if (!out.is_open()) { std::cerr << "Cannot write map.bsp\n"; return 1; }
    out.write("MYSB", 4);
    uint32_t nv = (uint32_t)(verts.size() / 6);
    uint32_t ni = (uint32_t)indices.size();
    out.write((char*)&nv, 4);
    out.write((char*)&ni, 4);
    out.write((char*)verts.data(), verts.size() * sizeof(float));
    out.write((char*)indices.data(), indices.size() * sizeof(uint32_t));

    // 写入出生点
    out.write((char*)spawnPos, sizeof(spawnPos));
    out.write((char*)spawnAngles, sizeof(spawnAngles));

    // 写入触发器数量和数据
    uint32_t numTriggers = triggerBounds.size() / 6;
    out.write((char*)&numTriggers, 4);
    out.write((char*)triggerBounds.data(), triggerBounds.size() * sizeof(float));

    std::cout << "Compiled map.bsp: " << nv << " verts, " << ni << " indices\n";
    std::cout << "Spawn: (" << spawnPos[0] << ", " << spawnPos[1] << ", " << spawnPos[2] << ")\n";
    std::cout << "Triggers: " << numTriggers << "\n";
    return 0;
}