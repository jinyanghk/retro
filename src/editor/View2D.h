// src/editor/View2D.h
#pragma once

#include "Panels.h"

// 三个 2D 视图
void DrawTopView(PanelResult& out);
void DrawFrontView(PanelResult& out);
void DrawSideView(PanelResult& out);

// 一次性画三个
void Draw2DViews(PanelResult& out);
