cmake_minimum_required(VERSION 3.16)
project(retro)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
    add_compile_options(/W3) # 或者 /W4
    # 强制 Release 模式（与 Jolt.lib 匹配）
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
    endif()
    # 强制使用动态运行库 /MD（与 Release 的 Jolt.lib 匹配）
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)    
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
else()
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# ==========================================
# 依赖查找与路径设置
# ==========================================
if(MSVC)
    message(STATUS "MSVC build: Using local dependencies from Desktop")

    # 1. glm (纯头文件)
    set(GLM_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../glm")

    # 2. SDL2
    set(SDL2_DIR "${CMAKE_SOURCE_DIR}/../SDL2-2.32.10")
    set(SDL2_INCLUDE_DIR "${SDL2_DIR}/include")
    set(SDL2_LIBRARY "${SDL2_DIR}/lib/x64/SDL2.lib")
    set(SDL2MAIN_LIBRARY "${SDL2_DIR}/lib/x64/SDL2main.lib")

    # 3. SDL2_mixer
    set(SDL2_MIXER_DIR "${CMAKE_SOURCE_DIR}/../SDL2_mixer-2.8.2")
    set(SDL2_MIXER_INCLUDE_DIR "${SDL2_MIXER_DIR}/include")
    set(SDL2_MIXER_LIBRARY "${SDL2_MIXER_DIR}/lib/x64/SDL2_mixer.lib")

    # 4. GLEW
    set(GLEW_DIR "${CMAKE_SOURCE_DIR}/../glew-2.3.1")
    set(GLEW_INCLUDE_DIR "${GLEW_DIR}/include")
    set(GLEW_LIBRARY "${GLEW_DIR}/lib/Release/x64/glew32.lib")

    # 5. Jolt
    set(JOLT_DIR "${CMAKE_SOURCE_DIR}/../JoltPhysics")
    set(JOLT_INCLUDE_DIR "${JOLT_DIR}")
    set(JOLT_LIBRARY "${JOLT_DIR}/Build/Build-MSVC/Jolt.lib")

    # 6. nlohmann_json (纯头文件，放在项目父目录)
    set(NLOHMANN_JSON_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../nlohmann_json")

    # 7. assimp
    set(ASSIMP_DIR "${CMAKE_SOURCE_DIR}/../assimp-6.0.6")
    set(ASSIMP_INCLUDE_DIR "${ASSIMP_DIR}/include")
    # 注意：assimp 通常需要链接 .lib，你给的是 .dll，这里假设 .lib 在 lib 目录下
    set(ASSIMP_LIBRARY "${ASSIMP_DIR}/build/lib/assimp-vc145-mt.lib")

    # 8. OpenGL (Windows 自带)
    find_package(OpenGL REQUIRED)

    # 9. Python3 (生成 shaders)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

else()
    find_package(SDL2 REQUIRED)
    find_package(SDL2_mixer REQUIRED)
    find_package(glm REQUIRED)
    find_package(Jolt REQUIRED)
    find_package(GLEW REQUIRED)
    find_package(OpenGL REQUIRED)
    find_package(nlohmann_json REQUIRED)
    find_package(Git REQUIRED)
    find_package(assimp REQUIRED)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
endif()

# ---------- GIT ----------
execute_process(COMMAND ${GIT_EXECUTABLE} log -1 --format=%h OUTPUT_VARIABLE GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD OUTPUT_VARIABLE GIT_BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE)
configure_file(src/common/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/version.h)

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

if(MSVC)
    target_include_directories(imgui PUBLIC ${SDL2_INCLUDE_DIR})
    target_link_libraries(imgui PUBLIC ${SDL2_LIBRARY} OpenGL::GL)
else()
    target_link_libraries(imgui PUBLIC SDL2::SDL2 OpenGL::GL)
endif()

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
# core
# ==========================================
add_library(core SHARED src/core/Log.cpp)
target_compile_definitions(core PRIVATE CORE_EXPORTS)
target_include_directories(core PUBLIC src/common)

# ==========================================
# geometry
# ==========================================
add_library(geometry SHARED src/geometry/BVH.cpp)
target_compile_definitions(geometry PRIVATE GEOMETRY_EXPORTS)
target_include_directories(geometry PUBLIC src/geometry src/common)

if(MSVC)
    target_include_directories(geometry PUBLIC ${GLM_INCLUDE_DIR})
    target_link_libraries(geometry PUBLIC core)
else()
    target_link_libraries(geometry PUBLIC core glm::glm)
endif()

# ==========================================
# physics
# ==========================================
add_library(physics SHARED src/physics/Physics.cpp src/physics/Trace.cpp)
target_compile_definitions(physics PRIVATE PHYSICS_EXPORTS)
target_include_directories(physics PUBLIC src/common src/geometry)

if(MSVC)
    target_include_directories(physics PUBLIC ${JOLT_INCLUDE_DIR} ${SDL2_MIXER_INCLUDE_DIR} ${SDL2_INCLUDE_DIR} ${GLM_INCLUDE_DIR})
    target_link_libraries(physics PUBLIC core geometry ${JOLT_LIBRARY} ${SDL2_MIXER_LIBRARY})
else()
    target_link_libraries(physics PUBLIC core geometry glm::glm Jolt::Jolt SDL2_mixer::SDL2_mixer)
endif()

# ==========================================
# assets
# ==========================================
add_library(assets SHARED
    src/assets/FileSystem.cpp
    src/assets/TextureCache.cpp
    src/assets/MapLoader.cpp
    src/assets/Audio.cpp
    src/assets/ModelLoader.cpp
    src/assets/WeaponModel.cpp
)
target_compile_definitions(assets PRIVATE ASSET_EXPORTS)
target_include_directories(assets PUBLIC src/assets src/common)

if(MSVC)
    target_include_directories(assets PUBLIC ${GLEW_INCLUDE_DIR} ${SDL2_INCLUDE_DIR} ${SDL2_MIXER_INCLUDE_DIR} ${NLOHMANN_JSON_INCLUDE_DIR} ${ASSIMP_INCLUDE_DIR} ${GLM_INCLUDE_DIR})
    target_link_libraries(assets PUBLIC core ${GLEW_LIBRARY} ${SDL2_MIXER_LIBRARY} ${SDL2_LIBRARY} ${ASSIMP_LIBRARY} OpenGL::GL)
else()
    target_link_libraries(assets PUBLIC core GLEW::GLEW nlohmann_json::nlohmann_json SDL2_mixer::SDL2_mixer SDL2::SDL2 assimp::assimp)
endif()

# ---------- shader 嵌入 ----------
set(SHADER_DIR ${CMAKE_SOURCE_DIR}/src/shaders)
set(SHADER_GEN ${CMAKE_CURRENT_BINARY_DIR}/shaders_generated.h)
file(GLOB SHADER_FILES ${SHADER_DIR}/*.vert ${SHADER_DIR}/*.frag)
add_custom_command(
    OUTPUT ${SHADER_GEN}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/embed_shaders.py ${SHADER_DIR} ${SHADER_GEN}
    DEPENDS ${SHADER_FILES} ${CMAKE_SOURCE_DIR}/tools/embed_shaders.py
    COMMENT "Embedding GLSL shaders"
)
add_custom_target(shaders_gen DEPENDS ${SHADER_GEN})

# ---------- editor ----------
add_executable(editor
    src/editor/main.cpp src/editor/SceneGlobals.cpp src/editor/Brush.cpp
    src/editor/Entity.cpp src/editor/UndoStack.cpp src/editor/EditorLayout.cpp
    src/editor/GraphEditorPanel.cpp src/editor/GameDatabase.cpp src/editor/EditorPanels.cpp
    src/editor/View2D.cpp src/editor/MapIO.cpp
)
add_dependencies(editor shaders_gen)
add_custom_command(TARGET editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/misc/halflife2.json $<TARGET_FILE_DIR:editor>/editor.json
)
target_include_directories(editor PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR} src/core)

if(MSVC)
    target_include_directories(editor PRIVATE ${SDL2_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${NLOHMANN_JSON_INCLUDE_DIR} ${GLM_INCLUDE_DIR})
    target_link_libraries(editor PRIVATE imgui imguizmo ${SDL2MAIN_LIBRARY} ${SDL2_LIBRARY} ${GLEW_LIBRARY} OpenGL::GL core geometry assets game)
else()
    target_link_libraries(editor PRIVATE imgui imguizmo SDL2::SDL2main SDL2::SDL2 glm::glm GLEW::GLEW OpenGL::GL nlohmann_json::nlohmann_json core geometry assets game)
endif()

# ---------- bvh (map) ----------
add_executable(bvh
    src/map/main.cpp src/map/JsonParser.cpp src/map/BrushBuilder.cpp
    src/map/LumpPacker.cpp src/map/LightBaker.cpp src/map/NavGridBuilder.cpp
    src/map/NodeGraphBuilder.cpp
)
target_include_directories(bvh PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/common ${CMAKE_CURRENT_BINARY_DIR})

if(MSVC)
    target_include_directories(bvh PRIVATE ${NLOHMANN_JSON_INCLUDE_DIR} ${GLM_INCLUDE_DIR})
    target_link_libraries(bvh PRIVATE core geometry assets)
else()
    target_link_libraries(bvh PRIVATE nlohmann_json::nlohmann_json core geometry assets)
endif()

# ==========================================
# game
# ==========================================
add_library(game SHARED
    src/game/Player.cpp src/game/Weapon.cpp src/game/Enemy.cpp
    src/game/AStar.cpp src/game/Level.cpp
)
target_compile_definitions(game PRIVATE GAME_EXPORTS)
target_include_directories(game PUBLIC src/game src/common src/physics ${CMAKE_CURRENT_BINARY_DIR})

if(MSVC)
    target_include_directories(game PUBLIC ${JOLT_INCLUDE_DIR} ${SDL2_INCLUDE_DIR} ${SDL2_MIXER_INCLUDE_DIR} ${NLOHMANN_JSON_INCLUDE_DIR} ${GLM_INCLUDE_DIR})
    target_link_libraries(game PUBLIC core geometry physics assets imgui ${JOLT_LIBRARY} ${SDL2_LIBRARY} ${SDL2_MIXER_LIBRARY})
else()
    target_link_libraries(game PUBLIC core geometry physics assets imgui Jolt::Jolt glm::glm SDL2::SDL2 nlohmann_json::nlohmann_json)
endif()

# ---------- retro ----------
add_executable(retro src/retro/main.cpp src/retro/Render.cpp)
add_dependencies(retro shaders_gen)
target_include_directories(retro PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/common src/physics ${CMAKE_CURRENT_BINARY_DIR})

if(MSVC)
    target_include_directories(retro PRIVATE ${SDL2_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${NLOHMANN_JSON_INCLUDE_DIR} ${GLM_INCLUDE_DIR})
    target_link_libraries(retro PRIVATE imgui imguizmo ${SDL2MAIN_LIBRARY} ${SDL2_LIBRARY} ${GLEW_LIBRARY} OpenGL::GL core geometry assets game)
else()
    target_link_libraries(retro PRIVATE game imgui SDL2::SDL2main SDL2::SDL2 GLEW::GLEW OpenGL::GL)
endif()