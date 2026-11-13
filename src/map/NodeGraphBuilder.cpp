#include "MapCompiler.h"
#include <iostream>
#include <glm/glm.hpp>

extern bool IsOccluded(const glm::vec3& start, const glm::vec3& end, const MeshBuffers& collisionMesh);

// 射线向下探测地面高度
static float RaycastFloor(const glm::vec3& start, const MeshBuffers& collisionMesh) {
    float closestT = 1e9f;
    for (size_t i = 0; i < collisionMesh.indices.size(); i += 3) {
        uint32_t i0 = collisionMesh.indices[i+0];
        uint32_t i1 = collisionMesh.indices[i+1];
        uint32_t i2 = collisionMesh.indices[i+2];
        glm::vec3 v0(collisionMesh.verts[i0*3+0], collisionMesh.verts[i0*3+1], collisionMesh.verts[i0*3+2]);
        glm::vec3 v1(collisionMesh.verts[i1*3+0], collisionMesh.verts[i1*3+1], collisionMesh.verts[i1*3+2]);
        glm::vec3 v2(collisionMesh.verts[i2*3+0], collisionMesh.verts[i2*3+1], collisionMesh.verts[i2*3+2]);
        
        glm::vec3 edge1 = v1 - v0, edge2 = v2 - v0;
        glm::vec3 h = glm::cross(glm::vec3(0,-1,0), edge2);
        float a = glm::dot(edge1, h);
        if (a > -0.00001f && a < 0.00001f) continue;
        float f = 1.0f / a;
        glm::vec3 s = start - v0;
        float u = f * glm::dot(s, h);
        if (u < 0.0f || u > 1.0f) continue;
        glm::vec3 q = glm::cross(s, edge1);
        float v = f * glm::dot(glm::vec3(0,-1,0), q);
        if (v < 0.0f || u + v > 1.0f) continue;
        float t = f * glm::dot(edge2, q);
        if (t > 0.00001f && t < closestT) closestT = t;
    }
    return (closestT < 1e8f) ? closestT : -1.0f;
}

void BuildNodeGraph(const MapData& map, const MeshBuffers& collisionMesh, 
                    std::vector<NodeRecord>& outNodes, std::vector<uint32_t>& outLinks)
{
    std::vector<glm::vec3> validNodePositions;
    
    // 1. 收集 info_node，从它自己头上向下打射线
    for (const auto& e : map.entities) {
        if (e.classname == "info_node") {
            // 从节点原位置上方 32 单位往下打
            glm::vec3 topPos = glm::vec3(e.origin.x, e.origin.y + 32.0f, e.origin.z);
            float floorDist = RaycastFloor(topPos, collisionMesh);
            
            if (floorDist < 0.0f || floorDist > 300.0f) {
                // 如果打不到地，或者地面离得很远（说明悬空），直接弃用这个节点
                continue; 
            }
            
            // 强制使用物理地板高度，并抬高 16 单位（作为丧尸脚底）
            float floorY = topPos.y - floorDist;
            glm::vec3 correctedPos(e.origin.x, floorY + 16.0f, e.origin.z);
            
            validNodePositions.push_back(correctedPos);
        }
    }

    if (validNodePositions.empty()) {
        std::cout << "[NodeGraph] No valid info_node found.\n";
        return;
    }

    // 👈 缩短最大连线距离，防止远程连线穿墙
    const float MAX_LINK_DISTANCE = 256.0f; 

    outNodes.clear();
    outLinks.clear();
    outNodes.resize(validNodePositions.size());

    // 2. 两两连线（起点抬高，避免打到地板）
    for (size_t i = 0; i < validNodePositions.size(); ++i) {
        outNodes[i].pos[0] = validNodePositions[i].x;
        outNodes[i].pos[1] = validNodePositions[i].y;
        outNodes[i].pos[2] = validNodePositions[i].z;
        outNodes[i].firstLink = (uint32_t)outLinks.size();
        outNodes[i].numLinks = 0;

        for (size_t j = 0; j < validNodePositions.size(); ++j) {
            if (i == j) continue;
            float dist = glm::distance(validNodePositions[i], validNodePositions[j]);
            if (dist > MAX_LINK_DISTANCE) continue;

            // 起点和终点都抬高 32 单位（大腿高度），避免连线被地板/台阶误判为阻挡
            glm::vec3 start = validNodePositions[i] + glm::vec3(0, 32, 0);
            glm::vec3 end   = validNodePositions[j] + glm::vec3(0, 32, 0);
            
            // 👈 注意：这里需要开启 IsOccluded 里的检测，否则穿墙线全出来了
            if (!IsOccluded(start, end, collisionMesh)) {
                outLinks.push_back((uint32_t)j);
                outNodes[i].numLinks++;
            }
        }
    }
    
    std::cout << "[NodeGraph] Generated " << outNodes.size() << " nodes and " << outLinks.size() << " links.\n";
}