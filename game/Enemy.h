#pragma once
#include "Common.h"
#include "Player.h"

// 更新所有敌人 AI、物理与互动
void UpdateEnemies(std::vector<EnemyInfo>& enemies, PlayerState& player, float dt,
                   JPH::BodyInterface& body_interface,
                   const std::vector<JPH::BodyID>& physboxIds,
                   std::vector<ExplosiveBarrel>& barrels,
                   const std::vector<DoorStateForTrace>& doorTrace,
                   const std::vector<glm::vec3>& physboxMins,
                   const std::vector<glm::vec3>& physboxMaxs,
                   Mix_Chunk* sndHit);