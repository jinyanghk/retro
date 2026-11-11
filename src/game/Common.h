#pragma once

#ifdef _MSC_VER
#include "SDL.h"
#include "SDL_mixer.h"
#else
#include <SDL2/SDL.h>
#include "SDL2/SDL_mixer.h"
#endif
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
#include <thread>
#include <iostream>
#include <utility>

// Jolt 核心
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/MotionProperties.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>

#include "version.h"

// 👇 跨模块共享 POD（ExplosiveBarrel / EnemyInfo / DoorStateForTrace / BulletHole）
#include "GameTypes.h"

#ifdef _WIN32
#include <windows.h>
#endif