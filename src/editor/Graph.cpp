#include "Graph.h"
#include "Globals.h"
#include "UndoStack.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "GraphEditor.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>

// 适配最新版 ImGuizmo 的 GraphEditor::Delegate
class EditorGraph : public GraphEditor::Delegate {
public:
    // 自定义节点数据
    struct MyNode {
        std::string name;
        float x, y;
        bool selected;
        int entityIndex;
    };

    std::vector<MyNode> mNodes;
    std::vector<GraphEditor::Link> mLinks;
    std::map<std::pair<int, int>, int> mLinkMap;

    // 🔥 全局配置：是否隐藏未连线的实体
    bool m_bHideUnconnected = true;

    // 👈 先定义静态数组
    static const char* s_inputNames[];
    static const char* s_outputNames[];

    const GraphEditor::Template mTemplates[1] = {
        {
            IM_COL32(160, 160, 180, 255), // header
            IM_COL32(100, 100, 140, 255), // background
            IM_COL32(110, 110, 150, 255), // header hover
            1,                            // input slots
            s_inputNames,                 // 👈 用静态数组
            nullptr,                      // input colors
            1,                            // output slots
            s_outputNames,                // 👈 用静态数组
            nullptr                       // output colors
        }
    };

    void Rebuild() {
        mNodes.clear();
        mLinks.clear();
        mLinkMap.clear();

        // 1. 扫描所有连接关系，找出参与连线的实体 ID
        std::set<int> connectedEntityIndices; // 参与连线的实体索引
        std::set<std::string> referencedTargets; // 被引用的 targetname

        for (int i = 0; i < (int)g_entities.size(); ++i) {
            auto& e = g_entities[i];
            if (!e.m_connections.empty()) {
                connectedEntityIndices.insert(i); // 它是发出者，保留
                for (const auto& conn : e.m_connections) {
                    if (!conn.targetName.empty()) {
                        referencedTargets.insert(conn.targetName);
                    }
                }
            }
        }

        // 把被引用的 targetname 对应的实体也标记为保留
        for (int i = 0; i < (int)g_entities.size(); ++i) {
            auto& e = g_entities[i];
            auto it = e.m_properties.find("targetname");
            if (it != e.m_properties.end() && !it->second.empty()) {
                if (referencedTargets.count(it->second) > 0) {
                    connectedEntityIndices.insert(i);
                }
            }
        }

        // 2. 根据白名单和开关决定是否过滤节点
        struct NodeData {
            int entityIndex;
            std::string name;
            std::string targetName;
            bool hasIn;
            bool hasOut;
            int level;
        };
        std::vector<NodeData> rawNodes;

        for (int i = 0; i < (int)g_entities.size(); ++i) {
            auto& e = g_entities[i];
            if (e.m_szName.empty() || e.m_szName == "worldspawn") continue;

            // 👈 核心白名单：只允许逻辑相关的实体进入 Graph
            bool isLogic = (e.m_szName.find("trigger_") != std::string::npos) ||
                        (e.m_szName.find("logic_") != std::string::npos) ||
                        (e.m_szName == "func_door") ||
                        (e.m_szName == "func_button") ||
                        (e.m_szName == "prop_physics") ||
                        (e.m_szName == "func_physbox") ||
                        (e.m_szName == "env_explosion");

            if (!isLogic) continue; // 不是逻辑实体，直接跳过

            std::string tname = "";
            auto it = e.m_properties.find("targetname");
            if (it != e.m_properties.end()) tname = it->second;

            bool hasOut = !e.m_connections.empty();
            bool hasIn = !tname.empty() && referencedTargets.count(tname) > 0;

            // 如果开启了“隐藏未连线”，且它没参与任何连线，跳过
            if (m_bHideUnconnected && !connectedEntityIndices.count(i)) {
                continue;
            }

            // 计算层级
            int level = 1;
            if (!hasIn && hasOut) level = 0; 
            else if (hasIn && !hasOut) level = 2;
            else if (!hasIn && !hasOut) level = 3; // 孤立节点

            std::string nodeName = e.m_szName;
            if (!tname.empty()) nodeName += "\n[" + tname + "]";

            rawNodes.push_back({ i, nodeName, tname, hasIn, hasOut, level });
        }

        // 3. 按 level 排序（Input -> Logic -> Output）
        std::stable_sort(rawNodes.begin(), rawNodes.end(), [](const NodeData& a, const NodeData& b) {
            return a.level < b.level; 
        });

        // 4. 生成节点并排版
        int nodeId = 0;
        static int lastLevel = -1;
        static int yOffset = 0;
        for (const auto& nd : rawNodes) {
            MyNode n;
            n.name = nd.name;
            n.x = 100 + nd.level * 350; 
            
            if (nd.level != lastLevel) {
                lastLevel = nd.level;
                yOffset = 0;
            }
            n.y = 100 + yOffset;
            yOffset += 150;

            n.selected = false;
            n.entityIndex = nd.entityIndex;
            mNodes.push_back(n);
            nodeId++;
        }

        // 5. 连线
        for (int i = 0; i < (int)mNodes.size(); ++i) {
            auto& inputEnt = g_entities[mNodes[i].entityIndex];
            for (size_t cIdx = 0; cIdx < inputEnt.m_connections.size(); ++cIdx) {
                auto& out = inputEnt.m_connections[cIdx];
                for (int j = 0; j < (int)mNodes.size(); ++j) {
                    if (i == j) continue;
                    auto& targetEnt = g_entities[mNodes[j].entityIndex];
                    auto it = targetEnt.m_properties.find("targetname");
                    if (it != targetEnt.m_properties.end() && it->second == out.targetName) {
                        GraphEditor::Link link;
                        link.mInputNodeIndex = i;
                        link.mOutputNodeIndex = j;
                        link.mInputSlotIndex = 0;
                        link.mOutputSlotIndex = 0;
                        mLinks.push_back(link);
                        mLinkMap[{i, j}] = (int)cIdx;
                        break;
                    }
                }
            }
        }
    }

    // ===== GraphEditor::Delegate 接口 =====

    bool AllowedLink(GraphEditor::NodeIndex from, GraphEditor::NodeIndex to) override {
        return from != to;
    }

    void SelectNode(GraphEditor::NodeIndex nodeIndex, bool selected) override {
        if (nodeIndex >= 0 && nodeIndex < mNodes.size()) {
            mNodes[nodeIndex].selected = selected;
            g_iSelectedEntity = mNodes[nodeIndex].entityIndex;
            g_iSelectedBrush = -1;
        }
    }

    void MoveSelectedNodes(const ImVec2 delta) override {
        // 👈 注意：如果是拖动过程，delta 会持续变化。
        // 为了不把 Undo 栈撑爆，我们需要在拖动开始时压入快照。
        // 由于 ImGuizmo 没有直接提供 DragStart，这里用个简单的 bool 标记
        static bool isDragging = false;
        if (!isDragging && (delta.x != 0.0f || delta.y != 0.0f)) {
            UndoStack::Instance().PushSnapshot("Move Graph Nodes");
            isDragging = true;
        }
        // 如果当前帧没有位移，说明松手了，重置标记
        if (delta.x == 0.0f && delta.y == 0.0f) {
            isDragging = false;
        }

        for (auto& node : mNodes) {
            if (node.selected) {
                node.x += delta.x;
                node.y += delta.y;
            }
        }
    }

    void AddLink(GraphEditor::NodeIndex inputNodeIndex, GraphEditor::SlotIndex inputSlotIndex,
                 GraphEditor::NodeIndex outputNodeIndex, GraphEditor::SlotIndex outputSlotIndex) override {
        
        // 👈 添加连线前，保存快照
        UndoStack::Instance().PushSnapshot("Graph Add Link");

        auto& inputEnt = g_entities[mNodes[inputNodeIndex].entityIndex];
        auto& outputEnt = g_entities[mNodes[outputNodeIndex].entityIndex];

        std::string targetName = "";
        auto it = outputEnt.m_properties.find("targetname");
        if (it != outputEnt.m_properties.end() && !it->second.empty()) {
            targetName = it->second;
        } else {
            targetName = "entity_" + std::to_string(outputNodeIndex);
            outputEnt.m_properties["targetname"] = targetName;
        }

        EntityConnection newConn;
        newConn.connectionName = "OnTrigger";
        newConn.targetName = targetName;
        newConn.inputName = "Open";
        inputEnt.m_connections.push_back(newConn);

        GraphEditor::Link newLink;
        newLink.mInputNodeIndex = inputNodeIndex;
        newLink.mOutputNodeIndex = outputNodeIndex;
        newLink.mInputSlotIndex = inputSlotIndex;
        newLink.mOutputSlotIndex = outputSlotIndex;
        mLinks.push_back(newLink);
        mLinkMap[{inputNodeIndex, outputNodeIndex}] = (int)inputEnt.m_connections.size() - 1;
    }

    void DelLink(GraphEditor::LinkIndex linkIndex) override {
        if (linkIndex < 0 || linkIndex >= mLinks.size()) return;

        // 👈 删除连线前，保存快照
        UndoStack::Instance().PushSnapshot("Graph Del Link");

        auto& link = mLinks[linkIndex];
        if (mLinkMap.count({link.mInputNodeIndex, link.mOutputNodeIndex})) {
            int outIndex = mLinkMap[{link.mInputNodeIndex, link.mOutputNodeIndex}];
            auto& inputEnt = g_entities[mNodes[link.mInputNodeIndex].entityIndex];
            if (outIndex >= 0 && outIndex < inputEnt.m_connections.size()) {
                inputEnt.m_connections.erase(inputEnt.m_connections.begin() + outIndex);
            }
            mLinkMap.erase({link.mInputNodeIndex, link.mOutputNodeIndex});
        }
        mLinks.erase(mLinks.begin() + linkIndex);
    }

    void RightClick(GraphEditor::NodeIndex nodeIndex, GraphEditor::SlotIndex slotIndexInput, GraphEditor::SlotIndex outputSlotIndex) override {}

    void CustomDraw(ImDrawList* drawList, ImRect rectangle, GraphEditor::NodeIndex nodeIndex) override {
        // 官方示例里会在这里用 drawList 绘制节点内的文字
        if (nodeIndex < mNodes.size()) {
            drawList->AddText(ImVec2(rectangle.Min.x + 8, rectangle.Min.y + 8), 
                IM_COL32(255, 255, 255, 255), mNodes[nodeIndex].name.c_str());
        }
    }

    const size_t GetTemplateCount() override { return 1; }
    const GraphEditor::Template GetTemplate(GraphEditor::TemplateIndex index) override { return mTemplates[0]; }

    const size_t GetNodeCount() override { return mNodes.size(); }
    const GraphEditor::Node GetNode(GraphEditor::NodeIndex index) override {
        const auto& n = mNodes[index];
        return GraphEditor::Node{
            n.name.c_str(),
            0,
            ImRect(ImVec2(n.x, n.y), ImVec2(n.x + 180, n.y + 80)),
            n.selected
        };
    }

    const size_t GetLinkCount() override { return mLinks.size(); }
    const GraphEditor::Link GetLink(GraphEditor::LinkIndex index) override { return mLinks[index]; }
};
// 👈 在类外定义
const char* EditorGraph::s_inputNames[] = { "In" };
const char* EditorGraph::s_outputNames[] = { "Out" };

static EditorGraph g_graph;
static GraphEditor::Options g_options;
static GraphEditor::ViewState g_viewState;
static GraphEditor::FitOnScreen g_fit = GraphEditor::Fit_None;
static bool g_needsRebuild = true;

void DrawGraphEditorPanel() {
    if (!ImGui::Begin("Graph")) { ImGui::End(); return; }

    // 🔥 UI 控制区
    if (ImGui::Button("Refresh")) g_needsRebuild = true;
    ImGui::SameLine();
    if (ImGui::Button("Fit All")) g_fit = GraphEditor::Fit_AllNodes;
    ImGui::SameLine();
    if (ImGui::Button("Fit Selected")) g_fit = GraphEditor::Fit_SelectedNodes;
    
    // 🔥 开关：切换是否显示未连接的实体
    ImGui::SameLine();
    if (ImGui::Checkbox("Hide Unconnected", &g_graph.m_bHideUnconnected)) {
        g_needsRebuild = true; // 状态改变时立即重建图
    }

    ImGui::BeginChild("##GraphCanvas", ImVec2(0, 0), true);
    
    if (g_needsRebuild) {
        g_graph.Rebuild();
        g_needsRebuild = false;
    }

    GraphEditor::Show(g_graph, g_options, g_viewState, true, &g_fit);
    g_fit = GraphEditor::Fit_None;

    ImGui::EndChild();
    ImGui::End();
}