#pragma once

#include <glm/glm.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include "GameTypes.h"
#include "GameExport.h"

GAME_API void UpdatePlayer(PlayerState& p, float dt, JPH::BodyID player_body);
