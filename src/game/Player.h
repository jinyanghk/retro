#pragma once
#include "Common.h"
#include "MapLoader.h"
#include "Physics.h"

struct PlayerState {
    glm::vec3 pos;
    glm::vec3 vel = glm::vec3(0.0f);
    float camYaw = 0.0f;
    float camPitch = 0.0f;
    float hp = 100.0f;
    float damageTimer = 0.0f;
    bool isGrounded = false;
    bool isDead = false;
    
    // 常量
    const float radius = 16.0f;
    const float height = 72.0f;
    const float moveSpeed = 300.0f;
    const float jumpSpeed = 320.0f;
    const float gravity = 800.0f;
    const float mouseSensitivity = 0.1f;

    // 👈 新增：屏幕震动
    float shakeAmount = 0.0f;   // 当前震动幅度
    float shakeTimer = 0.0f;    // 震动剩余时间    
    
    // 获取眼睛位置
    glm::vec3 GetEyePos() const { return pos + glm::vec3(0.0f, 8.0f, 0.0f); }
    // 获取朝向
    glm::vec3 GetForward() const;
};

// 初始化玩家
void InitPlayer(PlayerState& p, const MapData& mapData);

// 处理玩家移动与物理碰撞
void UpdatePlayer(PlayerState& p, float dt, 
                  const std::vector<JPH::BodyID>& physboxIds,
                  const std::vector<ExplosiveBarrel>& barrels,
                  const std::vector<DoorStateForTrace>& doorTrace,
                  JPH::BodyInterface& body_interface,
                  JPH::BodyID player_body,
                  const std::vector<glm::vec3>& physboxMins,
                  const std::vector<glm::vec3>& physboxMaxs);

// 玩家推箱子逻辑
void PlayerPushEntities(PlayerState& p, float dt, 
                        JPH::BodyInterface& body_interface,
                        const std::vector<JPH::BodyID>& physboxIds,
                        std::vector<ExplosiveBarrel>& barrels,
                        const std::vector<glm::vec3>& physboxMins,
                        const std::vector<glm::vec3>& physboxMaxs);