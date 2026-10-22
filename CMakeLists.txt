cmake_minimum_required(VERSION 3.16)
project(retro)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED)
find_package(glm REQUIRED)
find_package(GLEW REQUIRED)
find_package(OpenGL REQUIRED)
find_package(nlohmann_json REQUIRED)

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
set(IMGUIZMO_DIR ${CMAKE_SOURCE_DIR}/../imguizmo/src/)
add_library(imguizmo STATIC
    ${IMGUIZMO_DIR}/ImGuizmo.cpp
    ${IMGUIZMO_DIR}/GraphEditor.cpp
    ${IMGUIZMO_DIR}/ImCurveEdit.cpp
    ${IMGUIZMO_DIR}/ImGradient.cpp
    ${IMGUIZMO_DIR}/ImSequencer.cpp
    ${IMGUIZMO_DIR}ImVectorEditor.cpp
)
target_include_directories(imguizmo PUBLIC ${IMGUIZMO_DIR} ${IMGUI_DIR})
target_link_libraries(imguizmo PUBLIC imgui)

# ---------- editor ----------
add_executable(editor
    editor.cpp
    scene/SceneGlobals.cpp
    scene/Brush.cpp
    scene/Entity.cpp
    app/UndoStack.cpp
    app/EditorLayout.cpp
    fgd/FgdDatabase.cpp
    ui/EditorPanels.cpp
    ui/View2D.cpp
    map/MapExport.cpp
)
# 让 build 目录自动同步 fgd 文件
add_custom_command(TARGET editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:editor>/fgd
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/fgd/halflife2.json
        $<TARGET_FILE_DIR:editor>/fgd/halflife2.json
)
target_include_directories(editor PRIVATE ${CMAKE_SOURCE_DIR})
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
add_executable(map map.cpp)
target_link_libraries(map PRIVATE nlohmann_json::nlohmann_json)

# ---------- game ----------
add_executable(game game.cpp)
target_link_libraries(game PRIVATE
    imgui
    SDL2::SDL2main
    SDL2::SDL2
    glm::glm
    GLEW::GLEW
    OpenGL::GL
)