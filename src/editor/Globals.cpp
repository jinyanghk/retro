#include "Globals.h"

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

EditorTool  g_iCurrentTool = TOOL_SELECT;
std::string g_szPendingEntityClass = "light";

// 👈 新增：根据 Group ID 查找 Index
int FindGroupIndexById(uint64_t groupId) {
    if (groupId == 0) return -1;
    for (int i = 0; i < (int)g_brushGroups.size(); i++) {
        if (g_brushGroups[i].id == groupId) return i;
    }
    return -1;
}

// 👈 新增：判断某个组是否是另一个组的祖先（防成环）
bool IsGroupAncestor(uint64_t ancestorId, uint64_t childId) {
    int curr = FindGroupIndexById(childId);
    while (curr >= 0) {
        if (g_brushGroups[curr].id == ancestorId) return true;
        curr = FindGroupIndexById(g_brushGroups[curr].parentId);
    }
    return false;
}