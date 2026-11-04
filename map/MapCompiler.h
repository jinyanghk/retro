#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include "MapFormat.h"

struct Vec3 { float x, y, z; };

struct BrushFace {
    std::vector<Vec3> verts;
    std::vector<int>  indices;
    Vec3              normal{0,0,1};
    std::string       material;   // v6: 面材质（空 = 继承 brush 默认）
};

struct Brush {
    std::vector<Vec3>      verts;
    std::vector<BrushFace> faces;
    Vec3 mins{1e9f, 1e9f, 1e9f};
    Vec3 maxs{-1e9f, -1e9f, -1e9f};
    // v6: 每面材质名，按 [FACE_POS_Z, FACE_NEG_Z, FACE_POS_X, FACE_NEG_X, FACE_POS_Y, FACE_NEG_Y] 顺序
    // 空字符串 = 用默认材质 "DEV/DEV_MEASUREGENERIC01"
    std::string faceMaterials[6];    
};

struct Entity {
    std::string classname;
    Vec3 origin{0,0,0};
    Vec3 angles{0,0,0};
    Vec3 bboxMins{-32,-32,-32};
    Vec3 bboxMaxs{ 32, 32, 32};
    std::string targetname;
    float angleDeg = 0.f, lip = 128.f, speed = 100.f, wait = 4.f;
    std::string target;
    nlohmann::json properties = nlohmann::json::object();
    nlohmann::json customs = nlohmann::json::object();
    nlohmann::json connections    = nlohmann::json::array();
};

struct MapData {
    std::vector<Brush>  brushes;
    std::vector<Entity> entities;
    float spawnPos[3]    = {0.f, 32.f, 0.f};
    float spawnAngles[3] = {0.f, 0.f, 0.f};
};

struct MeshBuffers {
    std::vector<float>    verts;
    std::vector<uint32_t> indices;
    // v6: 按材质分组（每个 group 是 indices 的一个连续切片）
    struct MaterialGroup {
        std::string name;
        uint32_t    indexOffset = 0;  // 在 indices 里的起始下标
        uint32_t    indexCount  = 0;  // 索引个数
    };
    std::vector<MaterialGroup> materialGroups;    
};

struct LightBakeOptions {
    int   samplesPerLight = 8;
    float ambient         = 0.1f;
    bool  enabled         = false;
};

struct NavGrid {
    float minX = 0, minZ = 0;
    float cellSize = 16.0f;
    uint32_t cols = 0, rows = 0;
    std::vector<uint8_t> walkable;
    std::vector<float>   heights; // 👈 必须加上这个
};

// ---------- 各模块入口 ----------

bool ParseJson(const char* path, MapData& out);

void CollectEntities(const MapData& map,
                     std::vector<float>& triggerBounds,
                     std::vector<std::string>& triggerTargets,
                     std::vector<float>& physboxBounds,
                     std::vector<float>& doorBounds,
                     std::vector<std::string>& doorNames,
                     std::vector<float>& zombieSpawns,
                     std::vector<float>& barrelSpawns);

void BuildBrushMeshes(const std::vector<Brush>& brushes,
                      MeshBuffers& renderMesh,
                      MeshBuffers& collisionMesh);

void BakeVertexLighting(const MapData& map,
                        MeshBuffers& renderMesh,
                        const MeshBuffers& collisionMesh,
                        const LightBakeOptions& opts);

void BuildNavGrid(const MapData& map, const MeshBuffers& collisionMesh, NavGrid& outGrid);

bool IsOccluded(const glm::vec3& start, 
                const glm::vec3& end, 
                const MeshBuffers& collisionMesh);

void BuildNodeGraph(const MapData& map, const MeshBuffers& collisionMesh, 
                    std::vector<NodeRecord>& outNodes, std::vector<uint32_t>& outLinks);

bool PackLumps(const MapData& map,
               const MeshBuffers& renderMesh,
               const MeshBuffers& collisionMesh,
               const std::vector<float>& triggerBounds,
               const std::vector<std::string>& triggerTargets,
               const std::vector<float>& physboxBounds,
               const std::vector<float>& doorBounds,
               const std::vector<std::string>& doorNames,
               const std::vector<float>& zombieSpawns,
               const std::vector<float>& barrelSpawns,
               const NavGrid& navGrid,
               const std::vector<NodeRecord>& nodeRecs,   // 👈 新增
               const std::vector<uint32_t>& nodeLinks,    // 👈 新增
               const char* outPath);