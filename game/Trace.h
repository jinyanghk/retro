#pragma once
#include "BVH.h"

// 射线检测动态物理箱子与爆炸桶
float TraceDynamicBoxes(JPH::BodyInterface& bi,
                        const std::vector<JPH::BodyID>& ids,
                        const std::vector<ExplosiveBarrel>& barrels,
                        JPH::BodyID ignoreBody,
                        const glm::vec3& start, const glm::vec3& dir,
                        float maxDist);

// 射线检测门
float TraceDoors(const std::vector<DoorStateForTrace>& ds,
                 const glm::vec3& start, const glm::vec3& dir, float maxDist);

// 综合武器开火检测
float TraceFire(const glm::vec3& start, const glm::vec3& dir,
                JPH::BodyInterface& bi, const std::vector<JPH::BodyID>& physboxIds,
                const std::vector<DoorStateForTrace>& doorTrace,
                const std::vector<EnemyInfo>& enemies,
                const std::vector<ExplosiveBarrel>& barrels,
                glm::vec3& outNormal, int& outHitType, int& outHitIndex);

// 爆炸逻辑
void ExplodeBarrel(int idx, JPH::BodyInterface& bi,
                   std::vector<ExplosiveBarrel>& barrels,
                   std::vector<JPH::BodyID>& physboxIds,
                   std::vector<EnemyInfo>& enemies,
                   glm::vec3 playerPos, float& playerHP,
                   Mix_Chunk* sndHit);