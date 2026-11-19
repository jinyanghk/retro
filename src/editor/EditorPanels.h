// src/editor/EditorPanels.h
#pragma once

#include <string>
#include "Entity.h"
#include "Brush.h"

struct BrushPanelResult {
    bool bRequestNew = false;
    bool bRequestDelete = false;
    uint64_t targetId = 0;         // 👈 新增：要删除/复制的 Brush ID
    int  iTargetIndex = -1;        // 保留以兼容旧代码，但不用了
    
    bool bRequestDuplicate = false;
    uint64_t duplicateId = 0;      // 👈 新增：要复制的 Brush ID

    bool bRequestDeleteEntity = false;
    int  iTargetEntityIndex = -1;
    uint64_t targetEntityId = 0;   // 👈 新增：要删除的 Entity ID

    bool bRequestNewEntity = false;
    std::string szNewEntityClass;
};

void DrawMenuBar();
void DrawToolsPanel();
void DrawConsolePanel();
void DrawViewportPanel();
void DrawScenePanel(int* pSelEntity, int* pSelBrush, BrushPanelResult& out);

void SetExportMapHandler(void (*pfn)());
void SetImportMapHandler(void (*pfn)());
void SetCompileAndRunHandler(void (*pfn)());
void SetLoadBvhHandler(void (*pfn)());
void OpenCompileOptionsWindow();
void DrawCompileOptionsWindow();
void DrawMaterialBrowserWindow();
