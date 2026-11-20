cmake_minimum_required(VERSION 3.16)
project(RetroEngine VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(USE_VCPKG "Use vcpkg packages on Windows (for GitHub Actions CI)" OFF)

# Force dynamic runtime /MD to match Release Jolt.lib
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    add_compile_options(/utf-8 /EHsc /W2)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-Wall -Wextra)
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(Git QUIET)

# ==========================================================
# 1. Dependency Configuration (Windows Local / CI vcpkg vs Linux/MSYS2)
# ==========================================================
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/../imgui")
set(IMGUIZMO_DIR "${CMAKE_SOURCE_DIR}/../ImGuizmo")

if(MSVC)
    set(JOLT_DIR "${CMAKE_SOURCE_DIR}/../JoltPhysics")
    set(JOLT_INCLUDE_DIR "${JOLT_DIR}")
    if(EXISTS "${JOLT_DIR}/Build/Build-MSVC/Release/Jolt.lib")
        set(JOLT_LIBRARY "${JOLT_DIR}/Build/Build-MSVC/Release/Jolt.lib")
    else()
        set(JOLT_LIBRARY "${JOLT_DIR}/Build/Build-MSVC/Jolt.lib")
    endif()

    if(USE_VCPKG)
        find_package(SDL2 CONFIG REQUIRED)
        find_package(SDL2_mixer CONFIG REQUIRED)
        find_package(glew REQUIRED)
        find_package(glm CONFIG REQUIRED)
        find_package(nlohmann_json CONFIG REQUIRED)
        find_package(assimp CONFIG REQUIRED)

        set(SDL2_LIBRARY SDL2::SDL2)
        set(SDL2MAIN_LIBRARY SDL2::SDL2main)
        set(SDL2_MIXER_LIBRARY SDL2_mixer::SDL2_mixer)
        set(GLEW_LIBRARY GLEW::GLEW)
        set(ASSIMP_LIBRARY assimp::assimp)
    else()
        # Local Windows 11 Desktop Paths
        set(SDL2_DIR "${CMAKE_SOURCE_DIR}/../SDL2-2.32.10")
        set(SDL2_INCLUDE_DIR "${SDL2_DIR}/include")
        set(SDL2_LIBRARY "${SDL2_DIR}/lib/x64/SDL2.lib")
        set(SDL2MAIN_LIBRARY "${SDL2_DIR}/lib/x64/SDL2main.lib")

        set(SDL2_MIXER_DIR "${CMAKE_SOURCE_DIR}/../SDL2_mixer-2.8.1")
        set(SDL2_MIXER_INCLUDE_DIR "${SDL2_MIXER_DIR}/include")
        set(SDL2_MIXER_LIBRARY "${SDL2_MIXER_DIR}/lib/x64/SDL2_mixer.lib")

        set(GLEW_DIR "${CMAKE_SOURCE_DIR}/../glew-2.2.0")
        set(GLEW_INCLUDE_DIR "${GLEW_DIR}/include")
        set(GLEW_LIBRARY "${GLEW_DIR}/lib/Release/x64/glew32.lib")

        set(GLM_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../glm")
        set(JSON_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../json/single_include")

        set(ASSIMP_DIR "${CMAKE_SOURCE_DIR}/../assimp-6.0.4")
        set(ASSIMP_INCLUDE_DIR "${ASSIMP_DIR}/include" "${ASSIMP_DIR}/build/include")
        file(GLOB ASSIMP_FOUND_LIBS "${ASSIMP_DIR}/build/lib/Release/assimp*.lib" "${ASSIMP_DIR}/build/lib/assimp*.lib" "${ASSIMP_DIR}/lib/assimp*.lib")
        if(ASSIMP_FOUND_LIBS)
            list(GET ASSIMP_FOUND_LIBS 0 ASSIMP_LIBRARY)
        else()
            set(ASSIMP_LIBRARY "${ASSIMP_DIR}/build/lib/Release/assimp-vc145-mt.lib")
        endif()
    endif()
else()
    # Linux / MSYS2 / macOS
    find_package(SDL2 REQUIRED)
    find_package(SDL2_mixer REQUIRED)
    find_package(glm REQUIRED)
    find_package(OpenGL REQUIRED)
    find_package(GLEW REQUIRED)
    find_package(nlohmann_json REQUIRED)
    find_package(assimp REQUIRED)
    find_package(Jolt REQUIRED)
endif()

# ==========================================================
# 2. Git Info & Embedded Shaders
# ==========================================================
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if(NOT GIT_HASH)
    set(GIT_HASH "unknown")
endif()
if(NOT GIT_BRANCH)
    set(GIT_BRANCH "main")
endif()

add_compile_definitions(
    GIT_HASH="${GIT_HASH}"
    GIT_BRANCH="${GIT_BRANCH}"
)

set(SHADER_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders_generated.h")
file(GLOB SHADER_FILES
    "${CMAKE_SOURCE_DIR}/src/shaders/*.vert"
    "${CMAKE_SOURCE_DIR}/src/shaders/*.frag"
)
add_custom_command(
    OUTPUT ${SHADER_OUTPUT}
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/embed_shaders.py"
            "${CMAKE_SOURCE_DIR}/src/shaders"
            ${SHADER_OUTPUT}
    DEPENDS ${SHADER_FILES} "${CMAKE_SOURCE_DIR}/tools/embed_shaders.py"
    COMMENT "Embedding GLSL shaders into C++ header..."
)
add_custom_target(GenerateShaders ALL DEPENDS ${SHADER_OUTPUT})

# ==========================================================
# 3. Shared Libraries (DLL / SO)
# ==========================================================
add_library(core SHARED src/core/FileSystem.cpp)
target_include_directories(core PUBLIC src/common)
target_compile_definitions(core PRIVATE CORE_EXPORTS)

add_library(geometry SHARED
    src/geometry/Brush.cpp
    src/geometry/Entity.cpp
    src/geometry/GameDatabase.cpp
    src/geometry/MapIO.cpp
    src/geometry/UndoStack.cpp
)
target_include_directories(geometry PUBLIC src/common)
target_compile_definitions(geometry PRIVATE GEOMETRY_EXPORTS)
if(MSVC AND NOT USE_VCPKG)
    target_include_directories(geometry PUBLIC ${GLM_INCLUDE_DIR} ${JSON_INCLUDE_DIR})
    target_link_libraries(geometry PUBLIC core)
elseif(MSVC AND USE_VCPKG)
    target_link_libraries(geometry PUBLIC core glm::glm nlohmann_json::nlohmann_json)
else()
    target_link_libraries(geometry PUBLIC core glm::glm nlohmann_json::nlohmann_json)
endif()

add_library(physics SHARED
    src/physics/Physics.cpp
    src/physics/Trace.cpp
)
target_include_directories(physics PUBLIC src/common)
target_compile_definitions(physics PRIVATE PHYSICS_EXPORTS)
if(MSVC)
    target_include_directories(physics PUBLIC ${JOLT_INCLUDE_DIR} ${SDL2_MIXER_INCLUDE_DIR} ${SDL2_INCLUDE_DIR} ${GLM_INCLUDE_DIR})
    target_link_libraries(physics PUBLIC core geometry ${JOLT_LIBRARY} ${SDL2_MIXER_LIBRARY})
    if(USE_VCPKG)
        target_link_libraries(physics PUBLIC glm::glm)
    endif()
    target_compile_definitions(physics PRIVATE
        JPH_DEBUG_RENDERER
        JPH_FLOATING_POINT_EXCEPTIONS_ENABLED
        JPH_OBJECT_LAYER_BITS=16
        JPH_OBJECT_STREAM
        JPH_PROFILE_ENABLED
        JPH_USE_CPU_COMPUTE
    )
else()
    target_link_libraries(physics PUBLIC core geometry Jolt::Jolt SDL2_mixer::SDL2_mixer)
endif()

add_library(assets SHARED
    src/assets/Audio.cpp
    src/assets/MapLoader.cpp
    src/assets/ModelLoader.cpp
    src/assets/TextureCache.cpp
    src/assets/WeaponModel.cpp
)
target_include_directories(assets PUBLIC src/common "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(assets PRIVATE ASSETS_EXPORTS)
add_dependencies(assets GenerateShaders)
if(MSVC AND NOT USE_VCPKG)
    target_include_directories(assets PUBLIC ${SDL2_INCLUDE_DIR} ${SDL2_MIXER_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${GLM_INCLUDE_DIR} ${ASSIMP_INCLUDE_DIR})
    target_link_libraries(assets PUBLIC core ${SDL2_LIBRARY} ${SDL2_MIXER_LIBRARY} ${GLEW_LIBRARY} opengl32 ${ASSIMP_LIBRARY})
elseif(MSVC AND USE_VCPKG)
    target_link_libraries(assets PUBLIC core ${SDL2_LIBRARY} ${SDL2_MIXER_LIBRARY} ${GLEW_LIBRARY} opengl32 glm::glm ${ASSIMP_LIBRARY})
else()
    target_link_libraries(assets PUBLIC core SDL2::SDL2 SDL2_mixer::SDL2_mixer GLEW::GLEW OpenGL::GL assimp::assimp)
endif()

add_library(game SHARED
    src/game/AStar.cpp
    src/game/Enemy.cpp
    src/game/Level.cpp
    src/game/Player.cpp
    src/game/Weapon.cpp
)
target_include_directories(game PUBLIC src/common)
target_compile_definitions(game PRIVATE GAME_EXPORTS)
if(MSVC AND NOT USE_VCPKG)
    target_include_directories(game PUBLIC ${JOLT_INCLUDE_DIR} ${SDL2_INCLUDE_DIR} ${SDL2_MIXER_INCLUDE_DIR} ${GLM_INCLUDE_DIR})
    target_link_libraries(game PUBLIC core physics)
elseif(MSVC AND USE_VCPKG)
    target_include_directories(game PUBLIC ${JOLT_INCLUDE_DIR})
    target_link_libraries(game PUBLIC core physics ${SDL2_LIBRARY} ${SDL2_MIXER_LIBRARY} glm::glm)
else()
    target_link_libraries(game PUBLIC core physics)
endif()

# ==========================================================
# 4. Executables (retro, editor, bvh)
# ==========================================================
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

add_executable(retro
    src/retro/main.cpp
    src/retro/Render.cpp
    ${IMGUI_SOURCES}
)
target_include_directories(retro PRIVATE src/common ${IMGUI_DIR} ${IMGUI_DIR}/backends "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_dependencies(retro GenerateShaders)
if(MSVC AND NOT USE_VCPKG)
    target_include_directories(retro PRIVATE ${JOLT_INCLUDE_DIR} ${SDL2_INCLUDE_DIR} ${SDL2_MIXER_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${GLM_INCLUDE_DIR} ${ASSIMP_INCLUDE_DIR})
    target_link_libraries(retro PRIVATE core geometry physics assets game ${SDL2MAIN_LIBRARY} ${SDL2_LIBRARY} ${SDL2_MIXER_LIBRARY} ${GLEW_LIBRARY} opengl32 ${ASSIMP_LIBRARY})
elseif(MSVC AND USE_VCPKG)
    target_include_directories(retro PRIVATE ${JOLT_INCLUDE_DIR})
    target_link_libraries(retro PRIVATE core geometry physics assets game ${SDL2MAIN_LIBRARY} ${SDL2_LIBRARY} ${SDL2_MIXER_LIBRARY} ${GLEW_LIBRARY} opengl32 glm::glm ${ASSIMP_LIBRARY})
else()
    target_link_libraries(retro PRIVATE core geometry physics assets game SDL2::SDL2main SDL2::SDL2 SDL2_mixer::SDL2_mixer GLEW::GLEW OpenGL::GL assimp::assimp)
endif()

add_executable(editor
    src/editor/main.cpp
    src/editor/EditorLayout.cpp
    src/editor/EditorPanels.cpp
    src/editor/GraphEditorPanel.cpp
    src/editor/View2D.cpp
    ${IMGUI_SOURCES}
    ${IMGUIZMO_DIR}/GraphEditor.cpp
)
target_include_directories(editor PRIVATE src/common ${IMGUI_DIR} ${IMGUI_DIR}/backends ${IMGUIZMO_DIR} "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_dependencies(editor GenerateShaders)
if(MSVC AND NOT USE_VCPKG)
    target_include_directories(editor PRIVATE ${SDL2_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${GLM_INCLUDE_DIR} ${JSON_INCLUDE_DIR})
    target_link_libraries(editor PRIVATE core geometry ${SDL2MAIN_LIBRARY} ${SDL2_LIBRARY} ${GLEW_LIBRARY} opengl32)
elseif(MSVC AND USE_VCPKG)
    target_link_libraries(editor PRIVATE core geometry ${SDL2MAIN_LIBRARY} ${SDL2_LIBRARY} ${GLEW_LIBRARY} opengl32 glm::glm nlohmann_json::nlohmann_json)
else()
    target_link_libraries(editor PRIVATE core geometry SDL2::SDL2main SDL2::SDL2 GLEW::GLEW OpenGL::GL)
endif()

add_executable(bvh
    src/map/main.cpp
    src/map/BrushBuilder.cpp
    src/map/JsonParser.cpp
    src/map/LightBaker.cpp
    src/map/LumpPacker.cpp
    src/map/NodeGraphBuilder.cpp
)
target_include_directories(bvh PRIVATE src/common)
if(MSVC AND NOT USE_VCPKG)
    target_include_directories(bvh PRIVATE ${GLM_INCLUDE_DIR} ${JSON_INCLUDE_DIR})
    target_link_libraries(bvh PRIVATE core geometry)
elseif(MSVC AND USE_VCPKG)
    target_link_libraries(bvh PRIVATE core geometry glm::glm nlohmann_json::nlohmann_json)
else()
    target_link_libraries(bvh PRIVATE core geometry)
endif()
