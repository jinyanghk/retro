#include "MapCompiler.h"
#include <cmath>
#include <iostream>
#include <algorithm>
#include <glm/glm.hpp>
#include <cstdio>

constexpr float RAY_EPSILON = 0.01f;

static bool g_occlusionEnabled = true; 

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

bool IsOccluded(const glm::vec3& start, const glm::vec3& end, const MeshBuffers& collisionMesh) {
    if (!g_occlusionEnabled) return false;

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

    // ==========================================
    // 🎨 P3: Ravenholm 冷蓝月光调色
    // ==========================================
    
    // 1. 环境光：压暗，带一点深蓝色调
    glm::vec3 globalAmbient(0.12f, 0.14f, 0.18f); 
    
    // 2. 月光方向：从侧上方斜射下来
    glm::vec3 moonDir = glm::normalize(glm::vec3(-0.4f, -1.0f, 0.2f)); 
    
    // 3. 月光颜色与强度：冷蓝，亮度适中
    glm::vec3 moonColor(0.25f, 0.35f, 0.65f); 
    float moonIntensity = 0.6f;

    // 1. 解析实体（点光源和聚光灯逻辑不变）
    for (const auto& e : map.entities) {
        // --- 太阳/天空光（light_environment）---
        if (e.classname == "light_environment") {
            // 如果有指定的 _light，依然读取，但颜色会被我们强制拉向冷色调
            if (e.properties.contains("_light") && e.properties["_light"].is_string()) {
                float r=255,g=255,b=255,bright=200;
                sscanf(e.properties["_light"].get<std::string>().c_str(), "%f %f %f %f", &r,&g,&b,&bright);
                
                // 将原本可能偏暖的日光，混合一点我们的冷蓝色，防止全图泛黄
                glm::vec3 originalSunColor(r/255.0f, g/255.0f, b/255.0f);
                moonColor = glm::mix(originalSunColor, glm::vec3(0.25f, 0.35f, 0.65f), 0.7f);
                moonIntensity = (bright / 255.0f) * 0.6f; // 强度压低一点
            }
            continue;
        }

        // --- 点光源和聚光灯 ---
        if (e.classname == "light" || e.classname == "light_spot") {
            Light L;
            L.pos = glm::vec3(e.origin.x, e.origin.y, e.origin.z);
            // 室内灯光可以稍微偏暖一点（比如 0.9, 0.85, 0.8），形成冷暖对比
            L.color = glm::vec3(0.9f, 0.85f, 0.75f); 
            L.intensity = 1.0f;
            L.radius = 8192.0f;

            if (e.properties.contains("_light") && e.properties["_light"].is_string()) {
                std::string s = e.properties["_light"].get<std::string>();
                float r = 255, g = 255, b = 255, bright = 200;
                int parsed = sscanf(s.c_str(), "%f %f %f %f", &r, &g, &b, &bright);
                if (parsed >= 3) {
                    L.color = glm::vec3(r/255.0f, g/255.0f, b/255.0f);
                    if (parsed == 3) bright = std::max({r, g, b});
                    L.intensity = bright / 255.0f; 
                } else if (parsed == 1) {
                    L.intensity = r / 255.0f;
                }
            }
            if (e.properties.contains("_distance") && e.properties["_distance"].is_string()) {
                try { L.radius = std::stof(e.properties["_distance"].get<std::string>()); } catch(...) {}
            }

            if (e.classname == "light_spot" && L.radius > 2000.0f) {
                L.radius = 1000.0f; 
            }
            lights.push_back(L);
        }
    }

    std::cout << "[LightBaker] Baking " << lights.size() << " point lights + 1 cold moon onto " << (renderMesh.verts.size()/11) << " vertices...\n";

    // 2. 遍历渲染网格顶点，累加光照
    uint32_t nv = (uint32_t)(renderMesh.verts.size() / 11);
    for (uint32_t i = 0; i < nv; i++) {
        glm::vec3 pos  (renderMesh.verts[i*11+0], renderMesh.verts[i*11+1], renderMesh.verts[i*11+2]);
        glm::vec3 norm (renderMesh.verts[i*11+3], renderMesh.verts[i*11+4], renderMesh.verts[i*11+5]);
        
        glm::vec3 totalLight = globalAmbient;

        // 月光（无遮挡，只看法线）
        float ndotl_moon = glm::max(0.0f, glm::dot(norm, -moonDir));
        totalLight += moonColor * moonIntensity * ndotl_moon;

        // 点光源
        for (const auto& L : lights) {
            glm::vec3 toLight = L.pos - pos;
            float dist = glm::length(toLight);
            if (dist > L.radius) continue;
            
            glm::vec3 startPos = pos + norm * 2.0f;
            
            if (!IsOccluded(startPos, L.pos, collisionMesh)) {
                float ndotl = glm::max(0.0f, glm::dot(norm, toLight / dist));
                float atten = glm::max(0.0f, 1.0f - (dist / L.radius));
                totalLight += L.color * (L.intensity / 2.0f) * ndotl * atten;
            }
        }

        // 限制颜色并写入顶点颜色（stride 11 的 offset 6,7,8）
        renderMesh.verts[i*11+6] = std::min(totalLight.r, 1.0f);
        renderMesh.verts[i*11+7] = std::min(totalLight.g, 1.0f);
        renderMesh.verts[i*11+8] = std::min(totalLight.b, 1.0f);
    }
}