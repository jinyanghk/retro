#include "BVH.h"

std::vector<CollisionTri> g_sortedTris;
std::vector<BVHNode>      g_bvh;

bool RaycastTriangle(const glm::vec3& orig, const glm::vec3& dir, const CollisionTri& tri, float& t, glm::vec3& outNormal) {
    const float EPSILON = 1e-7f;
    glm::vec3 edge1 = tri.v1 - tri.v0;
    glm::vec3 edge2 = tri.v2 - tri.v0;
    glm::vec3 h = glm::cross(dir, edge2);
    float a = glm::dot(edge1, h);
    if (fabsf(a) < EPSILON) return false;
    float f = 1.0f / a;
    glm::vec3 s = orig - tri.v0;
    float u = f * glm::dot(s, h);
    if (u < 0.0f || u > 1.0f) return false;
    glm::vec3 q = glm::cross(s, edge1);
    float v = f * glm::dot(dir, q);
    if (v < 0.0f || u + v > 1.0f) return false;
    t = f * glm::dot(edge2, q);
    if (t > EPSILON) {
        outNormal = tri.normal;
        return true;
    }
    return false;
}

int BuildBVH(int start, int count) {
    int nodeIdx = (int)g_bvh.size();
    g_bvh.push_back({});
    glm::vec3 mn(1e30f), mx(-1e30f);
    for (int i = start; i < start + count; i++) {
        const auto& t = g_sortedTris[i];
        mn = glm::min(mn, glm::min(t.v0, glm::min(t.v1, t.v2)));
        mx = glm::max(mx, glm::max(t.v0, glm::max(t.v1, t.v2)));
    }
    g_bvh[nodeIdx].bmin = mn; g_bvh[nodeIdx].bmax = mx;
    if (count <= 4) {
        g_bvh[nodeIdx].start = start; g_bvh[nodeIdx].count = count;
        return nodeIdx;
    }
    glm::vec3 extent = mx - mn;
    int axis = (extent.y > extent.x) ? 1 : 0;
    if (extent.z > ((axis == 0) ? extent.x : extent.y)) axis = 2;
    float mid = mn[axis] + extent[axis] * 0.5f;
    int leftCount = 0;
    for (int i = start; i < start + count; i++) {
        glm::vec3 c = (g_sortedTris[i].v0 + g_sortedTris[i].v1 + g_sortedTris[i].v2) * (1.0f / 3.0f);
        if (c[axis] < mid) leftCount++;
    }
    if (leftCount == 0 || leftCount == count) leftCount = count / 2;
    std::nth_element(
        g_sortedTris.begin() + start, g_sortedTris.begin() + start + leftCount, g_sortedTris.begin() + start + count,
        [axis](const CollisionTri& a, const CollisionTri& b) {
            glm::vec3 ca = (a.v0 + a.v1 + a.v2) * (1.0f / 3.0f);
            glm::vec3 cb = (b.v0 + b.v1 + b.v2) * (1.0f / 3.0f);
            return ca[axis] < cb[axis];
        });
    int leftIdx = BuildBVH(start, leftCount);
    int rightIdx = BuildBVH(start + leftCount, count - leftCount);
    g_bvh[nodeIdx].left = leftIdx; g_bvh[nodeIdx].right = rightIdx;
    return nodeIdx;
}

bool RayAABB(const glm::vec3& o, const glm::vec3& d, const glm::vec3& bmin, const glm::vec3& bmax, float& tmin, float& tmax) {
    tmin = 0.0f; tmax = 1e30f;
    for (int i = 0; i < 3; i++) {
        if (fabsf(d[i]) < 1e-8f) {
            if (o[i] < bmin[i] || o[i] > bmax[i]) return false;
        } else {
            float invD = 1.0f / d[i];
            float t0 = (bmin[i] - o[i]) * invD, t1 = (bmax[i] - o[i]) * invD;
            if (t0 > t1) std::swap(t0, t1);
            if (t0 > tmin) tmin = t0;
            if (t1 < tmax) tmax = t1;
            if (tmin > tmax) return false;
        }
    }
    return true;
}

float TraceBVH(const glm::vec3& start, const glm::vec3& dir, glm::vec3& outNormal) {
    if (g_bvh.empty()) return -1.0f;
    float closestT = 1e30f; bool hit = false;
    int stack[64], sp = 0; stack[sp++] = 0;
    while (sp > 0) {
        int nodeIdx = stack[--sp];
        const BVHNode& node = g_bvh[nodeIdx];
        float tmin, tmax;
        if (!RayAABB(start, dir, node.bmin, node.bmax, tmin, tmax)) continue;
        if (tmin > closestT) continue;
        if (node.isLeaf()) {
            for (int i = node.start; i < node.start + node.count; i++) {
                float t;
                glm::vec3 n;
                if (RaycastTriangle(start, dir, g_sortedTris[i], t, n) && t < closestT) { 
                    closestT = t; 
                    outNormal = n;
                    hit = true; 
                }
            }
        } else {
            if (sp + 2 <= 64) { stack[sp++] = node.left; stack[sp++] = node.right; }
        }
    }
    return hit ? closestT : -1.0f;
}