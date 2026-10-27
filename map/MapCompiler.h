#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "MapFormat.h"   // common/MapFormat.h，LumpDir / TriggerRecord / DoorRecord

// ---------- 通用小向量 ----------
struct Vec3 { float x, y, z; };

// ---------- 编译中间产物 ----------
struct BrushFace {
    std::vector<Vec3>     verts;     // 面顶点（已三角化前的多边形）
    std::vector<int>      indices;   // 索引到 verts（3 或 4 个）
    Vec3                  normal{0,0,1};
};

struct Brush {
    std::vector<Vec3>     verts;     // 8 个角点
    std::vector<BrushFace> faces;
};

struct Entity {
    std::string classname;
    // 常用字段（已知的）
    Vec3 origin{0,0,0};
    Vec3 angles{0,0,0};
    Vec3 bboxMins{-32,-32,-32};
    Vec3 bboxMaxs{ 32, 32, 32};
    // 门专用
    std::string targetname;
    float angleDeg = 0.f, lip = 128.f, speed = 100.f, wait = 4.f;
    // Trigger 专用
    std::string target;                 // 从 outputs[0].target 或 properties.target 解析
    // TODO(属性透传): 保留未知 properties
    // std::map<std::string, std::string> properties;
};

struct MapData {
    std::vector<Brush>  brushes;
    std::vector<Entity> entities;

    // 解析阶段填充的 spawn
    float spawnPos[3]    = {0.f, 32.f, 0.f};
    float spawnAngles[3] = {0.f, 0.f, 0.f};
};

// ---------- 各模块入口 ----------

// JsonParser.cpp
bool ParseJson(const char* path, MapData& out);
// 从实体列表里挑出各类实体，填充对应容器
void CollectEntities(const MapData& map,
                     std::vector<float>& triggerBounds,
                     std::vector<std::string>& triggerTargets,
                     std::vector<float>& physboxBounds,
                     std::vector<float>& doorBounds,
                     std::vector<std::string>& doorNames,
                     std::vector<float>& zombieSpawns,
                     std::vector<float>& barrelSpawns);

// BrushBuilder.cpp
struct MeshBuffers {
    std::vector<float>    verts;      // 6 float / 顶点 (pos + normal)
    std::vector<uint32_t> indices;
};
void BuildBrushMeshes(const std::vector<Brush>& brushes,
                      MeshBuffers& renderMesh,
                      MeshBuffers& collisionMesh);

// LumpPacker.cpp
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
               const char* outPath);

// LightBaker.cpp
struct LightBakeOptions {
    int   samplesPerLight = 8;
    float ambient         = 0.1f;
    bool  enabled         = false;   // TODO: 目前默认关闭
};
void BakeVertexLighting(const MapData& map,
                        MeshBuffers& renderMesh,
                        const LightBakeOptions& opts);