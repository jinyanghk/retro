#include "Common.h"
#include "GameTypes.h"
#include "MapLoader.h"
#include "Physics.h"
#include "Player.h"
#include "Level.h"
#include "Audio.h"
#include "Render.h"
#include "FileSystem.h"


void MyTerminate() {
    printf("[CRASH] Unhandled exception, terminating.\n");
    fflush(stdout);
    std::abort();
}

int main(int argc, char* argv[]) {
    std::cout << "branch: " << Version::GIT_BRANCH
                << ", commit: " << Version::GIT_HASH << ", "
                << Version::CMAKE_BUILD_TYPE << std::endl;

    std::set_terminate(MyTerminate);

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "+map") == 0 && i + 1 < argc) {
            currentMap = argv[++i];
        }
    }

    FileSystem::Init(".");
#ifdef _WIN32
    ::SetProcessDPIAware();
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    int scrW = 1280;
    int scrH = 720;
    SDL_Window* window = SDL_CreateWindow("Retro Engine", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          scrW, scrH, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        return 1;
    }                                          
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "glewInit failed\n";
        return 1;
    }
    glEnable(GL_DEPTH_TEST);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    MapData mapData;
    if (!LoadMap((currentMap+".bvh").c_str(), mapData)) return 1;

    RenderAssets render;
    InitRender(render, mapData);

    InitPhysics();
    CreateStaticMesh(mapData.GetCollisionMeshView());
    printf("[Main] Static mesh: cnv=%u, cni=%u (indices %%3=%u)\n",
        mapData.cnv, mapData.cni, mapData.cni % 3);

    PlayerState player;
    player.pos = glm::make_vec3(mapData.spawnPos) + glm::vec3(0.0f, 36.0f, 0.0f);
    player.camYaw = 0;
    player.health = 100.0f;

    JPH::BodyID player_body = CreatePlayerBody(player.pos.x, player.pos.y, player.pos.z);

    InitLevel(mapData, player);

    std::vector<BulletHole> bulletHoles;
    bool running = true;
    bool mouseCaptured = true;

    AudioAssets audio;
    InitAudio(audio);

    SDL_SetRelativeMouseMode(SDL_TRUE);

    Uint64 lastCounter = SDL_GetPerformanceCounter();
    const Uint64 freq = SDL_GetPerformanceFrequency();

    while (running) {
        Uint64 now = SDL_GetPerformanceCounter();
        float deltaTime = (float)(now - lastCounter) / (float)freq;
        lastCounter = now;
        if (deltaTime > 0.05f) deltaTime = 0.05f;

        LevelInput levelInput{};
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);

            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
                scrW = event.window.data1;
                scrH = event.window.data2;
            } else if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                SDL_Scancode sc = event.key.keysym.scancode;
                if (sc == SDL_SCANCODE_ESCAPE) {
                    mouseCaptured = !mouseCaptured;
                    SDL_SetRelativeMouseMode(mouseCaptured ? SDL_TRUE : SDL_FALSE);
                } else if (sc == SDL_SCANCODE_1) {
                    levelInput.switchWeaponSlot = 0; // Pistol
                } else if (sc == SDL_SCANCODE_2) {
                    levelInput.switchWeaponSlot = 1; // Shotgun
                } else if (sc == SDL_SCANCODE_3) {
                    levelInput.switchWeaponSlot = 2; // Gravity Gun
                } else if (sc == SDL_SCANCODE_E) {
                    levelInput.useJustPressed = true;
                } else if (sc == SDL_SCANCODE_R) {
                    player.health = 100.0f;
                    player.pos = glm::make_vec3(mapData.spawnPos) + glm::vec3(0.0f, 36.0f, 0.0f);
                    player.camYaw = 0.0f;
                    player.vel = glm::vec3(0.0f);
                }
            } else if (event.type == SDL_MOUSEBUTTONDOWN && mouseCaptured) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    levelInput.primaryJustPressed = true;
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    levelInput.secondaryJustPressed = true;
                }
            } else if (event.type == SDL_MOUSEMOTION && mouseCaptured) {
                player.camYaw += event.motion.xrel * player.mouseSensitivity;
                player.camPitch -= event.motion.yrel * player.mouseSensitivity;
                if (player.camPitch > 89.0f) player.camPitch = 89.0f;
                if (player.camPitch < -89.0f) player.camPitch = -89.0f;
            }
        }

        Uint32 mouseButtons = SDL_GetMouseState(nullptr, nullptr);
        levelInput.primaryDown = mouseCaptured && ((mouseButtons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0);

        // WASD Movement
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        glm::vec3 wishDir(0.0f);
        if (mouseCaptured) {
            float yawRad = glm::radians(player.camYaw);
            glm::vec3 forward(std::cos(yawRad), 0.0f, std::sin(yawRad));
            glm::vec3 right(-forward.z, 0.0f, forward.x);

            if (keys[SDL_SCANCODE_W]) wishDir += forward;
            if (keys[SDL_SCANCODE_S]) wishDir -= forward;
            if (keys[SDL_SCANCODE_D]) wishDir += right;
            if (keys[SDL_SCANCODE_A]) wishDir -= right;
        }
        if (glm::length(wishDir) > 0.001f) {
            wishDir = glm::normalize(wishDir);
            player.vel.x = wishDir.x * player.moveSpeed;
            player.vel.z = wishDir.z * player.moveSpeed;
        } else {
            player.vel.x = 0.0f;
            player.vel.z = 0.0f;
        }

        if (mouseCaptured && keys[SDL_SCANCODE_SPACE] && player.isGrounded) {
            player.vel.y = player.jumpSpeed;
            player.isGrounded = false;
        }

        // Update Player, Physics, and World Entities
        UpdatePlayer(player, deltaTime, player_body);

        float prevHp = player.health;
        UpdateLevel(player, levelInput, deltaTime);
        if (player.health < prevHp) {
            player.damageTimer = 0.2f;
            player.shakeAmount = 4.0f;
            player.shakeTimer = 0.25f;
        }
        if (player.health <= 0.0f) {
            player.health = 100.0f;
            player.health = 100.0f;
            player.pos = glm::make_vec3(mapData.spawnPos) + glm::vec3(0.0f, 36.0f, 0.0f);
            player.camYaw = 0.0f;
            player.vel = glm::vec3(0.0f);
        }

        UpdatePhysics(deltaTime);

        if (player.damageTimer > 0.0f) player.damageTimer -= deltaTime;
        if (player.shakeTimer > 0.0f) {
            player.shakeTimer -= deltaTime;
            if (player.shakeTimer <= 0.0f) player.shakeAmount = 0.0f;
        }

        // Render Frame
        GameRenderSnapshot snap = GetGameRenderSnapshot();
        RenderFrame(render, player, snap, mapData, bulletHoles, scrW, scrH);

        // ImGui HUD
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_Always);
        ImGui::Begin("Debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar);
        ImGui::Text("Map: %s | Entities: %d", currentMap.c_str(), snap.boxCount);
        ImGui::Text("Pos: (%.1f, %.1f, %.1f) | Grounded: %s", player.pos.x, player.pos.y, player.pos.z, player.isGrounded ? "YES" : "NO");
        const char* weapNames[] = { "Pistol [1]", "Shotgun [2]", "Gravity Gun [3]" };
        int wIdx = (snap.activeWeapon >= 0 && snap.activeWeapon <= 2) ? snap.activeWeapon : 0;
        ImGui::Text("Weapon: %s", weapNames[wIdx]);
        ImGui::End();

        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        ImU32 crossColor = snap.holdingObject ? IM_COL32(255, 180, 30, 255) : IM_COL32(50, 255, 50, 255);
        float cx = scrW * 0.5f;
        float cy = scrH * 0.5f;
        drawList->AddLine(ImVec2(cx - 8, cy), ImVec2(cx + 8, cy), crossColor, 2.0f);
        drawList->AddLine(ImVec2(cx, cy - 8), ImVec2(cx, cy + 8), crossColor, 2.0f);

        if (player.damageTimer > 0.0f) {
            int alpha = (int)(player.damageTimer * 150.0f / 0.2f);
            drawList->AddRectFilled(ImVec2(0, 0), ImVec2((float)scrW, (float)scrH), IM_COL32(255, 0, 0, alpha));
        }

        char hpText[32];
        std::snprintf(hpText, sizeof(hpText), "HP: %.0f", player.health);
        drawList->AddText(ImVec2(24.0f, (float)scrH - 42.0f), IM_COL32(255, 220, 50, 255), hpText);

        // ==========================================================
        // Visual Debug (3D 投影到 2D 屏幕)
        // ==========================================================
        ImDrawList* fgDrawList = ImGui::GetForegroundDrawList();
        
        auto debugZombies = GetZombieDebugInfo();
        if (!debugZombies.empty()) {
            // 辅助函数：将世界坐标 3D 投影到屏幕 2D
            auto WorldToScreen = [&](const glm::vec3& worldPos) {
                glm::mat4 view = glm::lookAt(player.GetEyePos(), player.GetEyePos() + player.GetForward(), glm::vec3(0, 1, 0));
                glm::mat4 proj = glm::perspective(glm::radians(85.0f), (float)scrW / (float)scrH, 0.1f, 100000.0f);
                glm::vec4 clip = proj * view * glm::vec4(worldPos, 1.0f);
                if (clip.w <= 0.0f) return ImVec2(-9999, -9999); // 在相机后面
                glm::vec3 ndc = glm::vec3(clip) / clip.w;
                return ImVec2((ndc.x + 1.0f) * 0.5f * scrW, (1.0f - ndc.y) * 0.5f * scrH);
            };

            for (const auto& z : debugZombies) {
                // 1. 画丧尸头顶的物理中心（黄色文字）
                ImVec2 centerScreen = WorldToScreen(z.pos + glm::vec3(0, 72, 0));
                if (centerScreen.x > -1000) {
                    const char* stateText = "IDLE";
                    if (z.state == 1) stateText = "CHASE";
                    if (z.state == 2) stateText = "ATTACK";
                    
                    ImU32 color = (z.state == 1) ? IM_COL32(255, 0, 0, 255) : IM_COL32(255, 255, 0, 255);
                    fgDrawList->AddText(centerScreen, color, stateText);
                }

                // 2. 画 A* 寻路路径（绿色线段）
                if (!z.path.empty()) {
                    for (size_t i = 0; i < z.path.size() - 1; i++) {
                        ImVec2 p1 = WorldToScreen(z.path[i]);
                        ImVec2 p2 = WorldToScreen(z.path[i+1]);
                        if (p1.x > -1000 && p2.x > -1000) {
                            fgDrawList->AddLine(p1, p2, IM_COL32(0, 255, 0, 255), 2.0f);
                        }
                    }
                    // 画当前指向的路径节点（红色圆圈）
                    if (!z.path.empty()) {
                        ImVec2 targetScreen = WorldToScreen(z.path.front());
                        if (targetScreen.x > -1000) {
                            fgDrawList->AddCircle(targetScreen, 6.0f, IM_COL32(255, 0, 0, 255), 12, 2.0f);
                        }
                    }
                }
            }
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);

        // Seamless level transition via trigger_changelevel
        char nextMapBuf[128];
        if (PollLevelChange(nextMapBuf, sizeof(nextMapBuf))) {
            currentMap = nextMapBuf;
            std::printf("[SYSTEM] Loading new map: %s\n", currentMap.c_str());

            CleanupLevel();
            DestroyBodySafe(player_body);
            ShutdownRender(render);
            ShutdownPhysics();

            mapData = MapData{};
            if (!LoadMap((currentMap + ".bvh").c_str(), mapData)) {
                std::cerr << "Failed to load next map: " << currentMap << ".bvh\n";
                break;
            }

            InitPhysics();
            CreateStaticMesh(mapData.GetCollisionMeshView());

            InitRender(render, mapData);
            player.pos = glm::make_vec3(mapData.spawnPos) + glm::vec3(0.0f, 36.0f, 0.0f);
            player.camYaw = 0;
            player.vel = glm::vec3(0.0f);
            player_body = CreatePlayerBody(player.pos.x, player.pos.y, player.pos.z);
            InitLevel(mapData, player);
            bulletHoles.clear();
        }
    }
    CleanupLevel();
    DestroyBodySafe(player_body);
    ShutdownRender(render);
    ShutdownPhysics();
    ShutdownAudio(audio);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}