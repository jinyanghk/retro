#pragma once
#include "Common.h"
#include "GameTypes.h"
#include "MapLoader.h"
#include "ModelLoader.h"
#include "WeaponModel.h"

struct RenderAssets {
    GLuint mapProgram = 0;
    GLuint entityProgram = 0;
    GLuint gunProgram = 0;
    
    GLuint mapVAO = 0, mapVBO = 0, mapEBO = 0;
    GLuint boxVAO = 0, boxVBO = 0, boxEBO = 0;
    GLuint gunVAO = 0, gunVBO = 0, gunEBO = 0;
    GLuint gunIndexCount = 0;
    GLuint boxIndexCount = 0;
    std::vector<GLuint> materialTexIds;
    GLuint whiteTex = 0;

    GLuint skyboxProgram = 0;
    GLuint skyboxVAO = 0, skyboxVBO = 0;
    GLuint skyboxTex = 0;    

    GLuint modelProgram = 0;
    std::vector<Mesh> zombieModel;

    std::unordered_map<std::string, BoneInfo> boneMap;
    int boneCounter = 0;
    std::vector<Animation> zombieAnims;

    WeaponModelData weaponModel;

    int FindAnimIndex(int animState) const {
        std::string target;
        switch (animState) {
            case 0: target = "Idle01"; break;
            case 1: target = "a_walk2"; break;
            case 2: target = "attackD"; break;
            case 3: target = "physflinch3"; break;
            case 4: target = "slump_a"; break;
            default: target = "Idle01"; break;
        }
        for (size_t i = 0; i != zombieAnims.size(); ++i) {
            if (zombieAnims[i].name.find(target) != std::string::npos) return (int)i;
        }
        return 0;
    }    
};

void InitRender(RenderAssets& render, const MapData& mapData);
void ShutdownRender(RenderAssets& render);

void RenderFrame(RenderAssets& render, const PlayerState& player, const GameRenderSnapshot& snapshot,
                 const MapData& mapData, const std::vector<BulletHole>& bulletHoles, int scrW, int scrH);
