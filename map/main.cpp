#include "MapCompiler.h"
#include <iostream>
#include "version.h"

int main() {
    std::cout << "branch: " << Version::GIT_BRANCH
              << ", commit: " << Version::GIT_HASH << std::endl;

    MapData map;
    if (!ParseJson("map.json", map)) return 1;

    // 各类实体收集
    std::vector<float> triggerBounds, physboxBounds, doorBounds, zombieSpawns, barrelSpawns;
    std::vector<std::string> triggerTargets, doorNames;
    CollectEntities(map, triggerBounds, triggerTargets, physboxBounds,
                    doorBounds, doorNames, zombieSpawns, barrelSpawns);

    // 构建渲染 / 碰撞网格
    MeshBuffers renderMesh, collisionMesh;
    BuildBrushMeshes(map.brushes, renderMesh, collisionMesh);

    // 光照烘焙（当前默认关闭）
    LightBakeOptions bakeOpts;
    bakeOpts.enabled = true; 
    BakeVertexLighting(map, renderMesh, collisionMesh, bakeOpts);

    // 👈 生成网格
    NavGrid navGrid;
    BuildNavGrid(map, collisionMesh, navGrid);

    // 打包
    if (!PackLumps(map, renderMesh, collisionMesh,
                   triggerBounds, triggerTargets,
                   physboxBounds, doorBounds, doorNames,
                   zombieSpawns, barrelSpawns,
                   navGrid, "map.bvh"))
        return 1;

    return 0;
}