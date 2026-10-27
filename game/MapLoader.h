#pragma once
#include "Common.h"
#include "MapFormat.h"   // 🔥 引入共享定义

struct MapData {
    // 几何
    uint32_t nv = 0, ni = 0;
    std::vector<float> verts;
    std::vector<uint32_t> indices;

    // 碰撞
    uint32_t cnv = 0, cni = 0;
    std::vector<float> collisionVerts;
    std::vector<uint32_t> collisionIndices;

    // 出生点
    float spawnPos[3] = { 0, 128, -400 };
    float spawnAngles[3] = { 0, 0, 0 };

    // Triggers
    std::vector<glm::vec3> triggerMins, triggerMaxs;
    std::vector<std::string> triggerTargets;  // 🔥 新增

    // Physbox
    std::vector<glm::vec3> physboxMins, physboxMaxs;

    // Doors
    struct DoorInfo {
        glm::vec3 mins, maxs;
        glm::vec3 openDir;
        float openDist = 128.0f;
        float speed = 100.0f;
        float waitTime = 4.0f;
        float openAmount = 0.0f;
        bool isOpening = false;
        float waitTimer = 0.0f;
        std::string targetname;  // 🔥 新增
    };
    std::vector<DoorInfo> doors;

    // 敌人与桶
    std::vector<glm::vec3> zombieSpawns;
    std::vector<glm::vec3> barrelSpawns;
};

bool LoadMap(const char* filename, MapData& outData);