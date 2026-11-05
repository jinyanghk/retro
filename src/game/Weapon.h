#pragma once
#include "Common.h"
#include "Player.h"

struct GravityGunState {
    JPH::BodyID heldBody;
    bool holding = false;
    float holdDistance = 300.0f;
    JPH::BodyID justReleasedBody;
    float releaseCooldown = 0.0f;
};

struct WeaponState {
    float recoilTimer = 0.0f;
    const float recoilDuration = 0.1f;
    float crosshairHitTimer = 0.0f;
};

// 开火（左键）
void FireWeapon(PlayerState& p, WeaponState& w, GravityGunState& g, float dt,
                JPH::BodyInterface& body_interface,
                const std::vector<JPH::BodyID>& physboxIds,
                const std::vector<DoorStateForTrace>& doorTrace,
                std::vector<EnemyInfo>& enemies,
                std::vector<ExplosiveBarrel>& barrels,
                std::vector<struct BulletHole>& bulletHoles,
                Mix_Chunk* sndShoot, Mix_Chunk* sndHit, Mix_Chunk* sndExplosion);

// 重力枪逻辑更新
void UpdateGravityGun(PlayerState& p, GravityGunState& g, bool bGrabPressed, bool bThrowPressed, float dt,
                      JPH::BodyInterface& body_interface,
                      const std::vector<JPH::BodyID>& physboxIds,
                      const std::vector<DoorStateForTrace>& doorTrace,
                      const std::vector<EnemyInfo>& enemies,
                      std::vector<ExplosiveBarrel>& barrels,
                      Mix_Chunk* sndDoor, Mix_Chunk* sndShoot);