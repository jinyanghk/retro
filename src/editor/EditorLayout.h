// src/editor/EditorLayout.h
#pragma once

#include "imgui.h"

namespace EditorLayout
{
    enum LayoutMode
    {
        LAYOUT_DEFAULT = 0,   // 3D 全屏，右侧只有 Entity / Brush
        LAYOUT_QUAD,          // 田字（3D 左上，2D 分三块）
    };

    void Begin();
    void End();

    void ResetDefaultLayout();
    void SetLayoutMode(LayoutMode mode);
    LayoutMode GetLayoutMode();
}
