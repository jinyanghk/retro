#include "NodeEditor.h"
#include "SceneGlobals.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"
#include <string>
#include <vector>
#include <map>

// 简单的事件节点定义
struct EventNode {
    int id;
    std::string name;
    ImVec2 pos;
    bool isEntityNode; // 如果是实体节点，还是事件节点（目前用实体作为节点）
    int entityIndex = -1;
    
    // UI 状态
    bool showOutputMenu = false;
    ImVec2 outputMenuPos;
};

static std::vector<EventNode> g_nodes;
static int g_nextNodeId = 1;
static bool g_showLinkPopup = false;
static int g_linkSourceNode = -1;

void DrawNodeEditorTab() {
    if (!ImGui::Begin("Node Editor")) { ImGui::End(); return; }

    ImGui::Text("Entity I/O Connections");
    ImGui::Separator();

    static ImVec2 scrolling(0.0f, 0.0f);
    static bool show_grid = true;
    
    // 工具栏
    ImGui::Checkbox("Show Grid", &show_grid);
    ImGui::SameLine();
    if (ImGui::Button("Refresh Nodes")) {
        g_nodes.clear();
        for (int i = 0; i < (int)g_entities.size(); i++) {
            // 只为有名字的实体建节点
            std::string name = g_entities[i].m_szName;
            if (name.empty() || name == "worldspawn") continue;
            
            EventNode n;
            n.id = g_nextNodeId++;
            n.name = name;
            n.pos = ImVec2(100 + i * 50, 100 + i * 50); // 初始随便散开
            n.isEntityNode = true;
            n.entityIndex = i;
            g_nodes.push_back(n);
        }
    }

    ImGui::BeginChild("NodeCanvas", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

    // 画布交互
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 offset = scrolling;
    
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
    ImVec2 canvas_sz = ImGui::GetWindowSize();
    if (canvas_sz.x < 50.0f) canvas_sz.x = 50.0f;
    if (canvas_sz.y < 50.0f) canvas_sz.y = 50.0f;
    ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);

    // 画背景网格
    if (show_grid) {
        const float GRID_STEP = 64.0f;
        for (float x = fmodf(offset.x, GRID_STEP); x < canvas_sz.x; x += GRID_STEP)
            draw_list->AddLine(ImVec2(canvas_p0.x + x, canvas_p0.y), ImVec2(canvas_p0.x + x, canvas_p1.y), IM_COL32(200, 200, 200, 40));
        for (float y = fmodf(offset.y, GRID_STEP); y < canvas_sz.y; y += GRID_STEP)
            draw_list->AddLine(ImVec2(canvas_p0.x, canvas_p0.y + y), ImVec2(canvas_p1.x, canvas_p0.y + y), IM_COL32(200, 200, 200, 40));
    }

    // 交互（拖拽画布）
    ImGui::InvisibleButton("canvas", canvas_sz, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool is_hovered = ImGui::IsItemHovered();
    const bool is_active = ImGui::IsItemActive();
    if (is_active && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        scrolling.x += io.MouseDelta.x;
        scrolling.y += io.MouseDelta.y;
    }

    ImGui::PushClipRect(canvas_p0, canvas_p1, true);
    
    // 核心：ImGuizmo Node 绘制
    ImGuizmo::BeginFrame();
    ImGuizmo::SetRect(canvas_p0.x, canvas_p0.y, canvas_sz.x, canvas_sz.y);
    
    // 如果实体变了，更新节点状态
    for (auto& n : g_nodes) {
        if (n.entityIndex < 0 || n.entityIndex >= (int)g_entities.size()) continue;
        auto& ent = g_entities[n.entityIndex];
        
        // 用 ImGuizmo 的节点
        ImGuizmo::SetID(n.id);
        ImGuizmo::PushID(n.id);
        
        // 这里简化，把节点作为 Input/Output 块
        // 实际上 ImGuizmo 的 API 是 BeginNode / EndNode
        // 我们用一个简单的流程图逻辑
        ImGui::SetCursorScreenPos(n.pos);
        
        ImGui::BeginGroup();
        ImGui::Button(n.name.c_str(), ImVec2(150, 0));
        ImGui::EndGroup();
        
        n.pos = ImGui::GetItemRectMin(); // 跟踪位置
        ImGuizmo::PopID();
    }

    ImGui::PopClipRect();
    ImGui::EndChild();
    ImGui::End();
}