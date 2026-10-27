#include "GraphEditorPanel.h"
#include "SceneGlobals.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "GraphEditor.h"
#include <string>
#include <vector>
#include <map>

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

    // 只能有一种模板
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

        int nodeId = 0;
        for (int i = 0; i < (int)g_entities.size(); ++i) {
            auto& e = g_entities[i];
            if (e.m_szName.empty() || e.m_szName == "worldspawn") continue;

            MyNode n;
            n.name = e.m_szName;
            // 如果 entity 有 targetname，显示在名字里
            auto it = e.m_properties.find("targetname");
            if (it != e.m_properties.end() && !it->second.empty()) {
                n.name += "\n[" + it->second + "]";
            }
            n.x = 100 + (nodeId % 4) * 250;
            n.y = 100 + (nodeId / 4) * 150;
            n.selected = false;
            n.entityIndex = i;
            mNodes.push_back(n);
            nodeId++;
        }

        // 连线：遍历 inputs
        for (int i = 0; i < (int)mNodes.size(); ++i) {
            auto& inputEnt = g_entities[mNodes[i].entityIndex];
            for (auto& out : inputEnt.m_outputs) {
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
                        mLinkMap[{i, j}] = (int)(&out - &inputEnt.m_outputs[0]);
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
        }

        EntityOutput newOut;
        newOut.outputName = "OnTrigger";
        newOut.targetName = targetName;
        newOut.inputName = "Open";
        inputEnt.m_outputs.push_back(newOut);

        GraphEditor::Link newLink;
        newLink.mInputNodeIndex = inputNodeIndex;
        newLink.mOutputNodeIndex = outputNodeIndex;
        newLink.mInputSlotIndex = inputSlotIndex;
        newLink.mOutputSlotIndex = outputSlotIndex;
        mLinks.push_back(newLink);
        mLinkMap[{inputNodeIndex, outputNodeIndex}] = (int)inputEnt.m_outputs.size() - 1;
    }

    void DelLink(GraphEditor::LinkIndex linkIndex) override {
        if (linkIndex < 0 || linkIndex >= mLinks.size()) return;
        auto& link = mLinks[linkIndex];
        if (mLinkMap.count({link.mInputNodeIndex, link.mOutputNodeIndex})) {
            int outIndex = mLinkMap[{link.mInputNodeIndex, link.mOutputNodeIndex}];
            auto& inputEnt = g_entities[mNodes[link.mInputNodeIndex].entityIndex];
            if (outIndex >= 0 && outIndex < inputEnt.m_outputs.size()) {
                inputEnt.m_outputs.erase(inputEnt.m_outputs.begin() + outIndex);
            }
            mLinkMap.erase({link.mInputNodeIndex, link.mOutputNodeIndex});
        }
        mLinks.erase(mLinks.begin() + linkIndex);
    }

    void RightClick(GraphEditor::NodeIndex nodeIndex, GraphEditor::SlotIndex slotIndexInput, GraphEditor::SlotIndex slotIndexOutput) override {
    }

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

    if (ImGui::Button("Refresh Graph")) g_needsRebuild = true;
    ImGui::SameLine();
    if (ImGui::Button("Fit All")) g_fit = GraphEditor::Fit_AllNodes;
    ImGui::SameLine();
    if (ImGui::Button("Fit Selected")) g_fit = GraphEditor::Fit_SelectedNodes;
    ImGui::SameLine();
    ImGui::TextDisabled("(Drag right dot to left dot)");

    if (g_needsRebuild) {
        g_graph.Rebuild();
        g_needsRebuild = false;
    }

    ImGui::BeginChild("##GraphCanvas", ImVec2(0, 0), true);
    
    // 根据官方示例，需要传入 delegate, options, viewState, show, &fit
    GraphEditor::Show(g_graph, g_options, g_viewState, true, &g_fit);
    g_fit = GraphEditor::Fit_None;

    ImGui::EndChild();
    ImGui::End();
}