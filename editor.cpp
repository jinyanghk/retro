#include <GL/glew.h>

#include <SDL.h>
#include <SDL_opengl.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include "ImGuizmo.h"

#include <fstream>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#endif

// 极简 Shader
const char* vs_src = R"(
    #version 330 core
    layout(location=0) in vec3 aPos;
    layout(location=1) in vec3 aNormal;
    uniform mat4 uMVP;
    out vec3 vNormal;
    void main() { 
        gl_Position = uMVP * vec4(aPos, 1.0); 
        vNormal = aNormal;
    }
)";
const char* fs_src = R"(
    #version 330 core
    in vec3 vNormal;
    out vec4 FragColor;
    void main() { 
        FragColor = vec4(abs(vNormal), 1.0); 
    }
)";

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

    float main_scale = 1.0f;
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("Map Editor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800, window_flags);

    int glW = 1280, glH = 800;
    SDL_GetWindowSizeInPixels(window, &glW, &glH);

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { printf("GLEW init failed\n"); return 1; }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;
    
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    // 立方体数据（带法线）
    float cube_verts[] = {
        -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,   0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,   0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
        -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,   0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
        -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,   0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,   0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
        -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,   0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
        -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
        -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
         0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,   0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,   0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
         0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,   0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,   0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,   0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,   0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,   0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
        -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,   0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,   0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
        -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,   0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f
    };

    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cube_verts), cube_verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));

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

    // 相机与 Gizmo 状态
    glm::vec3 camPos(3.0f, -3.0f, 3.0f);
    glm::vec3 camTarget(0.0f, 0.0f, 0.0f);
    glm::mat4 view = glm::lookAt(camPos, camTarget, glm::vec3(0, 0, 1));

    glm::mat4 modelMatrix = glm::mat4(1.0f);

    float baseSize = 64.0f; 

    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) done = true;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE) done = true;
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) done = true;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        ImGuiIO& io = ImGui::GetIO();

        // 1. 你的 UI 窗口
        ImGui::Begin("Map Editor");
        ImGui::Text("Use the gizmo to move/scale the cube.");
        ImGui::Text("Mouse: %.1f, %.1f", io.MousePos.x, io.MousePos.y);
        
        if (ImGui::Button("Export map.json")) {
            glm::vec3 pos = glm::vec3(modelMatrix[3]);
            glm::vec3 scale;
            scale.x = glm::length(glm::vec3(modelMatrix[0]));
            scale.y = glm::length(glm::vec3(modelMatrix[1]));
            scale.z = glm::length(glm::vec3(modelMatrix[2]));

            float hx = (baseSize * 0.5f) * scale.x;
            float hy = (baseSize * 0.5f) * scale.y;
            float hz = (baseSize * 0.5f) * scale.z;

            std::ofstream f("map.json");
            f << "{\n  \"brushes\": [\n    {\n";
            f << "      \"min\": [" << (pos.x - hx) << ", " << (pos.y - hy) << ", " << (pos.z - hz) << "],\n";
            f << "      \"max\": [" << (pos.x + hx) << ", " << (pos.y + hy) << ", " << (pos.z + hz) << "],\n";
            f << "      \"material\": \"dev_grid\"\n    }\n  ],\n";
            f << "  \"entities\": [\n    { \"classname\": \"info_player_start\", \"origin\": [0, 0, 16] }\n  ]\n}\n";
            f.close();
            printf("Exported map.json\n");
        }
        ImGui::End();

        // 2. 先计算投影矩阵
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), io.DisplaySize.x / io.DisplaySize.y, 0.1f, 1000.0f);

        // 3. 绘制 ImGuizmo 的透明全屏层（这层必须放在最后，或者紧贴在 ImGui 渲染前）
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0,0,0,0)); // 完全透明
        
        ImGui::Begin("GizmoOverlay", nullptr, 
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | 
            ImGuiWindowFlags_NoBringToFrontOnFocus); 
        
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
        
        ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection),
                             ImGuizmo::TRANSLATE, ImGuizmo::WORLD,
                             glm::value_ptr(modelMatrix));
                             
        ImGui::End();
        ImGui::PopStyleColor();

        // 4. 渲染 3D 场景
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(program);
        glm::mat4 mvp = projection * view * modelMatrix;
        GLint mvp_loc = glGetUniformLocation(program, "uMVP");
        glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, glm::value_ptr(mvp));

        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 36);

        // 5. 渲染 ImGui
        ImGui::Render();
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