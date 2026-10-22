#include "app/EditorLayout.h"

#include "imgui.h"
#include "imgui_internal.h"

namespace EditorLayout
{

static bool       s_bLayoutInitialized = false;
static bool       s_bResetRequested    = false;
static LayoutMode s_layoutMode         = LAYOUT_DEFAULT;

//-----------------------------------------------------------------------------
void SetLayoutMode(LayoutMode mode)
{
    s_layoutMode       = mode;
    s_bResetRequested  = true;
}

//-----------------------------------------------------------------------------
LayoutMode GetLayoutMode()
{
    return s_layoutMode;
}

//-----------------------------------------------------------------------------
void ResetDefaultLayout()
{
    s_bResetRequested = true;
}

//-----------------------------------------------------------------------------
void Begin()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags host_flags = 0;
    host_flags |= ImGuiWindowFlags_NoTitleBar;
    host_flags |= ImGuiWindowFlags_NoCollapse;
    host_flags |= ImGuiWindowFlags_NoResize;
    host_flags |= ImGuiWindowFlags_NoMove;
    host_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
    host_flags |= ImGuiWindowFlags_NoNavFocus;
    host_flags |= ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("##DockHost", nullptr, host_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("EditorDockSpace");

    if (!s_bLayoutInitialized || s_bResetRequested)
    {
        s_bLayoutInitialized = true;
        s_bResetRequested    = false;

        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id,
            ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

        if (s_layoutMode == LAYOUT_DEFAULT)
        {
            // 右侧 Entity / Brush，底部 Console / Tools，中央 3D
            ImGuiID dock_main   = dockspace_id;
            ImGuiID dock_right  = ImGui::DockBuilderSplitNode(
                dock_main, ImGuiDir_Right, 0.28f, nullptr, &dock_main);
            ImGuiID dock_bottom = ImGui::DockBuilderSplitNode(
                dock_main, ImGuiDir_Down, 0.22f, nullptr, &dock_main);

            ImGui::DockBuilderDockWindow("3D View", dock_main);
            ImGui::DockBuilderDockWindow("Entity",  dock_right);
            ImGui::DockBuilderDockWindow("Brush",   dock_right);
            ImGui::DockBuilderDockWindow("Console", dock_bottom);
            ImGui::DockBuilderDockWindow("Tools", dock_bottom);

            // 不 dock 任何窗口到 dock_main（保持中央节点空，Passthru 生效）
        }
        else if (s_layoutMode == LAYOUT_UNITY)
        {
            // 右侧 Entity / Brush + 2D 视图（tab 化），底部 Console / Tools
            ImGuiID dock_main   = dockspace_id;
            ImGuiID dock_right  = ImGui::DockBuilderSplitNode(
                dock_main, ImGuiDir_Right, 0.28f, nullptr, &dock_main);
            ImGuiID dock_bottom = ImGui::DockBuilderSplitNode(
                dock_main, ImGuiDir_Down, 0.22f, nullptr, &dock_main);

            ImGui::DockBuilderDockWindow("Entity",     dock_right);
            ImGui::DockBuilderDockWindow("Brush",      dock_right);
            ImGui::DockBuilderDockWindow("Top View",   dock_right);
            ImGui::DockBuilderDockWindow("Front View", dock_right);
            ImGui::DockBuilderDockWindow("Side View",  dock_right);
            ImGui::DockBuilderDockWindow("Console",    dock_bottom);
            ImGui::DockBuilderDockWindow("Tools",    dock_bottom);
        }
        else if (s_layoutMode == LAYOUT_QUAD)
        {
            // 田字：左上 3D，右上 Top View，右下 Front View，左下 Side View
            //
            // 关键：PassthruCentralNode 让"中央节点"透出 3D。
            // 所以"中央节点"必须落在"左上"。
            //
            // 分割顺序：
            //   1. 先从 dockspace 右侧分割出 dock_right（40%），dock_main 变为左侧
            //   2. 再从 dock_main 下方分割出 dock_left_bottom（40%），dock_main 变为左上
            //   3. 此时 dock_main 就是"中央节点" = 左上
            //
            ImGuiID dock_main  = dockspace_id;
            ImGuiID dock_right = ImGui::DockBuilderSplitNode(
                dock_main, ImGuiDir_Right, 0.40f, nullptr, &dock_main);

            ImGuiID dock_left_top    = dock_main;   // 中央节点（左上）
            ImGuiID dock_left_bottom = ImGui::DockBuilderSplitNode(
                dock_left_top, ImGuiDir_Down, 0.40f, nullptr, &dock_left_top);

            // 右侧再上下分割
            ImGuiID dock_right_top    = dock_right;
            ImGuiID dock_right_bottom = ImGui::DockBuilderSplitNode(
                dock_right_top, ImGuiDir_Down, 0.50f, nullptr, &dock_right_top);

            // Dock 窗口
            // ★ 不 dock 任何窗口到 dock_left_top（保持"中央节点"空，Passthru 生效）

            ImGui::DockBuilderDockWindow("Top View",   dock_right_top);
            ImGui::DockBuilderDockWindow("Front View", dock_right_bottom);
            ImGui::DockBuilderDockWindow("Side View",  dock_left_bottom);

            ImGui::DockBuilderDockWindow("Entity",  dock_right_top);      // 和 Top View tab 化
            ImGui::DockBuilderDockWindow("Brush",   dock_right_top);
            ImGui::DockBuilderDockWindow("Console", dock_right_bottom);   // 和 Front View tab 化
            ImGui::DockBuilderDockWindow("Tools", dock_right_bottom);
        }

        ImGui::DockBuilderFinish(dockspace_id);
    }

    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f),
        ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::End();
}

//-----------------------------------------------------------------------------
void End()
{
}

} // namespace EditorLayout