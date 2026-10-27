#include "MapCompiler.h"

static void PushVertex(MeshBuffers& m, const Vec3& p, const Vec3& n) {
    m.verts.insert(m.verts.end(), {p.x, p.y, p.z, n.x, n.y, n.z});
}

void BuildBrushMeshes(const std::vector<Brush>& brushes,
                      MeshBuffers& renderMesh,
                      MeshBuffers& collisionMesh)
{
    for (const auto& b : brushes) {
        if (b.verts.size() < 8) continue;
        for (const auto& f : b.faces) {
            if (f.indices.size() < 3) continue;
            int i0 = f.indices[0];
            int i1 = f.indices[1];
            int i2 = f.indices[2];
            int i3 = f.indices.size() >= 4 ? f.indices[3] : i2;
            if (i0 < 0 || i0 >= (int)b.verts.size() ||
                i1 < 0 || i1 >= (int)b.verts.size() ||
                i2 < 0 || i2 >= (int)b.verts.size() ||
                i3 < 0 || i3 >= (int)b.verts.size()) continue;

            // 渲染：每个面重新 push 4 个顶点，索引 local
            uint32_t base = (uint32_t)(renderMesh.verts.size() / 6);
            PushVertex(renderMesh, b.verts[i0], f.normal);
            PushVertex(renderMesh, b.verts[i1], f.normal);
            PushVertex(renderMesh, b.verts[i2], f.normal);
            PushVertex(renderMesh, b.verts[i3], f.normal);
            renderMesh.indices.insert(renderMesh.indices.end(),
                { base+0, base+1, base+2, base+0, base+2, base+3 });

            // 碰撞：同样 4 顶点，但只存 pos
            uint32_t cbase = (uint32_t)(collisionMesh.verts.size() / 3);
            collisionMesh.verts.insert(collisionMesh.verts.end(),
                { b.verts[i0].x, b.verts[i0].y, b.verts[i0].z,
                  b.verts[i1].x, b.verts[i1].y, b.verts[i1].z,
                  b.verts[i2].x, b.verts[i2].y, b.verts[i2].z,
                  b.verts[i3].x, b.verts[i3].y, b.verts[i3].z });
            collisionMesh.indices.insert(collisionMesh.indices.end(),
                { cbase+0, cbase+1, cbase+2, cbase+0, cbase+2, cbase+3 });
        }
    }
}