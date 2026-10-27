#pragma once
#include "Common.h"

struct DoorInfo {
    glm::vec3 mins, maxs;
    glm::vec3 openDir;
    float openDist = 128.0f;
    float speed    = 100.0f;
    float waitTime = 4.0f;
    float openAmount = 0.0f;
    bool  isOpening  = false;
    float waitTimer  = 0.0f;
};

struct MapData {
    // 地图几何体
    uint32_t nv = 0, ni = 0;
    std::vector<float> verts;
    std::vector<uint32_t> indices;

    // 🔥 新增：纯碰撞几何体
    uint32_t cnv = 0, cni = 0;
    std::vector<float> collisionVerts;
    std::vector<uint32_t> collisionIndices;

    // 玩家出生点
    float spawnPos[3] = { 0.0f, 128.0f, -400.0f };
    float spawnAngles[3] = { 0.0f, 0.0f, 0.0f };

    // 实体区域
    std::vector<glm::vec3> triggerMins, triggerMaxs;
    std::vector<glm::vec3> physboxMins, physboxMaxs;
    std::vector<DoorInfo> doors;
    std::vector<glm::vec3> zombieSpawns;
    std::vector<glm::vec3> barrelSpawns;
};

// 读取 map.bvh，成功返回 true
bool LoadMap(const char* filename, MapData& outData);