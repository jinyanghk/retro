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
    
    // 1. 收集 info_node，并强制矫正高度
    for (const auto& e : map.entities) {
        if (e.classname == "info_node") {
            glm::vec3 topPos = glm::vec3(e.origin.x, 5000.0f, e.origin.z);
            float floorDist = RaycastFloor(topPos, collisionMesh);
            
            if (floorDist < 0.0f) continue; 
            
            // 强制使用物理地板高度，并抬高 16 单位
            float floorY = topPos.y - floorDist;
            glm::vec3 correctedPos(e.origin.x, floorY + 16.0f, e.origin.z);
            
            // 检查天花板高度
            float ceilDist = RaycastFloor(glm::vec3(correctedPos.x, correctedPos.y + 2.0f, correctedPos.z), collisionMesh);
            // 不需要天花板检测，直接放行
            
            validNodePositions.push_back(correctedPos);
        }
    }

    if (validNodePositions.empty()) {
        std::cout << "[NodeGraph] No valid info_node found.\n";
        return;
    }

    const float MAX_LINK_DISTANCE = 2048.0f; // 👈 放宽到 50 米

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

            // 起点和终点都抬高 64 单位，避免连线被地板误判为阻挡
            glm::vec3 start = validNodePositions[i] + glm::vec3(0, 64, 0);
            glm::vec3 end   = validNodePositions[j] + glm::vec3(0, 64, 0);
            
            if (!IsOccluded(start, end, collisionMesh)) {
                outLinks.push_back((uint32_t)j);
                outNodes[i].numLinks++;
            }
        }
    }
    
    std::cout << "[NodeGraph] Generated " << outNodes.size() << " nodes and " << outLinks.size() << " links.\n";
}