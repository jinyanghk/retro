#pragma once

#include <glm/glm.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include "GameTypes.h"

void UpdatePlayer(PlayerState& p, float dt, JPH::BodyID player_body);
