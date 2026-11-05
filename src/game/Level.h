#pragma once

#include "Physics.h"
#include "Enemy.h"
#include "BVH.h"
#include "GameExport.h"

// 状态机
enum class LevelState {
    PLAYING,             // 正常战斗
    DOOR_CLOSING,        // 门正在关闭，等待切图
    DOOR_OPENING         // 门正在打开
};

extern GAME_API LevelState currentState;
extern GAME_API float transitionTimer;

extern GAME_API std::string currentMap;
extern GAME_API std::string nextMap;
extern GAME_API bool pendingMapSwitch;

GAME_API void CleanupCurrentLevel(JPH::BodyInterface& body_interface,
                         std::vector<JPH::BodyID>& physboxIds,
                         std::vector<JPH::BodyID>& doorBodies,
                         std::vector<EnemyInfo>& enemies,
                         JPH::BodyID& player_body);