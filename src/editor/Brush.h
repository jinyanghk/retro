// src/editor/Brush.h
#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

enum BrushFaceIndex {
    FACE_POS_Z = 0, FACE_NEG_Z = 1,
    FACE_POS_X = 2, FACE_NEG_X = 3,
    FACE_POS_Y = 4, FACE_NEG_Y = 5
};

struct BrushFace_t {
    glm::vec3   m_vecNormal = glm::vec3(0, 0, 1);
    int         m_nVertexIndices[4] = {0,0,0,0};
    std::string m_szMaterial;   // 空 = 继承 brush 的 m_szMaterial
    std::string m_szUAxis = "[1 0 0 0] 1";  // 👈 新增
    std::string m_szVAxis = "[0 1 0 0] 1";  // 👈 新增
};

// 权威数据：8 顶点 + 6 面。m_vecPos / m_vecSize 是派生量（AABB 中心 / 半尺寸）
struct CBrush {
    glm::vec3   m_vecPos  = glm::vec3(0);
    glm::vec3   m_vecSize = glm::vec3(16);   // 半尺寸
    glm::vec3   m_angRot  = glm::vec3(0);    // 度 (pitch,yaw,roll)，旋转已烘进顶点时保持 0
    std::string m_szMaterial = "DEV/DEV_MEASUREGENERIC01";
    int         m_iTexId = 0;

    std::vector<glm::vec3> m_vertices;   // size == 8
    std::vector<BrushFace_t> m_faces;    // size == 6

    glm::vec3 GetBBoxMins() const { return m_vecPos - m_vecSize; }
    glm::vec3 GetBBoxMaxs() const { return m_vecPos + m_vecSize; }

    void RebuildPrimitiveMesh();       // 从 pos/size 生成 8 顶点 + 6 面
    void RecalcBoundsFromVertices();   // 从顶点重算 pos/size
    void RecalcFaceNormals();          // 从顶点重算法线
    void RotateVertices(const glm::vec3& deltaAnglesDeg);
};

// face → 4 个顶点索引（纯数据，face/edge/vertex 编辑时查表）
extern const int kFaceVertexIndices[6][4];
// edge(12) → 2 个顶点索引
extern const int kEdgeVertexIndices[12][2];
// 射线-所有 Brush 求交（Editor 里不依赖 BVH，改 brush 立即生效）
// 返回最近命中距离；未命中返回 -1
float TraceBrushes(const std::vector<CBrush>& brushes,
                   const glm::vec3& origin, const glm::vec3& dir,
                   glm::vec3& outNormal);
// face 中心
glm::vec3 GetFaceCenter(const CBrush& b, int faceIdx);
// edge 中点
glm::vec3 GetEdgeCenter(const CBrush& b, int edgeIdx);
