#include "EditorPanels.h"

#include <glm/gtc/type_ptr.hpp>

#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>
#include <map>
#include <algorithm>

#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"

#include "SceneGlobals.h"
#include "UndoStack.h"
#include "GameDatabase.h"
#include "EditorLayout.h"
#include "Log.h"

// ---------- handlers ----------
static void (*s_pfnExportMap)() = nullptr;
static void (*s_pfnImportMap)() = nullptr;
static void (*s_pfnCompileAndRun)() = nullptr;
static void (*s_pfnLoadBvh)() = nullptr;

void SetExportMapHandler(void (*pfn)()) { s_pfnExportMap = pfn; }
void SetImportMapHandler(void (*pfn)()) { s_pfnImportMap = pfn; }
void SetCompileAndRunHandler(void (*pfn)()) { s_pfnCompileAndRun = pfn; }
void SetLoadBvhHandler(void (*pfn)()) { s_pfnLoadBvh = pfn; }

// ---------------------------------------------------------------------------
#include <GL/glew.h>
// 👈 声明 main.cpp 里的全局变量
extern GLuint GetMaterialThumb(const std::string& matName);
extern void SetFlyMode(bool enable);
extern void CheckNavCollisions();
extern glm::vec3* GetEditorCamTarget();
extern float* GetEditorCamDist();
extern void InitVisibilityArrays();

// 👈 全局状态
static bool   g_bShowMaterialBrowser = false;
static std::string* g_pTargetMaterial = nullptr; // 当前正在编辑的材质指针
static char   g_matBrowserFilter[128] = "";
static std::string g_selectedMaterialTemp;  // 用于中转
static std::string g_selectedFaceMaterialTemp; // 给 Face 级别用 👈 新增
static char s_sceneFilter[128] = "";


// 👈 在主循环（ImGui::NewFrame 之后）调用
void DrawMaterialBrowserWindow() {
    if (!g_bShowMaterialBrowser) return;

    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Material Browser", &g_bShowMaterialBrowser)) {
        // 1. 顶部搜索栏
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##MatFilter", "Filter materials...", g_matBrowserFilter, sizeof(g_matBrowserFilter));
        ImGui::Separator();

        // 2. 筛选逻辑
        std::string filterLower = g_matBrowserFilter;
        std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);

        // 3. 网格布局
        float thumbSize = 128.0f; // 👈 大缩略图
        float padding = 8.0f;
        float cellSize = thumbSize + padding * 2;
        float availWidth = ImGui::GetContentRegionAvail().x;
        int columns = std::max(1, (int)(availWidth / cellSize));

        if (ImGui::BeginChild("##MatGridScroll", ImVec2(0, 0), true)) {
            int colCount = 0;
            ImGuiListClipper clipper;
            
            // 过滤后的结果（暂存指针）
            std::vector<const std::string*> filtered;
            for (auto& m : g_allMaterials) {
                std::string lower = m;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (filterLower.empty() || lower.find(filterLower) != std::string::npos) {
                    filtered.push_back(&m);
                }
            }

            clipper.Begin((int)filtered.size());
            // 这里为了网格布局，每行算一个 clipper 元素
            clipper.Begin(((int)filtered.size() + columns - 1) / columns);
            
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                    for (int col = 0; col < columns; col++) {
                        int idx = row * columns + col;
                        if (idx >= (int)filtered.size()) break;
                        
                        const std::string& mat = *filtered[idx];
                        GLuint texId = GetMaterialThumb(mat);
                        
                        ImGui::PushID(idx);
                        ImGui::BeginGroup();
                        
                        // 画缩略图按钮
                        if (ImGui::ImageButton(mat.c_str(), (ImTextureID)(intptr_t)texId, ImVec2(thumbSize, thumbSize))) {
                            // 点击了材质
                            if (g_pTargetMaterial) {
                                UndoStack::Instance().PushSnapshot("Material Change");
                                *g_pTargetMaterial = mat;
                            }
                            g_bShowMaterialBrowser = false;
                        }
                        
                        // 显示名字（限制宽度避免撑爆）
                        float textWidth = ImGui::CalcTextSize(mat.c_str()).x;
                        if (textWidth > cellSize - padding) {
                            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cellSize - padding);
                            ImGui::TextWrapped("%s", mat.c_str());
                            ImGui::PopTextWrapPos();
                        } else {
                            ImGui::TextUnformatted(mat.c_str());
                        }
                        
                        ImGui::EndGroup();
                        
                        // 如果是同一行，继续 SameLine
                        if (col < columns - 1 && idx + 1 < (int)filtered.size()) {
                            ImGui::SameLine();
                        }
                        ImGui::PopID();
                    }
                }
            }
            ImGui::EndChild();
        }
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
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

            ImGui::MenuItem("Show Nav Nodes", nullptr, &g_bShowNav);

            if (ImGui::MenuItem("Check Nav Collisions")) {
                CheckNavCollisions();
            }

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

// ---------------------------------------------------------------------------
void DrawToolsPanel()
{
    if (!ImGui::Begin("Tools")) { ImGui::End(); return; }

    if (ImGui::CollapsingHeader("Gizmo:", ImGuiTreeNodeFlags_DefaultOpen))
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

// ---------------------------------------------------------------------------
static bool InputTextString(const char* label, std::string& value)
{
    char buf[512];
    strncpy(buf, value.c_str(), sizeof(buf)-1); buf[sizeof(buf)-1] = 0;
    if (ImGui::InputText(label, buf, sizeof(buf))) { value = buf; return true; }
    return false;
}

// ---------------------------------------------------------------------------
static int pendingDeleteGroupIdx = -1;
void DrawScenePanel(int* pSelEntity, int* pSelBrush, BrushPanelResult& out)
{
    if (!ImGui::Begin("Scene")) { ImGui::End(); return; }

    // 1. 搜索栏
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##SceneFilter", "Search name / index...", s_sceneFilter, sizeof(s_sceneFilter));
    
    std::string filterLower = s_sceneFilter;
    std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);

    // 2. 全局显示开关
    ImGui::Checkbox("Show Brushes", &g_bShowBrushes);
    ImGui::SameLine();
    ImGui::Checkbox("Show Entities", &g_bShowEntities);
    ImGui::Separator();

    float treeHeight = ImGui::GetContentRegionAvail().y * 0.5f;
    
    // ================= 上半部分：单一世界树 =================
    ImGui::BeginChild("##SceneTreeChild", ImVec2(0, treeHeight), true);
    
    static int dragItemIdx = -1;
    static bool dragIsEntity = false;
    static int pendingDeleteGroupIdx = -1; // 👈 用于延迟删除，防止崩溃

    // 根节点：Scene
    if (ImGui::TreeNodeEx("Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
        
        // 1. 先渲染所有的 Group
        auto RenderGroupNode = [&](int gIdx) {
            auto& grp = g_brushGroups[gIdx];
            ImGui::PushID(gIdx + 30000);
            
            bool groupVis = (grp.visible != 0);
            if (ImGui::Checkbox("##vis", &groupVis)) {
                grp.visible = groupVis ? 1 : 0;
                for (int bi : grp.brushIndices) if (bi < (int)g_brushVisible.size()) g_brushVisible[bi] = grp.visible;
                for (int ei : grp.entityIndices) if (ei < (int)g_entityVisible.size()) g_entityVisible[ei] = grp.visible;
            }
            ImGui::SameLine();
            
            bool isOpen = ImGui::TreeNodeEx(grp.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow);
            
            // 点击名字选中组
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                g_iSelectedGroup = gIdx;
                *pSelBrush = -1;
                *pSelEntity = -1;
            }

            // 👈 专门给 Group 名字绑定右键菜单（用 OpenPopup 彻底隔离上下文）
            std::string popupName = "GroupCtx_" + std::to_string(gIdx);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                ImGui::OpenPopup(popupName.c_str());
            }

            if (ImGui::BeginPopup(popupName.c_str())) {
                if (ImGui::MenuItem("Delete Group")) {
                    pendingDeleteGroupIdx = gIdx; // 延迟删除，防止迭代器失效
                }
                ImGui::EndPopup();
            }

            // 作为拖拽目标
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DRAG_ITEM")) {
                    int draggedIdx = *(const int*)payload->Data;
                    if (dragIsEntity) {
                        bool alreadyIn = false;
                        for (int e : grp.entityIndices) if (e == draggedIdx) { alreadyIn = true; break; }
                        if (!alreadyIn) grp.entityIndices.push_back(draggedIdx);
                    } else {
                        bool alreadyIn = false;
                        for (int b : grp.brushIndices) if (b == draggedIdx) { alreadyIn = true; break; }
                        if (!alreadyIn) grp.brushIndices.push_back(draggedIdx);
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (isOpen) {
                // 渲染组内的 Brushes
                int removeBrushIdx = -1;
                for (int k = 0; k < (int)grp.brushIndices.size(); k++) {
                    int bi = grp.brushIndices[k];
                    ImGui::PushID(bi + 40000);
                    
                    bool vis = (bi < (int)g_brushVisible.size()) ? (g_brushVisible[bi] != 0) : true;
                    if (ImGui::Checkbox("##vis", &vis)) {
                        if (bi < (int)g_brushVisible.size()) g_brushVisible[bi] = vis ? 1 : 0;
                    }
                    ImGui::SameLine();
                    
                    // 👈 加上按钮宽度计算
                    float btnW = ImGui::CalcTextSize("Dup Del").x + ImGui::GetStyle().FramePadding.x * 2.0f + 20.0f;
                    float availW = ImGui::GetContentRegionAvail().x;
                    
                    bool sel = (*pSelBrush == bi);
                    if (ImGui::Selectable(("Brush #" + std::to_string(bi)).c_str(), sel, ImGuiSelectableFlags_AllowOverlap, ImVec2(availW - btnW - 10.0f, 0))) {
                        *pSelBrush = bi;
                        *pSelEntity = -1;
                        g_iSelectedGroup = -1;
                    }
                    
                    // 👈 加上按钮
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Dup")) { out.bRequestDuplicate = true; out.iTargetIndex = bi; }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Del")) { out.bRequestDelete = true; out.iTargetIndex = bi; }
                    
                    if (ImGui::BeginPopupContextItem(("rmB" + std::to_string(bi)).c_str())) {
                        if (ImGui::MenuItem("Remove from Group")) removeBrushIdx = k;
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
                if (removeBrushIdx >= 0) grp.brushIndices.erase(grp.brushIndices.begin() + removeBrushIdx);

                // 渲染组内的 Entities
                int removeEntityIdx = -1;
                for (int k = 0; k < (int)grp.entityIndices.size(); k++) {
                    int ei = grp.entityIndices[k];
                    ImGui::PushID(ei + 50000);
                    
                    bool vis = (ei < (int)g_entityVisible.size()) ? (g_entityVisible[ei] != 0) : true;
                    if (ImGui::Checkbox("##vis", &vis)) {
                        if (ei < (int)g_entityVisible.size()) g_entityVisible[ei] = vis ? 1 : 0;
                    }
                    ImGui::SameLine();
                    
                    // 👈 加上按钮宽度计算
                    float btnW = ImGui::CalcTextSize("Tel Del").x + ImGui::GetStyle().FramePadding.x * 2.0f + 20.0f;
                    float availW = ImGui::GetContentRegionAvail().x;
                    
                    bool sel = (*pSelEntity == ei);
                    std::string label = g_entities[ei].m_szName + " #" + std::to_string(ei);
                    if (ImGui::Selectable(label.c_str(), sel, ImGuiSelectableFlags_AllowOverlap, ImVec2(availW - btnW - 10.0f, 0))) {
                        *pSelEntity = ei;
                        *pSelBrush = -1;
                        g_iSelectedGroup = -1;
                    }
                    
                    // 👈 加上按钮
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Tel")) {
                        if (GetEditorCamTarget()) *GetEditorCamTarget() = g_entities[ei].m_vecPos;
                        if (GetEditorCamDist()) *GetEditorCamDist() = 150.0f;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Del")) { out.bRequestDeleteEntity = true; out.iTargetEntityIndex = ei; }
                    
                    if (ImGui::BeginPopupContextItem(("rmE" + std::to_string(ei)).c_str())) {
                        if (ImGui::MenuItem("Remove from Group")) removeEntityIdx = k;
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
                if (removeEntityIdx >= 0) grp.entityIndices.erase(grp.entityIndices.begin() + removeEntityIdx);

                ImGui::TreePop();
            }
            ImGui::PopID();
        };

        // 执行组渲染
        for (int g = 0; g < (int)g_brushGroups.size(); g++) {
            RenderGroupNode(g);
        }

        // 👈 安全清理被删除的组（防崩溃核心）
        if (pendingDeleteGroupIdx >= 0) {
            auto& grp = g_brushGroups[pendingDeleteGroupIdx];
            for (int bi : grp.brushIndices) if (bi < (int)g_brushVisible.size()) g_brushVisible[bi] = 1;
            for (int ei : grp.entityIndices) if (ei < (int)g_entityVisible.size()) g_entityVisible[ei] = 1;
            g_brushGroups.erase(g_brushGroups.begin() + pendingDeleteGroupIdx);
            pendingDeleteGroupIdx = -1;
            g_iSelectedGroup = -1;
        }

        // 2. 渲染散落的 Entities
        for (int i = 0; i < (int)g_entities.size(); i++) {
            bool inAnyGroup = false;
            for (const auto& grp : g_brushGroups) {
                for (int ei : grp.entityIndices) if (ei == i) { inAnyGroup = true; break; }
                if (inAnyGroup) break;
            }
            if (inAnyGroup) continue;

            auto& e = g_entities[i];
            std::string label = e.m_szName + " #" + std::to_string(i);
            if (!filterLower.empty()) {
                std::string l = label; std::transform(l.begin(), l.end(), l.begin(), ::tolower);
                if (l.find(filterLower) == std::string::npos) continue;
            }

            ImGui::PushID(i);
            bool vis = (i < (int)g_entityVisible.size()) ? (g_entityVisible[i] != 0) : true;
            if (ImGui::Checkbox("##vis", &vis)) {
                if (i < (int)g_entityVisible.size()) g_entityVisible[i] = vis ? 1 : 0;
            }
            ImGui::SameLine();

            float btnW = ImGui::CalcTextSize("Tel Del").x + ImGui::GetStyle().FramePadding.x * 2.0f + 20.0f;
            float availW = ImGui::GetContentRegionAvail().x;
            bool sel = (*pSelEntity == i);
            if (ImGui::Selectable(label.c_str(), sel, ImGuiSelectableFlags_AllowOverlap, ImVec2(availW - btnW - 10.0f, 0))) {
                *pSelEntity = i;
                *pSelBrush = -1;
                g_iSelectedGroup = -1;
            }
            
            // 拖拽 Entity
            if (ImGui::BeginDragDropSource()) {
                dragItemIdx = i;
                dragIsEntity = true;
                ImGui::SetDragDropPayload("DRAG_ITEM", &dragItemIdx, sizeof(int));
                ImGui::Text("Entity #%d", i);
                ImGui::EndDragDropSource();
            }
            
            // 拖拽建组
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DRAG_ITEM")) {
                    int draggedIdx = *(const int*)payload->Data;
                    if (draggedIdx != i) {
                        BrushGroup newGrp;
                        newGrp.name = "Group_" + std::to_string(g_brushGroups.size());
                        if (dragIsEntity) {
                            newGrp.entityIndices.push_back(draggedIdx);
                            newGrp.entityIndices.push_back(i);
                        } else {
                            newGrp.brushIndices.push_back(draggedIdx);
                            newGrp.entityIndices.push_back(i);
                        }
                        g_brushGroups.push_back(newGrp);
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("Tel")) {
                if (GetEditorCamTarget()) *GetEditorCamTarget() = e.m_vecPos;
                if (GetEditorCamDist()) *GetEditorCamDist() = 150.0f;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Del")) { out.bRequestDeleteEntity = true; out.iTargetEntityIndex = i; }
            ImGui::PopID();
        }

        // 3. 渲染散落的 Brushes
        for (int i = 0; i < (int)g_brushes.size(); i++) {
            bool inAnyGroup = false;
            for (const auto& grp : g_brushGroups) {
                for (int bi : grp.brushIndices) if (bi == i) { inAnyGroup = true; break; }
                if (inAnyGroup) break;
            }
            if (inAnyGroup) continue;

            auto& b = g_brushes[i];
            std::string label = "brush #" + std::to_string(i);
            if (!filterLower.empty()) {
                std::string l = label; std::transform(l.begin(), l.end(), l.begin(), ::tolower);
                if (l.find(filterLower) == std::string::npos) continue;
            }

            ImGui::PushID(i + 10000);
            bool vis = (i < (int)g_brushVisible.size()) ? (g_brushVisible[i] != 0) : true;
            if (ImGui::Checkbox("##vis", &vis)) {
                if (i < (int)g_brushVisible.size()) g_brushVisible[i] = vis ? 1 : 0;
            }
            ImGui::SameLine();

            float btnW = ImGui::CalcTextSize("Dup Del").x + ImGui::GetStyle().FramePadding.x * 2.0f + 20.0f;
            float availW = ImGui::GetContentRegionAvail().x;
            bool sel = (*pSelBrush == i);
            if (ImGui::Selectable(label.c_str(), sel, ImGuiSelectableFlags_AllowOverlap, ImVec2(availW - btnW - 10.0f, 0))) {
                *pSelBrush = i;
                *pSelEntity = -1;
                g_iSelectedGroup = -1;
            }

            if (ImGui::BeginDragDropSource()) {
                dragItemIdx = i;
                dragIsEntity = false;
                ImGui::SetDragDropPayload("DRAG_ITEM", &dragItemIdx, sizeof(int));
                ImGui::Text("Brush #%d", i);
                ImGui::EndDragDropSource();
            }

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DRAG_ITEM")) {
                    int draggedIdx = *(const int*)payload->Data;
                    if (draggedIdx != i) {
                        BrushGroup newGrp;
                        newGrp.name = "Group_" + std::to_string(g_brushGroups.size());
                        if (dragIsEntity) {
                            newGrp.entityIndices.push_back(draggedIdx);
                            newGrp.brushIndices.push_back(i);
                        } else {
                            newGrp.brushIndices.push_back(draggedIdx);
                            newGrp.brushIndices.push_back(i);
                        }
                        g_brushGroups.push_back(newGrp);
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("Dup")) { out.bRequestDuplicate = true; out.iTargetIndex = i; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Del")) { out.bRequestDelete = true; out.iTargetIndex = i; }
            ImGui::PopID();
        }
        
        ImGui::TreePop();
    }
    
    ImGui::EndChild();
    ImGui::Separator();

    // ================= 下半部分：属性面板 =================
    ImGui::BeginChild("##PropertiesChild", ImVec2(0, 0), true);
    
    // 👈 优先显示选中组的属性面板
    if (g_iSelectedGroup >= 0 && g_iSelectedGroup < (int)g_brushGroups.size()) {
        auto& grp = g_brushGroups[g_iSelectedGroup];
        ImGui::TextColored(ImVec4(0, 1, 1, 1), "Selected Group");
        
        char nameBuf[128];
        strncpy(nameBuf, grp.name.c_str(), sizeof(nameBuf));
        if (ImGui::InputText("Group Name", nameBuf, sizeof(nameBuf))) {
            grp.name = nameBuf;
        }
        
        ImGui::Text("Contains: %zu Brushes, %zu Entities", grp.brushIndices.size(), grp.entityIndices.size());
        ImGui::Separator();
        ImGui::TextDisabled("(Click a child inside the tree to edit its properties)");
    } 
    else if (*pSelEntity >= 0 && *pSelEntity < (int)g_entities.size()) {
        CEntity& e = g_entities[*pSelEntity];
        ImGui::TextColored(ImVec4(1,1,0,1), "Selected: %s", e.m_szName.c_str());
        
        // 位置和旋转（带 BBox 更新）
        glm::vec3 prevPos = e.m_vecPos;
        if (ImGui::DragFloat3("Position", glm::value_ptr(e.m_vecPos), 1.0f)) {
            e.m_vecBBoxMins += (e.m_vecPos - prevPos);
            e.m_vecBBoxMaxs += (e.m_vecPos - prevPos);
        }
        if (ImGui::IsItemActivated()) UndoStack::Instance().PushSnapshot("Entity Pos");
        
        if (ImGui::DragFloat3("Angles", glm::value_ptr(e.m_angRot), 0.5f)) {
            UpdateEntityBBox(e);
        }
        if (ImGui::IsItemActivated()) UndoStack::Instance().PushSnapshot("Entity Rot");

        ImGui::Separator();
        ImGui::Text("Properties:");

        std::vector<GdbKeyValue> kvs;
        if (GameDatabase::Instance().GetKeyValues(e.m_szName, kvs)) {
            for (const auto& kv : kvs) {
                ImGui::PushID(kv.name.c_str());
                std::string cur;
                auto it = e.m_properties.find(kv.name);
                cur = (it == e.m_properties.end()) ? kv.defaultValue : it->second;

                std::string label = kv.name;
                if (!kv.description.empty()) label = kv.description + "##" + kv.name;

                if (kv.type == "choices" && !kv.choices.empty()) {
                    int idx = 0;
                    for (size_t c = 0; c < kv.choices.size(); c++)
                        if (kv.choices[c].value == cur) { idx = (int)c; break; }
                    std::vector<const char*> caps;
                    for (auto& c : kv.choices) caps.push_back(c.caption.c_str());
                    if (ImGui::Combo(label.c_str(), &idx, caps.data(), (int)caps.size())) {
                        UndoStack::Instance().PushSnapshot("Entity Property");
                        e.m_properties[kv.name] = kv.choices[idx].value;
                    }
                } else if (kv.type == "integer" || kv.type == "bool") {
                    int v = atoi(cur.c_str());
                    if (ImGui::DragInt(label.c_str(), &v)) {
                        if (ImGui::IsItemActivated()) UndoStack::Instance().PushSnapshot("Entity Property");
                        e.m_properties[kv.name] = std::to_string(v);
                    }
                } else if (kv.type == "float") {
                    float v = (float)atof(cur.c_str());
                    if (ImGui::DragFloat(label.c_str(), &v)) {
                        if (ImGui::IsItemActivated()) UndoStack::Instance().PushSnapshot("Entity Property");
                        char b[64]; snprintf(b, sizeof(b), "%.4f", v);
                        e.m_properties[kv.name] = b;
                    }
                } else if (kv.type == "color255" || kv.type == "color1") {
                    float c[3] = {1,1,1};
                    sscanf(cur.c_str(), "%f %f %f", &c[0], &c[1], &c[2]);
                    if (kv.type == "color255") { c[0]/=255; c[1]/=255; c[2]/=255; }
                    if (ImGui::ColorEdit3(label.c_str(), c)) {
                        if (ImGui::IsItemActivated()) UndoStack::Instance().PushSnapshot("Entity Property");
                        char b[64];
                        if (kv.type == "color255") snprintf(b, sizeof(b), "%d %d %d", (int)(c[0]*255), (int)(c[1]*255), (int)(c[2]*255));
                        else snprintf(b, sizeof(b), "%.3f %.3f %.3f", c[0], c[1], c[2]);
                        e.m_properties[kv.name] = b;
                    }
                } else if (kv.type == "vector" || kv.type == "origin" || kv.type == "angle") {
                    float v[3] = {0,0,0};
                    sscanf(cur.c_str(), "%f %f %f", &v[0], &v[1], &v[2]);
                    if (ImGui::DragFloat3(label.c_str(), v, 0.5f)) {
                        if (ImGui::IsItemActivated()) UndoStack::Instance().PushSnapshot("Entity Property");
                        char b[128]; snprintf(b, sizeof(b), "%.2f %.2f %.2f", v[0], v[1], v[2]);
                        e.m_properties[kv.name] = b;
                    }
                } else {
                    std::string tmp = cur;
                    if (InputTextString(label.c_str(), tmp)) {
                        if (ImGui::IsItemActivated()) UndoStack::Instance().PushSnapshot("Entity Property");
                        e.m_properties[kv.name] = tmp;
                    }
                }
                if (!kv.longDesc.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kv.longDesc.c_str());
                ImGui::PopID();
            }
        }

        // Custom Properties
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Custom Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
            std::string keyToDelete;
            for (auto& pair : e.m_customProperties) {
                ImGui::PushID(pair.first.c_str());
                ImGui::Text("%s", pair.first.c_str());
                ImGui::SameLine(140.0f);
                ImGui::SetNextItemWidth(-60.0f);
                std::string val = pair.second;
                if (InputTextString("##val", val)) {
                    if (ImGui::IsItemActivated()) UndoStack::Instance().PushSnapshot("Custom KV Value");
                    pair.second = val;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Del")) keyToDelete = pair.first;
                ImGui::PopID();
            }
            if (!keyToDelete.empty()) {
                UndoStack::Instance().PushSnapshot("Delete Custom KV");
                e.m_customProperties.erase(keyToDelete);
            }
            ImGui::Spacing();
            ImGui::TextDisabled("Add New Property:");
            static char s_newKey[64] = "";
            static char s_newValue[256] = "";
            ImGui::SetNextItemWidth(130.0f);
            ImGui::InputTextWithHint("##NewKey", "Key", s_newKey, sizeof(s_newKey));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-60.0f);
            ImGui::InputTextWithHint("##NewValue", "Value", s_newValue, sizeof(s_newValue));
            ImGui::SameLine();
            if (ImGui::Button("Add", ImVec2(50, 0))) {
                if (strlen(s_newKey) > 0) {
                    UndoStack::Instance().PushSnapshot("Add Custom KV");
                    e.m_customProperties[s_newKey] = s_newValue;
                    s_newKey[0] = '\0'; s_newValue[0] = '\0';
                }
            }
        }        

    } 
    else if (*pSelBrush >= 0 && *pSelBrush < (int)g_brushes.size()) {
        CBrush& b = g_brushes[*pSelBrush];
        ImGui::TextColored(ImVec4(1,1,0,1), "Selected: brush #%d", *pSelBrush);
        
        if (ImGui::DragFloat3("Center", glm::value_ptr(b.m_vecPos), 1.0f)) {
            if (ImGui::IsItemActivated()) UndoStack::Instance().PushSnapshot("Brush Center");
            b.RebuildPrimitiveMesh();
        }
        // 👈 加上了 0.1f 的最小值限制，防止变成 0 导致崩溃
        if (ImGui::DragFloat3("HalfSize", glm::value_ptr(b.m_vecSize), 0.5f, 0.1f, 4096.f)) {
            if (ImGui::IsItemActivated()) UndoStack::Instance().PushSnapshot("Brush Size");
            b.RebuildPrimitiveMesh();
        }
    } 
    else {
        ImGui::TextDisabled("Nothing selected.");
    }
    
    ImGui::EndChild();
    ImGui::End();
}

// ---------------------------------------------------------------------------
void DrawConsolePanel()
{
    if (!ImGui::Begin("Console")) { ImGui::End(); return; }
    if (ImGui::Button("Clear")) g_logLines.clear();
    ImGui::SameLine();
    ImGui::TextDisabled("(%d lines)", (int)g_logLines.size());
    ImGui::Separator();
    ImGui::BeginChild("##LogScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (auto& line : g_logLines) {
        ImVec4 col(0.85f,0.85f,0.85f,1);
        if (line.find("FAILED") != std::string::npos ||
            line.find("ERROR")  != std::string::npos) col = ImVec4(1,0.4f,0.4f,1);
        else if (line.find("OK") != std::string::npos) col = ImVec4(0.5f,1,0.5f,1);
        ImGui::TextColored(col, "%s", line.c_str());
    }
    ImGui::EndChild();
    ImGui::End();
}

// ---------------------------------------------------------------------------
void DrawScenePanel()
{
    if (!ImGui::Begin("Scene")) { ImGui::End(); return; }
    ImGui::End();
}

// ---------------------------------------------------------------------------
void DrawViewportPanel()
{
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus
        | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    ImGui::Begin("3D View", nullptr, flags);
    ImGui::End();
    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------
static bool s_bCompileOpen = false;
void OpenCompileOptionsWindow() { s_bCompileOpen = true; }
void DrawCompileOptionsWindow()
{
    if (!s_bCompileOpen) return;
    ImGui::SetNextWindowSize(ImVec2(480, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Compile Options", &s_bCompileOpen)) {

    }
    ImGui::End();
}