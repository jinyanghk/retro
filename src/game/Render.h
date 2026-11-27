#pragma once

#include <glm/glm.hpp>
#include "World.h"
#include "Weapon.h"
#include "Player.h"

struct RenderContext {
    // Keep your existing OpenGL VAOs, shaders, and map mesh buffers here
    unsigned int mapVao = 0;
    int mapVertexCount = 0;
    unsigned int shaderProgram = 0;
    unsigned int boxVao = 0;
};

void RenderWorldEntities(const RenderContext& ctx, const World& world, const glm::mat4& viewProj);
