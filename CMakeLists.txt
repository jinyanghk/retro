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
    ${CMAKE_SOURCE_DIR}/version.hpp.in
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

# ---------- editor ----------
add_executable(editor
    editor/main.cpp
    editor/SceneGlobals.cpp
    editor/Brush.cpp
    editor/Entity.cpp
    editor/UndoStack.cpp
    editor/EditorLayout.cpp
    editor/GraphEditorPanel.cpp
    editor/GameDatabase.cpp
    editor/EditorPanels.cpp
    editor/View2D.cpp
    editor/MapIO.cpp
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
)

# ---------- map ----------
add_executable(map 
    map/main.cpp
)
target_include_directories(map PRIVATE 
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(map PRIVATE nlohmann_json::nlohmann_json)

# ---------- engine ----------
add_library(engine SHARED 
    engine/engine.cpp
)
target_include_directories(engine PRIVATE 
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(engine PRIVATE 
    nlohmann_json::nlohmann_json
)

# ---------- game ----------
add_executable(game 
    game/main.cpp
    game/BVH.cpp
    game/Trace.cpp
    game/MapLoader.cpp
    game/Physics.cpp
    game/Player.cpp
    game/Weapon.cpp
    game/Audio.cpp
    game/Enemy.cpp
    game/Render.cpp
)
target_include_directories(game PRIVATE 
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_CURRENT_BINARY_DIR}
)
# 让 build 目录自动同步 sounds
#add_custom_command(TARGET game POST_BUILD
#    COMMAND ${CMAKE_COMMAND} -E copy_directory
#        ${CMAKE_SOURCE_DIR}/sounds
#        $<TARGET_FILE_DIR:game>/sounds
#)
target_link_libraries(game PRIVATE
    imgui
    SDL2::SDL2main
    SDL2::SDL2
    SDL2_mixer::SDL2_mixer
    glm::glm
    Jolt::Jolt
    GLEW::GLEW
    OpenGL::GL
)