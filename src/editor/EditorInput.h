#pragma once
#include <SDL.h>

// 处理 SDL 事件（鼠标、键盘等）
void ProcessInput(SDL_Event& ev, bool& running, bool& ioWantCaptureKeyboard);

// 处理持续按压（WASD 移动、飞行模式等）
void UpdateCameraMovement(float dt, bool ioWantCaptureKeyboard);