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
    s_layoutMode = LAYOUT_DEFAULT;
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
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

        // 预热窗口
        const char* allWindows[] = {
            "3D View", "Top View", "Front View", "Side View",
            "Entity", "Brush", "Console", "Tools"
        };
        for (const char* name : allWindows) {
            ImGui::Begin(name, nullptr, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar);
            ImGui::End();
        }

        ImGuiID dock_main = dockspace_id;
        
        // 1. 先切右侧（占 25%）
        ImGuiID dock_right = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.25f, nullptr, &dock_main);
        
        // 2. 再切底部（占 20%）
        ImGuiID dock_bottom = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.20f, nullptr, &dock_main);

        // 右侧和底部的固定窗口
        ImGui::DockBuilderDockWindow("Entity",  dock_right);
        ImGui::DockBuilderDockWindow("Brush",   dock_right);
        ImGui::DockBuilderDockWindow("Console", dock_bottom);
        ImGui::DockBuilderDockWindow("Tools",   dock_bottom);

        if (s_layoutMode == LAYOUT_DEFAULT)
        {
            // 默认：四个视图全叠在 dock_main
            ImGui::DockBuilderDockWindow("3D View",    dock_main);
            ImGui::DockBuilderDockWindow("Top View",   dock_main);
            ImGui::DockBuilderDockWindow("Front View", dock_main);
            ImGui::DockBuilderDockWindow("Side View",  dock_main);
        }
        else if (s_layoutMode == LAYOUT_QUAD)
        {
            // 四宫格：先把 dock_main 垂直（上下）一分为二
            ImGuiID dock_top_half, dock_bottom_half;
            dock_top_half = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Up, 0.50f, nullptr, &dock_bottom_half);
            
            // 然后再把上下两半分别水平（左右）一分为二
            ImGuiID dock_top_left, dock_top_right;
            dock_top_left = ImGui::DockBuilderSplitNode(dock_top_half, ImGuiDir_Left, 0.50f, nullptr, &dock_top_right);

            ImGuiID dock_bottom_left, dock_bottom_right;
            dock_bottom_left = ImGui::DockBuilderSplitNode(dock_bottom_half, ImGuiDir_Left, 0.50f, nullptr, &dock_bottom_right);

            // 分配四个视图
            ImGui::DockBuilderDockWindow("3D View",    dock_top_left);
            ImGui::DockBuilderDockWindow("Top View",   dock_top_right);
            ImGui::DockBuilderDockWindow("Front View", dock_bottom_left);
            ImGui::DockBuilderDockWindow("Side View",  dock_bottom_right);
        }

        ImGui::DockBuilderFinish(dockspace_id);
    }

    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), 0);
    ImGui::End();
}

//-----------------------------------------------------------------------------
void End()
{
}

} // namespace EditorLayout