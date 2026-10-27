#pragma once
#include "Common.h"

struct BVHNode {
    glm::vec3 bmin, bmax;
    int left = -1, right = -1;
    int start = 0, count = 0;
    bool isLeaf() const { return count > 0; }
};

// 全局的三角形和 BVH 节点数组
extern std::vector<CollisionTri> g_sortedTris;
extern std::vector<BVHNode>      g_bvh;

// 核心检测接口
bool RaycastTriangle(const glm::vec3& orig, const glm::vec3& dir, const CollisionTri& tri, float& t, glm::vec3& outNormal);
int BuildBVH(int start, int count);
bool RayAABB(const glm::vec3& o, const glm::vec3& d, const glm::vec3& bmin, const glm::vec3& bmax, float& tmin, float& tmax);
float TraceBVH(const glm::vec3& start, const glm::vec3& dir, glm::vec3& outNormal);