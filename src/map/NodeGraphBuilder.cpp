#include "MapCompiler.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <thread>
#include <chrono>
#include <glm/glm.hpp>

extern bool IsOccluded(const glm::vec3& start, const glm::vec3& end, const MeshBuffers& collisionMesh);

static float RaycastFloor(const glm::vec3& start, const MeshBuffers& collisionMesh) {
    float closestT = 1e9f;
    for (size_t i = 0; i + 2 < collisionMesh.indices.size(); i += 3) {
        uint32_t i0 = collisionMesh.indices[i + 0];
        uint32_t i1 = collisionMesh.indices[i + 1];
        uint32_t i2 = collisionMesh.indices[i + 2];
        glm::vec3 v0(collisionMesh.verts[i0 * 3 + 0], collisionMesh.verts[i0 * 3 + 1], collisionMesh.verts[i0 * 3 + 2]);
        glm::vec3 v1(collisionMesh.verts[i1 * 3 + 0], collisionMesh.verts[i1 * 3 + 1], collisionMesh.verts[i1 * 3 + 2]);
        glm::vec3 v2(collisionMesh.verts[i2 * 3 + 0], collisionMesh.verts[i2 * 3 + 1], collisionMesh.verts[i2 * 3 + 2]);

        glm::vec3 edge1 = v1 - v0, edge2 = v2 - v0;
        glm::vec3 h = glm::cross(glm::vec3(0, -1, 0), edge2);
        float a = glm::dot(edge1, h);
        if (std::abs(a) < 1e-5f) continue;
        float f = 1.0f / a;
        glm::vec3 s = start - v0;
        float u = f * glm::dot(s, h);
        if (u < 0.0f || u > 1.0f) continue;
        glm::vec3 q = glm::cross(s, edge1);
        float v = f * glm::dot(glm::vec3(0, -1, 0), q);
        if (v < 0.0f || u + v > 1.0f) continue;
        float t = f * glm::dot(edge2, q);
        if (t > 1e-5f && t < closestT) closestT = t;
    }
    return (closestT < 1e8f) ? closestT : -1.0f;
}

void BuildNodeGraph(const MapData& map, const MeshBuffers& collisionMesh,
                    std::vector<NodeRecord>& outNodes, std::vector<uint32_t>& outLinks)
{
    auto t0 = std::chrono::steady_clock::now();

    std::vector<glm::vec3> rawNodeOrigins;
    for (const auto& e : map.entities) {
        if (e.classname == "info_node") {
            rawNodeOrigins.emplace_back(e.origin.x, e.origin.y, e.origin.z);
        }
    }

    if (rawNodeOrigins.empty()) {
        std::cout << "[NodeGraph] No info_node entities found.\n";
        return;
    }

    unsigned int numThreads = std::max(1u, std::thread::hardware_concurrency());
    std::cout << "[NodeGraph] Processing " << rawNodeOrigins.size() << " nodes across "
              << numThreads << " threads (" << (collisionMesh.indices.size() / 3) << " tris)...\n";

    // 1. Parallel floor-snap pass (each thread writes to its own index in floorHeights)
    std::vector<float> floorHeights(rawNodeOrigins.size(), -1e9f);
    {
        std::vector<std::thread> workers;
        for (unsigned int t = 0; t < numThreads; ++t) {
            size_t startIdx = (rawNodeOrigins.size() * t) / numThreads;
            size_t endIdx   = (rawNodeOrigins.size() * (t + 1)) / numThreads;
            workers.emplace_back([&, startIdx, endIdx]() {
                for (size_t idx = startIdx; idx < endIdx; ++idx) {
                    glm::vec3 topPos = rawNodeOrigins[idx] + glm::vec3(0.0f, 32.0f, 0.0f);
                    float floorDist = RaycastFloor(topPos, collisionMesh);
                    if (floorDist >= 0.0f && floorDist <= 300.0f) {
                        floorHeights[idx] = topPos.y - floorDist;
                    }
                }
            });
        }
        for (auto& w : workers) w.join();
    }

    std::vector<glm::vec3> validNodePositions;
    for (size_t idx = 0; idx < rawNodeOrigins.size(); ++idx) {
        if (floorHeights[idx] > -1e8f) {
            validNodePositions.emplace_back(rawNodeOrigins[idx].x, floorHeights[idx] + 16.0f, rawNodeOrigins[idx].z);
        }
    }

    if (validNodePositions.empty()) {
        std::cout << "[NodeGraph] No valid grounded info_node found.\n";
        return;
    }

    const float MAX_LINK_DISTANCE = 256.0f;
    const float MAX_STEP_HEIGHT   = 64.0f;
    const float MAX_SLOPE_RATIO   = 1.25f;

    // 2. Parallel pairwise visibility pass (each thread owns its own rows adj[i] -> zero locks!)
    std::vector<std::vector<uint32_t>> adj(validNodePositions.size());
    {
        std::vector<std::thread> workers;
        for (unsigned int t = 0; t < numThreads; ++t) {
            size_t startI = (validNodePositions.size() * t) / numThreads;
            size_t endI   = (validNodePositions.size() * (t + 1)) / numThreads;
            workers.emplace_back([&, startI, endI]() {
                for (size_t i = startI; i < endI; ++i) {
                    const glm::vec3& pA = validNodePositions[i];
                    for (size_t j = 0; j < validNodePositions.size(); ++j) {
                        if (i == j) continue;
                        const glm::vec3& pB = validNodePositions[j];

                        float dy = std::abs(pA.y - pB.y);
                        if (dy > MAX_STEP_HEIGHT) continue;

                        float horizDist = glm::distance(glm::vec2(pA.x, pA.z), glm::vec2(pB.x, pB.z));
                        if (horizDist < 1.0f || (dy / horizDist) > MAX_SLOPE_RATIO) continue;

                        float dist = glm::distance(pA, pB);
                        if (dist > MAX_LINK_DISTANCE) continue;

                        bool blockedLow  = IsOccluded(pA + glm::vec3(0, 12.0f, 0), pB + glm::vec3(0, 12.0f, 0), collisionMesh);
                        if (blockedLow) continue;

                        bool blockedHigh = IsOccluded(pA + glm::vec3(0, 48.0f, 0), pB + glm::vec3(0, 48.0f, 0), collisionMesh);
                        if (!blockedHigh) {
                            adj[i].push_back((uint32_t)j);
                        }
                    }
                }
            });
        }
        for (auto& w : workers) w.join();
    }

    // 3. Flatten into CSR NodeRecord format
    outNodes.clear();
    outLinks.clear();
    outNodes.resize(validNodePositions.size());

    for (size_t i = 0; i < validNodePositions.size(); ++i) {
        outNodes[i].pos[0] = validNodePositions[i].x;
        outNodes[i].pos[1] = validNodePositions[i].y;
        outNodes[i].pos[2] = validNodePositions[i].z;
        outNodes[i].firstLink = (uint32_t)outLinks.size();
        outNodes[i].numLinks = (uint32_t)adj[i].size();
        for (uint32_t neighbor : adj[i]) {
            outLinks.push_back(neighbor);
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    std::cout << "[NodeGraph] Generated " << outNodes.size() << " nodes and "
              << outLinks.size() << " links in " << ms << " ms.\n";
}
