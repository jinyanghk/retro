#pragma once

#include <string>
#include "GameExport.h"
#include "GameTypes.h"
#include "MapFormat.h"

struct MapData;

struct LevelInput {
    bool primaryDown = false;
    bool primaryJustPressed = false;
    bool secondaryJustPressed = false;
    bool useJustPressed = false;
    int switchWeaponSlot = -1; // -1 = no change, 0 = Pistol, 1 = Shotgun, 2 = GravityGun
};


GAME_API void InitLevel(const MapData& mapData, PlayerState& outPlayer);

GAME_API void UpdateLevel(PlayerState& player, const LevelInput& input, float dt);
GAME_API void CleanupLevel();

GAME_API GameRenderSnapshot GetGameRenderSnapshot();
GAME_API bool PollLevelChange(char* outNextMap, size_t maxLen);
