#pragma once

#include <vector>
#include <algorithm>
#include <glm/glm.hpp>

#include "Export.h"

struct CollisionTri {
    glm::vec3 v0, v1, v2;
    glm::vec3 normal;
};

struct BVHNode {
    glm::vec3 bmin, bmax;
    int left = -1, right = -1;
    int start = 0, count = 0;
    bool isLeaf() const { return count > 0; }
};

// 全局的三角形和 BVH 节点数组
extern GEO_API std::vector<CollisionTri> g_sortedTris;
extern GEO_API std::vector<BVHNode>      g_bvh;

// 核心检测接口
GEO_API bool RaycastTriangle(const glm::vec3& orig, const glm::vec3& dir, const CollisionTri& tri, float& t, glm::vec3& outNormal);
GEO_API int BuildBVH(int start, int count);
GEO_API bool RayAABB(const glm::vec3& o, const glm::vec3& d, const glm::vec3& bmin, const glm::vec3& bmax, float& tmin, float& tmax);
GEO_API float TraceBVH(const glm::vec3& start, const glm::vec3& dir, glm::vec3& outNormal);