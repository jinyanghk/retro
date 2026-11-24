#pragma once

#include <string>
#include <glm/glm.hpp>
#include "Entity.h"
#include "Brush.h"

struct PanelResult {
    bool bRequestNew = false;
    bool bRequestDelete = false;
    uint64_t targetId = 0;
    
    bool bRequestDuplicate = false;
    uint64_t duplicateId = 0;

    bool bRequestDeleteEntity = false;
    int  iTargetEntityIndex = -1;
    uint64_t targetEntityId = 0;

    bool bRequestNewEntity = false;
    std::string szNewEntityClass;
    
    // 2D 视图落点
    glm::vec3 newBrushPos  = glm::vec3(0, 32, 0);
    glm::vec3 newEntityPos = glm::vec3(0, 32, 0);
};

void DrawMenuBar();
void DrawToolsPanel();
void DrawConsolePanel();
void DrawScenePanel(int* pSelEntity, int* pSelBrush, PanelResult& out);
void DrawMaterialBrowserWindow();

void SetExportMapHandler(void (*pfn)());
void SetImportMapHandler(void (*pfn)());
void SetCompileAndRunHandler(void (*pfn)());
void SetLoadBvhHandler(void (*pfn)());
void DrawCompileOptionsWindow();