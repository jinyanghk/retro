#include "scene/SceneGlobals.h"

std::vector<CBrush>  g_brushes;
std::vector<CEntity> g_entities;

int   g_iSelectedBrush  = -1;
int   g_iSelectedEntity = -1;
int   g_iSelectedFace   = 0;

int   g_iGizmoMode = GIZMO_TRANSLATE;
float g_flGridSize = 16.0f;

bool  g_bShowGizmo = true;
bool  g_bXRayGizmo = false;
bool  g_bAutoSaveEnabled = true;
bool  g_bBlockToolActive = false;