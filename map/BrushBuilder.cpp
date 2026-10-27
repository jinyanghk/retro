#include "MapCompiler.h"
#include <cmath>

static void PushQuad(MeshBuffers& renderMesh, MeshBuffers& collisionMesh,
                     const Vec3& v0, const Vec3& v1, const Vec3& v2, const Vec3& v3, 
                     const Vec3& normal, const Vec3& color) 
{
    uint32_t rBase = (uint32_t)(renderMesh.verts.size() / 9);
    uint32_t cBase = (uint32_t)(collisionMesh.verts.size() / 3);

    Vec3 verts[4] = { v0, v1, v2, v3 };
    for (int i = 0; i < 4; ++i) {
        renderMesh.verts.insert(renderMesh.verts.end(), {
            verts[i].x, verts[i].y, verts[i].z, normal.x, normal.y, normal.z, color.x, color.y, color.z
        });
        collisionMesh.verts.insert(collisionMesh.verts.end(), {
            verts[i].x, verts[i].y, verts[i].z
        });
    }
    
    // 构造两个三角形
    renderMesh.indices.insert(renderMesh.indices.end(), { rBase, rBase+1, rBase+2, rBase, rBase+2, rBase+3 });
    collisionMesh.indices.insert(collisionMesh.indices.end(), { cBase, cBase+1, cBase+2, cBase, cBase+2, cBase+3 });
}

void BuildBrushMeshes(const std::vector<Brush>& brushes, MeshBuffers& renderMesh, MeshBuffers& collisionMesh) {
    Vec3 kWhite{1,1,1};
    
    for (const auto& b : brushes) {
        Vec3 mn = b.mins, mx = b.maxs;
        
        // 如果算出来的包围盒太小（比如退化成了点），就跳过
        if (mn.x > mx.x || mn.y > mx.y || mn.z > mx.z) continue;

        // 生成标准的 6 个面（每个面严格顺时针/逆时针，保证法线向外）
        Vec3 p0 = {mn.x, mn.y, mn.z};
        Vec3 p1 = {mx.x, mn.y, mn.z};
        Vec3 p2 = {mx.x, mx.y, mn.z};
        Vec3 p3 = {mn.x, mx.y, mn.z};
        
        Vec3 p4 = {mn.x, mn.y, mx.z};
        Vec3 p5 = {mx.x, mn.y, mx.z};
        Vec3 p6 = {mx.x, mx.y, mx.z};
        Vec3 p7 = {mn.x, mx.y, mx.z};

        // 后 (-Z)
        PushQuad(renderMesh, collisionMesh, p0, p3, p2, p1, {0,0,-1}, kWhite);
        // 前 (+Z)
        PushQuad(renderMesh, collisionMesh, p4, p5, p6, p7, {0,0,1}, kWhite);
        // 左 (-X)
        PushQuad(renderMesh, collisionMesh, p0, p4, p7, p3, {-1,0,0}, kWhite);
        // 右 (+X)
        PushQuad(renderMesh, collisionMesh, p1, p2, p6, p5, {1,0,0}, kWhite);
        // 下 (-Y)
        PushQuad(renderMesh, collisionMesh, p0, p1, p5, p4, {0,-1,0}, kWhite);
        // 上 (+Y)
        PushQuad(renderMesh, collisionMesh, p3, p7, p6, p2, {0,1,0}, kWhite);
    }
}