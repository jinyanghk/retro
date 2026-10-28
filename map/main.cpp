#include "MapCompiler.h"
#include <iostream>
#include <cstring>
#include "version.h"

int main(int argc, char** argv) {
    std::cout << "branch: " << Version::GIT_BRANCH
              << ", commit: " << Version::GIT_HASH << std::endl;

    // 👈 解析命令行参数：默认不生成 NavGrid
    bool genNavGrid = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--nav") == 0 || std::strcmp(argv[i], "-n") == 0) {
            genNavGrid = true;
        }
    }

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

    // 光照烘焙
    LightBakeOptions bakeOpts;
    bakeOpts.enabled = true;
    BakeVertexLighting(map, renderMesh, collisionMesh, bakeOpts);

    // 👈 条件生成导航网格
    NavGrid navGrid;
    if (genNavGrid) {
        BuildNavGrid(map, collisionMesh, navGrid);
    }

    // 打包（依然传入 navGrid，没生成的话 cols=0，PackLumps 里会自动跳过）
    if (!PackLumps(map, renderMesh, collisionMesh,
                   triggerBounds, triggerTargets,
                   physboxBounds, doorBounds, doorNames,
                   zombieSpawns, barrelSpawns,
                   navGrid, "map.bvh"))
        return 1;

    return 0;
}