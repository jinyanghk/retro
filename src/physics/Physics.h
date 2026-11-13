#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyInterface.h>
//#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>  // 👈 新增
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

#include "PhyExport.h"

// 初始化 Jolt
PHYSICS_API void InitPhysics();
// 关闭 Jolt 并清理内存
PHYSICS_API void ShutdownPhysics();

// 获取全局物理系统和接口
PHYSICS_API JPH::PhysicsSystem& GetPhysicsSystem();
PHYSICS_API JPH::BodyInterface& GetBodyInterface();

// 更新物理世界
PHYSICS_API void UpdatePhysics(float deltaTime);

// 注册静态地图碰撞网格
PHYSICS_API void CreateStaticMesh(const std::vector<float>& verts, const std::vector<uint32_t>& indices, uint32_t nv, uint32_t ni);

// 创建玩家 Kinematic 刚体
PHYSICS_API JPH::BodyID CreatePlayerBody(float x, float y, float z);

// 创建动态物理箱子
PHYSICS_API JPH::BodyID CreatePhysBox(float x, float y, float z, float hx, float hy, float hz);

// 创建门（Kinematic）
PHYSICS_API JPH::BodyID CreateDoorBody(float x, float y, float z, float hx, float hy, float hz);

// 创建爆炸桶（Dynamic）
PHYSICS_API JPH::BodyID CreateBarrelBody(float x, float y, float z);

// 移动 Kinematic 刚体
PHYSICS_API void MoveKinematicBody(JPH::BodyID id, float x, float y, float z);

PHYSICS_API void SetBodyToDynamic(JPH::BodyID id);

PHYSICS_API JPH::BodyID CreateTrapBody(float x, float y, float z, float hx, float hy, float hz);

// 👈 新的 CharacterVirtual 句柄接口
PHYSICS_API int CreateEnemyCharacter(float x, float y, float z);
PHYSICS_API glm::vec3 UpdateEnemyCharacter(int charId, const glm::vec3& vel, float dt, bool& outGrounded);
PHYSICS_API void DestroyEnemyCharacter(int charId);

PHYSICS_API void DestroyBodySafe(JPH::BodyID id);