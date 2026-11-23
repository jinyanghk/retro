#include "EditorGizmo.h"
#include "SceneGlobals.h"
#include "UndoStack.h"
#include "ImGuizmo.h"
#include <glm/gtc/type_ptr.hpp>
#include "Brush.h"
#include "Entity.h"
#include "math_compat.h"

struct DragSnapshot {
    glm::vec3 pivot;
    std::vector<glm::vec3> vertices;
    glm::vec3 entityPos;
    glm::vec3 entityAngles;
    float screenStartX, screenStartY;
    bool  bValid = false;
} g_dragSnap;

// 确保你 main.cpp 里引用的全局拖拽状态一致
extern bool g_bDraggingGizmo; 

void DrawAndManipulateGizmo(const glm::mat4& view, const glm::mat4& proj,
                            ImVec2 viewportPos, ImVec2 viewportSize) {
    g_bDraggingGizmo = false;
    if (g_iGizmoMode == GIZMO_NONE || !g_bShowGizmo) return;

    bool isSubEdit = (g_iGizmoMode == GIZMO_FACE || g_iGizmoMode == GIZMO_EDGE || g_iGizmoMode == GIZMO_VERTEX);
    if (isSubEdit) {
        if (g_iSelectedBrush < 0 || g_iSelectedBrush >= (int)g_brushes.size() || g_iSubEditIndex < 0) return;
    } else {
        if (g_iSelectedBrush < 0 && g_iSelectedEntity < 0) return;
    }

    bool isBrush = false;
    glm::vec3 pivot(0);
    glm::mat4 baseMat = glm::mat4(1.0f);

    if (isSubEdit) {
        isBrush = true;
        const CBrush& b = g_brushes[g_iSelectedBrush];
        if (g_dragSnap.bValid) pivot = g_dragSnap.pivot;
        else {
            if (g_iGizmoMode == GIZMO_FACE)        pivot = GetFaceCenter(b, g_iSubEditIndex);
            else if (g_iGizmoMode == GIZMO_EDGE)   pivot = (b.m_vertices[kEdgeVertexIndices[g_iSubEditIndex][0]] + b.m_vertices[kEdgeVertexIndices[g_iSubEditIndex][1]]) * 0.5f;
            else                                   pivot = b.m_vertices[g_iSubEditIndex];
        }
        baseMat = glm::translate(glm::mat4(1.0f), pivot);
    } else {
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
    bool bUsing = ImGuizmo::Manipulate(glm::value_ptr(viewIm), glm::value_ptr(projIm), op, ImGuizmo::LOCAL, glm::value_ptr(manipulateMat), nullptr, snapPtr, nullptr, nullptr);

    ImVec2 mousePos = ImGui::GetMousePos();

    if (bUsing && !g_dragSnap.bValid) {
        g_dragSnap.bValid = true;
        UndoStack::Instance().PushSnapshot("Gizmo Edit");
        g_dragSnap.screenStartX = mousePos.x;
        g_dragSnap.screenStartY = mousePos.y;
        if (isBrush) {
            g_dragSnap.pivot = pivot;
            g_dragSnap.vertices = g_brushes[g_iSelectedBrush].m_vertices;
        } else if (g_iSelectedEntity >= 0) {
            g_dragSnap.entityPos = g_entities[g_iSelectedEntity].m_vecPos;
            g_dragSnap.entityAngles = g_entities[g_iSelectedEntity].m_angRot;
        }
    }

    if (bUsing && g_dragSnap.bValid) {
        if (isSubEdit) {
            float tr[3], rt[3], sc[3];
            ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(manipulateMat), tr, rt, sc);
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
            CBrush& b = g_brushes[g_iSelectedBrush];
            b.m_vertices = g_dragSnap.vertices;
            glm::vec3 center = g_dragSnap.pivot;

            if (g_iGizmoMode == GIZMO_TRANSLATE) {
                float tr[3], rt[3], sc[3];
                ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(manipulateMat), tr, rt, sc);
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
            CEntity& e = g_entities[g_iSelectedEntity];
            float tr[3], rt[3], sc[3];
            ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(manipulateMat), tr, rt, sc);
            e.m_vecPos = glm::vec3(tr[0], tr[1], tr[2]);
            if (g_iGizmoMode == GIZMO_ROTATE) e.m_angRot = glm::vec3(rt[0], rt[1], rt[2]);
            UpdateEntityBBox(e);
        }
    }
    if (!bUsing && g_dragSnap.bValid) g_dragSnap.bValid = false;
    g_bDraggingGizmo = bUsing;
}