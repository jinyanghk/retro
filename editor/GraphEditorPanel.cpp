#include "GraphEditorPanel.h"
#include "SceneGlobals.h"
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

    const GraphEditor::Template mTemplates[1] = {
        {
            IM_COL32(160, 160, 180, 255), // header
            IM_COL32(100, 100, 140, 255), // background
            IM_COL32(110, 110, 150, 255), // header hover
            1,                            // input slots
            (const char*[]){"In"},        // input names
            nullptr,                      // input colors
            1,                            // output slots
            (const char*[]){"Out"},       // output names
            nullptr                       // output colors
        }
    };

    void Rebuild() {
        mNodes.clear();
        mLinks.clear();
        mLinkMap.clear();

        // 🔥 1. 收集所有被引用的 targetname
        std::set<std::string> referencedTargets;
        for (const auto& e : g_entities) {
            for (const auto& conn : e.m_connections) {
                if (!conn.targetName.empty()) {
                    referencedTargets.insert(conn.targetName);
                }
            }
        }

        // 🔥 2. 定义哪些实体是“逻辑实体”（即使没连线也强制显示）
        auto IsLogicEntity = [](const std::string& classname) {
            // 只要名字里带 trigger / door / button / logic / relay 的，都算逻辑实体
            return classname.find("trigger") != std::string::npos ||
                   classname.find("door") != std::string::npos ||
                   classname.find("button") != std::string::npos ||
                   classname.find("logic") != std::string::npos ||
                   classname.find("relay") != std::string::npos;
        };

        int nodeId = 0;
        // 临时存储 实体在 g_entities 里的真实索引，映射到连续节点 ID
        std::vector<int> validEntityIndices;

        for (int i = 0; i < (int)g_entities.size(); ++i) {
            auto& e = g_entities[i];
            if (e.m_szName.empty() || e.m_szName == "worldspawn") continue;

            // 获取 targetname
            std::string tname = "";
            auto it = e.m_properties.find("targetname");
            if (it != e.m_properties.end()) tname = it->second;

            bool hasOutgoing = !e.m_connections.empty();
            bool hasIncoming = !tname.empty() && referencedTargets.count(tname) > 0;
            bool isLogic = IsLogicEntity(e.m_szName);

            // 🔥 过滤规则：如果开启了隐藏，且既没出也没进，且不是逻辑实体，则跳过
            if (m_bHideUnconnected) {
                if (!hasOutgoing && !hasIncoming && !isLogic) {
                    continue;
                }
            }

            MyNode n;
            n.name = e.m_szName;
            if (!tname.empty()) {
                n.name += "\n[" + tname + "]";
            }
            // 排版：简单的网格排列
            n.x = 100 + (nodeId % 4) * 250;
            n.y = 100 + (nodeId / 4) * 150;
            n.selected = false;
            n.entityIndex = i;
            
            mNodes.push_back(n);
            validEntityIndices.push_back(i);
            nodeId++;
        }

        // 3. 连线
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
        for (auto& node : mNodes) {
            if (node.selected) {
                node.x += delta.x;
                node.y += delta.y;
            }
        }
    }

    void AddLink(GraphEditor::NodeIndex inputNodeIndex, GraphEditor::SlotIndex inputSlotIndex,
                 GraphEditor::NodeIndex outputNodeIndex, GraphEditor::SlotIndex outputSlotIndex) override {
        auto& inputEnt = g_entities[mNodes[inputNodeIndex].entityIndex];
        auto& outputEnt = g_entities[mNodes[outputNodeIndex].entityIndex];

        std::string targetName = "";
        auto it = outputEnt.m_properties.find("targetname");
        if (it != outputEnt.m_properties.end() && !it->second.empty()) {
            targetName = it->second;
        } else {
            targetName = "entity_" + std::to_string(outputNodeIndex);
            outputEnt.m_properties["targetname"] = targetName;
            // 注意：这里如果你有 UndoStack，记得在这里 PushSnapshot
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