#pragma once

#include "Physics.h"
#include "Enemy.h"
#include "BVH.h"

// 状态机
enum class LevelState {
    PLAYING,             // 正常战斗
    DOOR_CLOSING,        // 门正在关闭，等待切图
    DOOR_OPENING         // 门正在打开
};

extern LevelState currentState;
extern float transitionTimer;

extern std::string currentMap;
extern std::string nextMap;
extern bool pendingMapSwitch;

void CleanupCurrentLevel(JPH::BodyInterface& body_interface,
                         std::vector<JPH::BodyID>& physboxIds,
                         std::vector<JPH::BodyID>& doorBodies,
                         std::vector<EnemyInfo>& enemies,
                         JPH::BodyID& player_body);