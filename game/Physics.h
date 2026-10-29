#pragma once
#include "Common.h"

// 初始化 Jolt
void InitPhysics();
// 关闭 Jolt 并清理内存
void ShutdownPhysics();

// 获取全局物理系统和接口
JPH::PhysicsSystem& GetPhysicsSystem();
JPH::BodyInterface& GetBodyInterface();

// 更新物理世界
void UpdatePhysics(float deltaTime);

// 注册静态地图碰撞网格
void CreateStaticMesh(const std::vector<float>& verts, const std::vector<uint32_t>& indices, uint32_t nv, uint32_t ni);

// 创建玩家 Kinematic 刚体
JPH::BodyID CreatePlayerBody(float x, float y, float z);

// 创建动态物理箱子
JPH::BodyID CreatePhysBox(float x, float y, float z, float hx, float hy, float hz);

// 创建门（Kinematic）
JPH::BodyID CreateDoorBody(float x, float y, float z, float hx, float hy, float hz);

// 创建爆炸桶（Dynamic）
JPH::BodyID CreateBarrelBody(float x, float y, float z);

// 移动 Kinematic 刚体
void MoveKinematicBody(JPH::BodyID id, float x, float y, float z);

// 创建敌人的 Kinematic 刚体
JPH::BodyID CreateEnemyBody(float x, float y, float z);

void SetBodyToDynamic(JPH::BodyID id);

JPH::BodyID CreateTrapBody(float x, float y, float z, float hx, float hy, float hz);