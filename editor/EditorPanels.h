#ifndef UI_EDITOR_PANELS_H
#define UI_EDITOR_PANELS_H

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
void DrawEntityPanel(CEntity* entities, int count, int* pSelEntity, int* pSelBrush, BrushPanelResult& out);
void DrawBrushPanel(const CBrush* brushes, int count, int* pSelBrush, int* pSelEntity, BrushPanelResult& out);
void DrawConsolePanel();
void DrawViewportPanel();

void SetExportMapHandler(void (*pfn)());
void SetImportMapHandler(void (*pfn)());
void SetCompileAndRunHandler(void (*pfn)());
void OpenCompileOptionsWindow();
void DrawCompileOptionsWindow();

void LogAdd(const char* s);

#endif