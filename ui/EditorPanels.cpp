#include "ui/EditorPanels.h"

#include <glm/gtc/type_ptr.hpp>

#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"

#include "scene/SceneGlobals.h"
#include "app/UndoStack.h"
#include "fgd/FgdDatabase.h"

// ---------- handlers ----------
static void (*s_pfnExportMap)() = nullptr;
static void (*s_pfnImportMap)() = nullptr;
static void (*s_pfnCompileAndRun)() = nullptr;

void SetExportMapHandler(void (*pfn)()) { s_pfnExportMap = pfn; }
void SetImportMapHandler(void (*pfn)()) { s_pfnImportMap = pfn; }
void SetCompileAndRunHandler(void (*pfn)()) { s_pfnCompileAndRun = pfn; }

// ---------- 简单日志缓冲（替代 Hammer 的 LogBuffer）----------
#include <deque>
static std::deque<std::string> g_logLines;
void LogAdd(const char* s) {
    g_logLines.push_back(s ? s : "");
    if (g_logLines.size() > 1000) g_logLines.pop_front();
}
const std::deque<std::string>& LogLines() { return g_logLines; }

// ---------------------------------------------------------------------------
void DrawMenuBar()
{
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open map.json"))  { if (s_pfnImportMap) s_pfnImportMap(); }
            if (ImGui::MenuItem("Export map.json")) { if (s_pfnExportMap) s_pfnExportMap(); }
            ImGui::Separator();
            if (ImGui::MenuItem("Compile & Run"))   { if (s_pfnCompileAndRun) s_pfnCompileAndRun(); }
            if (ImGui::MenuItem("Compile Options...")) OpenCompileOptionsWindow();
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
        ImGui::EndMainMenuBar();
    }
}

// ---------------------------------------------------------------------------
void DrawToolsPanel()
{
    if (!ImGui::Begin("Tools")) { ImGui::End(); return; }

    ImGui::Text("Gizmo:");

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
        if (i > 0) ImGui::SameLine();
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
    ImGui::SameLine();
    if (g_CurrentGizmoOperation & ImGuizmo::TRANSLATE)
        ImGui::InputFloat3("Snap", &g_Snap[0]);
    if (g_CurrentGizmoOperation & ImGuizmo::ROTATE)
        ImGui::InputFloat("Angle Snap", &g_Snap[0]);
    if (g_CurrentGizmoOperation & ImGuizmo::SCALE)
        ImGui::InputFloat("Scale Snap", &g_Snap[0]);

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
void DrawEntityPanel(CEntity* entities, int count, int* pSelEntity, int* pSelBrush,
                     BrushPanelResult& out)
{
    if (!ImGui::Begin("Entity")) { ImGui::End(); return; }

    // 列表
    if (ImGui::Button("+ New Entity")) {
        out.bRequestNewEntity = true;
        out.szNewEntityClass   = "info_player_start";
    }
    ImGui::SameLine();
    static char s_filter[64] = "";
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputText("Filter##EntityList", s_filter, sizeof(s_filter));

    ImGui::Separator();
    ImGui::Text("Entities (%d):", count);
    
    ImGui::BeginChild("##EntityListScroll", ImVec2(0, 200), true);
    for (int i = 0; i < count; i++) {
        ImGui::PushID(i);

        const ImGuiStyle& st = ImGui::GetStyle();
        float framePadX = st.FramePadding.x * 2.0f;
        float btnDelW   = ImGui::CalcTextSize("Del").x + framePadX;
        float spacing   = st.ItemSpacing.x;
        float availW    = ImGui::GetContentRegionAvail().x;
        float selW = availW - btnDelW - spacing - 4.0f;
        if (selW < 60.0f) selW = 60.0f;

        char label[128];
        snprintf(label, sizeof(label), "%s", entities[i].m_szName.c_str());
        bool sel = (i == *pSelEntity);
        if (ImGui::Selectable(label, sel, ImGuiSelectableFlags_None, ImVec2(selW, 0))) {
            *pSelEntity = i;
            *pSelBrush  = -1;
        }

        ImGui::SameLine();
        ImGui::PushID("Del");
        if (ImGui::SmallButton("Del")) {
            LogAdd("[UI] Del clicked for entity");
            out.bRequestDeleteEntity = true;
            out.iTargetEntityIndex   = i;
        }
        ImGui::PopID();

        ImGui::PopID();
    }
    ImGui::EndChild();

    // 属性
    ImGui::Separator();
    if (*pSelEntity >= 0 && *pSelEntity < count) {
        CEntity& e = entities[*pSelEntity];
        ImGui::Text("Selected: %s", e.m_szName.c_str());

        if (ImGui::DragFloat3("Position", glm::value_ptr(e.m_vecPos), 1.0f, -4096.f, 4096.f, "%.1f")) {}
        if (ImGui::IsItemActivated()) UndoStack::Instance().PushSnapshot("Entity Pos");

        if (ImGui::DragFloat3("Angles", glm::value_ptr(e.m_angRot), 0.5f, -180.f, 180.f, "%.1f")) {}
        if (ImGui::IsItemActivated()) UndoStack::Instance().PushSnapshot("Entity Rot");

        ImGui::Separator();
        ImGui::Text("FGD Properties:");

        std::vector<FgdKeyValue> kvs;
        if (FgdDatabase::Instance().GetKeyValues(e.m_szName, kvs)) {
            for (const auto& kv : kvs) {
                ImGui::PushID(kv.name.c_str());

                std::string cur;
                auto it = e.m_properties.find(kv.name);
                cur = (it == e.m_properties.end()) ? kv.defaultValue : it->second;

                std::string label = kv.name;
                if (!kv.description.empty()) {
                    label = kv.description + "##" + kv.name;
                }

                // 根据 type 选控件
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
                        if (kv.type == "color255")
                            snprintf(b, sizeof(b), "%d %d %d", (int)(c[0]*255), (int)(c[1]*255), (int)(c[2]*255));
                        else
                            snprintf(b, sizeof(b), "%.3f %.3f %.3f", c[0], c[1], c[2]);
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
                if (!kv.longDesc.empty() && ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", kv.longDesc.c_str());

                ImGui::PopID();
            }
        } else {
            ImGui::TextDisabled("(no FGD data for %s)", e.m_szName.c_str());
        }
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
void DrawBrushPanel(const CBrush* brushes, int count, int* pSelBrush, int* pSelEntity,
                    BrushPanelResult& out)
{
    if (!ImGui::Begin("Brush")) { ImGui::End(); return; }

    if (ImGui::Button("+ New")) out.bRequestNew = true;
    ImGui::Separator();

    ImGui::Text("Brushes (%d):", count);
    
    ImGui::BeginChild("##BrushListScroll", ImVec2(0, 200), true);
    for (int i = 0; i < count; i++) {
        ImGui::PushID(i);

        // ---- 计算按钮宽度，给 Selectable 留出精确剩余宽度 ----
        const ImGuiStyle& st = ImGui::GetStyle();
        float framePadX = st.FramePadding.x * 2.0f;
        float btnDupW   = ImGui::CalcTextSize("Dup").x + framePadX;
        float btnDelW   = ImGui::CalcTextSize("Del").x + framePadX;
        float spacing   = st.ItemSpacing.x;
        float availW    = ImGui::GetContentRegionAvail().x;
        // Dup + Del + 两个 SameLine 的 spacing
        float selW = availW - btnDupW - btnDelW - spacing * 2.0f - 4.0f;
        if (selW < 60.0f) selW = 60.0f;

        char label[64];
        snprintf(label, sizeof(label), "brush #%d", i);
        bool sel = (i == *pSelBrush);
        if (ImGui::Selectable(label, sel, ImGuiSelectableFlags_None, ImVec2(selW, 0))) {
            *pSelBrush = i;
            *pSelEntity = -1;
        }

        ImGui::SameLine();
        ImGui::PushID("Dup");
        if (ImGui::SmallButton("Dup")) {
            LogAdd("[UI] Dup clicked for brush");   // printf -> LogAdd
            out.bRequestDuplicate = true;
            out.iTargetIndex = i;
        }
        ImGui::PopID();

        ImGui::SameLine();
        ImGui::PushID("Del");
        if (ImGui::SmallButton("Del")) {
            LogAdd("[UI] Del clicked for brush");
            out.bRequestDelete = true;
            out.iTargetIndex = i;
        }
        ImGui::PopID();

        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::Separator();
    if (*pSelBrush >= 0 && *pSelBrush < count) {
        CBrush& b = const_cast<CBrush&>(brushes[*pSelBrush]);
        ImGui::Text("Brush #%d", *pSelBrush);
        
        if (ImGui::DragFloat3("Center", glm::value_ptr(b.m_vecPos), 1.0f)) {
            if (ImGui::IsItemActivated()) UndoStack::Instance().PushSnapshot("Brush Center");
            b.RebuildPrimitiveMesh();
        }
        if (ImGui::DragFloat3("HalfSize", glm::value_ptr(b.m_vecSize), 0.5f, 0.1f, 4096.f)) {
            if (ImGui::IsItemActivated()) UndoStack::Instance().PushSnapshot("Brush Size");
            b.RebuildPrimitiveMesh();
        }

        ImGui::Separator();
        ImGui::Text("Material (brush-level):");
        std::string mat = b.m_szMaterial;
        if (InputTextString("##BrushMat", mat)) {
            if (ImGui::IsItemActivated()) UndoStack::Instance().PushSnapshot("Material");
            b.m_szMaterial = mat;
        }

        ImGui::Separator();
        ImGui::Text("Per-face materials:");
        const char* faceNames[6] = {"+Z","-Z","+X","-X","+Y","-Y"};
        if (g_iSelectedFace < 0 || g_iSelectedFace >= 6) g_iSelectedFace = 0;
        ImGui::Combo("Face", &g_iSelectedFace, faceNames, 6);
        std::string fm = b.m_faces[g_iSelectedFace].m_szMaterial;
        if (InputTextString("##FaceMat", fm)) {
            if (ImGui::IsItemActivated()) UndoStack::Instance().PushSnapshot("Face Material");
            b.m_faces[g_iSelectedFace].m_szMaterial = fm;
        }
        if (ImGui::Button("Reset to inherit")) {
            UndoStack::Instance().PushSnapshot("Face Material");
            b.m_faces[g_iSelectedFace].m_szMaterial.clear();
        }

        ImGui::Separator();
        if (ImGui::Button("Deselect")) *pSelBrush = -1;
    }

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
        static char mapName[128] = "mymap";
        static char outDir[128]  = ".";
        static bool runVbsp = true, runVvis = true, runVrad = true, copyMaps = true, launch = true;
        ImGui::InputText("Map Name", mapName, sizeof(mapName));
        ImGui::InputText("Output Dir", outDir, sizeof(outDir));
        ImGui::Separator();
        ImGui::Checkbox("Run VBSP", &runVbsp);
        ImGui::Checkbox("Run VVIS", &runVvis);
        ImGui::Checkbox("Run VRAD", &runVrad);
        ImGui::Checkbox("Copy BSP to maps/", &copyMaps);
        ImGui::Checkbox("Launch game", &launch);
        ImGui::Separator();
        if (ImGui::Button("Compile & Run")) { if (s_pfnCompileAndRun) s_pfnCompileAndRun(); }
        ImGui::SameLine();
        if (ImGui::Button("Close")) s_bCompileOpen = false;
    }
    ImGui::End();
}