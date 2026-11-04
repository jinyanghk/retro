#include "MapCompiler.h"
#include <iostream>
#include <cstring>
#include "version.h"

int main(int argc, char** argv) {
    std::cout << "branch: " << Version::GIT_BRANCH 
                << ", commit: " << Version::GIT_HASH << ", " 
                << Version::CMAKE_BUILD_TYPE << std::endl;

     // 👈 解析命令行参数：默认关闭光照烘焙和 Node Graph
    bool genNavGrid = false;
    bool doLightBake = false;
    std::string inPath = "map.json";
    std::string outPath = "map.bvh";

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--nav") == 0 || std::strcmp(argv[i], "-n") == 0) {
            genNavGrid = true;
        }
        if (std::strcmp(argv[i], "--bake") == 0 || std::strcmp(argv[i], "-b") == 0) {
            doLightBake = true;
        }   
        // 👈 新增 -i / -o 参数
        if (std::strcmp(argv[i], "-i") == 0 && i + 1 < argc) inPath = argv[++i];
        if (std::strcmp(argv[i], "-o") == 0 && i + 1 < argc) outPath = argv[++i];             
    }

    MapData map;
    if (!ParseJson(inPath.c_str(), map)) return 1;

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
bakeOpts.ambient = 0.6f;       // 从 0.1 提高到 0.6，防止全黑死角
bakeOpts.samplesPerLight = 8;        
        BakeVertexLighting(map, renderMesh, collisionMesh, bakeOpts);
    } else {
        std::cout << "[LightBaker] Skipped (use --bake to enable)\n";
    }

    // 👈 生成节点图
    std::vector<NodeRecord> nodeRecs;
    std::vector<uint32_t> nodeLinks;

    // 👈 条件生成导航网格
    NavGrid navGrid;
    if (genNavGrid) {
        //BuildNavGrid(map, collisionMesh, navGrid);
        BuildNodeGraph(map, collisionMesh, nodeRecs, nodeLinks);     
        printf("[Main] NodeRecs size before PackLumps: %zu, Links: %zu\n", nodeRecs.size(), nodeLinks.size()); // 👈 加这句   
    } else {
        std::cout << "[NodeGraph] Skipped (use --nav to enable)\n";
    }

    // 👈 打包时传入 nodeRecs 和 nodeLinks
    if (!PackLumps(map, renderMesh, collisionMesh,
                   triggerBounds, triggerTargets,
                   physboxBounds, doorBounds, doorNames,
                   zombieSpawns, barrelSpawns,
                   navGrid, 
                   nodeRecs, nodeLinks, // 👈 新增
                   outPath.c_str()))
        return 1;
    return 0;
}