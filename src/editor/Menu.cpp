#include "Panels.h"
#include "Globals.h"
#include "UndoStack.h"
#include "GameDatabase.h"
#include "Layout.h"
#include "Log.h"
#include "Internal.h"

#include "imgui.h"

#include <algorithm>

extern void (*s_pfnExportMap)();
extern void (*s_pfnImportMap)();
extern void (*s_pfnCompileAndRun)();
extern void (*s_pfnLoadBvh)();
extern void SetFlyMode(bool enable);
extern void CheckNavCollisions();

void DrawMenuBar()
{
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File")) {
            //if (ImGui::MenuItem("Open"))  { if (s_pfnImportMap) s_pfnImportMap(); }
            if (ImGui::MenuItem("Save")) { if (s_pfnExportMap) s_pfnExportMap(); }
            //ImGui::Separator();
            if (ImGui::MenuItem("Compile & Run"))   { if (s_pfnCompileAndRun) s_pfnCompileAndRun(); }
            //if (ImGui::MenuItem("Compile Options...")) OpenCompileOptionsWindow();
            if (ImGui::MenuItem("Load BVH")) { if (s_pfnLoadBvh) s_pfnLoadBvh(); }         
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {}
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            std::string ul = UndoStack::Instance().GetUndoLabel();
            std::string rl = UndoStack::Instance().GetRedoLabel();
            char ub[128], rb[128];
            snprintf(ub, sizeof(ub), "Undo %s", ul.c_str());
            snprintf(rb, sizeof(rb), "Redo %s", rl.c_str());
            if (ImGui::MenuItem(ub, "Ctrl+Z", false, UndoStack::Instance().CanUndo()))
                UndoStack::Instance().Undo();
            if (ImGui::MenuItem(rb, "Ctrl+Y", false, UndoStack::Instance().CanRedo()))
                UndoStack::Instance().Redo();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Nav")) {
            ImGui::MenuItem("Show Nav Nodes", nullptr, &g_bShowNav);
            if (ImGui::MenuItem("Check Nav Collisions")) {
                CheckNavCollisions();
            }

            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            if (ImGui::MenuItem("Default Layout (Tabs)", nullptr, EditorLayout::GetLayoutMode() == EditorLayout::LAYOUT_DEFAULT))
                EditorLayout::SetLayoutMode(EditorLayout::LAYOUT_DEFAULT);
            
            if (ImGui::MenuItem("Quad Layout (4 Views)", nullptr, EditorLayout::GetLayoutMode() == EditorLayout::LAYOUT_QUAD))
                EditorLayout::SetLayoutMode(EditorLayout::LAYOUT_QUAD);
            
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout"))
                EditorLayout::ResetDefaultLayout();

            ImGui::Separator();

            ImGui::MenuItem("Show Brushes", nullptr, &g_bShowBrushes);
            ImGui::MenuItem("Show Entities", nullptr, &g_bShowEntities);

            ImGui::Separator();
            // 👈 新增：飞行模式勾选
            if (ImGui::MenuItem("Fly Mode (WASD + Mouse Look)", nullptr, g_bFlyMode)) {
                g_bFlyMode = !g_bFlyMode;
                // 开启飞行模式时，取消锁定，防止鼠标卡死在屏幕中间
                SetFlyMode(g_bFlyMode);
            }

            ImGui::MenuItem("Render Textures", nullptr, &g_bRenderTexture);

            ImGui::EndMenu();
        }        
        ImGui::EndMainMenuBar();
    }
}

void DrawToolsPanel()
{
    if (!ImGui::Begin("Tools")) { ImGui::End(); return; }

    if (ImGui::CollapsingHeader("Mode", ImGuiTreeNodeFlags_DefaultOpen))
    {
        auto ToolRadio = [](const char* label, EditorTool t) {
            if (ImGui::RadioButton(label, g_iCurrentTool == t)) {
                g_iCurrentTool = t;
                g_bBlockToolActive = (t == TOOL_CREATE_BRUSH); // 👈 复用现有 Box Create
            }
            //ImGui::SameLine();
        };
        ToolRadio("Select",        TOOL_SELECT);
        ToolRadio("Multi-Select",  TOOL_MULTI_SELECT);
        ToolRadio("Create Brush",  TOOL_CREATE_BRUSH);
        ToolRadio("Create Entity", TOOL_CREATE_ENTITY);
        ImGui::NewLine();

        if (g_iCurrentTool == TOOL_CREATE_ENTITY) {
            static char s_toolEntFilter[64] = "";
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##ToolEntFilter", "Search classname...",
                                    s_toolEntFilter, sizeof(s_toolEntFilter));

            std::vector<std::string> allClasses = GameDatabase::Instance().GetClassList();
            std::sort(allClasses.begin(), allClasses.end());

            std::string filterStr = s_toolEntFilter;
            std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), ::tolower);

            ImGui::BeginChild("##ToolEntityPalette", ImVec2(0, 160), true);
            for (const auto& className : allClasses) {
                std::string lowerClass = className;
                std::transform(lowerClass.begin(), lowerClass.end(), lowerClass.begin(), ::tolower);
                if (!filterStr.empty() && lowerClass.find(filterStr) == std::string::npos) continue;
                if (ImGui::Selectable(className.c_str(), g_szPendingEntityClass == className))
                    g_szPendingEntityClass = className;
            }
            ImGui::EndChild();
        }
    }

    if (ImGui::CollapsingHeader("Gizmo", ImGuiTreeNodeFlags_DefaultOpen))
    {
        struct GizmoItem { const char* label; GizmoMode mode; };
        GizmoItem items[] = {
            { "None",      GIZMO_NONE },
            { "Translate", GIZMO_TRANSLATE },
            { "Rotate",    GIZMO_ROTATE },
            { "Scale",     GIZMO_SCALE },
            { "Face",      GIZMO_FACE },
            { "Edge",      GIZMO_EDGE },
            { "Vertex",    GIZMO_VERTEX },
        };

        for (int i = 0; i < IM_ARRAYSIZE(items); ++i) {
            //if (i > 0) ImGui::SameLine();
            bool b = (g_iGizmoMode == items[i].mode);
            if (ImGui::RadioButton(items[i].label, b))
                g_iGizmoMode = items[i].mode;
        }
        ImGui::Separator();
        if (ImGui::RadioButton("Local", g_CurrentGizmoMode == ImGuizmo::LOCAL))
            g_CurrentGizmoMode = ImGuizmo::LOCAL;
        ImGui::SameLine();
        if (ImGui::RadioButton("World", g_CurrentGizmoMode == ImGuizmo::WORLD))
            g_CurrentGizmoMode = ImGuizmo::WORLD;

        ImGui::Checkbox("Use Snap", &g_bUseSnap);
        //ImGui::SameLine();
        if (g_CurrentGizmoOperation & ImGuizmo::TRANSLATE)
            ImGui::InputFloat3("Snap", &g_Snap[0]);
        if (g_CurrentGizmoOperation & ImGuizmo::ROTATE)
            ImGui::InputFloat("Angle Snap", &g_Snap[0]);
        if (g_CurrentGizmoOperation & ImGuizmo::SCALE)
            ImGui::InputFloat("Scale Snap", &g_Snap[0]);
    }

    ImGui::End();
}

static bool s_bCompileOpen = false;
void OpenCompileOptionsWindow() { s_bCompileOpen = true; }
void DrawCompileOptionsWindow() { /* 粘贴原本的 */ }