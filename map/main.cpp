#include "MapCompiler.h"
#include <iostream>
#include <cstring>
#include "version.h"

int main(int argc, char** argv) {
    std::cout << "branch: " << Version::GIT_BRANCH 
                << ", commit: " << Version::GIT_HASH << ", " 
                << Version::CMAKE_BUILD_TYPE << std::endl;

     // 👈 解析命令行参数：默认关闭光照烘焙和 NavGrid
    bool genNavGrid = false;
    bool doLightBake = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--nav") == 0 || std::strcmp(argv[i], "-n") == 0) {
            genNavGrid = true;
        }
        if (std::strcmp(argv[i], "--bake") == 0 || std::strcmp(argv[i], "-b") == 0) {
            doLightBake = true;
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

    // 👈 条件执行光照烘焙
    if (doLightBake) {
        LightBakeOptions bakeOpts;
        bakeOpts.enabled = true;
        BakeVertexLighting(map, renderMesh, collisionMesh, bakeOpts);
    } else {
        std::cout << "[LightBaker] Skipped (use --bake to enable)\n";
    }

    // 👈 条件生成导航网格
    NavGrid navGrid;
    if (genNavGrid) {
        BuildNavGrid(map, collisionMesh, navGrid);
    } else {
        std::cout << "[NavGrid] Skipped (use --nav to enable)\n";
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