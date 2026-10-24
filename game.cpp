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

#ifdef _WIN32
#include <windows.h>
#endif

int main(int, char**) {
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

    SDL_Window* window = SDL_CreateWindow("Game", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { printf("GLEW init failed\n"); return 1; }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    // ---------- 加载 BSP ----------
    float spawnPos[3] = { 0.0f, 128.0f, -400.0f }; // 默认兜底
    float spawnAngles[3] = { 0.0f, 0.0f, 0.0f };

    std::ifstream f("map.bsp", std::ios::binary);
    if (!f.is_open()) { printf("Cannot open map.bsp\n"); return 1; }
    char magic[4]; f.read(magic, 4);
    if (strncmp(magic, "MYSB", 4) != 0) { printf("Invalid BSP magic\n"); return 1; }
    
    uint32_t nv, ni;
    f.read((char*)&nv, 4); f.read((char*)&ni, 4);
    
    std::vector<float> verts(nv * 6);
    f.read((char*)verts.data(), nv * 6 * 4);
    std::vector<uint32_t> indices(ni);
    f.read((char*)indices.data(), ni * 4);

    // ★ 尝试读取文件末尾的出生点数据
    if (f.read((char*)spawnPos, sizeof(spawnPos))) {
        f.read((char*)spawnAngles, sizeof(spawnAngles));
        printf("Loaded info_player_start at (%.1f, %.1f, %.1f)\n", spawnPos[0], spawnPos[1], spawnPos[2]);
    } else {
        printf("Warning: BSP file lacks spawn point data. Using default.\n");
    }
    f.close();

    // 上传到 GPU
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

    // Shader
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

    // ---------- 第一人称相机状态 ----------
    glm::vec3 camPos(spawnPos[0], spawnPos[1], spawnPos[2]);
    // Half-Life 的角度：Yaw(偏航) 0度朝向+X，Pitch(俯仰)正数朝下。对应到我们的 Y-up 视角需要适当转换。
    float camYaw = spawnAngles[1];   // 使用实体的 Yaw
    float camPitch = -spawnAngles[0]; // 实体的 Pitch 取反（HL里正俯仰是往下看）
    
    const float moveSpeed = 300.0f;
    const float mouseSensitivity = 0.1f; // 鼠标灵敏度
    
    // 鼠标锁定标志位（默认启动就锁定，按 ESC 解除，点击窗口重新锁定）
    bool mouseLocked = true;
    SDL_SetRelativeMouseMode(SDL_TRUE);

    bool running = true;
    Uint32 lastTime = SDL_GetTicks();

    while (running) {
        Uint32 currentTime = SDL_GetTicks();
        float deltaTime = (currentTime - lastTime) / 1000.0f;
        lastTime = currentTime;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE) running = false;
            
            // 处理鼠标锁定
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                if (mouseLocked) {
                    mouseLocked = false;
                    SDL_SetRelativeMouseMode(SDL_FALSE);
                } else {
                    running = false; // 未锁定时再按 ESC 退出游戏
                }
            }
            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                if (!mouseLocked) {
                    mouseLocked = true;
                    SDL_SetRelativeMouseMode(SDL_TRUE);
                }
            }

            // FPS 视角旋转
            if (event.type == SDL_MOUSEMOTION && mouseLocked) {
                camYaw += event.motion.xrel * mouseSensitivity;
                camPitch -= event.motion.yrel * mouseSensitivity; 
                // 限制俯仰角度（-89度到89度，防止画面翻转）
                if (camPitch > 89.0f) camPitch = 89.0f;
                if (camPitch < -89.0f) camPitch = -89.0f;
            }
        }

        // ---------- 键盘移动 ----------
        // 如果鼠标没锁定（比如在切出去操作 UI），就不要响应键盘移动，避免冲突
        if (mouseLocked && !io.WantCaptureKeyboard) {
            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            float yawRad = glm::radians(camYaw);
            
            // 计算水平前向向量
            glm::vec3 forward(cos(yawRad), 0.0f, sin(yawRad));
            glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));

            if (keys[SDL_SCANCODE_W]) camPos += forward * moveSpeed * deltaTime;
            if (keys[SDL_SCANCODE_S]) camPos -= forward * moveSpeed * deltaTime;
            if (keys[SDL_SCANCODE_A]) camPos -= right * moveSpeed * deltaTime;
            if (keys[SDL_SCANCODE_D]) camPos += right * moveSpeed * deltaTime;
            if (keys[SDL_SCANCODE_SPACE]) camPos.y += moveSpeed * deltaTime;
            if (keys[SDL_SCANCODE_LSHIFT]) camPos.y -= moveSpeed * deltaTime;
        }

        // ---------- 渲染 ----------
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGui::Begin("Debug");
        ImGui::Text("Vertices: %u, Indices: %u", nv, ni);
        ImGui::Text("Camera Pos: %.1f, %.1f, %.1f", camPos.x, camPos.y, camPos.z);
        ImGui::Text("Yaw: %.1f, Pitch: %.1f", camYaw, camPitch);
        ImGui::Text("Mouse Locked: %s (Press ESC to toggle)", mouseLocked ? "YES" : "NO");
        ImGui::End();
        ImGui::Render();

        int w, h; SDL_GetWindowSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);          // 恢复背面剔除
        glCullFace(GL_BACK);
        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(program);

        glm::mat4 projection = glm::perspective(glm::radians(90.0f), (float)w / (float)h, 0.1f, 5000.0f);
        
        // 计算 LookAt 的 Target
        float pitchRad = glm::radians(camPitch);
        glm::vec3 front;
        front.x = cos(glm::radians(camYaw)) * cos(pitchRad);
        front.y = sin(pitchRad);
        front.z = sin(glm::radians(camYaw)) * cos(pitchRad);
        
        glm::vec3 cameraTarget = camPos + glm::normalize(front);
        glm::mat4 view = glm::lookAt(camPos, cameraTarget, glm::vec3(0, 1, 0));

        glm::mat4 model = glm::mat4(1.0f);
        glm::mat4 mvp = projection * view * model;
        
        glUniformMatrix4fv(glGetUniformLocation(program, "uMVP"), 1, GL_FALSE, glm::value_ptr(mvp));
        glUniformMatrix4fv(glGetUniformLocation(program, "uModel"), 1, GL_FALSE, glm::value_ptr(model));

        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, ni, GL_UNSIGNED_INT, 0);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}