#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
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

// Jolt 核心头文件
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

#ifdef _WIN32
#include <windows.h>
#endif

// ============================================================================
// 基础数据结构
// ============================================================================
struct CollisionTri {
    glm::vec3 v0, v1, v2;
    glm::vec3 normal;
};

struct ExplosiveBarrel {
    JPH::BodyID bodyId;
    glm::vec3  spawnPos;
    float      hp = 20.0f;
    bool       exploded = false;
};

struct EnemyInfo {
    glm::vec3 pos;
    glm::vec3 vel = glm::vec3(0.0f);
    float hp = 100.0f;
    float radius = 20.0f;
    float height = 60.0f;
    bool isGrounded = false;
    float attackTimer = 0.0f;
    float stepCooldown = 0.0f;
    enum State { IDLE, CHASE, ATTACK, DEAD } state = CHASE;
    float deathTimer = 0.0f;
    JPH::BodyID bodyId;
    // 👈 新增：A* 寻路缓存
    std::vector<glm::vec3> path;
    int pathIndex = 0;
    float pathTimer = 0.0f; // 重新计算路径的冷却时间    
};

struct DoorStateForTrace {
    glm::vec3 mins, maxs;
    glm::vec3 openDir;
    float openDist;
    float openAmount;
};

struct BulletHole {
    glm::vec3 pos;
    glm::vec3 normal;
    float life;
};