#pragma once
#include "BVH.h"                              // glm + RayAABB
#include "GameTypes.h"                        // ExplosiveBarrel / EnemyInfo / DoorStateForTrace
#ifdef _MSC_VER
#include "SDL_mixer.h"
#else
#include "SDL2/SDL_mixer.h"
#endif
#include <glm/glm.hpp>
#include <vector>

#include "Export.h"

// Jolt 完整依赖（Trace.cpp 里用 GetShape()->GetWorldSpaceBounds / JPH::AABox）
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Geometry/AABox.h>

// 射线检测动态物理箱子与爆炸桶
PHYSICS_API float TraceDynamicBoxes(JPH::BodyInterface& bi,
                        const std::vector<JPH::BodyID>& ids,
                        const std::vector<ExplosiveBarrel>& barrels,
                        JPH::BodyID ignoreBody,
                        const glm::vec3& start, const glm::vec3& dir,
                        float maxDist);

// 射线检测门
PHYSICS_API float TraceDoors(const std::vector<DoorStateForTrace>& ds,
                 const glm::vec3& start, const glm::vec3& dir, float maxDist);

// 综合武器开火检测
PHYSICS_API float TraceFire(const glm::vec3& start, const glm::vec3& dir,
                JPH::BodyInterface& bi, const std::vector<JPH::BodyID>& physboxIds,
                const std::vector<DoorStateForTrace>& doorTrace,
                const std::vector<EnemyInfo>& enemies,
                const std::vector<ExplosiveBarrel>& barrels,
                glm::vec3& outNormal, int& outHitType, int& outHitIndex);

// 爆炸逻辑
PHYSICS_API void ExplodeBarrel(int idx, JPH::BodyInterface& bi,
                   std::vector<ExplosiveBarrel>& barrels,
                   std::vector<JPH::BodyID>& physboxIds,
                   std::vector<EnemyInfo>& enemies,
                   glm::vec3 playerPos, float& playerHP,
                   Mix_Chunk* sndHit);