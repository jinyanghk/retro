#include "MapCompiler.h"
#include <cmath>
#include <sstream>
#include <glm/glm.hpp>

static void ParseAxis(const std::string& axisStr, glm::vec3& outAxis, float& outShift, float& outScale) {
    glm::vec3 axis(1, 0, 0);
    float shift = 0.0f;
    float scale = 1.0f;
    size_t start = axisStr.find('[');
    size_t end = axisStr.find(']');
    if (start != std::string::npos && end != std::string::npos) {
        std::string inside = axisStr.substr(start + 1, end - start - 1);
        std::stringstream ss(inside);
        ss >> axis.x >> axis.y >> axis.z >> shift;
        std::string rest = axisStr.substr(end + 1);
        std::stringstream ss2(rest);
        ss2 >> scale;
    }
    outAxis = axis;
    outShift = shift;
    outScale = scale;
}

void BuildBrushMeshes(const std::vector<Brush>& brushes, MeshBuffers& renderMesh, MeshBuffers& collisionMesh) {
    glm::vec3 kWhite(1.0f);

    struct PendingFace {
        std::vector<Vec3> verts;
        Vec3 normal;
        std::string material;
        std::string uaxis;
        std::string vaxis;
    };
    std::vector<PendingFace> pendingFaces;

    for (const auto& b : brushes) {
        // 情况 A：优先用真实的 faces
        if (!b.faces.empty()) {
            for (const auto& f : b.faces) {
                if (f.verts.size() < 3) continue;
                PendingFace pf;
                pf.verts    = f.verts;
                pf.normal   = f.normal;
                pf.material = f.material.empty() ? "DEV/DEV_MEASUREGENERIC01" : f.material;
                pf.uaxis    = f.uaxis;
                pf.vaxis    = f.vaxis;
                pendingFaces.push_back(pf);
            }
        } 
        // 情况 B：兜底，没有 faces 就按 AABB 生成 6 个面
        else if (!b.verts.empty() && b.verts.size() >= 8) {
            Vec3 p0 = b.verts[0], p1 = b.verts[1], p2 = b.verts[2], p3 = b.verts[3];
            Vec3 p4 = b.verts[4], p5 = b.verts[5], p6 = b.verts[6], p7 = b.verts[7];
            struct QuadDef { std::vector<Vec3> v; Vec3 n; int faceIdx; };
            std::vector<QuadDef> quads = {
                { {p4, p5, p6, p7}, {0,0,1},  1 }, // +Z
                { {p0, p3, p2, p1}, {0,0,-1}, 0 }, // -Z
                { {p1, p2, p6, p5}, {1,0,0},  3 }, // +X
                { {p0, p4, p7, p3}, {-1,0,0}, 2 }, // -X
                { {p3, p7, p6, p2}, {0,1,0},  5 }, // +Y
                { {p0, p1, p5, p4}, {0,-1,0}, 4 }, // -Y
            };
            for (auto& q : quads) {
                PendingFace pf;
                pf.verts = q.v;
                pf.normal = q.n;
                std::string m = b.faceMaterials[q.faceIdx];
                pf.material = m.empty() ? "DEV/DEV_MEASUREGENERIC01" : m;
                pf.uaxis = "[1 0 0 0] 1";
                pf.vaxis = "[0 1 0 0] 1";
                pendingFaces.push_back(pf);
            }
        }
    }

    // 按材质分组
    std::vector<std::string> matOrder;
    std::unordered_map<std::string, std::vector<size_t>> byMat;
    for (size_t i = 0; i < pendingFaces.size(); ++i) {
        const std::string& m = pendingFaces[i].material;
        if (byMat.find(m) == byMat.end()) matOrder.push_back(m);
        byMat[m].push_back(i);
    }

    for (const std::string& mat : matOrder) {
        MeshBuffers::MaterialGroup grp;
        grp.name = mat;
        grp.indexOffset = (uint32_t)renderMesh.indices.size();

        for (size_t fi : byMat[mat]) {
            const auto& pf = pendingFaces[fi];
            
            glm::vec3 uAxis, vAxis;
            float uShift, vShift, uScale, vScale;
            ParseAxis(pf.uaxis, uAxis, uShift, uScale);
            ParseAxis(pf.vaxis, vAxis, vShift, vScale);
            
            uint32_t rBase = (uint32_t)(renderMesh.verts.size() / 11);
            uint32_t cBase = (uint32_t)(collisionMesh.verts.size() / 3);

            for (size_t i = 0; i < pf.verts.size(); ++i) {
                glm::vec3 pos(pf.verts[i].x, pf.verts[i].y, pf.verts[i].z);
                float u = (glm::dot(pos, uAxis) / uScale + uShift) / 1024.0f;
                float v = (glm::dot(pos, vAxis) / vScale + vShift) / 1024.0f;
                
                renderMesh.verts.insert(renderMesh.verts.end(), {
                    pos.x, pos.y, pos.z,
                    pf.normal.x, pf.normal.y, pf.normal.z,
                    kWhite.x, kWhite.y, kWhite.z,
                    u, v
                });
                collisionMesh.verts.insert(collisionMesh.verts.end(), {
                    pos.x, pos.y, pos.z
                });
            }

            // 扇形三角化
            for (size_t i = 1; i + 1 < pf.verts.size(); ++i) {
                renderMesh.indices.push_back(rBase + 0);
                renderMesh.indices.push_back(rBase + i);
                renderMesh.indices.push_back(rBase + i + 1);

                collisionMesh.indices.push_back(cBase + 0);
                collisionMesh.indices.push_back(cBase + i);
                collisionMesh.indices.push_back(cBase + i + 1);
            }
        }
        grp.indexCount = (uint32_t)renderMesh.indices.size() - grp.indexOffset;
        if (grp.indexCount > 0) renderMesh.materialGroups.push_back(grp);
    }
}