#ifndef SCENE_GLOBALS_H
#define SCENE_GLOBALS_H

#include <glm/glm.hpp>
#include <vector>
#include "scene/Brush.h"
#include "scene/Entity.h"

enum GizmoMode {
    GIZMO_NONE = 0,
    GIZMO_TRANSLATE,
    GIZMO_ROTATE,
    GIZMO_SCALE,
    GIZMO_FACE,
    GIZMO_EDGE,
    GIZMO_VERTEX,
};

extern std::vector<CBrush>  g_brushes;
extern std::vector<CEntity> g_entities;

extern int   g_iSelectedBrush;
extern int   g_iSelectedEntity;
extern int   g_iSelectedFace;

extern int   g_iGizmoMode;
extern float g_flGridSize;

extern bool  g_bShowGizmo;
extern bool  g_bXRayGizmo;
extern bool  g_bAutoSaveEnabled;
extern bool  g_bBlockToolActive;

#endif