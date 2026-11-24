#include <GL/glew.h>
#include <SDL.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include "ImGuizmo.h"

#include "Camera.h"
#include "Input.h"
#include "Viewport.h"
#include "GL.h"
#include "Globals.h"
#include "Layout.h"
#include "Panels.h"
#include "View2D.h"
#include "MapIO.h"
#include "MapLoader.h"
#include "TextureCache.h"
#include "version.h"
#include "Log.h"
#include "FileSystem.h"
#include "GameDatabase.h"
#include "UndoStack.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <iostream>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

// 全局定义
Camera g_cam;
bool g_bLeftMouseDown = false, g_bRightMouseDown = false, g_bMiddleMouseDown = false;
bool g_bDraggingGizmo = false;
int g_iMouseX = 0, g_iMouseY = 0;
static int g_iLastMouseX = 0, g_iLastMouseY = 0;

static SDL_Window* g_window = nullptr;
static SDL_GLContext g_glContext = nullptr;
static int g_winW = 1280, g_winH = 800;
static float main_scale = 1.0f;

static std::set<uint64_t> g_pendingDeleteBrushIds;
static std::set<uint64_t> g_pendingDeleteEntityIds;

// 材质的全局缓存
std::unordered_map<std::string, std::string> g_materialPathMap;
std::unordered_map<std::string, GLuint> g_matThumbCache;

// 暴露给 EditorPanels 和 View2D 的接口
glm::vec3* GetEditorCamTarget() { return &g_cam.target; }
float* GetEditorCamDist() { return &g_cam.dist; }

void InitVisibilityArrays() {
    g_brushVisible.assign(g_brushes.size(), 1);
    g_entityVisible.assign(g_entities.size(), 1);
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

static void ScanMaterialsRecursive(const std::string& rootDir) {
    g_allMaterials.clear(); g_materialPathMap.clear(); g_matThumbCache.clear();
    if (!fs::exists(rootDir)) return;
    for (auto& p : fs::recursive_directory_iterator(rootDir)) {
        if (!p.is_regular_file() || p.path().extension() != ".png" || p.path().filename() == "normal_map.png") continue;
        std::string rel = fs::relative(p.path(), rootDir).generic_string();
        rel = rel.substr(0, rel.size() - 4);
        g_allMaterials.push_back(rel);
        g_materialPathMap[rel] = p.path().generic_string();
    }
    std::sort(g_allMaterials.begin(), g_allMaterials.end());
}

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

int main(int argc, char** argv) {
    std::cout << "branch: " << Version::GIT_BRANCH 
                << ", commit: " << Version::GIT_HASH << ", " 
                << Version::CMAKE_BUILD_TYPE << std::endl;
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
    if (glewInit() != GLEW_OK) return 1;

    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); io.IniFilename = nullptr; io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
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
            ProcessInput(ev, running, io.WantCaptureKeyboard);
        }

        UpdateCameraMovement(1.0f / 60.0f, io.WantCaptureKeyboard);
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