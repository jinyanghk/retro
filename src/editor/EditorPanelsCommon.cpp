#include "EditorPanels.h"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cstring>
#include "imgui.h"
#include "SceneGlobals.h"
#include "Log.h"
#include "EditorPanelsInternal.h"

// 全局回调
void (*s_pfnExportMap)() = nullptr;
void (*s_pfnImportMap)() = nullptr;
void (*s_pfnCompileAndRun)() = nullptr;
void (*s_pfnLoadBvh)() = nullptr;

void SetExportMapHandler(void (*pfn)()) { s_pfnExportMap = pfn; }
void SetImportMapHandler(void (*pfn)()) { s_pfnImportMap = pfn; }
void SetCompileAndRunHandler(void (*pfn)()) { s_pfnCompileAndRun = pfn; }
void SetLoadBvhHandler(void (*pfn)()) { s_pfnLoadBvh = pfn; }

// 材质浏览器的共用状态
bool   g_bShowMaterialBrowser = false;
std::string* g_pTargetMaterial = nullptr;
char   g_matBrowserFilter[128] = "";
std::string g_selectedMaterialTemp;
std::string g_selectedFaceMaterialTemp;

// 工具函数（原文件里的）
bool InputTextString(const char* label, std::string& value) {
    char buf[512];
    strncpy(buf, value.c_str(), sizeof(buf)-1); buf[sizeof(buf)-1] = 0;
    if (ImGui::InputText(label, buf, sizeof(buf))) { value = buf; return true; }
    return false;
}

std::string GetEntitySearchString(const CEntity& e) {
    std::string result = e.m_szName + " " + std::to_string(e.m_id);
    for (const auto& kv : e.m_properties) result += " " + kv.first + "=" + kv.second;
    for (const auto& kv : e.m_customProperties) result += " " + kv.first + "=" + kv.second;
    return result;
}