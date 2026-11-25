#include "Panels.h"
#include "Globals.h"
#include "UndoStack.h"
#include "GameDatabase.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include "Internal.h"
#include <glm/gtc/type_ptr.hpp>

extern std::string GetEntitySearchString(const CEntity& e);
extern bool InputTextString(const char* label, std::string& value);
extern glm::vec3* GetEditorCamTarget();
extern float* GetEditorCamDist();

static char s_sceneFilter[128] = "";
static int pendingDeleteGroupIdx = -1;

// 👈 辅助函数：查找某物体所在的组
static int FindBrushGroupIndex(uint64_t bid) {
    for (int g = 0; g < (int)g_brushGroups.size(); g++) {
        for (uint64_t id : g_brushGroups[g].brushIds) {
            if (id == bid) return g;
        }
    }
    return -1;
}
static int FindEntityGroupIndex(uint64_t eid) {
    for (int g = 0; g < (int)g_brushGroups.size(); g++) {
        for (uint64_t id : g_brushGroups[g].entityIds) {
            if (id == eid) return g;
        }
    }
    return -1;
}

// 👈 递归设置组及其所有子孙节点的可见性
static void SetGroupVisibilityRecursive(uint64_t groupId, bool visible) {
    int gIdx = FindGroupIndexById(groupId);
    if (gIdx < 0) return;
    
    auto& grp = g_brushGroups[gIdx];
    grp.visible = visible;
    
    // 改直接成员
    for (uint64_t bid : grp.brushIds) {
        for (int i = 0; i < (int)g_brushes.size(); i++)
            if (g_brushes[i].m_id == bid && i < (int)g_brushVisible.size()) g_brushVisible[i] = visible;
    }
    for (uint64_t eid : grp.entityIds) {
        for (int i = 0; i < (int)g_entities.size(); i++)
            if (g_entities[i].m_id == eid && i < (int)g_entityVisible.size()) g_entityVisible[i] = visible;
    }
    
    // 递归改子组
    for (auto& sub : g_brushGroups) {
        if (sub.parentId == groupId) {
            SetGroupVisibilityRecursive(sub.id, visible);
        }
    }
}

void DrawScenePanel(int* pSelEntity, int* pSelBrush, PanelResult& out)
{
    if (!ImGui::Begin("Scene")) { ImGui::End(); return; }

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##SceneFilter", "Search name / ID / KV...", s_sceneFilter, sizeof(s_sceneFilter));
    
    std::string filterLower = s_sceneFilter;
    std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);

    float treeHeight = ImGui::GetContentRegionAvail().y * 0.5f;
    
    ImGui::BeginChild("##SceneTreeChild", ImVec2(0, treeHeight), true);
    
    static bool dragIsEntity = false;

    if (ImGui::TreeNodeEx("Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
        
        // 👈 改成 Lambda 形式的递归渲染
        std::function<void(uint64_t, int)> RenderGroupRecursive = [&](uint64_t parentId, int depth) {
            // 👈 用拷贝或者局部快照，避免循环中容器改变导致迭代器失效
            // 为了性能，我们只拷贝一层 ID
            std::vector<uint64_t> childGroupIds;
            for (const auto& g : g_brushGroups) {
                if (g.parentId == parentId) childGroupIds.push_back(g.id);
            }

            for (uint64_t childId : childGroupIds) {
                int gIdx = FindGroupIndexById(childId);
                if (gIdx < 0) continue; // 可能在渲染过程中被删了

                // 每次用的时候，安全地拿一下引用
                auto& grp = g_brushGroups[gIdx]; 
                if (grp.parentId != parentId) continue;

                ImGui::PushID(grp.id + 30000);
                if (depth > 0) ImGui::Indent(16.0f);

                bool groupVis = (grp.visible != 0);
                if (ImGui::Checkbox("##vis", &groupVis)) {
                    SetGroupVisibilityRecursive(grp.id, groupVis);
                }
                ImGui::SameLine();
                
                bool isOpen = ImGui::TreeNodeEx(grp.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow);
                
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                    g_iSelectedGroup = gIdx;
                    *pSelBrush = -1;
                    *pSelEntity = -1;
                }

                // 右键菜单
                std::string popupName = "GroupCtx_" + std::to_string(grp.id);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup(popupName.c_str());
                if (ImGui::BeginPopup(popupName.c_str())) {
                    if (ImGui::MenuItem("Delete Group")) pendingDeleteGroupIdx = gIdx;
                    ImGui::EndPopup();
                }

                // 接收拖拽
                if (ImGui::BeginDragDropTarget()) {
                    // 接收 Brush/Entity
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DRAG_ITEM")) {
                        uint64_t draggedId = *(const uint64_t*)payload->Data;
                        if (dragIsEntity) {
                            int oldG = FindEntityGroupIndex(draggedId);
                            if (oldG >= 0) {
                                auto& ids = g_brushGroups[oldG].entityIds;
                                ids.erase(std::remove(ids.begin(), ids.end(), draggedId), ids.end());
                            }
                            grp.entityIds.push_back(draggedId);
                        } else {
                            int oldG = FindBrushGroupIndex(draggedId);
                            if (oldG >= 0) {
                                auto& ids = g_brushGroups[oldG].brushIds;
                                ids.erase(std::remove(ids.begin(), ids.end(), draggedId), ids.end());
                            }
                            grp.brushIds.push_back(draggedId);
                        }
                    }
                    // 接收 Group（多级嵌套）
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DRAG_GROUP")) {
                        uint64_t draggedGroupId = *(const uint64_t*)payload->Data;
                        if (draggedGroupId != grp.id && !IsGroupAncestor(draggedGroupId, grp.id)) {
                            int draggedIdx = FindGroupIndexById(draggedGroupId);
                            if (draggedIdx >= 0) {
                                g_brushGroups[draggedIdx].parentId = grp.id;
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // 拖拽源（将 Group 拖走）
                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("DRAG_GROUP", &grp.id, sizeof(uint64_t));
                    ImGui::Text("Group: %s", grp.name.c_str());
                    ImGui::EndDragDropSource();
                }

                if (isOpen) {
                    // ====== 渲染组内的 Brushes ======
                    int removeBrushIdx = -1;
                    for (int k = 0; k < (int)grp.brushIds.size(); k++) {
                        uint64_t bid = grp.brushIds[k];
                        int bi = -1;
                        for (int i = 0; i < (int)g_brushes.size(); i++) {
                            if (g_brushes[i].m_id == bid) { bi = i; break; }
                        }
                        if (bi < 0) continue;

                        ImGui::PushID(bi + 40000);
                        
                        bool vis = (bi < (int)g_brushVisible.size()) ? (g_brushVisible[bi] != 0) : true;
                        if (ImGui::Checkbox("##vis", &vis)) {
                            if (bi < (int)g_brushVisible.size()) g_brushVisible[bi] = vis ? 1 : 0;
                        }
                        ImGui::SameLine();
                        
                        bool sel = (*pSelBrush == bi);
                        if (ImGui::Selectable(("Brush #" + std::to_string(bid)).c_str(), sel)) {
                            *pSelBrush = bi;
                            *pSelEntity = -1;
                            g_iSelectedGroup = -1;
                        }
                        
                        if (ImGui::BeginPopupContextItem(("rmB" + std::to_string(bi)).c_str())) {
                            if (ImGui::MenuItem("Duplicate")) { out.bRequestDuplicate = true; out.duplicateId = bid; }
                            if (ImGui::MenuItem("Delete")) { out.bRequestDelete = true; out.targetId = bid; }
                            if (ImGui::MenuItem("Teleport")) {
                                if (GetEditorCamTarget()) *GetEditorCamTarget() = g_brushes[bi].m_vecPos;
                                if (GetEditorCamDist()) *GetEditorCamDist() = 150.0f;
                            }
                            if (ImGui::MenuItem("Remove from Group")) removeBrushIdx = k;
                            ImGui::EndPopup();
                        }
                        ImGui::PopID();
                    }
                    if (removeBrushIdx >= 0) grp.brushIds.erase(grp.brushIds.begin() + removeBrushIdx);

                    // ====== 渲染组内的 Entities ======
                    int removeEntityIdx = -1;
                    for (int k = 0; k < (int)grp.entityIds.size(); k++) {
                        uint64_t eid = grp.entityIds[k];
                        int ei = -1;
                        for (int i = 0; i < (int)g_entities.size(); i++) {
                            if (g_entities[i].m_id == eid) { ei = i; break; }
                        }
                        if (ei < 0) continue;

                        auto& ent = g_entities[ei];
                        std::string tname = "";
                        auto it = ent.m_properties.find("targetname");
                        if (it != ent.m_properties.end()) tname = it->second;
                        std::string label = ent.m_szName + " #" + std::to_string(eid);
                        if (!tname.empty()) label += " [" + tname + "]";

                        if (!filterLower.empty()) {
                            std::string searchStr = GetEntitySearchString(ent);
                            std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);
                            if (searchStr.find(filterLower) == std::string::npos) continue;
                        }

                        ImGui::PushID(ei + 50000);
                        
                        bool vis = (ei < (int)g_entityVisible.size()) ? (g_entityVisible[ei] != 0) : true;
                        if (ImGui::Checkbox("##vis", &vis)) {
                            if (ei < (int)g_entityVisible.size()) g_entityVisible[ei] = vis ? 1 : 0;
                        }
                        ImGui::SameLine();
                        
                        bool sel = (*pSelEntity == ei);
                        if (ImGui::Selectable(label.c_str(), sel)) {
                            *pSelEntity = ei;
                            *pSelBrush = -1;
                            g_iSelectedGroup = -1;
                        }
                        
                        if (ImGui::BeginPopupContextItem(("rmE" + std::to_string(ei)).c_str())) {
                            if (ImGui::MenuItem("Delete")) { out.bRequestDeleteEntity = true; out.targetEntityId = eid; }
                            if (ImGui::MenuItem("Teleport")) {
                                if (GetEditorCamTarget()) *GetEditorCamTarget() = g_entities[ei].m_vecPos;
                                if (GetEditorCamDist()) *GetEditorCamDist() = 150.0f;
                            }
                            if (ImGui::MenuItem("Remove from Group")) removeEntityIdx = k;
                            ImGui::EndPopup();
                        }
                        ImGui::PopID();
                    }
                    if (removeEntityIdx >= 0) grp.entityIds.erase(grp.entityIds.begin() + removeEntityIdx);
                    
                    // 👈 递归渲染子组
                    RenderGroupRecursive(grp.id, depth + 1);
                    
                    ImGui::TreePop();
                }

                if (depth > 0) ImGui::Unindent(16.0f);
                ImGui::PopID();
            }
        };

        // 启动递归渲染
        RenderGroupRecursive(0, 0);

        // 删除 Group（安全处理子组）
        if (pendingDeleteGroupIdx >= 0) {
            uint64_t deletedId = g_brushGroups[pendingDeleteGroupIdx].id;
            uint64_t parentOfDeleted = g_brushGroups[pendingDeleteGroupIdx].parentId;
            
            for (auto& g : g_brushGroups) {
                if (g.parentId == deletedId) g.parentId = parentOfDeleted;
            }
            
            g_brushGroups.erase(g_brushGroups.begin() + pendingDeleteGroupIdx);
            pendingDeleteGroupIdx = -1;
            g_iSelectedGroup = -1;
        }

        // ====== 渲染散落的 Entities ======
        for (int i = 0; i < (int)g_entities.size(); i++) {
            bool inAnyGroup = false;
            uint64_t eid = g_entities[i].m_id;
            for (const auto& grp : g_brushGroups) {
                for (uint64_t id : grp.entityIds) if (id == eid) { inAnyGroup = true; break; }
                if (inAnyGroup) break;
            }
            if (inAnyGroup) continue;

            auto& e = g_entities[i];
            
            std::string tname = "";
            auto it = e.m_properties.find("targetname");
            if (it != e.m_properties.end()) tname = it->second;
            
            std::string label = e.m_szName + " #" + std::to_string(eid);
            if (!tname.empty()) label += " [" + tname + "]";
            
            if (!filterLower.empty()) {
                std::string searchStr = GetEntitySearchString(e);
                std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);
                if (searchStr.find(filterLower) == std::string::npos) continue;
            }

            ImGui::PushID(i);
            bool vis = (i < (int)g_entityVisible.size()) ? (g_entityVisible[i] != 0) : true;
            if (ImGui::Checkbox("##vis", &vis)) {
                if (i < (int)g_entityVisible.size()) g_entityVisible[i] = vis ? 1 : 0;
            }
            ImGui::SameLine();

            bool sel = (*pSelEntity == i);
            if (ImGui::Selectable(label.c_str(), sel)) {
                *pSelEntity = i;
                *pSelBrush = -1;
                g_iSelectedGroup = -1;
            }
            
            if (ImGui::BeginDragDropSource()) {
                dragIsEntity = true;
                ImGui::SetDragDropPayload("DRAG_ITEM", &eid, sizeof(uint64_t));
                ImGui::Text("Entity/ID: %llu", eid);
                ImGui::EndDragDropSource();
            }
            
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DRAG_ITEM")) {
                    uint64_t draggedId = *(const uint64_t*)payload->Data;
                    if (draggedId != eid) {
                        BrushGroup newGrp;
                        newGrp.id = GenerateUniqueID();      // 👈 分配 ID
                        newGrp.parentId = 0;
                        newGrp.name = "Group_" + std::to_string(newGrp.id);
                        if (dragIsEntity) {
                            newGrp.entityIds.push_back(draggedId);
                            newGrp.entityIds.push_back(eid);
                        } else {
                            newGrp.brushIds.push_back(draggedId);
                            newGrp.entityIds.push_back(eid);
                        }
                        g_brushGroups.push_back(newGrp);
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (ImGui::BeginPopupContextItem(("rmE" + std::to_string(i)).c_str())) {
                if (ImGui::MenuItem("Delete")) { out.bRequestDeleteEntity = true; out.targetEntityId = eid; }
                if (ImGui::MenuItem("Teleport")) {
                    if (GetEditorCamTarget()) *GetEditorCamTarget() = e.m_vecPos;
                    if (GetEditorCamDist()) *GetEditorCamDist() = 150.0f;
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }

        // ====== 渲染散落的 Brushes ======
        for (int i = 0; i < (int)g_brushes.size(); i++) {
            bool inAnyGroup = false;
            uint64_t bid = g_brushes[i].m_id;
            for (const auto& grp : g_brushGroups) {
                for (uint64_t id : grp.brushIds) if (id == bid) { inAnyGroup = true; break; }
                if (inAnyGroup) break;
            }
            if (inAnyGroup) continue;

            auto& b = g_brushes[i];
            std::string label = "brush #" + std::to_string(bid);
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

            bool sel = (*pSelBrush == i);
            if (ImGui::Selectable(label.c_str(), sel)) {
                *pSelBrush = i;
                *pSelEntity = -1;
                g_iSelectedGroup = -1;
            }

            if (ImGui::BeginDragDropSource()) {
                dragIsEntity = false;
                ImGui::SetDragDropPayload("DRAG_ITEM", &bid, sizeof(uint64_t));
                ImGui::Text("Brush/ID: %llu", bid);
                ImGui::EndDragDropSource();
            }

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DRAG_ITEM")) {
                    uint64_t draggedId = *(const uint64_t*)payload->Data;
                    if (draggedId != bid) {
                        BrushGroup newGrp;
                        newGrp.id = GenerateUniqueID();      // 👈 分配 ID
                        newGrp.parentId = 0;
                        newGrp.name = "Group_" + std::to_string(newGrp.id);
                        if (dragIsEntity) {
                            newGrp.entityIds.push_back(draggedId);
                            newGrp.brushIds.push_back(bid);
                        } else {
                            newGrp.brushIds.push_back(draggedId);
                            newGrp.brushIds.push_back(bid);
                        }
                        g_brushGroups.push_back(newGrp);
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (ImGui::BeginPopupContextItem(("rmB" + std::to_string(i)).c_str())) {
                if (ImGui::MenuItem("Duplicate")) { out.bRequestDuplicate = true; out.duplicateId = bid; }
                if (ImGui::MenuItem("Delete")) { out.bRequestDelete = true; out.targetId = bid; }
                if (ImGui::MenuItem("Teleport")) {
                    if (GetEditorCamTarget()) *GetEditorCamTarget() = b.m_vecPos;
                    if (GetEditorCamDist()) *GetEditorCamDist() = 150.0f;
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        
        ImGui::TreePop();
    }
    
    ImGui::EndChild();
    ImGui::Separator();

    // ================= 下半部分：属性面板 =================
    ImGui::BeginChild("##PropertiesChild", ImVec2(0, 0), true);
    
    if (g_iSelectedGroup >= 0 && g_iSelectedGroup < (int)g_brushGroups.size()) {
        auto& grp = g_brushGroups[g_iSelectedGroup];
        ImGui::TextColored(ImVec4(0, 1, 1, 1), "Selected Group");
        
        char nameBuf[128];
        strncpy(nameBuf, grp.name.c_str(), sizeof(nameBuf));
        if (ImGui::InputText("Group Name", nameBuf, sizeof(nameBuf))) {
            grp.name = nameBuf;
        }
        
        ImGui::Text("Contains: %zu Brushes, %zu Entities", grp.brushIds.size(), grp.entityIds.size());
        ImGui::Separator();
        ImGui::TextDisabled("(Click a child inside the tree to edit its properties)");
    } 
    else if (*pSelEntity >= 0 && *pSelEntity < (int)g_entities.size()) {
        CEntity& e = g_entities[*pSelEntity];
        
        if (ImGui::Button("Teleport")) {
            if (GetEditorCamTarget()) *GetEditorCamTarget() = e.m_vecPos;
            if (GetEditorCamDist()) *GetEditorCamDist() = 150.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            out.bRequestDeleteEntity = true;
            out.targetEntityId = e.m_id;
        }
        ImGui::Separator();

        // Entity Group 分配
        int curGroup = FindEntityGroupIndex(e.m_id);
        if (curGroup >= 0) {
            ImGui::Text("Group: %s", g_brushGroups[curGroup].name.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Remove from Group")) {
                auto& ids = g_brushGroups[curGroup].entityIds;
                ids.erase(std::remove(ids.begin(), ids.end(), e.m_id), ids.end());
            }
        } else {
            ImGui::Text("Group: <None>");
            ImGui::SameLine();
            if (ImGui::Button("Assign to Group...")) ImGui::OpenPopup("AssignEntityGroupPopup");
            if (ImGui::BeginPopup("AssignEntityGroupPopup")) {
                if (ImGui::MenuItem("Create New Group")) {
                    BrushGroup newGrp;
                    newGrp.id = GenerateUniqueID();
                    newGrp.parentId = 0;
                    newGrp.name = "Group_" + std::to_string(newGrp.id);
                    newGrp.entityIds.push_back(e.m_id);
                    g_brushGroups.push_back(newGrp);
                }
                ImGui::Separator();
                for (int g = 0; g < (int)g_brushGroups.size(); g++) {
                    if (ImGui::MenuItem(g_brushGroups[g].name.c_str())) {
                        g_brushGroups[g].entityIds.push_back(e.m_id);
                    }
                }
                ImGui::EndPopup();
            }
        }
        ImGui::Separator();

        ImGui::TextColored(ImVec4(1,1,0,1), "Selected: %s", e.m_szName.c_str());
        ImGui::TextDisabled("ID: %llu", e.m_id);
        
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
        
        if (ImGui::Button("Teleport")) {
            if (GetEditorCamTarget()) *GetEditorCamTarget() = b.m_vecPos;
            if (GetEditorCamDist()) *GetEditorCamDist() = 150.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Duplicate")) {
            out.bRequestDuplicate = true;
            out.duplicateId = b.m_id;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            out.bRequestDelete = true;
            out.targetId = b.m_id;
        }
        ImGui::Separator();

        // Brush Group 分配
        int curGroup = FindBrushGroupIndex(b.m_id);
        if (curGroup >= 0) {
            ImGui::Text("Group: %s", g_brushGroups[curGroup].name.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Remove from Group")) {
                auto& ids = g_brushGroups[curGroup].brushIds;
                ids.erase(std::remove(ids.begin(), ids.end(), b.m_id), ids.end());
            }
        } else {
            ImGui::Text("Group: <None>");
            ImGui::SameLine();
            if (ImGui::Button("Assign to Group...")) ImGui::OpenPopup("AssignBrushGroupPopup");
            if (ImGui::BeginPopup("AssignBrushGroupPopup")) {
                if (ImGui::MenuItem("Create New Group")) {
                    BrushGroup newGrp;
                    newGrp.id = GenerateUniqueID();
                    newGrp.parentId = 0;
                    newGrp.name = "Group_" + std::to_string(newGrp.id);
                    newGrp.brushIds.push_back(b.m_id);
                    g_brushGroups.push_back(newGrp);
                }
                ImGui::Separator();
                for (int g = 0; g < (int)g_brushGroups.size(); g++) {
                    if (ImGui::MenuItem(g_brushGroups[g].name.c_str())) {
                        g_brushGroups[g].brushIds.push_back(b.m_id);
                    }
                }
                ImGui::EndPopup();
            }
        }
        ImGui::Separator();

        ImGui::TextColored(ImVec4(1,1,0,1), "Selected: brush #%llu", b.m_id);
        ImGui::TextDisabled("Index: %d", *pSelBrush);
        
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

        char btnId[64];
        snprintf(btnId, sizeof(btnId), "##BrushMatBtn%d", *pSelBrush);
        const char* displayText = b.m_szMaterial.empty() ? "<empty>" : b.m_szMaterial.c_str();

        if (ImGui::Button((std::string(displayText) + btnId).c_str(), ImVec2(200, 0))) {
            g_selectedMaterialTemp = b.m_szMaterial;
            g_bShowMaterialBrowser = true;
            g_pTargetMaterial = &g_selectedMaterialTemp;
        }

        if (ImGui::Button("Reset to inherit##BrushMat")) {
            UndoStack::Instance().PushSnapshot("Brush Material");
            b.m_szMaterial = "DEV/DEV_MEASUREGENERIC01";
            for (int f = 0; f < 6; f++) {
                b.m_faces[f].m_szMaterial = "DEV/DEV_MEASUREGENERIC01";
            }
        }

        ImGui::Separator();
        ImGui::Text("Per-face materials:");
        const char* faceNames[6] = {"+Z(顶)","-Z(底)","+X(右)","-X(左)","+Y(后)","-Y(前)"};
        if (g_iSelectedFace < 0 || g_iSelectedFace >= 6) g_iSelectedFace = 0;
        ImGui::Combo("Face", &g_iSelectedFace, faceNames, 6);

        std::string& faceMat = b.m_faces[g_iSelectedFace].m_szMaterial;

        char faceBtnLabel[512];
        snprintf(faceBtnLabel, sizeof(faceBtnLabel), "%s##FaceMatBtn%d_%d",
                 faceMat.empty() ? "<inherit>" : faceMat.c_str(), *pSelBrush, g_iSelectedFace);

        if (ImGui::Button(faceBtnLabel, ImVec2(200, 0))) {
            g_selectedFaceMaterialTemp = faceMat;
            g_bShowMaterialBrowser = true;
            g_pTargetMaterial = &g_selectedFaceMaterialTemp;
        }

        if (ImGui::Button("Reset to inherit##FaceMat")) {
            UndoStack::Instance().PushSnapshot("Face Material");
            faceMat.clear();
        }        
    } 
    else {
        ImGui::TextDisabled("Nothing selected.");
    }
    
    ImGui::EndChild();
    ImGui::End();
}