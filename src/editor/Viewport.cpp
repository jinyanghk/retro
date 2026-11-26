#include "Viewport.h"
#include "Camera.h"
#include "GL.h"
#include "Picking.h"
#include "Gizmo.h"
#include "Globals.h"
#include "UndoStack.h"

#include <GL/glew.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>

// 全局变量定义
MapData g_loadedMapData;
bool g_bLoadedBvh = false;
std::vector<MapData::NodeData> g_editorNavNodes;
std::vector<BadNavLink> g_badNavLinks;
std::vector<BadNavNode> g_badNavNodes;

// main.cpp 里的全局输入状态
extern bool g_bRightMouseDown;
extern bool g_bDraggingGizmo;
extern bool g_bRenderTexture; // 确保有这个

void Draw3DView(bool& outClickedLeft, bool& outClickedRight, bool& outClickedMiddle) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("3D View", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse);
    
    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    int vpW = std::max(1, (int)viewportSize.x), vpH = std::max(1, (int)viewportSize.y);
    ImVec2 viewportPos = ImGui::GetCursorScreenPos();
    ImVec2 mousePos = ImGui::GetMousePos();
    bool bHovered = ImGui::IsWindowHovered();
    bool bMouseInViewport = bHovered && mousePos.x >= viewportPos.x && mousePos.x <= viewportPos.x + viewportSize.x && mousePos.y >= viewportPos.y && mousePos.y <= viewportPos.y + viewportSize.y;
    int localMouseX = (int)(mousePos.x - viewportPos.x), localMouseY = (int)(mousePos.y - viewportPos.y);

    if (bMouseInViewport && g_bRightMouseDown) {
        ImGuiIO& io = ImGui::GetIO();
        g_cam.yaw += io.MouseDelta.x * 0.2f; g_cam.pitch += io.MouseDelta.y * 0.2f;
        g_cam.pitch = glm::clamp(g_cam.pitch, -89.0f, 89.0f);
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    }

    glm::mat4 view = g_cam.GetView();
    // 把 far plane 放大到 50000 以防地图太大
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), (float)vpW/(float)vpH, 1.0f, 50000.0f);
    
    bool bIsSubEdit = (g_iGizmoMode == GIZMO_FACE || g_iGizmoMode == GIZMO_EDGE || g_iGizmoMode == GIZMO_VERTEX);
    g_iSubEditMode = bIsSubEdit ? (g_iGizmoMode == GIZMO_FACE ? SUB_FACE : (g_iGizmoMode == GIZMO_EDGE ? SUB_EDGE : SUB_VERTEX)) : SUB_NONE;

    ResizeFBO(vpW, vpH);
    BindFBO(); 
    glViewport(0, 0, vpW, vpH);
    glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::mat4 vp = proj * view;
    DrawGrid(vp);
    DrawBrushes(vp);
    DrawBrushWireframe(vp);
    if (g_bLoadedBvh && g_bShowBrushes) DrawLoadedBvh(vp);

    if (g_bShowBrushes) {
        for (size_t i = 0; i < g_brushes.size(); i++) {
            // 👈 判断是否在多选集合里
            if (g_selectedBrushIds.count(g_brushes[i].m_id)) {
                const auto& b = g_brushes[i];
                std::vector<float> buf;
                for (int e = 0; e < 12; e++) {
                    const auto& p0 = b.m_vertices[kEdgeVertexIndices[e][0]];
                    const auto& p1 = b.m_vertices[kEdgeVertexIndices[e][1]];
                    buf.insert(buf.end(), {p0.x,p0.y,p0.z, 1,1,0, p1.x,p1.y,p1.z, 1,1,0});
                }
                glDisable(GL_DEPTH_TEST); DrawColorLines(buf, vp); glEnable(GL_DEPTH_TEST);
            }
        }
    }  
    DrawEntityBBoxes(vp);

    if (bIsSubEdit && g_iSelectedBrush >= 0 && g_iSelectedBrush < (int)g_brushes.size()) {
        const CBrush& b = g_brushes[g_iSelectedBrush];
        if (g_iSubEditMode == SUB_FACE && g_iSubEditIndex >= 0 && g_iSubEditIndex < 6) {
            glm::vec3 quad[4];
            for (int k = 0; k < 4; k++) quad[k] = b.m_vertices[b.m_faces[g_iSubEditIndex].m_nVertexIndices[k]];
            DrawFaceHighlight(quad, vp);
        }
        if (g_iSubEditMode == SUB_EDGE && g_iSubEditIndex >= 0 && g_iSubEditIndex < 12) {
            int i0 = kEdgeVertexIndices[g_iSubEditIndex][0], i1 = kEdgeVertexIndices[g_iSubEditIndex][1];
            std::vector<float> buf = { b.m_vertices[i0].x, b.m_vertices[i0].y, b.m_vertices[i0].z, 1,1,0, b.m_vertices[i1].x, b.m_vertices[i1].y, b.m_vertices[i1].z, 1,1,0 };
            glDisable(GL_DEPTH_TEST); DrawColorLines(buf, vp); glEnable(GL_DEPTH_TEST);
        }
        if (g_iSubEditMode == SUB_VERTEX) {
            std::vector<std::pair<glm::vec3, glm::vec3>> pts;
            for (int v = 0; v < 8; v++) pts.emplace_back(b.m_vertices[v], v == g_iSubEditIndex ? glm::vec3(1,1,0) : glm::vec3(0.86f));
            DrawVertexPoints(pts, vp);
        }
    }

    if (g_bShowNav && !g_editorNavNodes.empty()) {
        std::vector<float> pointBuf, linkBuf, badLinkBuf, badPointBuf;
        for (const auto& n : g_editorNavNodes) {
            pointBuf.insert(pointBuf.end(), {n.pos.x, n.pos.y, n.pos.z, 0.2f, 1.0f, 0.2f});
            for (uint32_t li : n.links) {
                if (li < g_editorNavNodes.size()) {
                    const auto& t = g_editorNavNodes[li];
                    linkBuf.insert(linkBuf.end(), {n.pos.x, n.pos.y, n.pos.z, 0.2f, 0.6f, 0.2f, t.pos.x, t.pos.y, t.pos.z, 0.2f, 0.6f, 0.2f});
                }
            }
        }
        for (const auto& bl : g_badNavLinks) badLinkBuf.insert(badLinkBuf.end(), {bl.start.x, bl.start.y, bl.start.z, 1, 0, 0, bl.end.x, bl.end.y, bl.end.z, 1, 0, 0});
        for (const auto& bn : g_badNavNodes) badPointBuf.insert(badPointBuf.end(), {bn.pos.x, bn.pos.y, bn.pos.z, 1, 1, 0});

        DrawRawPrimitives(linkBuf, GL_LINES, 0, 1.5f, vp);
        DrawRawPrimitives(pointBuf, GL_POINTS, 6.0f, 0, vp);
        DrawRawPrimitives(badLinkBuf, GL_LINES, 0, 2.0f, vp);
        DrawRawPrimitives(badPointBuf, GL_POINTS, 12.0f, 0, vp);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    ImGui::Image((ImTextureID)(intptr_t)GetFBOTexture(), viewportSize, ImVec2(0, 1), ImVec2(1, 0));
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    DrawAndManipulateGizmo(view, proj, viewportPos, viewportSize);

    bool bIsUsingGizmo = ImGuizmo::IsUsing() || ImGuizmo::IsOver();
    if (bMouseInViewport && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !bIsUsingGizmo && !g_bDraggingGizmo && !g_bRightMouseDown) {
        if (bIsSubEdit && g_iSelectedBrush >= 0 && g_iSelectedBrush < (int)g_brushes.size()) {
            CBrush& b = g_brushes[g_iSelectedBrush];
            int hit = -1;
            if (g_iSubEditMode == SUB_FACE)        hit = PickFace  (b, view, proj, vpW, vpH, localMouseX, localMouseY);
            else if (g_iSubEditMode == SUB_EDGE)   hit = PickEdge  (b, view, proj, vpW, vpH, localMouseX, localMouseY);
            else if (g_iSubEditMode == SUB_VERTEX) hit = PickVertex(b, view, proj, vpW, vpH, localMouseX, localMouseY);
            if (hit >= 0) { g_iSubEditIndex = hit; UndoStack::Instance().PushSnapshot("Sub-Edit"); } else g_iSubEditIndex = -1;
        } else {
            glm::vec3 rayO, rayD;
            int bi = -1, ei = -1;
            if (ScreenToWorldRay(view, proj, vpW, vpH, localMouseX, localMouseY, rayO, rayD)) {
                bi = PickBrushRay(rayO, rayD); ei = PickEntityRay(rayO, rayD);
            }
            if (ei >= 0) { g_iSelectedEntity = ei; g_iSelectedBrush = -1; }
            else { g_iSelectedBrush = bi; g_iSelectedEntity = -1; }
            g_iSubEditIndex = -1;
        }
    }

    glm::mat4 camViewIm = view; float camDistance = g_cam.dist;
    ImVec2 viewManipulatePos(viewportPos.x + viewportSize.x - 128, viewportPos.y);
    ImGui::SetCursorScreenPos(viewManipulatePos);
    ImGuizmo::PushID("MyViewManip");
    ImGuizmo::ViewManipulate(glm::value_ptr(camViewIm), camDistance, viewManipulatePos, ImVec2(128, 128), 0x10101010);
    ImRect viewManipRect(viewManipulatePos, ImVec2(viewManipulatePos.x + 128, viewManipulatePos.y + 128));
    if (ImGuizmo::IsUsingViewManipulate() || (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(viewManipRect.Min, viewManipRect.Max))) {
        glm::mat4 invView = glm::inverse(camViewIm);
        glm::vec3 offset = glm::vec3(invView[3][0], invView[3][1], invView[3][2]) - g_cam.target;
        float r = glm::length(glm::vec2(offset.x, offset.z));
        if (r > 0.001f) { g_cam.yaw = glm::degrees(std::atan2(offset.z, offset.x)); g_cam.pitch = glm::degrees(std::atan2(offset.y, r)); }
        g_cam.dist = glm::length(offset);
    }
    ImGuizmo::PopID();
    ImGui::End();
    ImGui::PopStyleVar();
}