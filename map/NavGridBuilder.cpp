#include "MapCompiler.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <glm/glm.hpp>  // 👈 加上这个

// Möller–Trumbore 射线-三角形求交
static bool RayTriangleIntersect(const glm::vec3& orig, const glm::vec3& dir,
                                 const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, float& t) {
    glm::vec3 edge1 = v1 - v0;
    glm::vec3 edge2 = v2 - v0;
    glm::vec3 h = glm::cross(dir, edge2);
    float a = glm::dot(edge1, h);
    if (a > -0.00001f && a < 0.00001f) return false; 
    float f = 1.0f / a;
    glm::vec3 s = orig - v0;
    float u = f * glm::dot(s, h);
    if (u < 0.0f || u > 1.0f) return false;
    glm::vec3 q = glm::cross(s, edge1);
    float v = f * glm::dot(dir, q);
    if (v < 0.0f || u + v > 1.0f) return false;
    t = f * glm::dot(edge2, q);
    return t > 0.00001f;
}

void BuildNavGrid(const MapData& map, const MeshBuffers& collisionMesh, NavGrid& outGrid) {
    if (map.brushes.empty()) return;

    // 1. 计算整个地图的 XZ 包围盒
    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (const auto& b : map.brushes) {
        minX = std::min(minX, b.mins.x); maxX = std::max(maxX, b.maxs.x);
        minZ = std::min(minZ, b.mins.z); maxZ = std::max(maxZ, b.maxs.z);
    }

    outGrid.cellSize = 32.0f; // 每个格子 32x32
    outGrid.minX = std::floor(minX);
    outGrid.minZ = std::floor(minZ);
    outGrid.cols = (uint32_t)std::ceil((maxX - minX) / outGrid.cellSize);
    outGrid.rows = (uint32_t)std::ceil((maxZ - minZ) / outGrid.cellSize);
    outGrid.walkable.resize(outGrid.cols * outGrid.rows, 0);

    int walkableCount = 0;

    // 2. 遍历格子
    for (uint32_t r = 0; r < outGrid.rows; r++) {
        for (uint32_t c = 0; c < outGrid.cols; c++) {
            float x = outGrid.minX + c * outGrid.cellSize + outGrid.cellSize * 0.5f;
            float z = outGrid.minZ + r * outGrid.cellSize + outGrid.cellSize * 0.5f;
            
            glm::vec3 rayOrigin(x, 5000.0f, z);
            glm::vec3 rayDir(0, -1, 0);
            float closestT = 1e9f;
            glm::vec3 hitNormal(0, 1, 0);

            // 遍历碰撞网格三角形
            for (size_t i = 0; i < collisionMesh.indices.size(); i += 3) {
                uint32_t i0 = collisionMesh.indices[i+0];
                uint32_t i1 = collisionMesh.indices[i+1];
                uint32_t i2 = collisionMesh.indices[i+2];
                
                // 读取出三角形顶点
                glm::vec3 v0(collisionMesh.verts[i0*3+0], collisionMesh.verts[i0*3+1], collisionMesh.verts[i0*3+2]);
                glm::vec3 v1(collisionMesh.verts[i1*3+0], collisionMesh.verts[i1*3+1], collisionMesh.verts[i1*3+2]);
                glm::vec3 v2(collisionMesh.verts[i2*3+0], collisionMesh.verts[i2*3+1], collisionMesh.verts[i2*3+2]);

                float t;
                if (RayTriangleIntersect(rayOrigin, rayDir, v0, v1, v2, t)) {
                    if (t < closestT) {
                        closestT = t;
                        hitNormal = glm::normalize(glm::cross(v1 - v0, v2 - v0));
                    }
                }
            }

            // 命中检查：坡度需小于 45 度 (normal.y > 0.7)
            if (closestT < 1e8f && std::abs(hitNormal.y) > 0.7f) {
                float hitY = rayOrigin.y - closestT;
                if (hitY > -500.0f) { // 过滤掉无底洞
                    outGrid.walkable[r * outGrid.cols + c] = 1;
                    walkableCount++;
                }
            }
        }
    }
    std::cout << "[NavGrid] Generated " << outGrid.cols << "x" << outGrid.rows 
              << " grid, walkable cells: " << walkableCount << "\n";
}