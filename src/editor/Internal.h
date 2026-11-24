#pragma once
#include <string>

// 从 main.cpp 借来的接口
extern void SetFlyMode(bool enable);
extern void CheckNavCollisions();
extern glm::vec3* GetEditorCamTarget();
extern float* GetEditorCamDist();

// 材质浏览器状态
extern bool g_bShowMaterialBrowser;
extern std::string* g_pTargetMaterial;
extern std::string g_selectedMaterialTemp;
extern std::string g_selectedFaceMaterialTemp;

// 共用的辅助函数
extern bool InputTextString(const char* label, std::string& value);
extern std::string GetEntitySearchString(const CEntity& e);

// 菜单栏回调
extern void (*s_pfnExportMap)();
extern void (*s_pfnImportMap)();
extern void (*s_pfnCompileAndRun)();
extern void (*s_pfnLoadBvh)();