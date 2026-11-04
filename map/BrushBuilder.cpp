#include "MapCompiler.h"
#include <cmath>
#include <unordered_map>
#include <algorithm>

// 默认材质（editor 里 face.material 为空时用这个）
static const char* kDefaultMaterial = "DEV/DEV_MEASUREGENERIC01";

// 6 个标准方向对应的面索引（和 faceMaterials 顺序一致）
// [POS_Z, NEG_Z, POS_X, NEG_X, POS_Y, NEG_Y]
static const int kFaceOrder[6] = { 1, 0, 3, 2, 5, 4 }; // 对应 PushQuad 调用顺序

// 收集一个 quad 的 4 顶点 + 2 三角形（写入临时区）
struct QuadData {
    Vec3 verts[4];
    Vec3 normal;
    Vec3 color;
    uint32_t materialSlot;  // 0..5
};

void BuildBrushMeshes(const std::vector<Brush>& brushes, MeshBuffers& renderMesh, MeshBuffers& collisionMesh) {
    Vec3 kWhite{1,1,1};

    // 1. 先把所有 quad 收集到临时数组，按 material 分组
    struct PendingQuad {
        Vec3 v[4];
        Vec3 normal;
        Vec3 color;
        std::string material;
    };
    std::vector<PendingQuad> pendingQuads;

    for (const auto& b : brushes) {
        Vec3 mn = b.mins, mx = b.maxs;
        if (mn.x > mx.x || mn.y > mx.y || mn.z > mx.z) continue;

        Vec3 p0 = {mn.x, mn.y, mn.z};
        Vec3 p1 = {mx.x, mn.y, mn.z};
        Vec3 p2 = {mx.x, mx.y, mn.z};
        Vec3 p3 = {mn.x, mx.y, mn.z};
        Vec3 p4 = {mn.x, mn.y, mx.z};
        Vec3 p5 = {mx.x, mn.y, mx.z};
        Vec3 p6 = {mx.x, mx.y, mx.z};
        Vec3 p7 = {mn.x, mx.y, mx.z};

        // 每面: (v0,v1,v2,v3, normal, faceSlot)
        struct FaceDef { Vec3 v0,v1,v2,v3, n; int slot; };
        FaceDef faces[6] = {
            // slot 1 = POS_Z
            { p4, p5, p6, p7, {0,0,1},  1 },
            // slot 0 = NEG_Z
            { p0, p3, p2, p1, {0,0,-1}, 0 },
            // slot 3 = POS_X
            { p1, p2, p6, p5, {1,0,0},  3 },
            // slot 2 = NEG_X
            { p0, p4, p7, p3, {-1,0,0}, 2 },
            // slot 5 = POS_Y
            { p3, p7, p6, p2, {0,1,0},  5 },
            // slot 4 = NEG_Y
            { p0, p1, p5, p4, {0,-1,0}, 4 },
        };

        // 这里直接按 "材质名" 收集
        for (auto& f : faces) {
            std::string mat = b.faceMaterials[f.slot];
            if (mat.empty()) mat = kDefaultMaterial;

            PendingQuad q;
            q.v[0] = f.v0; q.v[1] = f.v1; q.v[2] = f.v2; q.v[3] = f.v3;
            q.normal = f.n;
            q.color  = kWhite;
            q.material = mat;
            pendingQuads.push_back(q);
        }
    }

    // 2. 按 material 名字稳定分组（保持首次出现顺序）
    std::vector<std::string> matOrder;
    std::unordered_map<std::string, std::vector<size_t>> byMat;
    for (size_t i = 0; i < pendingQuads.size(); ++i) {
        const std::string& m = pendingQuads[i].material;
        if (byMat.find(m) == byMat.end()) matOrder.push_back(m);
        byMat[m].push_back(i);
    }

    // 3. 按 material 顺序逐个展开：顶点照常 push，indices 连续写
    //    → 这样同一材质的 quad 的 indices 在 indices 数组里是连续的
    for (const std::string& mat : matOrder) {
        MeshBuffers::MaterialGroup grp;
        grp.name        = mat;
        grp.indexOffset = (uint32_t)renderMesh.indices.size();

        for (size_t qi : byMat[mat]) {
            const auto& q = pendingQuads[qi];
            uint32_t rBase = (uint32_t)(renderMesh.verts.size() / 9);
            uint32_t cBase = (uint32_t)(collisionMesh.verts.size() / 3);

            for (int k = 0; k < 4; ++k) {
                renderMesh.verts.insert(renderMesh.verts.end(), {
                    q.v[k].x, q.v[k].y, q.v[k].z,
                    q.normal.x, q.normal.y, q.normal.z,
                    q.color.x, q.color.y, q.color.z
                });
                collisionMesh.verts.insert(collisionMesh.verts.end(), {
                    q.v[k].x, q.v[k].y, q.v[k].z
                });
            }
            renderMesh.indices.insert(renderMesh.indices.end(), { rBase, rBase+1, rBase+2, rBase, rBase+2, rBase+3 });
            collisionMesh.indices.insert(collisionMesh.indices.end(), { cBase, cBase+1, cBase+2, cBase, cBase+2, cBase+3 });
        }

        grp.indexCount = (uint32_t)renderMesh.indices.size() - grp.indexOffset;
        if (grp.indexCount > 0) {
            renderMesh.materialGroups.push_back(grp);
        }
    }
}