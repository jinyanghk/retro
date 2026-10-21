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
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    // ---------- 加载 BSP ----------
    std::ifstream f("map.bsp", std::ios::binary);
    if (!f.is_open()) { printf("Cannot open map.bsp\n"); return 1; }
    char magic[4]; f.read(magic, 4);
    if (strncmp(magic, "MYSB", 4) != 0) { printf("Invalid BSP magic\n"); return 1; }
    uint32_t nv, ni;
    f.read((char*)&nv, 4); f.read((char*)&ni, 4);
    std::vector<float> verts(nv * 3);
    f.read((char*)verts.data(), nv * 3 * 4);
    std::vector<uint32_t> indices(ni);
    f.read((char*)indices.data(), ni * 4);

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
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    // 极简 Shader
    const char* vs_src = R"(
        #version 330 core
        layout(location=0) in vec3 aPos;
        uniform mat4 uMVP;
        out vec3 vNormal; // 传给片元着色器
        void main() { 
            gl_Position = uMVP * vec4(aPos, 1.0); 
            // 我们暂时把坐标归一化当法线用来调试（因为你在 map.cpp 里没导法线）
            vNormal = normalize(aPos); 
        }
    )";
    const char* fs_src = R"(
        #version 330 core
        in vec3 vNormal;
        out vec4 FragColor;
        void main() {
            // 把法线从 [-1, 1] 映射到 [0, 1] 作为颜色
            // 这样 +Z 面就是蓝色，+X 面是红色，+Y 面是绿色
            FragColor = vec4(abs(vNormal), 1.0); 
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

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE) running = false;
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) running = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGui::Begin("Debug");
        ImGui::Text("Vertices: %u, Indices: %u", nv, ni);
        ImGui::End();
        ImGui::Render();

        int w, h; SDL_GetWindowSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(program);

        glm::mat4 projection = glm::perspective(glm::radians(60.0f), (float)w / (float)h, 0.1f, 1000.0f);
        // 相机斜着看
        glm::vec3 cameraPos(200.0f, 200.0f, -300.0f);
        glm::vec3 cameraTarget(0.0f, 0.0f, 64.0f);
        glm::mat4 view = glm::lookAt(cameraPos, cameraTarget, glm::vec3(0, 1, 0));

        // ★ 让方块绕 Z 轴慢慢旋转 ★
        float time = SDL_GetTicks() / 1000.0f;
        glm::mat4 model = glm::rotate(glm::mat4(1.0f), time, glm::vec3(0, 0, 1));

        glm::mat4 mvp = projection * view * model;
        GLint mvp_loc = glGetUniformLocation(program, "uMVP");
        glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, glm::value_ptr(mvp));

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