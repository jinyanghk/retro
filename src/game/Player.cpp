#include "Player.h"
#include "Physics.h"
#include <algorithm>
#include <cmath>

static float TraceWorldDist(const glm::vec3& origin, const glm::vec3& dir, float maxDist, JPH::BodyID ignoreBody) {
    PhysicsRayHit hit = RaycastWorld(origin, dir, maxDist, ignoreBody);
    return hit.hit ? hit.distance : maxDist;
}

void UpdatePlayer(PlayerState& p, float dt, JPH::BodyID player_body) {
    // Gravity & vertical sweep
    p.vel.y -= 800.0f * dt;
    float dy = p.vel.y * dt;

    if (dy <= 0.0f) {
        float checkDist = -dy + p.halfHeight + 2.0f;
        float dist = TraceWorldDist(p.pos, glm::vec3(0.0f, -1.0f, 0.0f), checkDist, player_body);
        if (dist < checkDist) {
            p.pos.y -= (dist - p.halfHeight);
            p.vel.y = 0.0f;
            p.onGround = true;
        } else {
            p.pos.y += dy;
            p.onGround = false;
        }
    } else {
        float checkDist = dy + p.halfHeight;
        float dist = TraceWorldDist(p.pos, glm::vec3(0.0f, 1.0f, 0.0f), checkDist, player_body);
        if (dist < checkDist) {
            p.pos.y += std::max(0.0f, dist - p.halfHeight);
            p.vel.y = 0.0f;
        } else {
            p.pos.y += dy;
        }
        p.onGround = false;
    }

    // Horizontal X sweep
    float dx = p.vel.x * dt;
    if (std::abs(dx) > 0.0001f) {
        glm::vec3 dirX(dx > 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
        float checkDist = std::abs(dx) + p.radius;
        float dist = TraceWorldDist(p.pos, dirX, checkDist, player_body);
        if (dist < checkDist) {
            p.pos.x += dirX.x * std::max(0.0f, dist - p.radius);
            p.vel.x = 0.0f;
        } else {
            p.pos.x += dx;
        }
    }

    // Horizontal Z sweep
    float dz = p.vel.z * dt;
    if (std::abs(dz) > 0.0001f) {
        glm::vec3 dirZ(0.0f, 0.0f, dz > 0.0f ? 1.0f : -1.0f);
        float checkDist = std::abs(dz) + p.radius;
        float dist = TraceWorldDist(p.pos, dirZ, checkDist, player_body);
        if (dist < checkDist) {
            p.pos.z += dirZ.z * std::max(0.0f, dist - p.radius);
            p.vel.z = 0.0f;
        } else {
            p.pos.z += dz;
        }
    }

    if (!player_body.IsInvalid()) {
        MoveKinematicBody(player_body, p.pos.x, p.pos.y, p.pos.z);
    }
}
