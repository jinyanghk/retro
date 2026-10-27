#pragma once
#include "Common.h"
#include "MapLoader.h"
#include "Player.h"
#include "Weapon.h"

struct RenderAssets {
    GLuint program = 0;
    GLuint mapVAO = 0, mapVBO = 0, mapEBO = 0;
    GLuint boxVAO = 0, boxVBO = 0, boxEBO = 0;
    GLuint gunVAO = 0, gunVBO = 0, gunEBO = 0;
    GLuint gunIndexCount = 0;
    GLuint boxIndexCount = 0;
};

void InitRender(RenderAssets& render, const MapData& mapData);
void ShutdownRender(RenderAssets& render);

void RenderFrame(RenderAssets& render, const PlayerState& player, const WeaponState& weapon, const GravityGunState& gravityGun,
                 JPH::BodyInterface& body_interface,
                 const MapData& mapData,
                 const std::vector<JPH::BodyID>& physboxIds,
                 const std::vector<ExplosiveBarrel>& barrels,
                 const std::vector<JPH::BodyID>& doorBodies,
                 const std::vector<EnemyInfo>& enemies,
                 const std::vector<BulletHole>& bulletHoles,
                 int scrW, int scrH);