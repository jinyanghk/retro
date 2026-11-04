#pragma once
// ============================================================================
// GameTypes.h —— 跨模块共享的纯数据 POD
// 只允许依赖：glm、Jolt、STL。禁止 include SDL/GL/ImGui/GLEW。
// 位于：src/common/
// 目的：让 physics / render / game / editor 都能引用同一份类型定义，
//       而不需要把这些类型所在的"业务头"（Common.h/Player.h/...）反向拖进来。
// ============================================================================

#include <glm/glm.hpp>
#include <vector>

// Jolt BodyID 是值类型（内含 uint32），可以安全暴露在头文件里。
// 这里只引最小 Jolt 头，避免把整个 PhysicsSystem 拖进使用方。
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

// ---------------------------------------------------------------------------
// 爆炸桶
// ---------------------------------------------------------------------------
struct ExplosiveBarrel {
    JPH::BodyID bodyId;
    glm::vec3   spawnPos;
    float       hp = 20.0f;
    bool        exploded = false;
};

// ---------------------------------------------------------------------------
// 敌人
// ---------------------------------------------------------------------------
enum class EnemyState {
    IDLE,
    CHASE,
    ATTACK,
    STUNNED,
    DEAD
};

struct EnemyInfo {
    glm::vec3  pos;
    glm::vec3  vel = glm::vec3(0.0f);
    float      hp = 100.0f;
    float      radius = 20.0f;
    float      height = 60.0f;
    bool       isGrounded = false;
    float      attackTimer = 0.0f;
    float      stepCooldown = 0.0f;
    EnemyState state = EnemyState::IDLE;
    float      stunTimer = 0.0f;
    float      deathTimer = 0.0f;
    JPH::BodyID bodyId;

    // A* 寻路缓存
    std::vector<glm::vec3> path;
    int   pathIndex = 0;
    float pathTimer = 0.0f;
    float stuckTimer = 0.0f;
};

// ---------------------------------------------------------------------------
// 门（射线检测用，轻量状态）
// ---------------------------------------------------------------------------
struct DoorStateForTrace {
    glm::vec3 mins, maxs;
    glm::vec3 openDir;
    float openDist;
    float openAmount;
};

// ---------------------------------------------------------------------------
// 弹孔
// ---------------------------------------------------------------------------
struct BulletHole {
    glm::vec3 pos;
    glm::vec3 normal;
    float life;
};