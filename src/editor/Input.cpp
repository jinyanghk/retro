#include "Input.h"
#include "Camera.h"
#include "Globals.h"
#include "UndoStack.h"
#include "Log.h"
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>

extern bool g_bFlyMode;
extern bool g_bRightMouseDown;
extern bool g_bLeftMouseDown;
extern bool g_bMiddleMouseDown;
extern int g_iMouseX, g_iMouseY;
extern float g_flGridSize;

// 飞行模式由外部控制，暴露给 EditorPanels 的函数
void SetFlyMode(bool enable) {
    g_bFlyMode = enable;
    if (enable) {
        glm::vec3 eye = g_cam.GetEye();
        float rp = glm::radians(g_cam.pitch), ry = glm::radians(g_cam.yaw);
        g_cam.target = eye + glm::vec3(std::cos(rp)*std::cos(ry), std::sin(rp), std::cos(rp)*std::sin(ry)) * 10.0f;
        g_cam.dist = 0.0f;
        SDL_SetRelativeMouseMode(SDL_TRUE);
    } else {
        g_cam.dist = 200.0f;
        SDL_SetRelativeMouseMode(SDL_FALSE);
    }
}

void ProcessInput(SDL_Event& ev, bool& running, bool& ioWantCaptureKeyboard) {
    if (ev.type == SDL_QUIT) running = false;
    if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_RESIZED) {
        // 如果需要处理大小变化，可以在这里写
    }
    if (ev.type == SDL_MOUSEMOTION) {
        g_iMouseX = ev.motion.x; g_iMouseY = ev.motion.y;
        if (g_bFlyMode) {
            g_cam.yaw += ev.motion.xrel * 0.15f; g_cam.pitch += ev.motion.yrel * 0.15f;
            g_cam.pitch = glm::clamp(g_cam.pitch, -89.0f, 89.0f);
        }
    }
    if (ev.type == SDL_MOUSEBUTTONDOWN) {
        if (ev.button.button == SDL_BUTTON_LEFT)   g_bLeftMouseDown = true;
        if (ev.button.button == SDL_BUTTON_RIGHT)  g_bRightMouseDown = true;
        if (ev.button.button == SDL_BUTTON_MIDDLE) g_bMiddleMouseDown = true;
    }
    if (ev.type == SDL_MOUSEBUTTONUP) {
        if (ev.button.button == SDL_BUTTON_LEFT)   g_bLeftMouseDown = false;
        if (ev.button.button == SDL_BUTTON_RIGHT)  g_bRightMouseDown = false;
        if (ev.button.button == SDL_BUTTON_MIDDLE) g_bMiddleMouseDown = false;
    }
    if (ev.type == SDL_KEYDOWN && !ioWantCaptureKeyboard) {
        SDL_Keycode k = ev.key.keysym.sym;
        if (k == SDLK_ESCAPE) { if (g_bFlyMode) SetFlyMode(false); else running = false; }
        else if (k == SDLK_1) g_iGizmoMode = GIZMO_TRANSLATE;
        else if (k == SDLK_2) g_iGizmoMode = GIZMO_ROTATE;
        else if (k == SDLK_3) g_iGizmoMode = GIZMO_SCALE;
        else if (k == SDLK_4) g_iGizmoMode = GIZMO_FACE;
        else if (k == SDLK_5) g_iGizmoMode = GIZMO_EDGE;
        else if (k == SDLK_6) g_iGizmoMode = GIZMO_VERTEX;
        else if (k == SDLK_0) g_iGizmoMode = GIZMO_NONE;
        else if (k == SDLK_z && (ev.key.keysym.mod & KMOD_CTRL)) UndoStack::Instance().Undo();
        else if (k == SDLK_y && (ev.key.keysym.mod & KMOD_CTRL)) UndoStack::Instance().Redo();
        else if (k == SDLK_LEFTBRACKET) g_flGridSize = std::max(1.0f, g_flGridSize*0.5f);
        else if (k == SDLK_RIGHTBRACKET) g_flGridSize = std::min(128.0f, g_flGridSize*2.0f);
    }
}

void UpdateCameraMovement(float dt, bool ioWantCaptureKeyboard) {
    if (ioWantCaptureKeyboard) return;
    
    float flySpeed = g_bFlyMode ? 15.0f : (g_cam.dist * 1.2f * dt);
    float rp = glm::radians(g_cam.pitch), ry = glm::radians(g_cam.yaw);
    glm::vec3 fwd(std::cos(rp)*std::cos(ry), std::sin(rp), std::cos(rp)*std::sin(ry));
    glm::vec3 fwdH(fwd.x, 0, fwd.z); if (glm::length(fwdH) > 1e-4f) fwdH = glm::normalize(fwdH);
    glm::vec3 rightH = glm::normalize(glm::cross(glm::vec3(0, 1, 0), fwdH));
    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    
    if (g_bFlyMode) {
        glm::vec3 delta(0.0f);
        if (ks[SDL_SCANCODE_W]) delta -= fwd * flySpeed;
        if (ks[SDL_SCANCODE_S]) delta += fwd * flySpeed;
        if (ks[SDL_SCANCODE_A]) delta -= rightH * flySpeed;
        if (ks[SDL_SCANCODE_D]) delta += rightH * flySpeed;
        if (ks[SDL_SCANCODE_E]) delta.y += flySpeed;
        if (ks[SDL_SCANCODE_Q]) delta.y -= flySpeed;
        g_cam.target += delta; g_cam.dist = 1.0f;
    } else {
        if (ks[SDL_SCANCODE_W]) g_cam.target -= fwdH * flySpeed;
        if (ks[SDL_SCANCODE_S]) g_cam.target += fwdH * flySpeed;
        if (ks[SDL_SCANCODE_A]) g_cam.target -= rightH * flySpeed;
        if (ks[SDL_SCANCODE_D]) g_cam.target += rightH * flySpeed;
        if (ks[SDL_SCANCODE_E]) g_cam.target.y += flySpeed;
        if (ks[SDL_SCANCODE_Q]) g_cam.target.y -= flySpeed;
        
        if (ks[SDL_SCANCODE_LEFT])  g_cam.yaw -= 1.5f;
        if (ks[SDL_SCANCODE_RIGHT]) g_cam.yaw += 1.5f;
        if (ks[SDL_SCANCODE_UP])    g_cam.pitch -= 1.0f;
        if (ks[SDL_SCANCODE_DOWN])  g_cam.pitch += 1.0f;
        g_cam.pitch = glm::clamp(g_cam.pitch, -89.0f, 89.0f);
    }
}