#include <SDL2/SDL.h>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <fstream>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

struct Triangle {
    glm::vec3 v0, v1, v2;
    glm::vec3 normal;
};

bool RaycastTriangle(const glm::vec3& orig, const glm::vec3& dir, const Triangle& tri, float& t) {
    const float EPSILON = 0.0000001f;
    glm::vec3 edge1 = tri.v1 - tri.v0;
    glm::vec3 edge2 = tri.v2 - tri.v0;
    glm::vec3 h = glm::cross(dir, edge2);
    float a = glm::dot(edge1, h);
    // ★ 防止除零崩溃
    if (fabs(a) < EPSILON) return false; 
    float f = 1.0f / a;
    glm::vec3 s = orig - tri.v0;
    float u = f * glm::dot(s, h);
    if (u < 0.0f || u > 1.0f) return false;
    glm::vec3 q = glm::cross(s, edge1);
    float v = f * glm::dot(dir, q);
    if (v < 0.0f || u + v > 1.0f) return false;
    t = f * glm::dot(edge2, q);
    return t > EPSILON;
}

float TraceRay(const glm::vec3& start, const glm::vec3& dir, const std::vector<Triangle>& tris) {
    float closestT = 999999.0f;
    bool hit = false;
    for (const auto& tri : tris) {
        float t;
        if (RaycastTriangle(start, dir, tri, t)) {
            if (t < closestT) {
                closestT = t;
                hit = true;
            }
        }
    }
    return hit ? closestT : -1.0f;
}

int main(int, char**) {
#ifdef _WIN32
    ::SetProcessDPIAware();
#endif
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    // ... 窗口和 GL 初始化 (与之前相同) ...
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window* window = SDL_CreateWindow("Game", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { printf("GLEW init failed\n"); return 1; }

    // ... ImGui 初始化 ...
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    // ---------- 加载 BSP ----------
    float spawnPos[3] = { 0.0f, 128.0f, -400.0f };
    float spawnAngles[3] = { 0.0f, 0.0f, 0.0f };

    std::ifstream f("map.bsp", std::ios::binary);
    if (!f.is_open()) { printf("Cannot open map.bsp\n"); return 1; }
    char magic[4]; f.read(magic, 4);
    if (strncmp(magic, "MYSB", 4) != 0) { printf("Invalid BSP magic\n"); return 1; }
    uint32_t nv, ni;
    f.read((char*)&nv, 4); f.read((char*)&ni, 4);
    
    printf("BSP Load: nv=%u, ni=%u\n", nv, ni); // 调试信息
    if (nv == 0 || ni == 0) { printf("Empty map!\n"); return 1; }

    std::vector<float> verts(nv * 6);
    f.read((char*)verts.data(), nv * 6 * 4);
    std::vector<uint32_t> indices(ni);
    f.read((char*)indices.data(), ni * 4);

    if (f.read((char*)spawnPos, sizeof(spawnPos))) {
        f.read((char*)spawnAngles, sizeof(spawnAngles));
        printf("Spawn point: %.1f, %.1f, %.1f\n", spawnPos[0], spawnPos[1], spawnPos[2]);
    } else {
        printf("Warning: No spawn point data, using default.\n");
    }
    f.close();

    // ★ 防止顶点索引越界
    std::vector<Triangle> collisionTris;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i+1];
        uint32_t i2 = indices[i+2];
        
        // 边界检查
        if (i0 >= nv || i1 >= nv || i2 >= nv) {
            printf("Warning: Index out of bounds! i0=%u, i1=%u, i2=%u, nv=%u\n", i0, i1, i2, nv);
            continue;
        }
        
        glm::vec3 v0(verts[i0*6], verts[i0*6+1], verts[i0*6+2]);
        glm::vec3 v1(verts[i1*6], verts[i1*6+1], verts[i1*6+2]);
        glm::vec3 v2(verts[i2*6], verts[i2*6+1], verts[i2*6+2]);
        glm::vec3 n(verts[i0*6+3], verts[i0*6+4], verts[i0*6+5]);
        
        collisionTris.push_back({v0, v1, v2, n});
    }
    printf("Loaded %zu collision triangles.\n", collisionTris.size());

    // ... (上传到 GPU 的代码保持不变) ...
    GLuint vao, vbo, ebo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * 4, verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * 4, indices.data(), GL_STATIC_DRAW);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));

    // Shader 编译部分 (与之前相同) ...
    const char* vs_src = R"(
        #version 330 core
        layout(location=0) in vec3 aPos;
        layout(location=1) in vec3 aNormal;
        uniform mat4 uMVP;
        uniform mat4 uModel;
        out vec3 vNormal;
        void main() { 
            gl_Position = uMVP * vec4(aPos, 1.0); 
            vNormal = mat3(uModel) * aNormal;
        }
    )";
    const char* fs_src = R"(
        #version 330 core
        in vec3 vNormal;
        out vec4 FragColor;
        void main() {
            vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
            float diff = max(dot(vNormal, lightDir), 0.0);
            vec3 color = vec3(0.6, 0.6, 0.65) * (0.3 + 0.7 * diff);
            FragColor = vec4(color, 1.0); 
        }
    )";
    auto compile_shader = [](GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        return s;
    };
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    GLuint program = glCreateProgram();
    glAttachShader(program, vs); glAttachShader(program, fs); glLinkProgram(program);
    glDeleteShader(vs); glDeleteShader(fs);

    // ---------- 玩家物理状态 ----------
    glm::vec3 playerPos(spawnPos[0], spawnPos[1] + 36.0f, spawnPos[2]);
    glm::vec3 playerVel(0.0f);
    float camYaw = spawnAngles[1];
    float camPitch = -spawnAngles[0];

    const float playerRadius = 16.0f;
    const float playerHeight = 72.0f;
    const float gravity = 800.0f;
    const float jumpSpeed = 320.0f;
    const float moveSpeed = 300.0f;
    const float mouseSensitivity = 0.1f;
    
    bool isGrounded = false;
    bool mouseLocked = true;
    SDL_SetRelativeMouseMode(SDL_TRUE);

    bool running = true;
    Uint32 lastTime = SDL_GetTicks();

    while (running) {
        Uint32 currentTime = SDL_GetTicks();
        float deltaTime = (currentTime - lastTime) / 1000.0f;
        if (deltaTime > 0.05f) deltaTime = 0.05f; 
        lastTime = currentTime;

        // ... (事件循环保持之前的不变) ...
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE) running = false;
            
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                if (mouseLocked) {
                    mouseLocked = false;
                    SDL_SetRelativeMouseMode(SDL_FALSE);
                } else {
                    running = false;
                }
            }
            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                if (!mouseLocked) {
                    mouseLocked = true;
                    SDL_SetRelativeMouseMode(SDL_TRUE);
                }
            }
            if (event.type == SDL_MOUSEMOTION && mouseLocked) {
                camYaw += event.motion.xrel * mouseSensitivity;
                camPitch -= event.motion.yrel * mouseSensitivity; 
                if (camPitch > 89.0f) camPitch = 89.0f;
                if (camPitch < -89.0f) camPitch = -89.0f;
            }
        }

        // 物理与移动
        if (mouseLocked && !io.WantCaptureKeyboard) {
            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            float yawRad = glm::radians(camYaw);
            
            glm::vec3 forward(cos(yawRad), 0.0f, sin(yawRad));
            glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));

            glm::vec3 wishDir(0.0f);
            if (keys[SDL_SCANCODE_W]) wishDir += forward;
            if (keys[SDL_SCANCODE_S]) wishDir -= forward;
            if (keys[SDL_SCANCODE_D]) wishDir += right;
            if (keys[SDL_SCANCODE_A]) wishDir -= right;

            if (glm::length(wishDir) > 0.0f) {
                wishDir = glm::normalize(wishDir);
                playerVel.x = wishDir.x * moveSpeed;
                playerVel.z = wishDir.z * moveSpeed;
            } else {
                playerVel.x = 0.0f;
                playerVel.z = 0.0f;
            }

            if (keys[SDL_SCANCODE_SPACE] && isGrounded) {
                playerVel.y = jumpSpeed;
                isGrounded = false;
            }
        } else {
            playerVel.x = 0.0f; playerVel.z = 0.0f;
        }

        playerVel.y -= gravity * deltaTime;
        isGrounded = false;

        // ★ 碰撞检测优化：防止射线穿出地图产生极长射线
        glm::vec3 moveY(0.0f, playerVel.y * deltaTime, 0.0f);
        glm::vec3 newPosY = playerPos + moveY;
        
        float floorDist = TraceRay(newPosY, glm::vec3(0.0f, -1.0f, 0.0f), collisionTris);
        
        if (floorDist != -1.0f && floorDist <= (playerHeight * 0.5f + 0.5f)) {
            newPosY.y = newPosY.y - (floorDist - playerHeight * 0.5f);
            playerVel.y = 0.0f;
            if (moveY.y <= 0.0f) isGrounded = true;
        }

        float ceilDist = TraceRay(newPosY, glm::vec3(0.0f, 1.0f, 0.0f), collisionTris);
        if (ceilDist != -1.0f && ceilDist <= (playerHeight * 0.5f + 0.5f)) {
            newPosY.y = newPosY.y + (ceilDist - playerHeight * 0.5f);
            playerVel.y = 0.0f;
        }
        
        playerPos = newPosY;

        // ★ 水平碰撞的 X 轴
        float dx = playerVel.x * deltaTime;
        if (fabs(dx) > 0.0001f) {
            glm::vec3 dirX(signbit(dx) ? -1.0f : 1.0f, 0.0f, 0.0f);
            float distX = TraceRay(playerPos, dirX, collisionTris);
            if (distX != -1.0f && distX < playerRadius) {
                playerPos.x += (distX - playerRadius) * dirX.x;
                playerVel.x = 0.0f;
            } else {
                playerPos.x += dx;
            }
        }

        // ★ 水平碰撞的 Z 轴
        float dz = playerVel.z * deltaTime;
        if (fabs(dz) > 0.0001f) {
            glm::vec3 dirZ(0.0f, 0.0f, signbit(dz) ? -1.0f : 1.0f);
            float distZ = TraceRay(playerPos, dirZ, collisionTris);
            if (distZ != -1.0f && distZ < playerRadius) {
                playerPos.z += (distZ - playerRadius) * dirZ.z;
                playerVel.z = 0.0f;
            } else {
                playerPos.z += dz;
            }
        }

        // ... 渲染部分保持不变 ...
        // ---------- 渲染 ----------
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::Begin("Debug", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("Pos: %.1f, %.1f, %.1f", playerPos.x, playerPos.y, playerPos.z);
        ImGui::Text("Grounded: %s", isGrounded ? "YES" : "NO");
        ImGui::End();

        // ★ 准星：用透明全屏窗口承载，必须在 Render() 之前
        int scrW, scrH; SDL_GetWindowSize(window, &scrW, &scrH);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)scrW, (float)scrH));
        ImGui::Begin("##crosshair", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoBackground);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 center((float)scrW * 0.5f, (float)scrH * 0.5f);
        drawList->AddLine(ImVec2(center.x - 5, center.y), ImVec2(center.x + 5, center.y), IM_COL32(255, 255, 255, 255), 2.0f);
        drawList->AddLine(ImVec2(center.x, center.y - 5), ImVec2(center.x, center.y + 5), IM_COL32(255, 255, 255, 255), 2.0f);
        ImGui::End();

        ImGui::Render();

        int w, h; SDL_GetWindowSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(program);

        glm::mat4 projection = glm::perspective(glm::radians(90.0f), (float)w / (float)h, 0.1f, 5000.0f);
        
        float pitchRad = glm::radians(camPitch);
        glm::vec3 front;
        front.x = cos(glm::radians(camYaw)) * cos(pitchRad);
        front.y = sin(pitchRad);
        front.z = sin(glm::radians(camYaw)) * cos(pitchRad);
        
        glm::vec3 eyePos = playerPos + glm::vec3(0.0f, 8.0f, 0.0f);
        glm::vec3 cameraTarget = eyePos + glm::normalize(front);
        
        glm::mat4 view = glm::lookAt(eyePos, cameraTarget, glm::vec3(0, 1, 0));
        glm::mat4 model = glm::mat4(1.0f);
        glm::mat4 mvp = projection * view * model;
        
        glUniformMatrix4fv(glGetUniformLocation(program, "uMVP"), 1, GL_FALSE, glm::value_ptr(mvp));
        glUniformMatrix4fv(glGetUniformLocation(program, "uModel"), 1, GL_FALSE, glm::value_ptr(model));

        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, ni, GL_UNSIGNED_INT, 0);


        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // ... 清理代码保持不变 ...
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}