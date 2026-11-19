#include "SceneGlobals.h"

std::vector<CBrush>  g_brushes;
std::vector<CEntity> g_entities;

int   g_iSelectedBrush  = -1;
int   g_iSelectedEntity = -1;
int   g_iSelectedFace   = 0;

ImGuizmo::OPERATION g_CurrentGizmoOperation = ImGuizmo::TRANSLATE;
ImGuizmo::MODE      g_CurrentGizmoMode      = ImGuizmo::LOCAL;
bool                g_bUseSnap              = true;
float               g_Snap[3]               = { 16.0f, 16.0f, 16.0f };
float               g_Bounds[6]             = { -0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f };
bool                g_bBoundSizing          = false;
bool                g_bBoundSizingSnap      = false;
float               g_BoundsSnap[3]         = { 0.1f, 0.1f, 0.1f };

float g_flGridSize = 16.0f;

bool  g_bShowGizmo = true;
bool  g_bXRayGizmo = false;
bool  g_bAutoSaveEnabled = true;
bool  g_bBlockToolActive = false;

int  g_iSubEditMode  = SUB_NONE;
int  g_iSubEditIndex = -1;
bool g_bSubDragging  = false;

GizmoMode g_iGizmoMode = GIZMO_TRANSLATE;

bool g_bShowNav = false;

bool g_bFlyMode = false;

bool g_bRenderTexture = true;

bool g_bShowBrushes = true;
bool g_bShowEntities = true;

std::vector<BrushGroup> g_brushGroups;
std::vector<uint8_t> g_brushVisible; // 单个 brush 的可见性
std::vector<uint8_t> g_entityVisible; // 单个 entity 的可见性

std::vector<std::string> g_allMaterials;

int g_iSelectedGroup = -1;

uint64_t g_nextUniqueId = 1;
uint64_t GenerateUniqueID() { return g_nextUniqueId++; }