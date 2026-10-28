#include "MapCompiler.h"
#include <iostream>
#include <glm/glm.hpp>

// 声明 LightBaker.cpp 中暴露出来的 IsOccluded 函数
extern bool IsOccluded(const glm::vec3& start, const glm::vec3& end, const MeshBuffers& collisionMesh);

void BuildNodeGraph(const MapData& map, const MeshBuffers& collisionMesh, 
                    std::vector<NodeRecord>& outNodes, std::vector<uint32_t>& outLinks)
{
    std::vector<glm::vec3> nodePositions;
    
    // 1. 收集所有的 info_node 实体
    for (const auto& e : map.entities) {
        if (e.classname == "info_node") {
            // 稍微抬高一点，防止射线打到脚底下的地板
            nodePositions.push_back(glm::vec3(e.origin.x, e.origin.y + 10.0f, e.origin.z));
        }
    }

    if (nodePositions.empty()) {
        std::cout << "[NodeGraph] No info_node found in map. Skipping generation.\n";
        return;
    }

    const float MAX_LINK_DISTANCE = 512.0f; // 节点之间最大连线距离

    // 预分配输出
    outNodes.clear();
    outLinks.clear();
    outNodes.resize(nodePositions.size());

    // 2. 两两射线检测，建立连线
    for (size_t i = 0; i < nodePositions.size(); ++i) {
        outNodes[i].pos[0] = nodePositions[i].x;
        outNodes[i].pos[1] = nodePositions[i].y;
        outNodes[i].pos[2] = nodePositions[i].z;
        outNodes[i].firstLink = (uint32_t)outLinks.size();
        outNodes[i].numLinks = 0;

        for (size_t j = 0; j < nodePositions.size(); ++j) {
            if (i == j) continue;
            
            float dist = glm::distance(nodePositions[i], nodePositions[j]);
            if (dist > MAX_LINK_DISTANCE) continue;

            // 使用编译期的遮挡检测
            if (!IsOccluded(nodePositions[i], nodePositions[j], collisionMesh)) {
                outLinks.push_back((uint32_t)j);
                outNodes[i].numLinks++;
            }
        }
    }
    
    std::cout << "[NodeGraph] Generated " << outNodes.size() << " nodes and " << outLinks.size() << " links.\n";
}