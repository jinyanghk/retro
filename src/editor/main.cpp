#include <GL/glew.h>
#include <SDL.h>
#include <SDL_opengl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include "ImGuizmo.h"

#include "SceneGlobals.h"
#include "Entity.h"
#include "UndoStack.h"
#include "EditorLayout.h"
#include "GameDatabase.h"
#include "EditorPanels.h"
#include "View2D.h"
#include "MapIO.h"
#include "version.h"
#include "Log.h"
#include "FileSystem.h"
#include "MapLoader.h"
#include "TextureCache.h"
#include "BVH.h"

// 引入拆分出的模块
#include "EditorGL.h"
#include "EditorPicking.h"
#include "EditorGizmo.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <iostream>
#include <unordered_map>
#include <filesystem>
#include <set>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

// ---------------------------------------------------------
// 全局状态（原本在 SceneGlobals 或 main 的）
// ---------------------------------------------------------
struct Camera {
    glm::vec3 target = glm::vec3(0, 100, 0);
    float yaw   = 0.0f;
    float pitch = 0.0f;
    float dist  = 200.0f;
    glm::vec3 GetEye() const {
        float rp = glm::radians(pitch), ry = glm::radians(yaw);
        return target + glm::vec3(std::cos(rp)*std::cos(ry), std::sin(rp), std::cos(rp)*std::sin(ry)) * dist;
    }
    glm::mat4 GetView() const { return glm::lookAt(GetEye(), target, glm::vec3(0,1,0)); }
};

static SDL_Window*   g_window    = nullptr;
static SDL_GLContext g_glContext = nullptr;
static int           g_winW      = 1280;
static int           g_winH      = 800;
static float         main_scale  = 1.0f;

Camera        g_cam;
bool          g_bLeftMouseDown  = false;
bool          g_bRightMouseDown = false;
bool          g_bMiddleMouseDown= false;
bool          g_bDraggingGizmo  = false; // Gizmo.cpp 会 extern 这个
int           g_iMouseX = 0, g_iMouseY = 0;
int           g_iLastMouseX = 0, g_iLastMouseY = 0;

static MapData g_loadedMapData;
static bool g_bLoadedBvh = false;
static std::vector<MapData::NodeData> g_editorNavNodes;

struct BadNavLink { glm::vec3 start, end; };
struct BadNavNode { glm::vec3 pos; };
static std::vector<BadNavLink> g_badNavLinks;
static std::vector<BadNavNode> g_badNavNodes;

static std::set<uint64_t> g_pendingDeleteBrushIds;
static std::set<uint64_t> g_pendingDeleteEntityIds;

std::unordered_map<std::string, std::string> g_materialPathMap;
std::unordered_map<std::string, GLuint> g_matThumbCache;

// ---------------------------------------------------------
// 材质扫描
// ---------------------------------------------------------
static void ScanMaterialsRecursive(const std::string& rootDir) {
    g_allMaterials.clear(); g_materialPathMap.clear(); g_matThumbCache.clear();
    if (!fs::exists(rootDir)) return;
    int count = 0;
    for (auto& p : fs::recursive_directory_iterator(rootDir)) {
        if (!p.is_regular_file() || p.path().extension() != ".png" || p.path().filename() == "normal_map.png") continue;
        std::string rel = fs::relative(p.path(), rootDir).generic_string();
        rel = rel.substr(0, rel.size() - 4);
        g_allMaterials.push_back(rel);
        g_materialPathMap[rel] = p.path().generic_string();
        count++;
    }
    std::sort(g_allMaterials.begin(), g_allMaterials.end());
    printf("[Editor] Scanned %d materials from %s\n", count, rootDir.c_str());
}

GLuint GetMaterialThumb(const std::string& matName) {
    auto it = g_matThumbCache.find(matName);
    if (it != g_matThumbCache.end()) return it->second;
    GLuint texId = 0;
    auto pathIt = g_materialPathMap.find(matName);
    if (pathIt != g_materialPathMap.end()) texId = TextureCache::GetTextureByPath(pathIt->second);
    if (!texId) texId = TextureCache::GetTexture(matName);
    g_matThumbCache[matName] = texId;
    return texId;
}

void InitVisibilityArrays() {
    g_brushVisible.assign(g_brushes.size(), 1);
    g_entityVisible.assign(g_entities.size(), 1);
}

glm::vec3* GetEditorCamTarget() { return &g_cam.target; }
float* GetEditorCamDist() { return &g_cam.dist; }

// ---------------------------------------------------------
// 默认场景 & 回调
// ---------------------------------------------------------
static void BuildDefaultScene() {
    g_brushes.clear(); g_entities.clear();
    const float HALF_W=4096.0f, WALL_TH=16.0f, FLOOR_H=16.0f, CEIL_H=16.0f, ROOM_H=2048.0f;
    const float Y_CENTER=(ROOM_H+CEIL_H-FLOOR_H)*0.5f, Y_HALF=(ROOM_H+CEIL_H+FLOOR_H)*0.5f;
    auto MakeBrush = [](glm::vec3 pos, glm::vec3 halfSize, const char* mat) {
        CBrush b; b.m_id=GenerateUniqueID(); b.m_vecPos=pos; b.m_vecSize=halfSize; b.m_szMaterial=mat; b.RebuildPrimitiveMesh(); g_brushes.push_back(b);
    };
    MakeBrush({0, -FLOOR_H*0.5f, 0}, {HALF_W, FLOOR_H*0.5f, HALF_W}, "CONCRETE/CONCRETEFLOOR001A");
    MakeBrush({0, ROOM_H+CEIL_H*0.5f, 0}, {HALF_W, CEIL_H*0.5f, HALF_W}, "CONCRETE/CONCRETECEILING001A");
    MakeBrush({0, Y_CENTER, HALF_W+WALL_TH*0.5f}, {HALF_W+WALL_TH, Y_HALF, WALL_TH*0.5f}, "BRICK/BRICKWALL001A");
    MakeBrush({0, Y_CENTER, -(HALF_W+WALL_TH*0.5f)}, {HALF_W+WALL_TH, Y_HALF, WALL_TH*0.5f}, "BRICK/BRICKWALL001A");
    MakeBrush({-(HALF_W+WALL_TH*0.5f), Y_CENTER, 0}, {WALL_TH*0.5f, Y_HALF, HALF_W+WALL_TH}, "BRICK/BRICKWALL001A");
    MakeBrush({ (HALF_W+WALL_TH*0.5f), Y_CENTER, 0}, {WALL_TH*0.5f, Y_HALF, HALF_W+WALL_TH}, "BRICK/BRICKWALL001A");

    CEntity light; light.m_id=GenerateUniqueID(); light.m_iType=ENTITY_LIGHT; light.m_szName="light"; light.m_vecPos={0,2000,0};
    light.m_properties["_light"]="2048 2000 2000"; light.m_properties["_distance"]="8192"; UpdateEntityBBox(light); g_entities.push_back(light);
    CEntity spawn; spawn.m_id=GenerateUniqueID(); spawn.m_iType=ENTITY_PLAYER_START; spawn.m_szName="info_player_start"; spawn.m_vecPos={0,16,0};
    UpdateEntityBBox(spawn); g_entities.push_back(spawn);
    InitVisibilityArrays();
}

static void OnExportMap() { if (ExportMapJson(currentMap+".json")) LogAdd("[MAP] exported"); }
static void OnImportMap() {
    if (ImportMapJson(currentMap+".json")) {
        LogAdd("[MAP] imported"); g_iSelectedBrush = -1; g_iSelectedEntity = -1;
        if (!g_brushes.empty()) {
            glm::vec3 minPos(1e9f), maxPos(-1e9f);
            for (const auto& b : g_brushes) { minPos = glm::min(minPos, b.m_vecPos - b.m_vecSize); maxPos = glm::max(maxPos, b.m_vecPos + b.m_vecSize); }
            glm::vec3 center = (minPos + maxPos) * 0.5f, size = maxPos - minPos;
            g_cam.target = center; g_cam.dist = glm::length(size) * 0.6f; g_cam.yaw = 45.0f; g_cam.pitch = -30.0f;
        }
        InitVisibilityArrays();
        uint64_t maxId = 0;
        for (auto& b : g_brushes) if (b.m_id == 0) b.m_id = GenerateUniqueID();
        for (auto& e : g_entities) if (e.m_id == 0) e.m_id = GenerateUniqueID();
        for (auto& b : g_brushes) maxId = std::max(maxId, b.m_id);
        for (auto& e : g_entities) maxId = std::max(maxId, e.m_id);
        g_nextUniqueId = maxId + 1;
    }
}
static void OnLoadBvh() {
    if (!LoadMap((currentMap+".bvh").c_str(), g_loadedMapData)) { LogAdd("[EDITOR] Failed to load BVH"); return; }
    g_bLoadedBvh = true; g_editorNavNodes = g_loadedMapData.navNodes;
    UploadBvhToGL(g_loadedMapData);
}
static void OnCompileAndRun() {
    if (!ExportMapJson(currentMap+".json")) { LogAdd("[COMPILE] export FAILED"); return; }
#ifdef _WIN32
    std::string cmdMap = "bvh.exe", cmdGame = "retro.exe";
#else
    std::string cmdMap = "./bvh", cmdGame = "./retro";
#endif
    cmdMap+=" -n -b -i "+currentMap+".json -o "+currentMap+".bvh";
    int ret = system(cmdMap.c_str());
    char b[128]; snprintf(b, sizeof(b), "[COMPILE] %s exit=%d", cmdMap.c_str(), ret); LogAdd(b);
    cmdGame+=" +map "+currentMap;
    ret = system(cmdGame.c_str());
    snprintf(b, sizeof(b), "[COMPILE] %s exit=%d", cmdGame.c_str(), ret); LogAdd(b);
}

void CheckNavCollisions() {
    // 原样保留
    g_badNavLinks.clear(); g_badNavNodes.clear();
    for (size_t i = 0; i < g_editorNavNodes.size(); i++) {
        const auto& n = g_editorNavNodes[i];
        glm::vec3 hitNormal;
        float floorDist = TraceBrushes(g_brushes, n.pos + glm::vec3(0, 32, 0), glm::vec3(0, -1, 0), hitNormal);
        if (floorDist < 0.0f || floorDist > 80.0f) g_badNavNodes.push_back({ n.pos });
        for (uint32_t li : n.links) {
            if (li >= g_editorNavNodes.size()) continue;
            const auto& t = g_editorNavNodes[li];
            glm::vec3 dir = t.pos - n.pos; float dist = glm::length(dir);
            if (dist < 0.1f) continue; dir = glm::normalize(dir);
            float hitDist = TraceBrushes(g_brushes, n.pos + glm::vec3(0, 32, 0), dir, hitNormal);
            if (hitDist > 0.0f && hitDist < dist - 8.0f) g_badNavLinks.push_back({ n.pos, t.pos });
        }
    }
}

// ---------------------------------------------------------
// Draw3DView (精简后的核心绘制函数)
// ---------------------------------------------------------
static void Draw3DView(bool& outClickedLeft, bool& outClickedRight, bool& outClickedMiddle) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("3D View", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse);
    
    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    int vpW = std::max(1, (int)viewportSize.x), vpH = std::max(1, (int)viewportSize.y);
    ImVec2 viewportPos = ImGui::GetCursorScreenPos();
    ImVec2 mousePos = ImGui::GetMousePos();
    bool bHovered = ImGui::IsWindowHovered();
    bool bMouseInViewport = bHovered && mousePos.x >= viewportPos.x && mousePos.x <= viewportPos.x + viewportSize.x && mousePos.y >= viewportPos.y && mousePos.y <= viewportPos.y + viewportSize.y;
    int localMouseX = (int)(mousePos.x - viewportPos.x), localMouseY = (int)(mousePos.y - viewportPos.y);

    if (bMouseInViewport && g_bRightMouseDown) {
        ImGuiIO& io = ImGui::GetIO();
        g_cam.yaw += io.MouseDelta.x * 0.2f; g_cam.pitch += io.MouseDelta.y * 0.2f;
        g_cam.pitch = glm::clamp(g_cam.pitch, -89.0f, 89.0f);
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    }

    glm::mat4 view = g_cam.GetView();
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), (float)vpW/(float)vpH, 1.0f, 10000.0f);
    bool bIsSubEdit = (g_iGizmoMode == GIZMO_FACE || g_iGizmoMode == GIZMO_EDGE || g_iGizmoMode == GIZMO_VERTEX);
    g_iSubEditMode = bIsSubEdit ? (g_iGizmoMode == GIZMO_FACE ? SUB_FACE : (g_iGizmoMode == GIZMO_EDGE ? SUB_EDGE : SUB_VERTEX)) : SUB_NONE;

    ResizeFBO(vpW, vpH);
    BindFBO(); 
    glViewport(0, 0, vpW, vpH);
    glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::mat4 vp = proj * view;
    DrawGrid(vp);
    DrawBrushes(vp);
    DrawBrushWireframe(vp);
    if (g_bLoadedBvh && g_bShowBrushes) DrawLoadedBvh(vp);

    if (g_bShowBrushes && g_iSelectedBrush >= 0 && g_iSelectedBrush < (int)g_brushes.size()) {
        const auto& b = g_brushes[g_iSelectedBrush];
        std::vector<float> buf;
        for (int e = 0; e < 12; e++) {
            const auto& p0 = b.m_vertices[kEdgeVertexIndices[e][0]];
            const auto& p1 = b.m_vertices[kEdgeVertexIndices[e][1]];
            buf.insert(buf.end(), {p0.x,p0.y,p0.z, 1,1,0, p1.x,p1.y,p1.z, 1,1,0});
        }
        glDisable(GL_DEPTH_TEST); DrawColorLines(buf, vp); glEnable(GL_DEPTH_TEST);
    }    
    DrawEntityBBoxes(vp);

    if (bIsSubEdit && g_iSelectedBrush >= 0 && g_iSelectedBrush < (int)g_brushes.size()) {
        const CBrush& b = g_brushes[g_iSelectedBrush];
        if (g_iSubEditMode == SUB_FACE && g_iSubEditIndex >= 0 && g_iSubEditIndex < 6) {
            glm::vec3 quad[4];
            for (int k = 0; k < 4; k++) quad[k] = b.m_vertices[b.m_faces[g_iSubEditIndex].m_nVertexIndices[k]];
            DrawFaceHighlight(quad, vp);
        }
        if (g_iSubEditMode == SUB_EDGE && g_iSubEditIndex >= 0 && g_iSubEditIndex < 12) {
            int i0 = kEdgeVertexIndices[g_iSubEditIndex][0], i1 = kEdgeVertexIndices[g_iSubEditIndex][1];
            std::vector<float> buf = { b.m_vertices[i0].x, b.m_vertices[i0].y, b.m_vertices[i0].z, 1,1,0, b.m_vertices[i1].x, b.m_vertices[i1].y, b.m_vertices[i1].z, 1,1,0 };
            glDisable(GL_DEPTH_TEST); DrawColorLines(buf, vp); glEnable(GL_DEPTH_TEST);
        }
        if (g_iSubEditMode == SUB_VERTEX) {
            std::vector<std::pair<glm::vec3, glm::vec3>> pts;
            for (int v = 0; v < 8; v++) pts.emplace_back(b.m_vertices[v], v == g_iSubEditIndex ? glm::vec3(1,1,0) : glm::vec3(0.86f));
            DrawVertexPoints(pts, vp);
        }
    }

    if (g_bShowNav && !g_editorNavNodes.empty()) {
        std::vector<float> pointBuf, linkBuf, badLinkBuf, badPointBuf;

        for (const auto& n : g_editorNavNodes) {
            pointBuf.insert(pointBuf.end(), {n.pos.x, n.pos.y, n.pos.z, 0.2f, 1.0f, 0.2f});
            for (uint32_t li : n.links) {
                if (li < g_editorNavNodes.size()) {
                    const auto& t = g_editorNavNodes[li];
                    linkBuf.insert(linkBuf.end(), {n.pos.x, n.pos.y, n.pos.z, 0.2f, 0.6f, 0.2f, t.pos.x, t.pos.y, t.pos.z, 0.2f, 0.6f, 0.2f});
                }
            }
        }
        for (const auto& bl : g_badNavLinks) {
            badLinkBuf.insert(badLinkBuf.end(), {bl.start.x, bl.start.y, bl.start.z, 1, 0, 0, bl.end.x, bl.end.y, bl.end.z, 1, 0, 0});
        }
        for (const auto& bn : g_badNavNodes) {
            badPointBuf.insert(badPointBuf.end(), {bn.pos.x, bn.pos.y, bn.pos.z, 1, 1, 0});
        }

        DrawRawPrimitives(linkBuf, GL_LINES, 0, 1.5f, vp);
        DrawRawPrimitives(pointBuf, GL_POINTS, 6.0f, 0, vp);
        DrawRawPrimitives(badLinkBuf, GL_LINES, 0, 2.0f, vp);
        DrawRawPrimitives(badPointBuf, GL_POINTS, 12.0f, 0, vp);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0); // 👈 切回屏幕缓冲
    ImGui::Image((ImTextureID)(intptr_t)GetFBOTexture(), viewportSize, ImVec2(0, 1), ImVec2(1, 0));
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    DrawAndManipulateGizmo(view, proj, viewportPos, viewportSize);

    bool bIsUsingGizmo = ImGuizmo::IsUsing() || ImGuizmo::IsOver();
    if (bMouseInViewport && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !bIsUsingGizmo && !g_bDraggingGizmo && !g_bRightMouseDown) {
        if (bIsSubEdit && g_iSelectedBrush >= 0 && g_iSelectedBrush < (int)g_brushes.size()) {
            CBrush& b = g_brushes[g_iSelectedBrush];
            int hit = -1;
            if (g_iSubEditMode == SUB_FACE)        hit = PickFace  (b, view, proj, vpW, vpH, localMouseX, localMouseY);
            else if (g_iSubEditMode == SUB_EDGE)   hit = PickEdge  (b, view, proj, vpW, vpH, localMouseX, localMouseY);
            else if (g_iSubEditMode == SUB_VERTEX) hit = PickVertex(b, view, proj, vpW, vpH, localMouseX, localMouseY);
            if (hit >= 0) { g_iSubEditIndex = hit; UndoStack::Instance().PushSnapshot("Sub-Edit"); } else g_iSubEditIndex = -1;
        } else {
            glm::vec3 rayO, rayD;
            int bi = -1, ei = -1;
            if (ScreenToWorldRay(view, proj, vpW, vpH, localMouseX, localMouseY, rayO, rayD)) {
                bi = PickBrushRay(rayO, rayD); ei = PickEntityRay(rayO, rayD);
            }
            if (ei >= 0) { g_iSelectedEntity = ei; g_iSelectedBrush = -1; }
            else { g_iSelectedBrush = bi; g_iSelectedEntity = -1; }
            g_iSubEditIndex = -1;
        }
    }

    // 右上角相机控件
    glm::mat4 camViewIm = view; float camDistance = g_cam.dist;
    ImVec2 viewManipulatePos(viewportPos.x + viewportSize.x - 128, viewportPos.y);
    ImGui::SetCursorScreenPos(viewManipulatePos);
    ImGuizmo::PushID("MyViewManip");
    ImGuizmo::ViewManipulate(glm::value_ptr(camViewIm), camDistance, viewManipulatePos, ImVec2(128, 128), 0x10101010);
    ImRect viewManipRect(viewManipulatePos, ImVec2(viewManipulatePos.x + 128, viewManipulatePos.y + 128));
    if (ImGuizmo::IsUsingViewManipulate() || (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(viewManipRect.Min, viewManipRect.Max))) {
        glm::mat4 invView = glm::inverse(camViewIm);
        glm::vec3 offset = glm::vec3(invView[3][0], invView[3][1], invView[3][2]) - g_cam.target;
        float r = glm::length(glm::vec2(offset.x, offset.z));
        if (r > 0.001f) { g_cam.yaw = glm::degrees(std::atan2(offset.z, offset.x)); g_cam.pitch = glm::degrees(std::atan2(offset.y, r)); }
        g_cam.dist = glm::length(offset);
    }
    ImGuizmo::PopID();
    ImGui::End();
    ImGui::PopStyleVar();
}

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

// ---------------------------------------------------------
// Main
// ---------------------------------------------------------
int main(int argc, char** argv) {
    std::cout << "branch: " << Version::GIT_BRANCH << ", commit: " << Version::GIT_HASH << "\n";
    FileSystem::Init(".");
    ScanMaterialsRecursive("./materials");
    for (int i = 1; i < argc; ++i) if (std::strcmp(argv[i], "-map") == 0 && i + 1 < argc) currentMap = argv[++i];

#ifdef _WIN32
    ::SetProcessDPIAware();
#endif
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    g_window = SDL_CreateWindow("Retro Editor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, g_winW, g_winH, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    g_glContext = SDL_GL_CreateContext(g_window);
    SDL_GL_MakeCurrent(g_window, g_glContext);
    SDL_GL_SetSwapInterval(1);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { fprintf(stderr, "GLEW init failed\n"); return 1; }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    
    main_scale = ImGui_ImplSDL2_GetContentScaleForDisplay(0);
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;
    ImGui_ImplSDL2_InitForOpenGL(g_window, g_glContext);
    ImGui_ImplOpenGL3_Init("#version 330");

    InitGL();
    if (!GameDatabase::Instance().LoadFromFile("editor.json")) LogAdd("[GameDatabase] FAILED to load");
    BuildDefaultScene();
    SetExportMapHandler(OnExportMap); SetImportMapHandler(OnImportMap);
    SetCompileAndRunHandler(OnCompileAndRun); SetLoadBvhHandler(OnLoadBvh);
    OnImportMap();

    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_RESIZED) { g_winW = ev.window.data1; g_winH = ev.window.data2; }
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
            if (ev.type == SDL_KEYDOWN && !io.WantCaptureKeyboard) {
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
        if (!io.WantCaptureKeyboard) {
            float flySpeed = g_bFlyMode ? 15.0f : (g_cam.dist * 1.2f * 0.016f);
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

        for (auto& e : g_entities) UpdateEntityBBox(e);

        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
        ImGuizmo::BeginFrame();
        EditorLayout::Begin(); DrawMenuBar(); DrawToolsPanel();
        
        PanelResult panelRes;
        DrawScenePanel(&g_iSelectedEntity, &g_iSelectedBrush, panelRes);
        Draw3DView(g_bLeftMouseDown, g_bRightMouseDown, g_bMiddleMouseDown);
        Draw2DViews(panelRes);
        DrawCompileOptionsWindow(); DrawMaterialBrowserWindow();

        if (panelRes.bRequestNew) {
            UndoStack::Instance().PushSnapshot("New Brush");
            CBrush b; b.m_id = GenerateUniqueID(); b.m_vecPos = panelRes.newBrushPos; b.m_vecSize = glm::vec3(32,32,32);
            b.RebuildPrimitiveMesh(); g_brushes.push_back(b); g_brushVisible.push_back(1); g_iSelectedBrush = (int)g_brushes.size() - 1;
        }
        if (panelRes.bRequestDuplicate && panelRes.duplicateId != 0) {
            for (int i = 0; i < (int)g_brushes.size(); i++) if (g_brushes[i].m_id == panelRes.duplicateId) {
                UndoStack::Instance().PushSnapshot("Duplicate Brush");
                CBrush b = g_brushes[i]; b.m_id = GenerateUniqueID();
                for (auto& v : b.m_vertices) v += glm::vec3(32,0,0);
                b.m_vecPos += glm::vec3(32,0,0); g_brushes.push_back(b); g_brushVisible.push_back(1);
                g_iSelectedBrush = (int)g_brushes.size() - 1; break;
            }
        }
        if (panelRes.bRequestDelete && panelRes.targetId != 0) { g_pendingDeleteBrushIds.insert(panelRes.targetId); g_iSelectedBrush = -1; }
        if (panelRes.bRequestDeleteEntity && panelRes.targetEntityId != 0) { g_pendingDeleteEntityIds.insert(panelRes.targetEntityId); g_iSelectedEntity = -1; }
        
        if (panelRes.bRequestNewEntity && !panelRes.szNewEntityClass.empty()) {
            UndoStack::Instance().PushSnapshot("New Entity");
            CEntity e; e.m_id = GenerateUniqueID(); e.m_szName = panelRes.szNewEntityClass; e.m_vecPos = panelRes.newEntityPos;
            if (e.m_szName == "info_player_start") e.m_iType = ENTITY_PLAYER_START;
            else if (e.m_szName == "light") e.m_iType = ENTITY_LIGHT;
            UpdateEntityBBox(e); g_entities.push_back(e); g_entityVisible.push_back(1); g_iSelectedEntity = (int)g_entities.size() - 1;
        }

        ImGui::Render();
        glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, g_winW, g_winH);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(g_window);
        g_iLastMouseX = g_iMouseX; g_iLastMouseY = g_iMouseY;

        if (!g_pendingDeleteBrushIds.empty()) {
            UndoStack::Instance().PushSnapshot("Delete Brushes");
            std::vector<CBrush> newB; std::vector<uint8_t> newV;
            for (size_t i = 0; i < g_brushes.size(); i++) if (g_pendingDeleteBrushIds.find(g_brushes[i].m_id) == g_pendingDeleteBrushIds.end()) {
                newB.push_back(g_brushes[i]); if (i < g_brushVisible.size()) newV.push_back(g_brushVisible[i]);
            }
            g_brushes = std::move(newB); g_brushVisible = std::move(newV);
            g_pendingDeleteBrushIds.clear(); g_iSelectedBrush = -1;
        }
        if (!g_pendingDeleteEntityIds.empty()) {
            UndoStack::Instance().PushSnapshot("Delete Entities");
            std::vector<CEntity> newE; std::vector<uint8_t> newV;
            for (size_t i = 0; i < g_entities.size(); i++) if (g_pendingDeleteEntityIds.find(g_entities[i].m_id) == g_pendingDeleteEntityIds.end()) {
                newE.push_back(g_entities[i]); if (i < g_entityVisible.size()) newV.push_back(g_entityVisible[i]);
            }
            g_entities = std::move(newE); g_entityVisible = std::move(newV);
            g_pendingDeleteEntityIds.clear(); g_iSelectedEntity = -1;
        }
    }
    CleanupGL();
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext();
    SDL_GL_DeleteContext(g_glContext); SDL_DestroyWindow(g_window); SDL_Quit();
    return 0;
}