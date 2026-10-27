#pragma once
#include "Common.h"
#include "MapFormat.h"
#include <nlohmann/json.hpp>

struct MapData {
    // 几何（v3: stride 9 = pos3 + normal3 + color3）
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
    std::vector<std::string> triggerTargets;

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
        std::string targetname;
    };
    std::vector<DoorInfo> doors;

    // 敌人与桶
    std::vector<glm::vec3> zombieSpawns;
    std::vector<glm::vec3> barrelSpawns;

    // v3: 实体 KV 透传
    struct EntityKV {
        std::string classname;
        glm::vec3   origin{0};
        glm::vec3   angles{0};
        glm::vec3   bboxMins{-32};
        glm::vec3   bboxMaxs{ 32};
        nlohmann::json kv = nlohmann::json::object(); // properties + __outputs
    };
    std::vector<EntityKV> entities;

    // NavGrid
    float navMinX = 0, navMinZ = 0, navCellSize = 32.0f;
    uint32_t navCols = 0, navRows = 0;
    std::vector<uint8_t> navWalkable;    
};

bool LoadMap(const char* filename, MapData& outData);