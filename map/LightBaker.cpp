#include "MapCompiler.h"
#include <cmath>
#include <iostream>
#include <algorithm>
#include <glm/glm.hpp>
#include <cstdio>

constexpr float RAY_EPSILON = 0.01f;

// Möller–Trumbore 射线-三角形求交
static bool RayTriangleIntersect(const glm::vec3& orig, const glm::vec3& dir,
                                 const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                                 float& t) {
    glm::vec3 edge1 = v1 - v0;
    glm::vec3 edge2 = v2 - v0;
    glm::vec3 h = glm::cross(dir, edge2);
    float a = glm::dot(edge1, h);
    if (a > -RAY_EPSILON && a < RAY_EPSILON) return false; 
    float f = 1.0f / a;
    glm::vec3 s = orig - v0;
    float u = f * glm::dot(s, h);
    if (u < 0.0f || u > 1.0f) return false;
    glm::vec3 q = glm::cross(s, edge1);
    float v = f * glm::dot(dir, q);
    if (v < 0.0f || u + v > 1.0f) return false;
    t = f * glm::dot(edge2, q);
    return t > RAY_EPSILON;
}

// 检测两点之间是否被碰撞网格遮挡
bool IsOccluded(const glm::vec3& start, const glm::vec3& end, const MeshBuffers& collisionMesh) {
    glm::vec3 dir = end - start;
    float maxDist = glm::length(dir);
    if (maxDist < RAY_EPSILON) return false;
    dir /= maxDist;

    for (size_t i = 0; i < collisionMesh.indices.size(); i += 3) {
        uint32_t i0 = collisionMesh.indices[i+0];
        uint32_t i1 = collisionMesh.indices[i+1];
        uint32_t i2 = collisionMesh.indices[i+2];

        glm::vec3 v0(collisionMesh.verts[i0*3+0], collisionMesh.verts[i0*3+1], collisionMesh.verts[i0*3+2]);
        glm::vec3 v1(collisionMesh.verts[i1*3+0], collisionMesh.verts[i1*3+1], collisionMesh.verts[i1*3+2]);
        glm::vec3 v2(collisionMesh.verts[i2*3+0], collisionMesh.verts[i2*3+1], collisionMesh.verts[i2*3+2]);

        float t;
        if (RayTriangleIntersect(start, dir, v0, v1, v2, t)) {
            if (t < maxDist - RAY_EPSILON) {
                return true; 
            }
        }
    }
    return false;
}

void BakeVertexLighting(const MapData& map, MeshBuffers& renderMesh, const MeshBuffers& collisionMesh, const LightBakeOptions& opts) {
    if (!opts.enabled) return;

    struct Light { glm::vec3 pos; glm::vec3 color; float intensity; float radius; };
    std::vector<Light> lights;

    // 1. 解析 light 实体
    for (const auto& e : map.entities) {
        if (e.classname == "light") {
            Light L;
            L.pos = glm::vec3(e.origin.x, e.origin.y, e.origin.z);
            L.color = glm::vec3(1.0f); 
            L.intensity = 1.0f;
            L.radius = 8192.0f;

            if (e.properties.contains("_light") && e.properties["_light"].is_string()) {
                std::string s = e.properties["_light"].get<std::string>();
                float r = 255, g = 255, b = 255, bright = 200;
                int parsed = sscanf(s.c_str(), "%f %f %f %f", &r, &g, &b, &bright);
                
                if (parsed >= 3) {
                    // 统一归一化到 0~1，应对 2048 这种 HDR 极值
                    L.color = glm::vec3(r/255.0f, g/255.0f, b/255.0f);
                    if (parsed == 3) {
                        // 如果只有 R G B，直接把最大的通道当亮度
                        bright = std::max({r, g, b});
                    }
                    L.intensity = bright / 255.0f; 
                } else if (parsed == 1) {
                    L.intensity = r / 255.0f;
                }
            }
            if (e.properties.contains("_distance") && e.properties["_distance"].is_string()) {
                try { L.radius = std::stof(e.properties["_distance"].get<std::string>()); } catch(...) {}
            }
            lights.push_back(L);
        }
    }

    if (lights.empty()) {
        std::cout << "[LightBaker] No light entities found, skipping bake.\n";
        return;
    }
    std::cout << "[LightBaker] Baking " << lights.size() << " lights onto " << (renderMesh.verts.size()/9) << " vertices...\n";

    // 2. 遍历渲染网格顶点，累加光照
    uint32_t nv = (uint32_t)(renderMesh.verts.size() / 9);
    for (uint32_t i = 0; i < nv; i++) {
        glm::vec3 pos  (renderMesh.verts[i*9+0], renderMesh.verts[i*9+1], renderMesh.verts[i*9+2]);
        glm::vec3 norm (renderMesh.verts[i*9+3], renderMesh.verts[i*9+4], renderMesh.verts[i*9+5]);
        
        glm::vec3 totalLight = glm::vec3(opts.ambient);

        for (const auto& L : lights) {
            glm::vec3 toLight = L.pos - pos;
            float dist = glm::length(toLight);
            if (dist > L.radius) continue;
            
            // 沿法线稍微偏移，防止自遮挡
            glm::vec3 startPos = pos + norm * 0.5f;
            
            if (!IsOccluded(startPos, L.pos, collisionMesh)) {
                float ndotl = glm::max(0.0f, glm::dot(norm, toLight / dist));
                
                // 平滑的线性衰减
                float atten = glm::max(0.0f, 1.0f - (dist / L.radius));
                
                // 除以 4.0 缓和 HDR 强度，避免纯白过曝
                totalLight += L.color * (L.intensity / 4.0f) * ndotl * atten;
            }
        }

        // 把最终颜色限制在 0 ~ 1 之间
        renderMesh.verts[i*9+6] = std::min(totalLight.r, 1.0f);
        renderMesh.verts[i*9+7] = std::min(totalLight.g, 1.0f);
        renderMesh.verts[i*9+8] = std::min(totalLight.b, 1.0f);
    }
}