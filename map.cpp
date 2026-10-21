#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

int main() {
    std::ifstream f("map.json");
    if (!f.is_open()) {
        std::cerr << "Cannot open map.json\n";
        return 1;
    }
    json data = json::parse(f);

    std::vector<float> verts;
    std::vector<uint32_t> indices;

    for (const auto& brush : data["brushes"]) {
        float minX = brush["min"][0], minY = brush["min"][1], minZ = brush["min"][2];
        float maxX = brush["max"][0], maxY = brush["max"][1], maxZ = brush["max"][2];

        // 8 个顶点
        float v[8][3] = {
            {minX, minY, minZ}, {maxX, minY, minZ}, {maxX, maxY, minZ}, {minX, maxY, minZ},
            {minX, minY, maxZ}, {maxX, minY, maxZ}, {maxX, maxY, maxZ}, {minX, maxY, maxZ}
        };
        uint32_t base = (uint32_t)(verts.size() / 3);
        for (int i = 0; i < 8; i++) {
            verts.push_back(v[i][0]); verts.push_back(v[i][1]); verts.push_back(v[i][2]);
        }

        // 6 个面，每个面 2 个三角形
        uint32_t faces[6][6] = {
            {4,5,6, 4,6,7}, // +Z
            {1,0,3, 1,3,2}, // -Z
            {0,4,7, 0,7,3}, // -X
            {5,1,2, 5,2,6}, // +X
            {3,7,6, 3,6,2}, // +Y
            {0,1,5, 0,5,4}  // -Y
        };
        for (int i = 0; i < 6; i++) {
            for (int j = 0; j < 6; j++) indices.push_back(base + faces[i][j]);
        }
    }

    // 写入极简 BSP
    std::ofstream out("map.bsp", std::ios::binary);
    out.write("MYSB", 4);
    uint32_t nv = (uint32_t)(verts.size() / 3);
    uint32_t ni = (uint32_t)indices.size();
    out.write((char*)&nv, 4);
    out.write((char*)&ni, 4);
    out.write((char*)verts.data(), verts.size() * 4);
    out.write((char*)indices.data(), indices.size() * 4);

    std::cout << "Compiled map.bsp: " << nv << " verts, " << ni << " indices\n";
    return 0;
}