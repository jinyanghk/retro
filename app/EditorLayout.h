#ifndef EDITOR_LAYOUT_H
#define EDITOR_LAYOUT_H

namespace EditorLayout
{
    enum LayoutMode
    {
        LAYOUT_DEFAULT = 0,   // 3D 全屏，右侧只有 Entity / Brush
        LAYOUT_UNITY,         // 右侧 Entity / Brush + 2D 视图（tab 化）
        LAYOUT_QUAD,          // 田字（3D 左上，2D 分三块）
    };

    void Begin();
    void End();

    void ResetDefaultLayout();
    void SetLayoutMode(LayoutMode mode);
    LayoutMode GetLayoutMode();
}

#endif // EDITOR_LAYOUT_H