#ifndef SCENE_GLOBALS_H
#define SCENE_GLOBALS_H

#include <glm/glm.hpp>
#include <vector>
#include "imgui.h"      // ★ 必须加这一行
#include "scene/Brush.h"
#include "scene/Entity.h"
#include "ImGuizmo.h"

enum GizmoMode {
    GIZMO_NONE = 0,
    GIZMO_TRANSLATE,
    GIZMO_ROTATE,
    GIZMO_SCALE,
    GIZMO_FACE,
    GIZMO_EDGE,
    GIZMO_VERTEX
};

extern GizmoMode g_iGizmoMode;

enum SubEditMode {
    SUB_NONE = 0,
    SUB_FACE,
    SUB_EDGE,
    SUB_VERTEX
};

extern int   g_iSubEditMode;      // SUB_NONE / SUB_FACE / SUB_EDGE / SUB_VERTEX
extern int   g_iSubEditIndex;     // 选中面/边/点的索引
extern bool  g_bSubDragging;

extern std::vector<CBrush>  g_brushes;
extern std::vector<CEntity> g_entities;

extern int   g_iSelectedBrush;
extern int   g_iSelectedEntity;
extern int   g_iSelectedFace;

// Gizmo 状态（由 ImGuizmo 驱动）
extern ImGuizmo::OPERATION g_CurrentGizmoOperation;
extern ImGuizmo::MODE      g_CurrentGizmoMode;
extern bool                g_bUseSnap;
extern float               g_Snap[3];
extern float               g_Bounds[6];
extern bool                g_bBoundSizing;
extern bool                g_bBoundSizingSnap;
extern float               g_BoundsSnap[3];

extern float g_flGridSize;

extern bool  g_bShowGizmo;
extern bool  g_bXRayGizmo;
extern bool  g_bAutoSaveEnabled;
extern bool  g_bBlockToolActive;

#endif