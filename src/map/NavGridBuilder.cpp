#include "MapCompiler.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <glm/glm.hpp>

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

    // 👈 精度提升为 16
    outGrid.cellSize = 16.0f; 
    outGrid.minX = std::floor(minX);
    outGrid.minZ = std::floor(minZ);
    outGrid.cols = (uint32_t)std::ceil((maxX - minX) / outGrid.cellSize);
    outGrid.rows = (uint32_t)std::ceil((maxZ - minZ) / outGrid.cellSize);
    outGrid.walkable.resize(outGrid.cols * outGrid.rows, 0);
    outGrid.heights.resize(outGrid.cols * outGrid.rows, 0.0f);

    int walkableCount = 0;

    for (uint32_t r = 0; r < outGrid.rows; r++) {
        for (uint32_t c = 0; c < outGrid.cols; c++) {
            float cx = outGrid.minX + c * outGrid.cellSize + outGrid.cellSize * 0.5f;
            float cz = outGrid.minZ + r * outGrid.cellSize + outGrid.cellSize * 0.5f;
            
            // 👈 采样中心点 + 4个角的偏移，防止边缘漏光
            float offsets[5][2] = { {0,0}, {-4,-4}, {4,-4}, {-4,4}, {4,4} };
            int validRays = 0;
            float highestHitY = -1e9f;

            for (int k = 0; k < 5; k++) {
                glm::vec3 rayOrigin(cx + offsets[k][0], 5000.0f, cz + offsets[k][1]);
                glm::vec3 rayDir(0, -1, 0);
                float closestT = 1e9f;
                glm::vec3 hitNormal(0, 1, 0);

                for (size_t i = 0; i < collisionMesh.indices.size(); i += 3) {
                    uint32_t i0 = collisionMesh.indices[i+0];
                    uint32_t i1 = collisionMesh.indices[i+1];
                    uint32_t i2 = collisionMesh.indices[i+2];
                    
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

                if (closestT < 1e8f && std::abs(hitNormal.y) > 0.5f) {
                    float hitY = rayOrigin.y - closestT;
                    if (hitY > highestHitY) highestHitY = hitY;
                    validRays++;
                }
            }

            // 只要有至少 2 条射线打中地面，就认为这格可走
            if (validRays >= 2 && highestHitY > -500.0f) {
                int idx = r * outGrid.cols + c;
                outGrid.walkable[idx] = 1;
                outGrid.heights[idx] = highestHitY;
                walkableCount++;
            }
        }
    }
    std::cout << "[NavGrid] Generated " << outGrid.cols << "x" << outGrid.rows 
              << " grid (cellSize=16), walkable cells: " << walkableCount << "\n";
}