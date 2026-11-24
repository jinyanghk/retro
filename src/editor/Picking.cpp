#include "Picking.h"
#include "Globals.h"
#include "math_compat.h"
#include "BVH.h"
#include <cmath>
#include <algorithm>

bool ScreenPointInAABBWorld(const glm::vec3& worldMin, const glm::vec3& worldMax,
                                   const glm::mat4& view, const glm::mat4& proj,
                                   int w, int h, int mx, int my, float padding) {
    glm::vec3 corners[8] = {
        {worldMin.x,worldMin.y,worldMin.z},{worldMax.x,worldMin.y,worldMin.z},
        {worldMax.x,worldMax.y,worldMin.z},{worldMin.x,worldMax.y,worldMin.z},
        {worldMin.x,worldMin.y,worldMax.z},{worldMax.x,worldMin.y,worldMax.z},
        {worldMax.x,worldMax.y,worldMax.z},{worldMin.x,worldMax.y,worldMax.z},
    };
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (auto& c : corners) {
        float sx, sy;
        if (!WorldToScreen(c, view, proj, w, h, sx, sy)) return false;
        minX = std::min(minX, sx); maxX = std::max(maxX, sx);
        minY = std::min(minY, sy); maxY = std::max(maxY, sy);
    }
    return mx >= minX - padding && mx <= maxX + padding && my >= minY - padding && my <= maxY + padding;
}

bool ScreenToWorldRay(const glm::mat4& view, const glm::mat4& proj, int w, int h, int mx, int my, glm::vec3& outOrigin, glm::vec3& outDir) {
    float ndcX = (2.0f * mx) / w - 1.0f;
    float ndcY = 1.0f - (2.0f * my) / h;
    glm::mat4 invVP = glm::inverse(proj * view);
    glm::vec4 nearP = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farP  = invVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
    if (fabsf(nearP.w) < 1e-6f || fabsf(farP.w) < 1e-6f) return false;
    glm::vec3 nearW = glm::vec3(nearP) / nearP.w;
    glm::vec3 farW  = glm::vec3(farP)  / farP.w;
    outOrigin = nearW;
    outDir = glm::normalize(farW - nearW);
    return true;
}

int PickBrushRay(const glm::vec3& rayOrigin, const glm::vec3& rayDir) {
    int best = -1; float bestT = 1e30f;
    for (size_t i = 0; i < g_brushes.size(); i++) {
        if (i < g_brushVisible.size() && g_brushVisible[i] == 0) continue;
        const auto& b = g_brushes[i];
        if (b.m_vertices.size() < 8 || b.m_faces.size() < 6) continue;
        float tmin, tmax;
        if (!RayAABB(rayOrigin, rayDir, b.GetBBoxMins(), b.GetBBoxMaxs(), tmin, tmax)) continue;
        if (tmin > bestT) continue;
        for (int f = 0; f < 6; f++) {
            const auto& face = b.m_faces[f];
            const int* q = face.m_nVertexIndices;
            CollisionTri tris[2] = {
                { b.m_vertices[q[0]], b.m_vertices[q[1]], b.m_vertices[q[2]], face.m_vecNormal },
                { b.m_vertices[q[0]], b.m_vertices[q[2]], b.m_vertices[q[3]], face.m_vecNormal },
            };
            for (int t = 0; t < 2; t++) {
                float thit; glm::vec3 n;
                if (RaycastTriangle(rayOrigin, rayDir, tris[t], thit, n) && thit < bestT) { bestT = thit; best = (int)i; }
            }
        }
    }
    return best;
}

int PickEntityRay(const glm::vec3& rayOrigin, const glm::vec3& rayDir) {
    int best = -1; float bestT = 1e30f;
    for (size_t i = 0; i < g_entities.size(); i++) {
        if (i < g_entityVisible.size() && g_entityVisible[i] == 0) continue;
        float tmin, tmax;
        if (RayAABB(rayOrigin, rayDir, g_entities[i].m_vecBBoxMins, g_entities[i].m_vecBBoxMaxs, tmin, tmax)) {
            if (tmin >= 0.0f && tmin < bestT) { bestT = tmin; best = (int)i; }
        }
    }
    return best;
}

int PickVertex(const CBrush& b, const glm::mat4& view, const glm::mat4& proj, int w, int h, int mx, int my, float radiusPx) {
    int best = -1; float bestD2 = radiusPx*radiusPx;
    for (int i = 0; i < 8; i++) {
        float sx, sy;
        if (!WorldToScreen(b.m_vertices[i], view, proj, w, h, sx, sy)) continue;
        float d2 = (sx - mx)*(sx - mx) + (sy - my)*(sy - my);
        if (d2 < bestD2) { bestD2 = d2; best = i; }
    }
    return best;
}

int PickEdge(const CBrush& b, const glm::mat4& view, const glm::mat4& proj, int w, int h, int mx, int my, float radiusPx) {
    int best = -1; float bestD2 = radiusPx*radiusPx;
    for (int e = 0; e < 12; e++) {
        glm::vec3 p0 = b.m_vertices[kEdgeVertexIndices[e][0]];
        glm::vec3 p1 = b.m_vertices[kEdgeVertexIndices[e][1]];
        float sx0, sy0, sx1, sy1;
        if (!WorldToScreen(p0, view, proj, w, h, sx0, sy0)) continue;
        if (!WorldToScreen(p1, view, proj, w, h, sx1, sy1)) continue;
        float dx = sx1-sx0, dy = sy1-sy0, len2 = dx*dx + dy*dy;
        if (len2 < 1e-6f) continue;
        float t = glm::clamp(((mx-sx0)*dx + (my-sy0)*dy) / len2, 0.0f, 1.0f);
        float cx = sx0 + t*dx, cy = sy0 + t*dy;
        float d2 = (mx-cx)*(mx-cx) + (my-cy)*(my-cy);
        if (d2 < bestD2) { bestD2 = d2; best = e; }
    }
    return best;
}

int PickFace(const CBrush& b, const glm::mat4& view, const glm::mat4& proj, int w, int h, int mx, int my) {
    int best = -1; float bestD2 = 1e18f;
    for (int f = 0; f < 6; f++) {
        glm::vec3 c = GetFaceCenter(b, f);
        float sx, sy;
        if (!WorldToScreen(c, view, proj, w, h, sx, sy)) continue;
        float d2 = (sx-mx)*(sx-mx) + (sy-my)*(sy-my);
        if (d2 < bestD2) { bestD2 = d2; best = f; }
    }
    return best;
}