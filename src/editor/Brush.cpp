#include "Brush.h"

#include "math_compat.h"

#include <cmath>
#include <algorithm>

const int kFaceVertexIndices[6][4] = {
    {4,5,6,7}, // +Z
    {0,3,2,1}, // -Z
    {1,2,6,5}, // +X
    {0,4,7,3}, // -X
    {3,7,6,2}, // +Y
    {0,1,5,4}, // -Y
};

const int kEdgeVertexIndices[12][2] = {
    {0,1},{1,2},{2,3},{3,0},
    {4,5},{5,6},{6,7},{7,4},
    {0,4},{1,5},{2,6},{3,7},
};

void CBrush::RebuildPrimitiveMesh()
{
    glm::vec3 mins = GetBBoxMins();
    glm::vec3 maxs = GetBBoxMaxs();

    m_vertices.resize(8);
    m_vertices[0] = glm::vec3(mins.x, mins.y, mins.z);
    m_vertices[1] = glm::vec3(maxs.x, mins.y, mins.z);
    m_vertices[2] = glm::vec3(maxs.x, maxs.y, mins.z);
    m_vertices[3] = glm::vec3(mins.x, maxs.y, mins.z);
    m_vertices[4] = glm::vec3(mins.x, mins.y, maxs.z);
    m_vertices[5] = glm::vec3(maxs.x, mins.y, maxs.z);
    m_vertices[6] = glm::vec3(maxs.x, maxs.y, maxs.z);
    m_vertices[7] = glm::vec3(mins.x, maxs.y, maxs.z);

    m_faces.resize(6);
    m_faces[FACE_POS_Z] = { glm::vec3(0,0,1),  {4,5,6,7}, "" };
    m_faces[FACE_NEG_Z] = { glm::vec3(0,0,-1), {0,3,2,1}, "" };
    m_faces[FACE_POS_X] = { glm::vec3(1,0,0),  {1,2,6,5}, "" };
    m_faces[FACE_NEG_X] = { glm::vec3(-1,0,0), {0,4,7,3}, "" };
    m_faces[FACE_POS_Y] = { glm::vec3(0,1,0),  {3,7,6,2}, "" };
    m_faces[FACE_NEG_Y] = { glm::vec3(0,-1,0), {0,1,5,4}, "" };
}

void CBrush::RecalcBoundsFromVertices()
{
    if (m_vertices.size() < 8) return;
    glm::vec3 vmin(1e9f), vmax(-1e9f);
    for (auto& p : m_vertices) {
        vmin = glm::min(vmin, p);
        vmax = glm::max(vmax, p);
    }
    m_vecPos  = (vmin + vmax) * 0.5f;
    m_vecSize = (vmax - vmin) * 0.5f;
}

void CBrush::RecalcFaceNormals()
{
    for (auto& face : m_faces) {
        int i0 = face.m_nVertexIndices[0];
        int i1 = face.m_nVertexIndices[1];
        int i2 = face.m_nVertexIndices[2];
        glm::vec3 e1 = m_vertices[i1] - m_vertices[i0];
        glm::vec3 e2 = m_vertices[i2] - m_vertices[i0];
        glm::vec3 n = glm::cross(e1, e2);
        float len = glm::length(n);
        if (len > 1e-6f) face.m_vecNormal = n / len;
    }
}

void CBrush::RotateVertices(const glm::vec3& deltaAnglesDeg)
{
    if (m_vertices.size() < 8) return;
    glm::mat4 R = AngleMatrix(deltaAnglesDeg);
    for (auto& v : m_vertices)
        v = RotatePointAround(v, m_vecPos, R);
}

glm::vec3 GetFaceCenter(const CBrush& b, int faceIdx)
{
    glm::vec3 c(0);
    for (int i = 0; i < 4; i++) c += b.m_vertices[b.m_faces[faceIdx].m_nVertexIndices[i]];
    return c * 0.25f;
}

glm::vec3 GetEdgeCenter(const CBrush& b, int edgeIdx)
{
    return (b.m_vertices[kEdgeVertexIndices[edgeIdx][0]] +
            b.m_vertices[kEdgeVertexIndices[edgeIdx][1]]) * 0.5f;
}