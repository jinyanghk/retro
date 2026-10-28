#include "Common.h"
#include "BVH.h"
#include "Trace.h"
#include "MapLoader.h"
#include "Physics.h"
#include "Player.h"
#include "Weapon.h"
#include "Audio.h"
#include "Enemy.h"
#include "Render.h"
#include "AStar.h"

int main(int, char**) {
    std::cout << "branch: " << Version::GIT_BRANCH 
                << ", commit: " << Version::GIT_HASH << ", " 
                << Version::CMAKE_BUILD_TYPE << std::endl;
#ifdef _WIN32
    ::SetProcessDPIAware();
#endif
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window* window = SDL_CreateWindow("Game + Jolt", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) return 1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    MapData mapData;
    if (!LoadMap("map.bvh", mapData)) return 1;

    std::vector<BulletHole> bulletHoles;
    std::vector<CollisionTri> collisionTris;
    // 🔥 改用 collisionIndices 和 collisionVerts 来构建 BVH
    for (size_t i = 0; i + 2 < mapData.collisionIndices.size(); i += 3) {
        uint32_t i0 = mapData.collisionIndices[i], i1 = mapData.collisionIndices[i+1], i2 = mapData.collisionIndices[i+2];
        if (i0 >= mapData.cnv || i1 >= mapData.cnv || i2 >= mapData.cnv) continue;
        glm::vec3 v0(mapData.collisionVerts[i0*3], mapData.collisionVerts[i0*3+1], mapData.collisionVerts[i0*3+2]);
        glm::vec3 v1(mapData.collisionVerts[i1*3], mapData.collisionVerts[i1*3+1], mapData.collisionVerts[i1*3+2]);
        glm::vec3 v2(mapData.collisionVerts[i2*3], mapData.collisionVerts[i2*3+1], mapData.collisionVerts[i2*3+2]);
        
        // 用三角形算一个大致法线
        glm::vec3 n = glm::normalize(glm::cross(v1 - v0, v2 - v0));
        collisionTris.push_back({v0, v1, v2, n});
    }
    g_sortedTris = collisionTris;
    g_bvh.clear(); g_bvh.reserve(collisionTris.size() * 2 + 1);
    if (!g_sortedTris.empty()) BuildBVH(0, (int)g_sortedTris.size());

    RenderAssets render;
    InitRender(render, mapData);

    InitPhysics();
    JPH::BodyInterface& body_interface = GetBodyInterface();
    CreateStaticMesh(mapData.collisionVerts, mapData.collisionIndices, mapData.cnv, mapData.cni);

    JPH::BodyID player_body = CreatePlayerBody(mapData.spawnPos[0], mapData.spawnPos[1] + 36.0f, mapData.spawnPos[2]);

    std::vector<JPH::BodyID> physboxIds;
    for (size_t i = 0; i < mapData.physboxMins.size(); i++) {
        glm::vec3 mn = mapData.physboxMins[i];
        glm::vec3 mx = mapData.physboxMaxs[i];
        glm::vec3 center = (mn + mx) * 0.5f;
        glm::vec3 halfSize = (mx - mn) * 0.5f;
        physboxIds.push_back(CreatePhysBox(center.x, center.y, center.z, halfSize.x, halfSize.y, halfSize.z));
    }

    std::vector<JPH::BodyID> doorBodies;
    for (size_t i = 0; i < mapData.doors.size(); i++) {
        glm::vec3 center = (mapData.doors[i].mins + mapData.doors[i].maxs) * 0.5f;
        glm::vec3 halfSize = (mapData.doors[i].maxs - mapData.doors[i].mins) * 0.5f;
        doorBodies.push_back(CreateDoorBody(center.x, center.y, center.z, halfSize.x, halfSize.y, halfSize.z));
    }

    AudioAssets audio;
    InitAudio(audio);

    PlayerState player;
    InitPlayer(player, mapData);
    WeaponState weapon;
    GravityGunState gravityGun;
    bool mouseLocked = true;
    SDL_SetRelativeMouseMode(SDL_TRUE);

    std::vector<DoorStateForTrace> doorTrace;
    std::vector<EnemyInfo> enemies;
    auto SpawnAllEnemies = [&]() {
        enemies.clear();
        for(auto& pos : mapData.zombieSpawns) {
            EnemyInfo e;
            glm::vec3 spawnPos = pos;
            
            glm::vec3 dummyNormal;
            // 向下探测
            float floorDist = TraceBVH(glm::vec3(spawnPos.x, spawnPos.y + 20.0f, spawnPos.z), glm::vec3(0, -1, 0), dummyNormal);
            if (floorDist >= 0.0f && floorDist < 500.0f) {
                spawnPos.y = spawnPos.y + 20.0f - floorDist + e.height * 0.5f;
            }
            
            e.pos = spawnPos;
            e.bodyId = CreateEnemyBody(spawnPos.x, spawnPos.y, spawnPos.z); 
            enemies.push_back(e);
        }
    };
    SpawnAllEnemies();

    bool levelComplete = false;
    float levelCompleteTimer = 0.0f;

    bool bGrabPressed = false;
    bool bThrowPressed = false;

    std::vector<ExplosiveBarrel> barrels;
    for (auto& sp : mapData.barrelSpawns) {
        ExplosiveBarrel b;
        b.bodyId = CreateBarrelBody(sp.x, sp.y + 24.0f, sp.z);
        b.spawnPos = sp;
        barrels.push_back(b);
    }

    bool running = true;
    Uint32 lastTime = SDL_GetTicks();

    while (running) {
        Uint32 currentTime = SDL_GetTicks();
        float deltaTime = (currentTime - lastTime) / 1000.0f;
        if (deltaTime > 0.05f) deltaTime = 0.05f;
        lastTime = currentTime;

        bool bUsePressed = false;
        bool bFirePressed = false;
        bGrabPressed = false;
        bThrowPressed = false;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE) running = false;
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                if (mouseLocked) { mouseLocked = false; SDL_SetRelativeMouseMode(SDL_FALSE); }
                else running = false;
            }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_e && mouseLocked && !event.key.repeat) {
                bUsePressed = true;
            }
            if (levelComplete) {
                if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_r) {
                    levelComplete = false;
                    player.hp = 100.0f;
                    player.pos = glm::vec3(mapData.spawnPos[0], mapData.spawnPos[1] + 36.0f, mapData.spawnPos[2]);
                    player.vel = glm::vec3(0);
                    SpawnAllEnemies();
                    
                    for (size_t i = 0; i < physboxIds.size(); i++) {
                        MoveKinematicBody(physboxIds[i], (mapData.physboxMins[i].x + mapData.physboxMaxs[i].x)*0.5f, (mapData.physboxMins[i].y + mapData.physboxMaxs[i].y)*0.5f, (mapData.physboxMins[i].z + mapData.physboxMaxs[i].z)*0.5f);
                        body_interface.SetLinearVelocity(physboxIds[i], JPH::Vec3(0,0,0));
                    }
                    for (auto& d : mapData.doors) { d.openAmount = 0.0f; d.isOpening = false; d.waitTimer = 0.0f; }
                    for (auto& b : barrels) {
                        if (b.exploded) {
                            b.bodyId = CreateBarrelBody(b.spawnPos.x, b.spawnPos.y + 24.0f, b.spawnPos.z);
                            b.exploded = false; b.hp = 20.0f;
                        } else {
                            body_interface.SetPositionAndRotation(b.bodyId, JPH::RVec3(b.spawnPos.x, b.spawnPos.y + 24.0f, b.spawnPos.z), JPH::Quat::sIdentity(), JPH::EActivation::Activate);
                            body_interface.SetLinearVelocity(b.bodyId, JPH::Vec3(0,0,0));
                        }
                    }                    
                }
                continue;
            }                        
            if (event.type == SDL_MOUSEBUTTONDOWN) {
                if (!mouseLocked) { mouseLocked = true; SDL_SetRelativeMouseMode(SDL_TRUE); }
                else if (event.button.button == SDL_BUTTON_RIGHT) { bGrabPressed = true; }
                else if (event.button.button == SDL_BUTTON_LEFT) {
                    if (gravityGun.holding) { bThrowPressed = true; }
                    else { if (!event.button.clicks || event.button.clicks == 1) bFirePressed = true; }
                }
            }
            if (event.type == SDL_MOUSEMOTION && mouseLocked) {
                player.camYaw += event.motion.xrel * player.mouseSensitivity;
                player.camPitch -= event.motion.yrel * player.mouseSensitivity;
                if (player.camPitch > 89.0f) player.camPitch = 89.0f;
                if (player.camPitch < -89.0f) player.camPitch = -89.0f;
            }
        }

        // 移动输入
        if (!levelComplete && mouseLocked && !io.WantCaptureKeyboard) {
            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            float yawRad = glm::radians(player.camYaw);
            glm::vec3 forward(cos(yawRad), 0.0f, sin(yawRad));
            glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
            glm::vec3 wishDir(0.0f);
            if (keys[SDL_SCANCODE_W]) wishDir += forward;
            if (keys[SDL_SCANCODE_S]) wishDir -= forward;
            if (keys[SDL_SCANCODE_D]) wishDir += right;
            if (keys[SDL_SCANCODE_A]) wishDir -= right;
            if (glm::length(wishDir) > 0.0f) {
                wishDir = glm::normalize(wishDir);
                player.vel.x = wishDir.x * player.moveSpeed;
                player.vel.z = wishDir.z * player.moveSpeed;
            } else {
                player.vel.x = 0.0f; player.vel.z = 0.0f;
            }
            if (keys[SDL_SCANCODE_SPACE] && player.isGrounded) { 
                player.vel.y = player.jumpSpeed; 
                player.isGrounded = false; 
            }
        } else {
            player.vel.x = 0.0f; player.vel.z = 0.0f;
        }

        // 构建门的碰撞状态
        doorTrace.clear();
        doorTrace.reserve(mapData.doors.size());
        for (auto& d : mapData.doors) {
            DoorStateForTrace dst;
            dst.mins = d.mins; dst.maxs = d.maxs; dst.openDir = d.openDir; dst.openDist = d.openDist; dst.openAmount = d.openAmount;
            doorTrace.push_back(dst);
        }

        // 模块更新
        UpdatePlayer(player, deltaTime, physboxIds, barrels, doorTrace, body_interface, player_body, mapData.physboxMins, mapData.physboxMaxs);
        PlayerPushEntities(player, deltaTime, body_interface, physboxIds, barrels, mapData.physboxMins, mapData.physboxMaxs);

        // ====================================================================
        // 触发器检测
        // ====================================================================
        if (!levelComplete) {
            glm::vec3 pMin = player.pos - glm::vec3(player.radius, player.height * 0.5f, player.radius);
            glm::vec3 pMax = player.pos + glm::vec3(player.radius, player.height * 0.5f, player.radius);
            for (size_t i = 0; i < mapData.triggerMins.size(); i++) {
                if (pMax.x >= mapData.triggerMins[i].x && pMin.x <= mapData.triggerMaxs[i].x &&
                    pMax.y >= mapData.triggerMins[i].y && pMin.y <= mapData.triggerMaxs[i].y &&
                    pMax.z >= mapData.triggerMins[i].z && pMin.z <= mapData.triggerMaxs[i].z) {
                    
                    const std::string& tgt = mapData.triggerTargets[i];
                    
                    // 🔥 核心逻辑：判断触发目标是“通关”还是“开门”
                    if (tgt == "level_end") {
                        levelComplete = true;
                        levelCompleteTimer = 0.0f;
                        if (audio.sndHit) Mix_PlayChannel(-1, audio.sndHit, 0); // 播放通关音效
                        printf("[GAME] Level Complete Triggered!\n");
                    }
                    else if (!tgt.empty()) {
                        // 查找 target 对应的门
                        for (auto& door : mapData.doors) {
                            if (door.targetname == tgt && door.openAmount < 0.01f && !door.isOpening) {
                                door.isOpening = true;
                                door.waitTimer = 0.0f;
                                if (audio.sndDoor) Mix_PlayChannel(-1, audio.sndDoor, 0);
                                printf("[GAME] Opened door: %s\n", tgt.c_str());
                            }
                        }
                    } else {
                        // 如果什么都没填，兼容旧地图：默认通关（可选）
                        levelComplete = true;
                        levelCompleteTimer = 0.0f;
                    }
                }
            }
        }

        UpdateEnemies(enemies, player, deltaTime, body_interface, mapData, 
                physboxIds, barrels, doorTrace, 
                mapData.physboxMins, mapData.physboxMaxs, 
                audio.sndHit);

        if (player.damageTimer > 0.0f) player.damageTimer -= deltaTime;
        if (player.hp <= 0.0f) {
            player.hp = 100.0f;
            player.pos = glm::vec3(mapData.spawnPos[0], mapData.spawnPos[1] + 36.0f, mapData.spawnPos[2]);
            player.vel = glm::vec3(0);
        }

        if (!levelComplete && bFirePressed) {
            FireWeapon(player, weapon, gravityGun, deltaTime, body_interface, physboxIds, doorTrace, enemies, barrels, bulletHoles, audio.sndShoot, audio.sndHit, audio.sndExplosion);
        }

        UpdateGravityGun(player, gravityGun, bGrabPressed, bThrowPressed, deltaTime, body_interface, physboxIds, doorTrace, enemies, barrels, audio.sndDoor, audio.sndShoot);

        UpdatePhysics(deltaTime);

        // 门与开关交互
        if (bUsePressed) {
            glm::vec3 eyePos = player.GetEyePos();
            glm::vec3 front = player.GetForward();
            float bestT = 64.0f; int bestDoor = -1;
            for (size_t i = 0; i < mapData.doors.size(); i++) {
                glm::vec3 off = mapData.doors[i].openDir * (mapData.doors[i].openDist * mapData.doors[i].openAmount);
                float tmin, tmax;
                if (RayAABB(eyePos, front, mapData.doors[i].mins + off, mapData.doors[i].maxs + off, tmin, tmax)) {
                    if (tmin >= 0.0f && tmin < bestT) { bestT = tmin; bestDoor = (int)i; }
                }
            }
            if (bestDoor >= 0 && mapData.doors[bestDoor].openAmount < 0.01f && !mapData.doors[bestDoor].isOpening) {
                mapData.doors[bestDoor].isOpening = true;
                mapData.doors[bestDoor].waitTimer = 0.0f;
                if (audio.sndDoor) Mix_PlayChannel(-1, audio.sndDoor, 0);
            }
        }

        // 门动画与物理同步
        for (auto& d : mapData.doors) {
            float move = (d.speed * deltaTime) / std::max(d.openDist, 1.0f);
            if (d.isOpening) {
                d.openAmount += move;
                if (d.openAmount >= 1.0f) { d.openAmount = 1.0f; d.isOpening = false; d.waitTimer = d.waitTime; }
            } else if (d.waitTimer > 0.0f) {
                d.waitTimer -= deltaTime;
            } else if (d.openAmount > 0.0f) {
                d.openAmount -= move;
                if (d.openAmount <= 0.0f) d.openAmount = 0.0f;
            }
        }
        for (size_t i = 0; i < mapData.doors.size(); i++) {
            if (i >= doorBodies.size()) break;
            glm::vec3 center = (mapData.doors[i].mins + mapData.doors[i].maxs) * 0.5f + mapData.doors[i].openDir * (mapData.doors[i].openDist * mapData.doors[i].openAmount);
            MoveKinematicBody(doorBodies[i], center.x, center.y, center.z);
        }

        if (weapon.crosshairHitTimer > 0.0f) weapon.crosshairHitTimer -= deltaTime;
        if (weapon.recoilTimer > 0.0f) weapon.recoilTimer -= deltaTime;

        // ImGui & 渲染
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::Begin("Debug", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("Pos: %.1f, %.1f, %.1f", player.pos.x, player.pos.y, player.pos.z);
        ImGui::Text("Grounded: %s", player.isGrounded ? "YES" : "NO");
        ImGui::Text("Enemies: %zu", enemies.size());
        // 可选：在 Debug 窗口里显示寻路网格状态
        if (ImGui::CollapsingHeader("NavGrid")) {
            ImGui::Text("Cols: %u, Rows: %u", mapData.navCols, mapData.navRows);
            ImGui::Text("Walkable cells: ");
            size_t count = 0;
            for (auto b : mapData.navWalkable) if (b) count++;
            ImGui::SameLine(); ImGui::Text("%zu", count);
        }        
        ImGui::End();

        int scrW, scrH; SDL_GetWindowSize(window, &scrW, &scrH);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)scrW, (float)scrH));
        ImGui::Begin("##crosshair", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 center((float)scrW * 0.5f, (float)scrH * 0.5f);
        
        ImU32 crossColor;
        if (gravityGun.holding) crossColor = IM_COL32(80, 200, 255, 255);
        else if (weapon.crosshairHitTimer > 0.0f) crossColor = IM_COL32(255, 50, 50, 255);
        else crossColor = IM_COL32(255, 255, 255, 255);
        drawList->AddLine(ImVec2(center.x - 5, center.y), ImVec2(center.x + 5, center.y), crossColor, 2.0f);
        drawList->AddLine(ImVec2(center.x, center.y - 5), ImVec2(center.x, center.y + 5), crossColor, 2.0f);

        if (player.damageTimer > 0.0f) {
            drawList->AddRectFilled(ImVec2(0, 0), ImVec2((float)scrW, (float)scrH), IM_COL32(255, 0, 0, (int)(player.damageTimer * 150.0f / 0.2f)));
        }

        char hpText[64];
        snprintf(hpText, sizeof(hpText), "HP: %.0f", player.hp);
        drawList->AddText(ImGui::GetFont(), 32.0f, ImVec2(30, scrH - 60), IM_COL32(255, 255, 255, 255), hpText);

        if (levelComplete) {
            levelCompleteTimer += deltaTime;
            drawList->AddRectFilled(ImVec2(0, 0), ImVec2((float)scrW, (float)scrH), IM_COL32(0, 0, 0, 180));
            const char* winText = "LEVEL COMPLETE";
            ImVec2 textSize = ImGui::CalcTextSize(winText);
            drawList->AddText(ImGui::GetFont(), 64.0f, ImVec2((scrW - textSize.x * 2.0f) * 0.5f, scrH * 0.4f), IM_COL32(255, 215, 0, 255), winText);
            const char* subText = "Press 'R' to Restart";
            ImVec2 subSize = ImGui::CalcTextSize(subText);
            drawList->AddText(ImGui::GetFont(), 24.0f, ImVec2((scrW - subSize.x) * 0.5f, scrH * 0.55f), IM_COL32(255, 255, 255, 255), subText);
        }
        ImGui::End();
        ImGui::Render();

        // OpenGL 绘制
        glViewport(0, 0, scrW, scrH);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        RenderFrame(render, player, weapon, gravityGun, body_interface, mapData, physboxIds, barrels, doorBodies, enemies, bulletHoles, scrW, scrH);
        
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    for (auto id : physboxIds) { body_interface.RemoveBody(id); body_interface.DestroyBody(id); }
    for (auto id : doorBodies) { body_interface.RemoveBody(id); body_interface.DestroyBody(id); }

    ShutdownRender(render);
    ShutdownAudio(audio);
    ShutdownPhysics();

    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext(); SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window); 
    SDL_Quit();
    return 0;
}