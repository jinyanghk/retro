#pragma once
#include "Common.h"
#include "MapLoader.h"
#include "Player.h"
#include "Weapon.h"
#include "ModelLoader.h"

struct RenderAssets {
    // 三个独立的着色器程序
    GLuint mapProgram = 0;
    GLuint entityProgram = 0;
    GLuint gunProgram = 0;
    
    GLuint mapVAO = 0, mapVBO = 0, mapEBO = 0;
    GLuint boxVAO = 0, boxVBO = 0, boxEBO = 0;
    GLuint gunVAO = 0, gunVBO = 0, gunEBO = 0;
    GLuint gunIndexCount = 0;
    GLuint boxIndexCount = 0;
    std::vector<GLuint> materialTexIds;   // 和 mapData.materialGroups 一一对应
    GLuint whiteTex = 0;

    GLuint skyboxProgram = 0;
    GLuint skyboxVAO = 0, skyboxVBO = 0;
    GLuint skyboxTex = 0;    

    GLuint modelProgram = 0;
    std::vector<Mesh> zombieModel; // 👈 装载丧尸的网格
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