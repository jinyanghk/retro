#include "Gizmo.h"
#include "Globals.h"
#include "UndoStack.h"
#include "ImGuizmo.h"
#include <glm/gtc/type_ptr.hpp>
#include "Brush.h"
#include "Entity.h"
#include "math_compat.h"
#include <cmath>
#include <vector>
#include <algorithm>

struct DragSnapshot {
    glm::vec3 pivot;
    std::vector<glm::vec3> vertices;
    glm::vec3 entityPos;
    glm::vec3 entityAngles;
    float screenStartX, screenStartY;
    bool  bValid = false;
    
    // 👈 新增：多选快照
    std::vector<CBrush> multiBrushes;
    std::vector<CEntity> multiEntities;
} g_dragSnap;

extern bool g_bDraggingGizmo; 
extern std::set<uint64_t> g_selectedBrushIds;
extern std::set<uint64_t> g_selectedEntityIds;

void DrawAndManipulateGizmo(const glm::mat4& view, const glm::mat4& proj,
                            ImVec2 viewportPos, ImVec2 viewportSize) {
    g_bDraggingGizmo = false;
    if (g_iGizmoMode == GIZMO_NONE || !g_bShowGizmo) return;

    bool isSubEdit = (g_iGizmoMode == GIZMO_FACE || g_iGizmoMode == GIZMO_EDGE || g_iGizmoMode == GIZMO_VERTEX);
    
    // 判断当前是否处于多选状态
    int totalSelected = (int)g_selectedBrushIds.size() + (int)g_selectedEntityIds.size();
    bool isMulti = (!isSubEdit && totalSelected > 1);

    if (isSubEdit) {
        if (g_iSelectedBrush < 0 || g_iSelectedBrush >= (int)g_brushes.size() || g_iSubEditIndex < 0) return;
    } else if (!isMulti) {
        if (g_iSelectedBrush < 0 && g_iSelectedEntity < 0) return;
    }

    bool isBrush = false;
    glm::vec3 pivot(0);
    glm::mat4 baseMat = glm::mat4(1.0f);

    // ================= 计算 Gizmo 的基准位置 =================
    if (isMulti) {
        // 多选模式：计算所有选中物体的总包围盒中心
        glm::vec3 minPos(1e30f), maxPos(-1e30f);
        for (uint64_t id : g_selectedBrushIds) {
            for (const auto& b : g_brushes) {
                if (b.m_id == id) {
                    minPos = glm::min(minPos, b.GetBBoxMins());
                    maxPos = glm::max(maxPos, b.GetBBoxMaxs());
                    break;
                }
            }
        }
        for (uint64_t id : g_selectedEntityIds) {
            for (const auto& e : g_entities) {
                if (e.m_id == id) {
                    minPos = glm::min(minPos, e.m_vecBBoxMins);
                    maxPos = glm::max(maxPos, e.m_vecBBoxMaxs);
                    break;
                }
            }
        }
        pivot = (minPos + maxPos) * 0.5f;
        if (g_dragSnap.bValid) pivot = g_dragSnap.pivot; // 拖拽中保持初始 pivot 不变
        baseMat = glm::translate(glm::mat4(1.0f), pivot);
    } 
    else if (isSubEdit) {
        isBrush = true;
        const CBrush& b = g_brushes[g_iSelectedBrush];
        if (g_dragSnap.bValid) pivot = g_dragSnap.pivot;
        else {
            if (g_iGizmoMode == GIZMO_FACE)        pivot = GetFaceCenter(b, g_iSubEditIndex);
            else if (g_iGizmoMode == GIZMO_EDGE)   pivot = (b.m_vertices[kEdgeVertexIndices[g_iSubEditIndex][0]] + b.m_vertices[kEdgeVertexIndices[g_iSubEditIndex][1]]) * 0.5f;
            else                                   pivot = b.m_vertices[g_iSubEditIndex];
        }
        baseMat = glm::translate(glm::mat4(1.0f), pivot);
    } 
    else {
        if (g_iSelectedBrush >= 0) {
            isBrush = true;
            pivot = g_brushes[g_iSelectedBrush].m_vecPos;
            baseMat = glm::translate(glm::mat4(1.0f), pivot);
        } else if (g_iSelectedEntity >= 0) {
            pivot = g_entities[g_iSelectedEntity].m_vecPos;
            baseMat = glm::translate(glm::mat4(1.0f), pivot);
            baseMat *= AngleMatrix(g_entities[g_iSelectedEntity].m_angRot);
        }
    }

    // ================= 绘制 ImGuizmo =================
    glm::mat4 viewIm = view, projIm = proj, manipulateMat = baseMat;
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGui::SetCursorScreenPos(viewportPos);
    ImGuizmo::SetRect(viewportPos.x, viewportPos.y, viewportSize.x, viewportSize.y);

    ImGuizmo::OPERATION op = (ImGuizmo::OPERATION)0;
    if (g_iGizmoMode == GIZMO_TRANSLATE) op = ImGuizmo::TRANSLATE;
    else if (g_iGizmoMode == GIZMO_ROTATE) op = ImGuizmo::ROTATE;
    else if (g_iGizmoMode == GIZMO_SCALE) op = ImGuizmo::SCALE;
    else if (isSubEdit) op = ImGuizmo::TRANSLATE;

    float* snapPtr = (g_bUseSnap && g_iGizmoMode != GIZMO_SCALE) ? g_Snap : nullptr;
    
    bool bUsing = ImGuizmo::Manipulate(glm::value_ptr(viewIm), glm::value_ptr(projIm), 
                                       op, ImGuizmo::LOCAL, glm::value_ptr(manipulateMat), 
                                       nullptr, snapPtr, nullptr, nullptr);

    ImVec2 mousePos = ImGui::GetMousePos();

    // ================= 拖拽开始时：记录快照 =================
    if (bUsing && !g_dragSnap.bValid) {
        g_dragSnap.bValid = true;
        UndoStack::Instance().PushSnapshot("Gizmo Edit");
        g_dragSnap.screenStartX = mousePos.x;
        g_dragSnap.screenStartY = mousePos.y;
        
        if (isMulti) {
            g_dragSnap.pivot = pivot;
            g_dragSnap.multiBrushes.clear();
            g_dragSnap.multiEntities.clear();
            for (uint64_t id : g_selectedBrushIds) {
                for (const auto& b : g_brushes) if (b.m_id == id) g_dragSnap.multiBrushes.push_back(b);
            }
            for (uint64_t id : g_selectedEntityIds) {
                for (const auto& e : g_entities) if (e.m_id == id) g_dragSnap.multiEntities.push_back(e);
            }
        }
        else if (isBrush) {
            g_dragSnap.pivot = pivot;
            g_dragSnap.vertices = g_brushes[g_iSelectedBrush].m_vertices;
        } 
        else if (g_iSelectedEntity >= 0) {
            g_dragSnap.entityPos = g_entities[g_iSelectedEntity].m_vecPos;
            g_dragSnap.entityAngles = g_entities[g_iSelectedEntity].m_angRot;
        }
    }

    // ================= 拖拽过程中：应用变换 =================
    if (bUsing && g_dragSnap.bValid) {
        float tr[3], rt[3], sc[3];
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(manipulateMat), tr, rt, sc);

        if (isMulti) {
            // 👈 多选模式：围绕总中心点应用变换
            glm::vec3 deltaPos = glm::vec3(tr[0], tr[1], tr[2]) - pivot;
            
            if (g_iGizmoMode == GIZMO_TRANSLATE) {
                for (auto& b : g_dragSnap.multiBrushes) {
                    for (auto& v : b.m_vertices) v += deltaPos;
                    b.m_vecPos += deltaPos;
                }
                for (auto& e : g_dragSnap.multiEntities) {
                    e.m_vecPos += deltaPos;
                    e.m_vecBBoxMins += deltaPos;
                    e.m_vecBBoxMaxs += deltaPos;
                }
            }
            else if (g_iGizmoMode == GIZMO_ROTATE) {
                glm::mat4 rotM = glm::translate(glm::mat4(1.0f), pivot) 
                               * glm::mat4(glm::mat3(manipulateMat)) 
                               * glm::translate(glm::mat4(1.0f), -pivot);
                for (auto& b : g_dragSnap.multiBrushes) {
                    for (auto& v : b.m_vertices) v = glm::vec3(rotM * glm::vec4(v, 1.0f));
                }
                for (auto& e : g_dragSnap.multiEntities) {
                    glm::vec3 localPos = e.m_vecPos - pivot;
                    glm::vec3 newLocal = glm::vec3(glm::mat3(manipulateMat) * localPos);
                    e.m_vecPos = pivot + newLocal;
                    // 旋转的 BBox 暂时不做完美包围盒计算，保持原有 AABB 或仅移动
                    e.m_vecBBoxMins += (e.m_vecPos - pivot) - localPos;
                    e.m_vecBBoxMaxs += (e.m_vecPos - pivot) - localPos;
                }
            }
            else if (g_iGizmoMode == GIZMO_SCALE) {
                float dx = mousePos.x - g_dragSnap.screenStartX, dy = mousePos.y - g_dragSnap.screenStartY;
                float delta = (fabsf(dx) > fabsf(dy)) ? dx : -dy;
                float scaleFactor = std::max(0.01f, 1.0f + delta * 0.005f);
                
                glm::vec3 scVec(glm::length(glm::vec3(manipulateMat[0])), glm::length(glm::vec3(manipulateMat[1])), glm::length(glm::vec3(manipulateMat[2])));
                float diffX = fabsf(scVec.x - 1.0f), diffY = fabsf(scVec.y - 1.0f), diffZ = fabsf(scVec.z - 1.0f);
                glm::vec3 scaleVec(scaleFactor);
                if (diffX > 0.01f && diffX >= diffY && diffX >= diffZ) scaleVec = glm::vec3(scaleFactor, 1.0f, 1.0f);
                else if (diffY > 0.01f && diffY >= diffX && diffY >= diffZ) scaleVec = glm::vec3(1.0f, scaleFactor, 1.0f);
                else if (diffZ > 0.01f) scaleVec = glm::vec3(1.0f, 1.0f, scaleFactor);

                for (auto& b : g_dragSnap.multiBrushes) {
                    for (auto& v : b.m_vertices) {
                        glm::vec3 local = v - pivot;
                        local *= scaleVec;
                        v = pivot + local;
                    }
                }
                for (auto& e : g_dragSnap.multiEntities) {
                    glm::vec3 local = e.m_vecPos - pivot;
                    local *= scaleVec;
                    e.m_vecPos = pivot + local;
                    // Scale 同理，BBox 仅做简单移动适配
                    e.m_vecBBoxMins += (e.m_vecPos - pivot) - (e.m_vecPos - pivot) / scaleVec; 
                    e.m_vecBBoxMaxs += (e.m_vecPos - pivot) - (e.m_vecPos - pivot) / scaleVec; 
                }
            }
            
            // 将快照数据写回全局对象
            for (auto& snapB : g_dragSnap.multiBrushes) {
                for (auto& realB : g_brushes) {
                    if (realB.m_id == snapB.m_id) { 
                        realB = snapB; 
                        realB.RecalcBoundsFromVertices();
                        realB.RecalcFaceNormals();
                        break; 
                    }
                }
            }
            for (auto& snapE : g_dragSnap.multiEntities) {
                for (auto& realE : g_entities) {
                    if (realE.m_id == snapE.m_id) { 
                        realE = snapE; 
                        UpdateEntityBBox(realE);
                        break; 
                    }
                }
            }
        }
        else if (isSubEdit) {
            // 子编辑逻辑保持不变
            glm::vec3 offset = glm::vec3(tr[0], tr[1], tr[2]) - pivot;
            CBrush& b = g_brushes[g_iSelectedBrush];
            b.m_vertices = g_dragSnap.vertices;
            if (g_iGizmoMode == GIZMO_FACE) {
                for (int k = 0; k < 4; k++) b.m_vertices[b.m_faces[g_iSubEditIndex].m_nVertexIndices[k]] += offset;
            } else if (g_iGizmoMode == GIZMO_EDGE) {
                b.m_vertices[kEdgeVertexIndices[g_iSubEditIndex][0]] += offset;
                b.m_vertices[kEdgeVertexIndices[g_iSubEditIndex][1]] += offset;
            } else if (g_iGizmoMode == GIZMO_VERTEX) {
                b.m_vertices[g_iSubEditIndex] += offset;
            }
            b.RecalcBoundsFromVertices();
            b.RecalcFaceNormals();
        }
        else if (isBrush) {
            // 单选 Brush 逻辑保持不变
            CBrush& b = g_brushes[g_iSelectedBrush];
            b.m_vertices = g_dragSnap.vertices;
            glm::vec3 center = g_dragSnap.pivot;

            if (g_iGizmoMode == GIZMO_TRANSLATE) {
                glm::vec3 offset = glm::vec3(tr[0], tr[1], tr[2]) - center;
                for (auto& v : b.m_vertices) v += offset;
                b.m_vecPos = glm::vec3(tr[0], tr[1], tr[2]);
            } 
            else if (g_iGizmoMode == GIZMO_ROTATE) {
                glm::mat4 rotM = glm::translate(glm::mat4(1.0f), center) * glm::mat4(glm::mat3(manipulateMat)) * glm::translate(glm::mat4(1.0f), -center);
                for (auto& v : b.m_vertices) v = glm::vec3(rotM * glm::vec4(v, 1.0f));
                b.m_vecPos = center;
            } 
            else if (g_iGizmoMode == GIZMO_SCALE) {
                float dx = mousePos.x - g_dragSnap.screenStartX, dy = mousePos.y - g_dragSnap.screenStartY;
                float delta = (fabsf(dx) > fabsf(dy)) ? dx : -dy;
                float scaleFactor = std::max(0.01f, 1.0f + delta * 0.005f);
                glm::vec3 scVec(glm::length(glm::vec3(manipulateMat[0])), glm::length(glm::vec3(manipulateMat[1])), glm::length(glm::vec3(manipulateMat[2])));
                float diffX = fabsf(scVec.x - 1.0f), diffY = fabsf(scVec.y - 1.0f), diffZ = fabsf(scVec.z - 1.0f);
                glm::vec3 scaleVec(scaleFactor);
                if (diffX > 0.01f && diffX >= diffY && diffX >= diffZ) scaleVec = glm::vec3(scaleFactor, 1.0f, 1.0f);
                else if (diffY > 0.01f && diffY >= diffX && diffY >= diffZ) scaleVec = glm::vec3(1.0f, scaleFactor, 1.0f);
                else if (diffZ > 0.01f) scaleVec = glm::vec3(1.0f, 1.0f, scaleFactor);
                for (auto& v : b.m_vertices) {
                    glm::vec3 local = v - center;
                    local *= scaleVec;
                    v = center + local;
                }
                b.m_vecPos = center;
            }
            b.RecalcBoundsFromVertices();
            b.RecalcFaceNormals();
        } 
        else if (g_iSelectedEntity >= 0) {
            // 单选 Entity 逻辑保持不变
            CEntity& e = g_entities[g_iSelectedEntity];
            e.m_vecPos = glm::vec3(tr[0], tr[1], tr[2]);
            if (g_iGizmoMode == GIZMO_ROTATE) e.m_angRot = glm::vec3(rt[0], rt[1], rt[2]);
            UpdateEntityBBox(e);
        }
    }
    
    if (!bUsing && g_dragSnap.bValid) {
        g_dragSnap.bValid = false;
    }
    g_bDraggingGizmo = bUsing;
}