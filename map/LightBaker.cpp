#include "MapCompiler.h"
#include <cmath>

// TODO(光照烘焙):
//  1. 遍历 map.entities，收集 classname == "light" 的实体：
//       origin / properties.light (亮度) / properties._color / properties._distance
//  2. 对 renderMesh.verts 里每个顶点，从顶点位置向每个光源发射射线：
//       - 若被碰撞网格（collisionMesh，三角形）阻挡，则跳过
//       - 否则累加衰减：color * intensity * max(0, N·L) / (1 + d²)
//  3. 把结果乘 ambient，写入顶点色的第 7/8/9 个分量
//       —— 需要同步扩展 LUMP_GEOMETRY 的 stride（6 → 9）
//         并把 MapFormat.h 的 GEOMETRY 顶点格式改成 pos+normal+color
//  4. 当前先留空；跑通流程后再实现

void BakeVertexLighting(const MapData& map,
                        MeshBuffers& renderMesh,
                        const LightBakeOptions& opts)
{
    if (!opts.enabled) return;

    // --- 占位：收集光源 ---
    struct Light { Vec3 pos; float intensity; Vec3 color; float radius; };
    std::vector<Light> lights;
    for (const auto& e : map.entities) {
        if (e.classname == "light") {
            // TODO: 从 properties 读 intensity / _color / _distance
            lights.push_back({ e.origin, 1.0f, {1,1,1}, 512.0f });
        }
    }
    if (lights.empty()) return;

    // --- 占位：遍历顶点，累加光照 ---
    // renderMesh.verts 目前是 6 float/顶点。光照若写入 color，
    // 需先在此处扩展成 9 float/顶点（或在 BuildBrushMeshes 阶段直接留 9 位）。
    for (size_t i = 0; i < renderMesh.verts.size(); i += 6) {
        // Vec3 pos{ renderMesh.verts[i+0], renderMesh.verts[i+1], renderMesh.verts[i+2] };
        // Vec3 n  { renderMesh.verts[i+3], renderMesh.verts[i+4], renderMesh.verts[i+5] };
        // Vec3 acc {opts.ambient, opts.ambient, opts.ambient};
        // for (const auto& L : lights) { ... raycast against collisionMesh ... }
        // TODO: 写回 color
    }
}