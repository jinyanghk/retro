#include "ui/View2D.h"

#include <cmath>
#include <vector>
#include <algorithm>
#include "imgui.h"
#include "scene/SceneGlobals.h"
#include "scene/Brush.h"
#include "scene/Entity.h"
#include "app/UndoStack.h"

// ---------------------------------------------------------------------------
// 每个 2D 视图的相机状态 + 拖动状态
// ---------------------------------------------------------------------------
struct View2D
{
    float centerX = 0.0f;
    float centerY = 0.0f;
    float zoom    = 0.5f;

    int axisMapX  = 0;
    int axisMapY  = 2;
    int axisDepth = 1;

    // 拖动状态：brush
    bool   bDraggingBrush       = false;
    int    iDraggingBrush       = -1;
    float  fDragStartWorldX     = 0.0f;
    float  fDragStartWorldY     = 0.0f;
    glm::vec3 vecDragStartBrushPos;

    // 拖动状态：entity
    bool   bDraggingEntity      = false;
    int    iDraggingEntity      = -1;
    glm::vec3 vecDragStartEntityPos;
    glm::vec3 vecDragStartBBoxMins;
    glm::vec3 vecDragStartBBoxMaxs;

    // 缩放状态 entity
    bool   bResizingEntity      = false;
    int    iResizingEntity      = -1;
    int    iResizeEntityHandle  = -1;
    glm::vec3 vecResizeStartMins;
    glm::vec3 vecResizeStartMaxs;

    // 手柄缩放状态 (Brush)
    bool   bResizingBrush       = false;
    int    iResizingBrush       = -1;
    int    iResizeHandle        = -1;
    float  fResizeFixedMinX     = 0.0f;
    float  fResizeFixedMaxX     = 0.0f;
    float  fResizeFixedMinY     = 0.0f;
    float  fResizeFixedMaxY     = 0.0f;

    // 画框创建状态
    bool   bBoxCreating       = false;
    float  fBoxCreateStartX   = 0.0f;
    float  fBoxCreateStartY   = 0.0f;
    float  fBoxCreateEndX     = 0.0f;
    float  fBoxCreateEndY     = 0.0f;

    bool   bPushedDragSnapshot  = false;

    View2D() = default;
    View2D(float cx, float cy, float z, int ax, int ay, int ad)
        : centerX(cx), centerY(cy), zoom(z)
        , axisMapX(ax), axisMapY(ay), axisDepth(ad)
    {}
};

// 2D 视图专用的 UI 状态机 —— 每个视图独立一份！
struct View2DUIState {
    bool isDragging = false;
    enum Mode { NONE, DRAG_BRUSH, DRAG_ENTITY, RESIZE_BRUSH, RESIZE_ENTITY, BOX_CREATE } mode = NONE;
};

// 每个视图独立的状态，解决"只有 Side View 能拖"的 bug
static View2DUIState s_topUI;
static View2DUIState s_frontUI;
static View2DUIState s_sideUI;

static View2D s_topView(   0, 0, 0.5f, 0, 2, 1);
static View2D s_frontView( 0, 0, 0.5f, 0, 1, 2);
static View2D s_sideView(  0, 0, 0.5f, 2, 1, 0);

//-----------------------------------------------------------------------------
static ImVec2 WorldToScreen2D(float wx, float wy, const View2D& view,
                               const ImVec2& canvasPos, const ImVec2& canvasSize)
{
    float screenX = canvasPos.x + canvasSize.x * 0.5f + (wx - view.centerX) * view.zoom;
    float screenY = canvasPos.y + canvasSize.y * 0.5f - (wy - view.centerY) * view.zoom;
    return ImVec2(screenX, screenY);
}

static void ScreenToWorld2D(const ImVec2& screen, const View2D& view,
                            const ImVec2& canvasPos, const ImVec2& canvasSize,
                            float& outWX, float& outWY)
{
    outWX = view.centerX + (screen.x - canvasPos.x - canvasSize.x * 0.5f) / view.zoom;
    outWY = view.centerY - (screen.y - canvasPos.y - canvasSize.y * 0.5f) / view.zoom;
}

//-----------------------------------------------------------------------------
static void GetBrush2DRange(const CBrush& b, const View2D& view,
                            float& outMinX, float& outMaxX, float& outMinY, float& outMaxY)
{
    float mins[3] = { b.m_vecPos.x - b.m_vecSize.x, b.m_vecPos.y - b.m_vecSize.y, b.m_vecPos.z - b.m_vecSize.z };
    float maxs[3] = { b.m_vecPos.x + b.m_vecSize.x, b.m_vecPos.y + b.m_vecSize.y, b.m_vecPos.z + b.m_vecSize.z };
    outMinX = mins[view.axisMapX]; outMaxX = maxs[view.axisMapX];
    outMinY = mins[view.axisMapY]; outMaxY = maxs[view.axisMapY];
}

static void GetEntity2DRange(const CEntity& e, const View2D& view,
                             float& outMinX, float& outMaxX, float& outMinY, float& outMaxY)
{
    float mins[3] = { e.m_vecBBoxMins.x, e.m_vecBBoxMins.y, e.m_vecBBoxMins.z };
    float maxs[3] = { e.m_vecBBoxMaxs.x, e.m_vecBBoxMaxs.y, e.m_vecBBoxMaxs.z };
    outMinX = mins[view.axisMapX]; outMaxX = maxs[view.axisMapX];
    outMinY = mins[view.axisMapY]; outMaxY = maxs[view.axisMapY];
}

//-----------------------------------------------------------------------------
static void SetGlmAxis(glm::vec3& v, int axis, float value)
{
    if (axis == 0) v.x = value; else if (axis == 1) v.y = value; else v.z = value;
}
static float GetGlmAxis(const glm::vec3& v, int axis)
{
    if (axis == 0) return v.x; if (axis == 1) return v.y; return v.z;
}

//-----------------------------------------------------------------------------
static void GetBrushHandles(const CBrush& b, const View2D& view, float outHandles[8][2])
{
    float minX, maxX, minY, maxY;
    GetBrush2DRange(b, view, minX, maxX, minY, maxY);
    float midX = (minX + maxX) * 0.5f, midY = (minY + maxY) * 0.5f;

    outHandles[0][0] = minX; outHandles[0][1] = maxY;
    outHandles[1][0] = maxX; outHandles[1][1] = maxY;
    outHandles[2][0] = maxX; outHandles[2][1] = minY;
    outHandles[3][0] = minX; outHandles[3][1] = minY;
    outHandles[4][0] = midX; outHandles[4][1] = maxY;
    outHandles[5][0] = maxX; outHandles[5][1] = midY;
    outHandles[6][0] = midX; outHandles[6][1] = minY;
    outHandles[7][0] = minX; outHandles[7][1] = midY;
}

static void GetEntityHandles(const CEntity& e, const View2D& view, float outHandles[8][2])
{
    float minX, maxX, minY, maxY;
    GetEntity2DRange(e, view, minX, maxX, minY, maxY);
    float midX = (minX + maxX) * 0.5f, midY = (minY + maxY) * 0.5f;

    outHandles[0][0] = minX; outHandles[0][1] = maxY;
    outHandles[1][0] = maxX; outHandles[1][1] = maxY;
    outHandles[2][0] = maxX; outHandles[2][1] = minY;
    outHandles[3][0] = minX; outHandles[3][1] = minY;
    outHandles[4][0] = midX; outHandles[4][1] = maxY;
    outHandles[5][0] = maxX; outHandles[5][1] = midY;
    outHandles[6][0] = midX; outHandles[6][1] = minY;
    outHandles[7][0] = minX; outHandles[7][1] = midY;
}

static void DrawGrid2D(ImDrawList* draw, const View2D& view, const ImVec2& canvasPos, const ImVec2& canvasSize)
{
    if (g_flGridSize <= 0.0f) return;

    float worldMinX, worldMaxY;
    ScreenToWorld2D(canvasPos, view, canvasPos, canvasSize, worldMinX, worldMaxY);
    float worldMaxX, worldMinY;
    ScreenToWorld2D(ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                    view, canvasPos, canvasSize, worldMaxX, worldMinY);

    float gridPixels = g_flGridSize * view.zoom;
    if (gridPixels < 4.0f) return;

    ImU32 gridColor = IM_COL32(60, 60, 60, 255);

    for (float x = floorf(worldMinX / g_flGridSize) * g_flGridSize; x <= worldMaxX; x += g_flGridSize) {
        draw->AddLine(WorldToScreen2D(x, worldMinY, view, canvasPos, canvasSize),
                      WorldToScreen2D(x, worldMaxY, view, canvasPos, canvasSize), gridColor);
    }
    for (float y = floorf(worldMinY / g_flGridSize) * g_flGridSize; y <= worldMaxY; y += g_flGridSize) {
        draw->AddLine(WorldToScreen2D(worldMinX, y, view, canvasPos, canvasSize),
                      WorldToScreen2D(worldMaxX, y, view, canvasPos, canvasSize), gridColor);
    }

    ImVec2 origin = WorldToScreen2D(0, 0, view, canvasPos, canvasSize);
    draw->AddLine(ImVec2(origin.x - 12, origin.y), ImVec2(origin.x + 12, origin.y), IM_COL32(255, 255, 255, 128));
    draw->AddLine(ImVec2(origin.x, origin.y - 12), ImVec2(origin.x, origin.y + 12), IM_COL32(255, 255, 255, 128));
}

static void DrawBrushes2D(ImDrawList* draw, const View2D& view, const ImVec2& canvasPos, const ImVec2& canvasSize)
{
    for (size_t i = 0; i < g_brushes.size(); i++) {
        const CBrush& b = g_brushes[i];
        float minX, maxX, minY, maxY;
        GetBrush2DRange(b, view, minX, maxX, minY, maxY);

        ImVec2 p1 = WorldToScreen2D(minX, minY, view, canvasPos, canvasSize);
        ImVec2 p2 = WorldToScreen2D(maxX, maxY, view, canvasPos, canvasSize);
        ImVec2 minP(std::min(p1.x, p2.x), std::min(p1.y, p2.y)), maxP(std::max(p1.x, p2.x), std::max(p1.y, p2.y));

        if (maxP.x - minP.x < 4.0f) maxP.x = minP.x + 4.0f;
        if (maxP.y - minP.y < 4.0f) maxP.y = minP.y + 4.0f;

        bool bSelected = ((int)i == g_iSelectedBrush);
        ImU32 color = bSelected ? IM_COL32(255, 255, 0, 255) : IM_COL32(200, 200, 200, 255);

        draw->AddRectFilled(minP, maxP, IM_COL32(120, 120, 120, 80));
        draw->AddRect(minP, maxP, color, 0.0f, 0, bSelected ? 2.0f : 1.0f);
    }

    if (g_iSelectedBrush >= 0 && g_iSelectedBrush < (int)g_brushes.size()) {
        float handles[8][2];
        GetBrushHandles(g_brushes[g_iSelectedBrush], view, handles);
        for (int h = 0; h < 8; h++) {
            ImVec2 p = WorldToScreen2D(handles[h][0], handles[h][1], view, canvasPos, canvasSize);
            bool hl = (view.bResizingBrush && view.iResizeHandle == h);
            ImU32 fill = hl ? IM_COL32(255, 200, 0, 255) : IM_COL32(255, 255, 0, 255);
            ImVec2 hMin(p.x - 4.0f, p.y - 4.0f), hMax(p.x + 4.0f, p.y + 4.0f);
            draw->AddRectFilled(hMin, hMax, fill);
            draw->AddRect(hMin, hMax, IM_COL32(0, 0, 0, 255));
        }
    }
}

static void DrawEntities2D(ImDrawList* draw, const View2D& view, const ImVec2& canvasPos, const ImVec2& canvasSize)
{
    for (size_t i = 0; i < g_entities.size(); i++) {
        const CEntity& e = g_entities[i];
        bool bSelected = ((int)i == g_iSelectedEntity);
        ImU32 color = bSelected ? IM_COL32(255, 255, 0, 255) : IM_COL32(0, 200, 0, 255);

        float minX, maxX, minY, maxY;
        GetEntity2DRange(e, view, minX, maxX, minY, maxY);
        ImVec2 p1 = WorldToScreen2D(minX, minY, view, canvasPos, canvasSize);
        ImVec2 p2 = WorldToScreen2D(maxX, maxY, view, canvasPos, canvasSize);
        ImVec2 minP(std::min(p1.x, p2.x), std::min(p1.y, p2.y)), maxP(std::max(p1.x, p2.x), std::max(p1.y, p2.y));

        if (maxP.x - minP.x < 4.0f) maxP.x = minP.x + 4.0f;
        if (maxP.y - minP.y < 4.0f) maxP.y = minP.y + 4.0f;

        if (e.m_szName.find("trigger") != std::string::npos) {
            color = bSelected ? IM_COL32(255, 255, 0, 255) : IM_COL32(255, 100, 100, 255);
        }

        draw->AddRect(minP, maxP, color, 0.0f, 0, bSelected ? 2.0f : 1.0f);

        float wx = e.m_vecPos[view.axisMapX];
        float wy = e.m_vecPos[view.axisMapY];
        ImVec2 p = WorldToScreen2D(wx, wy, view, canvasPos, canvasSize);
        draw->AddCircle(p, 5.0f, color, 0, 1.5f);
        draw->AddLine(ImVec2(p.x - 8, p.y), ImVec2(p.x + 8, p.y), color);
        draw->AddLine(ImVec2(p.x, p.y - 8), ImVec2(p.x, p.y + 8), color);
    }

    if (g_iSelectedEntity >= 0 && g_iSelectedEntity < (int)g_entities.size()) {
        float handles[8][2];
        GetEntityHandles(g_entities[g_iSelectedEntity], view, handles);
        for (int h = 0; h < 8; h++) {
            ImVec2 p = WorldToScreen2D(handles[h][0], handles[h][1], view, canvasPos, canvasSize);
            bool hl = (view.bResizingEntity && view.iResizeEntityHandle == h);
            ImU32 fill = hl ? IM_COL32(255, 200, 0, 255) : IM_COL32(255, 255, 0, 255);
            ImVec2 hMin(p.x - 4.0f, p.y - 4.0f), hMax(p.x + 4.0f, p.y + 4.0f);
            draw->AddRectFilled(hMin, hMax, fill);
            draw->AddRect(hMin, hMax, IM_COL32(0, 0, 0, 255));
        }
    }
}

static int HitTestBrushHandles(const CBrush& b, const View2D& view, const ImVec2& mousePos, const ImVec2& canvasPos, const ImVec2& canvasSize)
{
    float handles[8][2];
    GetBrushHandles(b, view, handles);
    for (int h = 0; h < 8; h++) {
        ImVec2 p = WorldToScreen2D(handles[h][0], handles[h][1], view, canvasPos, canvasSize);
        if (mousePos.x >= p.x - 10.0f && mousePos.x <= p.x + 10.0f && mousePos.y >= p.y - 10.0f && mousePos.y <= p.y + 10.0f) return h;
    }
    return -1;
}

static int HitTestEntityHandles(const CEntity& e, const View2D& view, const ImVec2& mousePos, const ImVec2& canvasPos, const ImVec2& canvasSize)
{
    float handles[8][2];
    GetEntityHandles(e, view, handles);
    for (int h = 0; h < 8; h++) {
        ImVec2 p = WorldToScreen2D(handles[h][0], handles[h][1], view, canvasPos, canvasSize);
        if (mousePos.x >= p.x - 10.0f && mousePos.x <= p.x + 10.0f && mousePos.y >= p.y - 10.0f && mousePos.y <= p.y + 10.0f) return h;
    }
    return -1;
}

static void UpdateBrushResize(View2D& view, ImGuiIO& io, const ImVec2& canvasPos, const ImVec2& canvasSize)
{
    if (!view.bResizingBrush || view.iResizingBrush < 0 || view.iResizeHandle < 0) return;

    float wx, wy;
    ScreenToWorld2D(io.MousePos, view, canvasPos, canvasSize, wx, wy);

    if (!view.bPushedDragSnapshot) {
        UndoStack::Instance().PushSnapshot("2D Resize Brush");
        view.bPushedDragSnapshot = true;
    }

    CBrush& brush = g_brushes[view.iResizingBrush];
    float newMinX = view.fResizeFixedMinX, newMaxX = view.fResizeFixedMaxX;
    float newMinY = view.fResizeFixedMinY, newMaxY = view.fResizeFixedMaxY;
    int h = view.iResizeHandle;

    if (h == 0 || h == 3 || h == 7) newMinX = wx;
    if (h == 1 || h == 2 || h == 5) newMaxX = wx;
    if (h == 0 || h == 1 || h == 4) newMaxY = wy;
    if (h == 2 || h == 3 || h == 6) newMinY = wy;

    if (g_flGridSize > 0.0f) {
        if (h == 0 || h == 3 || h == 7) newMinX = roundf(newMinX / g_flGridSize) * g_flGridSize;
        if (h == 1 || h == 2 || h == 5) newMaxX = roundf(newMaxX / g_flGridSize) * g_flGridSize;
        if (h == 0 || h == 1 || h == 4) newMaxY = roundf(newMaxY / g_flGridSize) * g_flGridSize;
        if (h == 2 || h == 3 || h == 6) newMinY = roundf(newMinY / g_flGridSize) * g_flGridSize;
    }

    if (newMaxX - newMinX < 1.0f || newMaxY - newMinY < 1.0f) return;

    float newCenterX = (newMinX + newMaxX) * 0.5f, newHalfX = (newMaxX - newMinX) * 0.5f;
    float newCenterY = (newMinY + newMaxY) * 0.5f, newHalfY = (newMaxY - newMinY) * 0.5f;

    glm::vec3 oldPos = brush.m_vecPos, oldSize = brush.m_vecSize;
    SetGlmAxis(brush.m_vecPos, view.axisMapX, newCenterX); SetGlmAxis(brush.m_vecSize, view.axisMapX, newHalfX);
    SetGlmAxis(brush.m_vecPos, view.axisMapY, newCenterY); SetGlmAxis(brush.m_vecSize, view.axisMapY, newHalfY);

    glm::vec3 delta = brush.m_vecPos - oldPos;
    glm::vec3 scale;
    scale.x = (oldSize.x > 0.0001f) ? (brush.m_vecSize.x / oldSize.x) : 1.0f;
    scale.y = (oldSize.y > 0.0001f) ? (brush.m_vecSize.y / oldSize.y) : 1.0f;
    scale.z = (oldSize.z > 0.0001f) ? (brush.m_vecSize.z / oldSize.z) : 1.0f;

    for (auto& v : brush.m_vertices) {
        glm::vec3 local = v - oldPos;
        local *= scale;
        v = oldPos + local + delta;
    }
    brush.RecalcFaceNormals();
}

static void UpdateEntityResize(View2D& view, ImGuiIO& io, const ImVec2& canvasPos, const ImVec2& canvasSize)
{
    if (!view.bResizingEntity || view.iResizingEntity < 0 || view.iResizeEntityHandle < 0) return;

    if (!view.bPushedDragSnapshot) {
        UndoStack::Instance().PushSnapshot("2D Resize Entity");
        view.bPushedDragSnapshot = true;
    }

    CEntity& ent = g_entities[view.iResizingEntity];

    float dxWorld = io.MouseDelta.x / view.zoom;
    float dyWorld = -io.MouseDelta.y / view.zoom;

    int h = view.iResizeEntityHandle;

    if (h == 0 || h == 3 || h == 7) SetGlmAxis(ent.m_vecBBoxMins, view.axisMapX, GetGlmAxis(ent.m_vecBBoxMins, view.axisMapX) + dxWorld);
    if (h == 1 || h == 2 || h == 5) SetGlmAxis(ent.m_vecBBoxMaxs, view.axisMapX, GetGlmAxis(ent.m_vecBBoxMaxs, view.axisMapX) + dxWorld);

    if (h == 0 || h == 1 || h == 4) SetGlmAxis(ent.m_vecBBoxMaxs, view.axisMapY, GetGlmAxis(ent.m_vecBBoxMaxs, view.axisMapY) + dyWorld);
    if (h == 2 || h == 3 || h == 6) SetGlmAxis(ent.m_vecBBoxMins, view.axisMapY, GetGlmAxis(ent.m_vecBBoxMins, view.axisMapY) + dyWorld);

    for(int i=0; i<3; i++) {
        if (ent.m_vecBBoxMins[i] > ent.m_vecBBoxMaxs[i]) std::swap(ent.m_vecBBoxMins[i], ent.m_vecBBoxMaxs[i]);
    }

    ent.m_vecPos = (ent.m_vecBBoxMins + ent.m_vecBBoxMaxs) * 0.5f;
}

static void CreateBrushFromBox(View2D& view)
{
    float minX = std::min(view.fBoxCreateStartX, view.fBoxCreateEndX), maxX = std::max(view.fBoxCreateStartX, view.fBoxCreateEndX);
    float minY = std::min(view.fBoxCreateStartY, view.fBoxCreateEndY), maxY = std::max(view.fBoxCreateStartY, view.fBoxCreateEndY);
    if ((maxX - minX) < 1.0f || (maxY - minY) < 1.0f) return;

    if (g_flGridSize > 0.0f) {
        minX = roundf(minX / g_flGridSize) * g_flGridSize; maxX = roundf(maxX / g_flGridSize) * g_flGridSize;
        minY = roundf(minY / g_flGridSize) * g_flGridSize; maxY = roundf(maxY / g_flGridSize) * g_flGridSize;
    }

    float depthMin = -8.0f, depthMax = 8.0f;
    if (g_iSelectedBrush >= 0 && g_iSelectedBrush < (int)g_brushes.size()) {
        const CBrush& sel = g_brushes[g_iSelectedBrush];
        float selMin[3] = { sel.m_vecPos.x - sel.m_vecSize.x, sel.m_vecPos.y - sel.m_vecSize.y, sel.m_vecPos.z - sel.m_vecSize.z };
        float selMax[3] = { sel.m_vecPos.x + sel.m_vecSize.x, sel.m_vecPos.y + sel.m_vecSize.y, sel.m_vecPos.z + sel.m_vecSize.z };
        depthMin = selMin[view.axisDepth]; depthMax = selMax[view.axisDepth];
    }

    float minCoords[3], maxCoords[3];
    minCoords[view.axisMapX] = minX; maxCoords[view.axisMapX] = maxX;
    minCoords[view.axisMapY] = minY; maxCoords[view.axisMapY] = maxY;
    minCoords[view.axisDepth] = depthMin; maxCoords[view.axisDepth] = depthMax;

    CBrush newBrush;
    newBrush.m_vecPos = glm::vec3((minCoords[0]+maxCoords[0])*0.5f, (minCoords[1]+maxCoords[1])*0.5f, (minCoords[2]+maxCoords[2])*0.5f);
    newBrush.m_vecSize = glm::vec3((maxCoords[0]-minCoords[0])*0.5f, (maxCoords[1]-minCoords[1])*0.5f, (maxCoords[2]-minCoords[2])*0.5f);
    newBrush.m_szMaterial = "DEV/DEV_MEASUREGENERIC01";
    newBrush.RebuildPrimitiveMesh();

    UndoStack::Instance().PushSnapshot("Create Brush (2D Block)");
    g_brushes.push_back(newBrush);
    g_iSelectedBrush = (int)g_brushes.size() - 1;
    g_iSelectedEntity = -1;
}

// 拖拽开始时判定选择目标的辅助函数。返回 true 表示已经开始某种拖拽。
static bool BeginDragOnClick(View2D& view, View2DUIState& uiState,
                             ImGuiIO& io, const ImVec2& canvasPos, const ImVec2& canvasSize,
                             float wx, float wy)
{
    if (g_bBlockToolActive) {
        view.bBoxCreating = true;
        view.fBoxCreateStartX = view.fBoxCreateEndX = wx;
        view.fBoxCreateStartY = view.fBoxCreateEndY = wy;
        uiState.isDragging = true;
        uiState.mode = View2DUIState::BOX_CREATE;
        return true;
    }

    // 优先判定 Entity 手柄
    if (g_iSelectedEntity >= 0 && g_iSelectedEntity < (int)g_entities.size()) {
        int handle = HitTestEntityHandles(g_entities[g_iSelectedEntity], view, io.MousePos, canvasPos, canvasSize);
        if (handle >= 0) {
            view.bResizingEntity = true; view.iResizingEntity = g_iSelectedEntity; view.iResizeEntityHandle = handle;
            view.bPushedDragSnapshot = false;
            view.vecResizeStartMins = g_entities[g_iSelectedEntity].m_vecBBoxMins;
            view.vecResizeStartMaxs = g_entities[g_iSelectedEntity].m_vecBBoxMaxs;
            uiState.isDragging = true;
            uiState.mode = View2DUIState::RESIZE_ENTITY;
            return true;
        }
    }

    // 其次判定 Brush 手柄
    if (g_iSelectedBrush >= 0 && g_iSelectedBrush < (int)g_brushes.size()) {
        int handle = HitTestBrushHandles(g_brushes[g_iSelectedBrush], view, io.MousePos, canvasPos, canvasSize);
        if (handle >= 0) {
            view.bResizingBrush = true; view.iResizingBrush = g_iSelectedBrush; view.iResizeHandle = handle;
            view.bPushedDragSnapshot = false;
            GetBrush2DRange(g_brushes[view.iResizingBrush], view, view.fResizeFixedMinX, view.fResizeFixedMaxX, view.fResizeFixedMinY, view.fResizeFixedMaxY);
            uiState.isDragging = true;
            uiState.mode = View2DUIState::RESIZE_BRUSH;
            return true;
        }
    }

    // 再次判定实体身体
    {
        int hitEntity = -1;
        for (int i = (int)g_entities.size() - 1; i >= 0; i--) {
            float minX, maxX, minY, maxY; GetEntity2DRange(g_entities[i], view, minX, maxX, minY, maxY);
            if (wx >= minX && wx <= maxX && wy >= minY && wy <= maxY) { hitEntity = i; break; }
        }
        if (hitEntity >= 0) {
            g_iSelectedEntity = hitEntity; g_iSelectedBrush = -1;
            view.bDraggingEntity = true; view.iDraggingEntity = hitEntity;
            view.fDragStartWorldX = wx; view.fDragStartWorldY = wy;
            view.vecDragStartEntityPos = g_entities[hitEntity].m_vecPos; view.bPushedDragSnapshot = false;
            view.vecDragStartBBoxMins = g_entities[hitEntity].m_vecBBoxMins;
            view.vecDragStartBBoxMaxs = g_entities[hitEntity].m_vecBBoxMaxs;
            uiState.isDragging = true;
            uiState.mode = View2DUIState::DRAG_ENTITY;
            return true;
        }
    }

    // 最后判定 Brush 身体
    {
        int hitBrush = -1;
        for (int i = (int)g_brushes.size() - 1; i >= 0; i--) {
            float minX, maxX, minY, maxY; GetBrush2DRange(g_brushes[i], view, minX, maxX, minY, maxY);
            if (wx >= minX && wx <= maxX && wy >= minY && wy <= maxY) { hitBrush = i; break; }
        }
        if (hitBrush >= 0) {
            g_iSelectedBrush = hitBrush; g_iSelectedEntity = -1;
            view.bDraggingBrush = true; view.iDraggingBrush = hitBrush;
            view.fDragStartWorldX = wx; view.fDragStartWorldY = wy;
            view.vecDragStartBrushPos = g_brushes[hitBrush].m_vecPos; view.bPushedDragSnapshot = false;
            uiState.isDragging = true;
            uiState.mode = View2DUIState::DRAG_BRUSH;
            return true;
        }
    }

    // 点空白 —— 取消选择（不进入拖拽状态）
    g_iSelectedBrush = -1; g_iSelectedEntity = -1;
    return false;
}

// 拖动中的推进逻辑。返回 true 表示拖拽刚结束。
static bool TickDrag(View2D& view, View2DUIState& uiState,
                     ImGuiIO& io, const ImVec2& canvasPos, const ImVec2& canvasSize)
{
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (uiState.mode == View2DUIState::BOX_CREATE) {
            CreateBrushFromBox(view);
            view.bBoxCreating = false;
        }
        view.bDraggingBrush = false; view.iDraggingBrush = -1;
        view.bDraggingEntity = false; view.iDraggingEntity = -1;
        view.bResizingBrush = false; view.iResizingBrush = -1; view.iResizeHandle = -1;
        view.bResizingEntity = false; view.iResizingEntity = -1; view.iResizeEntityHandle = -1;
        view.bPushedDragSnapshot = false;
        uiState.isDragging = false;
        uiState.mode = View2DUIState::NONE;
        return true;
    }

    if (uiState.mode == View2DUIState::RESIZE_BRUSH) {
        UpdateBrushResize(view, io, canvasPos, canvasSize);
    } else if (uiState.mode == View2DUIState::RESIZE_ENTITY) {
        UpdateEntityResize(view, io, canvasPos, canvasSize);
    } else if (uiState.mode == View2DUIState::BOX_CREATE) {
        ScreenToWorld2D(io.MousePos, view, canvasPos, canvasSize, view.fBoxCreateEndX, view.fBoxCreateEndY);
    } else {
        bool bAnyDragging = view.bDraggingBrush || view.bDraggingEntity;
        if (bAnyDragging) {
            float wx, wy; ScreenToWorld2D(io.MousePos, view, canvasPos, canvasSize, wx, wy);
            float deltaX = wx - view.fDragStartWorldX, deltaY = wy - view.fDragStartWorldY;
            if (!view.bPushedDragSnapshot && (fabsf(deltaX) > 0.01f || fabsf(deltaY) > 0.01f)) {
                UndoStack::Instance().PushSnapshot(view.bDraggingBrush ? "2D Drag Brush" : "2D Drag Entity");
                view.bPushedDragSnapshot = true;
            }
            if (view.bDraggingBrush) {
                CBrush& brush = g_brushes[view.iDraggingBrush];
                glm::vec3 newPos = view.vecDragStartBrushPos;
                SetGlmAxis(newPos, view.axisMapX, GetGlmAxis(view.vecDragStartBrushPos, view.axisMapX) + deltaX);
                SetGlmAxis(newPos, view.axisMapY, GetGlmAxis(view.vecDragStartBrushPos, view.axisMapY) + deltaY);
                if (g_flGridSize > 0.0f) {
                    SetGlmAxis(newPos, view.axisMapX, roundf(GetGlmAxis(newPos, view.axisMapX) / g_flGridSize) * g_flGridSize);
                    SetGlmAxis(newPos, view.axisMapY, roundf(GetGlmAxis(newPos, view.axisMapY) / g_flGridSize) * g_flGridSize);
                }
                glm::vec3 offset = newPos - brush.m_vecPos;
                for (auto& v : brush.m_vertices) v += offset;
                brush.m_vecPos = newPos;
            } else if (view.bDraggingEntity) {
                CEntity& ent = g_entities[view.iDraggingEntity];
                glm::vec3 newPos = view.vecDragStartEntityPos;
                SetGlmAxis(newPos, view.axisMapX, GetGlmAxis(view.vecDragStartEntityPos, view.axisMapX) + deltaX);
                SetGlmAxis(newPos, view.axisMapY, GetGlmAxis(view.vecDragStartEntityPos, view.axisMapY) + deltaY);
                if (g_flGridSize > 0.0f) {
                    SetGlmAxis(newPos, view.axisMapX, roundf(GetGlmAxis(newPos, view.axisMapX) / g_flGridSize) * g_flGridSize);
                    SetGlmAxis(newPos, view.axisMapY, roundf(GetGlmAxis(newPos, view.axisMapY) / g_flGridSize) * g_flGridSize);
                }
                glm::vec3 offset = newPos - ent.m_vecPos;
                ent.m_vecPos = newPos;
                ent.m_vecBBoxMins += offset;
                ent.m_vecBBoxMaxs += offset;
            }
        }
    }
    return false;
}

static void Draw2DViewCommon(const char* name, View2D& view, View2DUIState& uiState)
{
    if (!ImGui::Begin(name)) { ImGui::End(); return; }
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    if (canvasSize.x < 1.0f || canvasSize.y < 1.0f) { ImGui::End(); return; }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(25, 25, 25, 255));

    ImGuiIO& io = ImGui::GetIO();

    // 固定位置放置 InvisibleButton（每帧都执行，保证 hover 稳定）
    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::InvisibleButton("##canvas", canvasSize,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    bool bHovered = ImGui::IsItemHovered();

    // -------- 1. 未在拖拽：处理 hover 上的滚轮、平移、点击起拖 --------
    if (!uiState.isDragging && bHovered) {
        if (io.MouseWheel != 0.0f) {
            float wxBefore, wyBefore;
            ScreenToWorld2D(io.MousePos, view, canvasPos, canvasSize, wxBefore, wyBefore);
            view.zoom *= (1.0f + io.MouseWheel * 0.15f);
            view.zoom = glm::clamp(view.zoom, 0.01f, 20.0f);
            float wxAfter, wyAfter;
            ScreenToWorld2D(io.MousePos, view, canvasPos, canvasSize, wxAfter, wyAfter);
            view.centerX += wxBefore - wxAfter; view.centerY += wyBefore - wyAfter;
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            view.centerX -= io.MouseDelta.x / view.zoom; view.centerY += io.MouseDelta.y / view.zoom;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            float wx, wy;
            ScreenToWorld2D(io.MousePos, view, canvasPos, canvasSize, wx, wy);
            BeginDragOnClick(view, uiState, io, canvasPos, canvasSize, wx, wy);
        }
    }

    // -------- 2. 正在拖拽：不管 hover 与否都推进 --------
    if (uiState.isDragging) {
        TickDrag(view, uiState, io, canvasPos, canvasSize);
    }

    // -------- 3. 绘制 --------
    DrawGrid2D(draw, view, canvasPos, canvasSize);
    DrawBrushes2D(draw, view, canvasPos, canvasSize);
    DrawEntities2D(draw, view, canvasPos, canvasSize);

    if (view.bBoxCreating) {
        ImVec2 p1 = WorldToScreen2D(view.fBoxCreateStartX, view.fBoxCreateStartY, view, canvasPos, canvasSize);
        ImVec2 p2 = WorldToScreen2D(view.fBoxCreateEndX, view.fBoxCreateEndY, view, canvasPos, canvasSize);
        ImVec2 minP(std::min(p1.x, p2.x), std::min(p1.y, p2.y)), maxP(std::max(p1.x, p2.x), std::max(p1.y, p2.y));
        draw->AddRectFilled(minP, maxP, IM_COL32(100, 150, 255, 60));
        draw->AddRect(minP, maxP, IM_COL32(100, 150, 255, 255), 0.0f, 0, 2.0f);
    }

    ImGui::End();
}

void DrawTopView()   { Draw2DViewCommon("Top View",   s_topView,   s_topUI);   }
void DrawFrontView() { Draw2DViewCommon("Front View", s_frontView, s_frontUI); }
void DrawSideView()  { Draw2DViewCommon("Side View",  s_sideView,  s_sideUI);  }

void Draw2DViews()
{
    DrawTopView();
    DrawFrontView();
    DrawSideView();
}