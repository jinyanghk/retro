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
#include "Level.h"
#include "FileSystem.h"

#include <exception>
#include <cstdlib>

void MyTerminate() {
    printf("[CRASH] Unhandled exception, terminating.\n");
    fflush(stdout);
    std::abort();
}

int main(int argc, char** argv) {
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
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window* window = SDL_CreateWindow("Retro Engine", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
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
    if (!LoadMap((currentMap+".bvh").c_str(), mapData)) return 1;

    std::vector<BulletHole> bulletHoles;
    std::vector<CollisionTri> collisionTris;
    // 🔥 改用 collisionIndices 和 collisionVerts 来构建 BVH
    // 计算顶点数组的安全上限（以顶点个数计，而不是浮点数个数）
    size_t vertexMemoryCap = mapData.collisionVerts.size() / 3;
    
    for (size_t i = 0; i + 2 < mapData.collisionIndices.size(); i += 3) {
        uint32_t i0 = mapData.collisionIndices[i];
        uint32_t i1 = mapData.collisionIndices[i+1];
        uint32_t i2 = mapData.collisionIndices[i+2];
        
        // 严格越界检查，如果索引大于顶点总数，直接跳过这个三角形
        if (i0 >= vertexMemoryCap || i1 >= vertexMemoryCap || i2 >= vertexMemoryCap) {
            continue;
        }
        
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

    printf("[Main] Before InitPhysics\n"); fflush(stdout);
    InitPhysics();
    printf("[Main] After InitPhysics\n"); fflush(stdout);
    JPH::BodyInterface& body_interface = GetBodyInterface();
    CreateStaticMesh(mapData.collisionVerts, mapData.collisionIndices, mapData.cnv, mapData.cni);

    JPH::BodyID player_body = CreatePlayerBody(mapData.spawnPos[0], mapData.spawnPos[1] + 36.0f, mapData.spawnPos[2]);

    std::vector<JPH::BodyID> physboxIds;
    for (size_t i = 0; i < mapData.physboxMins.size(); i++) {
        glm::vec3 mn = mapData.physboxMins[i];
        glm::vec3 mx = mapData.physboxMaxs[i];
        glm::vec3 center = (mn + mx) * 0.5f;
        glm::vec3 halfSize = (mx - mn) * 0.5f;
        
        // 👈 终极简化：只要这个箱子悬空（中心高度大于 400），它就是陷阱铁块！
        if (center.y > 400.0f) {
            physboxIds.push_back(CreateTrapBody(center.x, center.y, center.z, halfSize.x, halfSize.y, halfSize.z));
            printf("[TRAP] Created Kinematic trap block at Y=%.1f\n", center.y);
        } else {
            physboxIds.push_back(CreatePhysBox(center.x, center.y, center.z, halfSize.x, halfSize.y, halfSize.z));
        }
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


    // ==========================================================
    // 👈 修复 AI 卡墙：过滤掉悬空或卡在墙里的非法导航节点
    // ==========================================================
    std::vector<MapData::NodeData> validNodes;
    std::vector<uint32_t> remapIndex(mapData.navNodes.size(), -1); // 旧索引 -> 新索引映射

    for (size_t i = 0; i < mapData.navNodes.size(); ++i) {
        const auto& node = mapData.navNodes[i];
        glm::vec3 dummyN;
        
        // 1. 向下探测地面：从节点上方 64 单位向下打射线
        float distToFloor = TraceBVH(node.pos + glm::vec3(0, 64, 0), glm::vec3(0, -1, 0), dummyN);
        
        // 如果打不到地面，或者地面高度离节点太远（比如 > 80），说明这节点悬空，丢弃！
        if (distToFloor < 0.0f || distToFloor > 144.0f) { 
            continue; 
        }

        // 2. 检查它周围有没有被实体墙壁完全包裹（简单的 4 向探墙）
        bool valid = true;
        glm::vec3 dirs[4] = {{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}};
        int blockedCount = 0;
        for (auto& d : dirs) {
            float wallDist = TraceBVH(node.pos + glm::vec3(0, 32, 0), d, dummyN);
            // 如果每个方向 16 单位内就有墙，说明它卡在极小的角落里
            if (wallDist > 0.0f && wallDist < 16.0f) blockedCount++;
        }
        if (blockedCount >= 3) continue; // 被三面墙堵死，判定为非法节点

        // 把这个节点加入合法列表
        remapIndex[i] = (uint32_t)validNodes.size();
        validNodes.push_back(node);
    }

    // 3. 更新合法节点的 links 索引，并剔除穿墙的非法连线
    for (auto& node : validNodes) {
        std::vector<uint32_t> newLinks;
        for (auto oldLink : node.links) {
            if (oldLink < remapIndex.size() && remapIndex[oldLink] != -1) {
                // 👈 核心修复：射线检测两个节点之间有没有墙
                glm::vec3 targetPos = mapData.navNodes[oldLink].pos;
                glm::vec3 dir = targetPos - node.pos;
                float dist = glm::length(dir);
                
                if (dist < 0.1f) continue; // 防除零
                dir = glm::normalize(dir);
                
                // 从当前节点往目标节点打射线
                glm::vec3 hitNormal;
                float hitDist = TraceBVH(node.pos + glm::vec3(0, 32, 0), dir, hitNormal); // 抬高32单位，避免打到地板
                
                // 如果射线没打到东西，或者打到的距离比两个节点之间的距离还远（没被挡住），才保留连线
                if (hitDist < 0.0f || hitDist > dist - 10.0f) {
                    newLinks.push_back(remapIndex[oldLink]);
                } else {
                    // 被挡住了，这就是一条穿墙的假连线！
                    // printf("[Nav] Removed illegal link from %zu to %u\n", i, oldLink);
                }
            }
        }
        node.links = newLinks;
    }

    // 把过滤后的节点覆盖回去
    mapData.navNodes = std::move(validNodes);
    printf("[Nav] Filtered nodes: %zu valid nodes remain.\n", mapData.navNodes.size());


    std::vector<DoorStateForTrace> doorTrace;
    std::vector<EnemyInfo> enemies;
    auto SpawnAllEnemies = [&]() {
        for (auto& e : enemies) {
            if (e.charId >= 0) {
                DestroyEnemyCharacter(e.charId);
                e.charId = -1;
            }
        }
        enemies.clear();

        for (auto& pos : mapData.zombieSpawns) {
            EnemyInfo e;
            glm::vec3 spawnPos = pos;
            
            glm::vec3 dummyNormal;
            // 向下探测
            float floorDist = TraceBVH(glm::vec3(spawnPos.x, spawnPos.y + 20.0f, spawnPos.z), glm::vec3(0, -1, 0), dummyNormal);
            if (floorDist >= 0.0f && floorDist < 500.0f) {
                spawnPos.y = spawnPos.y + 20.0f - floorDist + e.height * 0.5f;
            }
            
            e.pos = spawnPos;
            e.charId = CreateEnemyCharacter(spawnPos.x, spawnPos.y, spawnPos.z);
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
        //printf("[Loop] Frame start\n");  // 👈 加这行
        fflush(stdout);
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
        //printf("[Loop] Before UpdatePlayer\n"); fflush(stdout);
        UpdatePlayer(player, deltaTime, physboxIds, barrels, doorTrace, body_interface, player_body, mapData.physboxMins, mapData.physboxMaxs);
        //printf("[Loop] After UpdatePlayer\n"); fflush(stdout);
        PlayerPushEntities(player, deltaTime, body_interface, physboxIds, barrels, mapData.physboxMins, mapData.physboxMaxs);

        // 👈 新增：更新屏幕震动衰减
        if (player.shakeTimer > 0.0f) {
            player.shakeTimer -= deltaTime;
            if (player.shakeTimer <= 0.0f) {
                player.shakeAmount = 0.0f;
            }
        }

        // ====================================================================
        // 触发器检测
        // ====================================================================
        if (!levelComplete && currentState == LevelState::PLAYING) {
            glm::vec3 pMin = player.pos - glm::vec3(player.radius, player.height * 0.5f, player.radius);
            glm::vec3 pMax = player.pos + glm::vec3(player.radius, player.height * 0.5f, player.radius);
            
            for (size_t i = 0; i < mapData.triggerMins.size(); i++) {
                if (pMax.x >= mapData.triggerMins[i].x && pMin.x <= mapData.triggerMaxs[i].x &&
                    pMax.y >= mapData.triggerMins[i].y && pMin.y <= mapData.triggerMaxs[i].y &&
                    pMax.z >= mapData.triggerMins[i].z && pMin.z <= mapData.triggerMaxs[i].z) {
                    
                    const std::string& tgt = mapData.triggerTargets[i];
                    if (!tgt.empty()) {
                        // 查找该触发器对应的实体
                        for (const auto& e : mapData.entities) {
                            if (e.kv.contains("customs") && e.kv["customs"].contains("next_map")) {
                                std::string nMap = e.kv["customs"]["next_map"].get<std::string>();
                                if (!nMap.empty()) {
                                    nextMap = nMap;
                                    currentState = LevelState::DOOR_CLOSING;
                                    transitionTimer = 0.0f;
                                    
                                    // 👈 强制关闭所有门
                                    for (auto& d : mapData.doors) {
                                        d.isOpening = false;
                                        d.waitTimer = 9999.0f;
                                    }
                                    printf("[SYSTEM] Triggered! Waiting for doors to close...\n");
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }

        // ==========================================================
        // 👈 新增：DOOR_CLOSING 状态（等待门关上）
        // ==========================================================
        if (currentState == LevelState::DOOR_CLOSING) {
            transitionTimer += deltaTime;
            
            // 检查是否所有门都关上了
            bool allClosed = true;
            for (auto& d : mapData.doors) {
                if (d.openAmount > 0.05f) {
                    allClosed = false;
                    break;
                }
            }

            // 如果门关上了，或者超时了（防止卡死）
            if (allClosed || transitionTimer > 2.0f) {
                printf("[SYSTEM] Doors closed. Loading next map...\n");
                pendingMapSwitch = true;
                currentState = LevelState::PLAYING; // 临时切回来，切图代码会接管
            }
        }

        //printf("[Loop] Before UpdateEnemies\n"); fflush(stdout);
                UpdateEnemies(enemies, player, deltaTime, body_interface, mapData, 
                physboxIds, barrels, doorTrace, 
                mapData.physboxMins, mapData.physboxMaxs, 
                gravityGun, // 👈 新增
                audio.sndHit);
        //printf("[Loop] After UpdateEnemies\n"); fflush(stdout);

        if (player.damageTimer > 0.0f) player.damageTimer -= deltaTime;
        if (player.hp <= 0.0f) {
            player.hp = 100.0f;
            player.pos = glm::vec3(mapData.spawnPos[0], mapData.spawnPos[1] + 36.0f, mapData.spawnPos[2]);
            player.vel = glm::vec3(0);
        }

        if (!levelComplete && bFirePressed) {
            FireWeapon(player, weapon, gravityGun, deltaTime, body_interface, physboxIds, doorTrace, enemies, barrels, bulletHoles, audio.sndShoot, audio.sndHit, audio.sndExplosion);
            
            // 👈 新增：开火震动
            player.shakeAmount = 1.5f;
            player.shakeTimer = 0.1f;
        }

        UpdateGravityGun(player, gravityGun, bGrabPressed, bThrowPressed, deltaTime, body_interface, physboxIds, doorTrace, enemies, barrels, audio.sndDoor, audio.sndShoot);

        //printf("[Loop] Before UpdatePhysics\n"); fflush(stdout);
        UpdatePhysics(deltaTime);
        //printf("[Loop] After UpdatePhysics\n"); fflush(stdout);

        // ==========================================================
        // 门与开关交互（按 E 开门）
        // ==========================================================
        if (bUsePressed && currentState == LevelState::PLAYING) { // 👈 只有正常游玩状态才允许按 E
            glm::vec3 eyePos = player.GetEyePos();
            glm::vec3 front = player.GetForward();
            float bestT = 64.0f; // 射线探测距离（64个单位）
            int bestDoor = -1;

            // 1. 向正前方发射线，找最近的门
            for (size_t i = 0; i < mapData.doors.size(); i++) {
                // 计算门当前的位置（加上它已经滑动的距离）
                glm::vec3 off = mapData.doors[i].openDir * (mapData.doors[i].openDist * mapData.doors[i].openAmount);
                float tmin, tmax;
                // RayAABB 是射线与 AABB 包围盒的求交函数
                if (RayAABB(eyePos, front, mapData.doors[i].mins + off, mapData.doors[i].maxs + off, tmin, tmax)) {
                    if (tmin >= 0.0f && tmin < bestT) { 
                        bestT = tmin; 
                        bestDoor = (int)i; 
                    }
                }
            }

            // 2. 如果找到了门，且门没开，就打开它
            if (bestDoor >= 0 && mapData.doors[bestDoor].openAmount < 0.01f && !mapData.doors[bestDoor].isOpening) {
                mapData.doors[bestDoor].isOpening = true;
                mapData.doors[bestDoor].waitTimer = 0.0f; // 重置等待计时器
                if (audio.sndDoor) Mix_PlayChannel(-1, audio.sndDoor, 0); // 播放开门音效
                printf("[GAME] Player opened door: %s\n", mapData.doors[bestDoor].targetname.c_str());
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
        
        // 👈 新增：丧尸列表控制台
        if (ImGui::CollapsingHeader("Enemy List", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (size_t i = 0; i < enemies.size(); ++i) {
                auto& e = enemies[i];
                ImGui::PushID((int)i);
                ImGui::Text("Z%d: State=%d, Stuck=%.1f", (int)i, (int)e.state, e.stuckTimer);
                ImGui::SameLine();
                if (ImGui::Button("Kill")) {
                    e.hp = 0; 
                    e.state = EnemyState::DEAD; 
                }
                ImGui::SameLine();
                if (ImGui::Button("TP")) {
                    player.pos = e.pos + glm::vec3(100.0f, 0.0f, 100.0f);
                }
                ImGui::PopID();
            }
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

            // 👈 新增：受击震动
            player.shakeAmount = 5.0f;
            player.shakeTimer = 0.3f;
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

        // ==========================================================
        // 👈 可视化 AI 调试工具 (3D 投影到 2D 屏幕)
        // ==========================================================
        ImDrawList* fgDrawList = ImGui::GetForegroundDrawList();
        for (const auto& e : enemies) {
            if (e.state == EnemyState::DEAD) continue;

            // 辅助函数：将世界坐标 3D 投影到屏幕 2D
            auto WorldToScreen = [&](const glm::vec3& worldPos) {
                glm::mat4 view = glm::lookAt(player.GetEyePos(), player.GetEyePos() + player.GetForward(), glm::vec3(0, 1, 0));
                glm::mat4 proj = glm::perspective(glm::radians(85.0f), (float)scrW / (float)scrH, 0.1f, 100000.0f);
                glm::vec4 clip = proj * view * glm::vec4(worldPos, 1.0f);
                if (clip.w <= 0.0f) return ImVec2(-9999, -9999); // 在相机后面
                glm::vec3 ndc = glm::vec3(clip) / clip.w;
                return ImVec2((ndc.x + 1.0f) * 0.5f * scrW, (1.0f - ndc.y) * 0.5f * scrH);
            };

            // 1. 画丧尸的物理中心（黄色大字）
            ImVec2 centerScreen = WorldToScreen(e.pos);
            fgDrawList->AddText(centerScreen, IM_COL32(255, 255, 0, 255), "ZOMBIE");

            // 2. 画 A* 路径（绿色线段）
            if (!e.path.empty()) {
                for (size_t i = 0; i < e.path.size() - 1; i++) {
                    ImVec2 p1 = WorldToScreen(e.path[i]);
                    ImVec2 p2 = WorldToScreen(e.path[i+1]);
                    // 避免在屏幕外乱画
                    if (p1.x > -1000 && p2.x > -1000) {
                        fgDrawList->AddLine(p1, p2, IM_COL32(0, 255, 0, 255), 2.0f);
                    }
                }
                // 画当前指向的路径节点（红色圆圈）
                if (e.pathIndex < e.path.size()) {
                    ImVec2 targetScreen = WorldToScreen(e.path[e.pathIndex]);
                    if (targetScreen.x > -1000) {
                        fgDrawList->AddCircle(targetScreen, 6.0f, IM_COL32(255, 0, 0, 255), 12, 2.0f);
                    }
                }
            }
        }

        ImGui::Render();

        // OpenGL 绘制
        glViewport(0, 0, scrW, scrH);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        //printf("[Loop] Before RenderFrame\n"); fflush(stdout);
        RenderFrame(render, player, weapon, gravityGun, body_interface, mapData, physboxIds, barrels, doorBodies, enemies, bulletHoles, scrW, scrH);
        //printf("[Loop] After RenderFrame\n"); fflush(stdout);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // ==========================================================
        // 👈 核心：执行地图加载与切换
        // ==========================================================
        if (pendingMapSwitch) {
            printf("[SYSTEM] Loading new map: %s\n", nextMap.c_str());
            
            // 1. 清空旧地图
            CleanupCurrentLevel(body_interface, physboxIds, doorBodies, enemies, player_body);
            
            // 2. 加载新地图
            currentMap = nextMap;
            mapData = MapData(); 
            if (!LoadMap((currentMap+".bvh").c_str(), mapData)) {
                printf("[ERROR] Failed to load map: %s\n", currentMap.c_str());
                running = false;
                break;
            }

            // 3. 重建 BVH 树
            size_t vertexMemoryCap = mapData.collisionVerts.size() / 3;
            for (size_t i = 0; i + 2 < mapData.collisionIndices.size(); i += 3) {
                uint32_t i0 = mapData.collisionIndices[i], i1 = mapData.collisionIndices[i+1], i2 = mapData.collisionIndices[i+2];
                if (i0 >= vertexMemoryCap || i1 >= vertexMemoryCap || i2 >= vertexMemoryCap) continue;
                glm::vec3 v0(mapData.collisionVerts[i0*3], mapData.collisionVerts[i0*3+1], mapData.collisionVerts[i0*3+2]);
                glm::vec3 v1(mapData.collisionVerts[i1*3], mapData.collisionVerts[i1*3+1], mapData.collisionVerts[i1*3+2]);
                glm::vec3 v2(mapData.collisionVerts[i2*3], mapData.collisionVerts[i2*3+1], mapData.collisionVerts[i2*3+2]);
                glm::vec3 n = glm::normalize(glm::cross(v1 - v0, v2 - v0));
                g_sortedTris.push_back({v0, v1, v2, n});
            }
            g_bvh.reserve(g_sortedTris.size() * 2 + 1);
            if (!g_sortedTris.empty()) BuildBVH(0, (int)g_sortedTris.size());

            // 4. 重建物理网格和玩家
            CreateStaticMesh(mapData.collisionVerts, mapData.collisionIndices, mapData.cnv, mapData.cni);
            player_body = CreatePlayerBody(mapData.spawnPos[0], mapData.spawnPos[1] + 36.0f, mapData.spawnPos[2]);
            player.pos = glm::vec3(mapData.spawnPos[0], mapData.spawnPos[1] + 36.0f, mapData.spawnPos[2]);
            player.vel = glm::vec3(0);

            // 5. 重建箱子、门、敌人
            for (size_t i = 0; i < mapData.physboxMins.size(); i++) {
                glm::vec3 center = (mapData.physboxMins[i] + mapData.physboxMaxs[i]) * 0.5f;
                glm::vec3 halfSize = (mapData.physboxMaxs[i] - mapData.physboxMins[i]) * 0.5f;
                physboxIds.push_back(CreatePhysBox(center.x, center.y, center.z, halfSize.x, halfSize.y, halfSize.z));
            }
            for (size_t i = 0; i < mapData.doors.size(); i++) {
                glm::vec3 center = (mapData.doors[i].mins + mapData.doors[i].maxs) * 0.5f;
                glm::vec3 halfSize = (mapData.doors[i].maxs - mapData.doors[i].mins) * 0.5f;
                doorBodies.push_back(CreateDoorBody(center.x, center.y, center.z, halfSize.x, halfSize.y, halfSize.z));
            }
            for (auto& pos : mapData.zombieSpawns) {
                EnemyInfo e;
                e.pos = pos;
                e.charId = CreateEnemyCharacter(pos.x, pos.y, pos.z);
                enemies.push_back(e);
            }

            // 6. 重建渲染资产
            ShutdownRender(render);
            InitRender(render, mapData);

            pendingMapSwitch = false;
            currentState = LevelState::DOOR_OPENING; // 👈 加载完成，进入开门状态
            transitionTimer = 0.0f;
            printf("[SYSTEM] Map loaded successfully!\n");
        }

        // ==========================================================
        // 👈 新增：DOOR_OPENING 状态（新图加载完后，开门）
        // ==========================================================
        if (currentState == LevelState::DOOR_OPENING) {
            transitionTimer += deltaTime;
            // 等待 0.5 秒再开门，让玩家有个心理缓冲
            if (transitionTimer > 0.5f) {
                for (auto& d : mapData.doors) {
                    if (d.targetname == "elevator_door") {
                        d.isOpening = true;
                        d.waitTimer = 0.0f;
                    }
                }
                currentState = LevelState::PLAYING;
                printf("[SYSTEM] Doors opening. Back to gameplay!\n");
            }
        }

        //printf("[Loop] Before SwapWindow\n"); fflush(stdout);
        SDL_GL_SwapWindow(window);
        //printf("[Loop] After SwapWindow\n"); fflush(stdout);
    }
    //printf("[Main] Exiting while loop\n"); fflush(stdout);

    for (auto id : physboxIds) { body_interface.RemoveBody(id); body_interface.DestroyBody(id); }
    //printf("[Main] Cleaned physbox\n"); fflush(stdout);
    for (auto id : doorBodies) { body_interface.RemoveBody(id); body_interface.DestroyBody(id); }
    //printf("[Main] Cleaned door\n"); fflush(stdout);

    ShutdownRender(render);
    //printf("[Main] Render shutdown done\n"); fflush(stdout);
    ShutdownAudio(audio);
    ShutdownPhysics();
    //printf("[Main] All shutdown done\n"); fflush(stdout);

    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext(); SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window); 
    SDL_Quit();
    //printf("[Main] Exiting...\n");
    fflush(stdout);
    return 0;
}