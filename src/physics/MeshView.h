#pragma once
#include <cstdint>

// 碰撞网格的只读视图。
// 契约：verts 为 tightly packed 的 pos3 数据，stride = 3。
// 消费者：Physics::CreateStaticMesh —— 唯一合法消费者。
// ⚠️ 永远不要用渲染网格（stride 11）构造本结构。
struct CollisionMeshView {
    const float*    verts   = nullptr;
    const uint32_t* indices = nullptr;
    uint32_t        nv      = 0;
    uint32_t        ni      = 0;

    static constexpr int kVertStride = 3;

    bool IsEmpty() const { return nv == 0 || ni < 3 || verts == nullptr || indices == nullptr; }
};