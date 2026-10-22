#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

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

    auto push_vertex = [&](const Vec3& p, const Vec3& n) -> uint32_t {
        uint32_t id = (uint32_t)(verts.size() / 6);
        verts.insert(verts.end(), {p.x, p.y, p.z, n.x, n.y, n.z});
        return id;
    };
    auto to_vec3 = [](const json& j) -> Vec3 {
        return Vec3{ j[0].get<float>(), j[1].get<float>(), j[2].get<float>() };
    };

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

    // 写 MYSB
    std::ofstream out("map.bsp", std::ios::binary);
    if (!out.is_open()) { std::cerr << "Cannot write map.bsp\n"; return 1; }
    out.write("MYSB", 4);
    uint32_t nv = (uint32_t)(verts.size() / 6);   // 顶点数（不是 float 数！）
    uint32_t ni = (uint32_t)indices.size();
    out.write((char*)&nv, 4);
    out.write((char*)&ni, 4);
    out.write((char*)verts.data(), verts.size() * sizeof(float));
    out.write((char*)indices.data(), indices.size() * sizeof(uint32_t));

    std::cout << "Compiled map.bsp: " << nv << " verts (" << verts.size()/6
              << "), " << ni << " indices\n";
    return 0;
}