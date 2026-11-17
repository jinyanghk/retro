// src/editor/EditorPanels.h
#pragma once

#include <string>
#include "Entity.h"
#include "Brush.h"

struct BrushPanelResult {
    bool bRequestNew = false;
    bool bRequestDelete = false;
    int  iTargetIndex = -1;
    bool bRequestDuplicate = false;

    bool bRequestDeleteEntity = false;
    int  iTargetEntityIndex = -1;

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
