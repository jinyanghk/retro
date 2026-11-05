cmake_minimum_required(VERSION 3.16)
project(retro)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED)
find_package(SDL2_mixer REQUIRED)
find_package(glm REQUIRED)
find_package(Jolt REQUIRED)
find_package(GLEW REQUIRED)
find_package(OpenGL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Git REQUIRED)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

# 针对 MSYS2 / MinGW-w64，屏蔽 DLL 导出的属性重定义警告
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wno-attributes)
endif()

# ---------- GIT ----------
# Get the latest abbreviated commit hash
execute_process(
    COMMAND ${GIT_EXECUTABLE} log -1 --format=%h
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Get the current branch name
execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Configure version.hpp out of the template
configure_file(
    src/common/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.h
)

# ---------- ImGui ----------
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/../imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL2::SDL2 OpenGL::GL)

# ---------- ImGuizmo ----------
set(IMGUIZMO_DIR ${CMAKE_SOURCE_DIR}/../ImGuizmo/src/)
add_library(imguizmo STATIC
    ${IMGUIZMO_DIR}/ImGuizmo.cpp
    ${IMGUIZMO_DIR}/GraphEditor.cpp
    ${IMGUIZMO_DIR}/ImCurveEdit.cpp
    ${IMGUIZMO_DIR}/ImGradient.cpp
    ${IMGUIZMO_DIR}/ImSequencer.cpp
    ${IMGUIZMO_DIR}/ImVectorEditor.cpp
)
target_include_directories(imguizmo PUBLIC ${IMGUIZMO_DIR} ${IMGUI_DIR})
target_link_libraries(imguizmo PUBLIC imgui)

# ==========================================
# core 动态库
# ==========================================
add_library(core SHARED
    src/core/Log.cpp
)
target_compile_definitions(core PRIVATE CORE_EXPORTS)
target_include_directories(core PUBLIC 
    src/common
)

# ==========================================
# geometry 动态库
# ==========================================
add_library(geometry SHARED
    src/geometry/BVH.cpp
    #src/geometry/Trace.cpp
)
target_compile_definitions(geometry PRIVATE GEOMETRY_EXPORTS)
target_include_directories(geometry PUBLIC 
    src/geometry
    src/common
)
target_link_libraries(geometry PUBLIC core glm::glm)

# ==========================================
# physics 动态库（Jolt 物理世界 + 射线检测 + 爆炸）
# 依赖：core / geometry / GameTypes / Jolt / SDL2_mixer
# 注意：不依赖 ImGui / GLEW / SDL2 主头 / game 业务头
# ==========================================
add_library(physics SHARED
    src/physics/Physics.cpp
    src/physics/Trace.cpp
)
target_compile_definitions(physics PRIVATE PHYSICS_EXPORTS)
target_include_directories(physics PUBLIC
    src/common     # 👈 让 GameTypes.h 对使用方可见
)
target_include_directories(physics PRIVATE
    src/geometry   # Trace.h 要 #include "BVH.h"
)
target_link_libraries(physics PUBLIC
    core
    geometry
    glm::glm
    Jolt::Jolt
    SDL2_mixer::SDL2_mixer
)

# ==========================================
# assets 动态库
# ==========================================
add_library(assets SHARED
    src/assets/FileSystem.cpp
)
target_compile_definitions(assets PRIVATE ASSET_EXPORTS)
target_include_directories(assets PUBLIC src/assets)
target_link_libraries(assets PUBLIC core)

# ---------- shader 嵌入 ----------
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(SHADER_DIR ${CMAKE_SOURCE_DIR}/src/shaders)
set(SHADER_GEN ${CMAKE_CURRENT_BINARY_DIR}/shaders_generated.h)

file(GLOB SHADER_FILES ${SHADER_DIR}/*.vert ${SHADER_DIR}/*.frag)

add_custom_command(
    OUTPUT ${SHADER_GEN}
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/embed_shaders.py
            ${SHADER_DIR} ${SHADER_GEN}
    DEPENDS ${SHADER_FILES} ${CMAKE_SOURCE_DIR}/tools/embed_shaders.py
    COMMENT "Embedding GLSL shaders"
)

add_custom_target(shaders_gen DEPENDS ${SHADER_GEN})

# ---------- editor ----------
add_executable(editor
    src/editor/main.cpp
    src/editor/SceneGlobals.cpp
    src/editor/Brush.cpp
    src/editor/Entity.cpp
    src/editor/UndoStack.cpp
    src/editor/EditorLayout.cpp
    src/editor/GraphEditorPanel.cpp
    src/editor/GameDatabase.cpp
    src/editor/EditorPanels.cpp
    src/editor/View2D.cpp
    src/editor/MapIO.cpp
)
# 让 build 目录自动同步 editor.json 文件
add_custom_command(TARGET editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/misc/halflife2.json
        $<TARGET_FILE_DIR:editor>/editor.json
)
target_include_directories(editor PRIVATE 
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    src/core
)
target_link_libraries(editor PRIVATE
    imgui
    imguizmo
    SDL2::SDL2main
    SDL2::SDL2
    glm::glm
    GLEW::GLEW
    OpenGL::GL
    nlohmann_json::nlohmann_json
    core
    geometry
    assets
    game
)

# ---------- map ----------
add_executable(map
    src/map/main.cpp
    src/map/JsonParser.cpp
    src/map/BrushBuilder.cpp
    src/map/LumpPacker.cpp
    src/map/LightBaker.cpp
    src/map/NavGridBuilder.cpp
    src/map/NodeGraphBuilder.cpp
)
target_include_directories(map PRIVATE 
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(map PRIVATE 
    nlohmann_json::nlohmann_json
    core
    geometry
    assets
)

# ==========================================
# game 逻辑动态库（供 game.exe 和 editor.exe 使用）
# ==========================================
add_library(game SHARED
    src/game/Player.cpp
    src/game/Weapon.cpp
    src/game/Enemy.cpp
    src/game/AStar.cpp
    src/game/Level.cpp
    src/game/Audio.cpp
    src/game/MapLoader.cpp
    # 注意：这里不包含 Render.cpp，也不包含 main.cpp
)
target_compile_definitions(game PRIVATE GAME_EXPORTS)
target_include_directories(game PUBLIC 
    src/game
    src/common
    src/physics
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(game PUBLIC
    core
    geometry
    physics
    assets
    imgui
    Jolt::Jolt
    glm::glm
    SDL2::SDL2
    SDL2_mixer::SDL2_mixer
    nlohmann_json::nlohmann_json
)

# ---------- game executable ----------
add_executable(retro 
    src/game/main.cpp
    src/game/Render.cpp
)
add_dependencies(retro shaders_gen)
target_include_directories(retro PRIVATE 
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
    src/physics
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(retro PRIVATE
    game
    imgui
    SDL2::SDL2main
    SDL2::SDL2
    GLEW::GLEW
    OpenGL::GL
)