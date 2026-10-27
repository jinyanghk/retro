#include "Player.h"
#include "BVH.h"
#include "Trace.h"

glm::vec3 PlayerState::GetForward() const {
    float pitchRad = glm::radians(camPitch);
    glm::vec3 front;
    front.x = cos(glm::radians(camYaw)) * cos(pitchRad);
    front.y = sin(pitchRad);
    front.z = sin(glm::radians(camYaw)) * cos(pitchRad);
    return glm::normalize(front);
}

void InitPlayer(PlayerState& p, const MapData& mapData) {
    p.pos = glm::vec3(mapData.spawnPos[0], mapData.spawnPos[1] + 36.0f, mapData.spawnPos[2]);
    p.camYaw = mapData.spawnAngles[1];
    p.camPitch = -mapData.spawnAngles[0];
    p.vel = glm::vec3(0.0f);
    p.hp = 100.0f;
    p.isGrounded = false;
    p.damageTimer = 0.0f;
    p.isDead = false;
}

void UpdatePlayer(PlayerState& p, float dt, 
                  const std::vector<JPH::BodyID>& physboxIds,
                  const std::vector<ExplosiveBarrel>& barrels,
                  const std::vector<DoorStateForTrace>& doorTrace,
                  JPH::BodyInterface& body_interface,
                  JPH::BodyID player_body,
                  const std::vector<glm::vec3>& physboxMins,
                  const std::vector<glm::vec3>& physboxMaxs) {
    // 重力
    p.vel.y -= p.gravity * dt;
    p.isGrounded = false;

    glm::vec3 dummyNormal;

    // 垂直碰撞
    glm::vec3 moveY(0.0f, p.vel.y * dt, 0.0f);
    glm::vec3 newPosY = p.pos + moveY;

    float floorDist = TraceBVH(newPosY, glm::vec3(0.0f, -1.0f, 0.0f), dummyNormal);
    {
        float dynDown = TraceDynamicBoxes(body_interface, physboxIds, barrels, player_body,
                                          newPosY, glm::vec3(0.0f, -1.0f, 0.0f),
                                          p.height);
        if (dynDown >= 0.0f && (floorDist < 0.0f || dynDown < floorDist)) floorDist = dynDown;
    }
    if (floorDist != -1.0f && floorDist <= (p.height * 0.5f + 0.5f)) {
        newPosY.y -= (floorDist - p.height * 0.5f); 
        p.vel.y = 0.0f;
        if (moveY.y <= 0.0f) p.isGrounded = true;
    }

    float ceilDist = TraceBVH(newPosY, glm::vec3(0.0f, 1.0f, 0.0f), dummyNormal);
    {
        float dynUp = TraceDynamicBoxes(body_interface, physboxIds, barrels, player_body,
                                        newPosY, glm::vec3(0.0f, 1.0f, 0.0f),
                                        p.height);
        if (dynUp >= 0.0f && (ceilDist < 0.0f || dynUp < ceilDist)) ceilDist = dynUp;
    }
    if (ceilDist != -1.0f && ceilDist <= (p.height * 0.5f + 0.5f)) {
        newPosY.y += (ceilDist - p.height * 0.5f); 
        p.vel.y = 0.0f;
    }
    p.pos = newPosY;

    // 水平碰撞 X
    float dx = p.vel.x * dt;
    if (fabsf(dx) > 0.0001f) {
        glm::vec3 dirX(dx > 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
        float distX = TraceBVH(p.pos, dirX, dummyNormal);
        {
            float dynX = TraceDynamicBoxes(body_interface, physboxIds, barrels, player_body,
                                           p.pos, dirX, p.radius);
            if (dynX >= 0.0f && (distX < 0.0f || dynX < distX)) distX = dynX;
        }
        {
            float doorX = TraceDoors(doorTrace, p.pos, dirX, p.radius);
            if (doorX >= 0.0f && (distX < 0.0f || doorX < distX)) distX = doorX;
        }
        if (distX != -1.0f && distX < p.radius) { 
            p.pos.x += (distX - p.radius) * dirX.x; 
            p.vel.x = 0.0f; 
        }
        else { p.pos.x += dx; }
    }
    // 水平碰撞 Z
    float dz = p.vel.z * dt;
    if (fabsf(dz) > 0.0001f) {
        glm::vec3 dirZ(0.0f, 0.0f, dz > 0.0f ? 1.0f : -1.0f);
        float distZ = TraceBVH(p.pos, dirZ, dummyNormal);
        {
            float dynZ = TraceDynamicBoxes(body_interface, physboxIds, barrels, player_body,
                                           p.pos, dirZ, p.radius);
            if (dynZ >= 0.0f && (distZ < 0.0f || dynZ < distZ)) distZ = dynZ;
        }
        {
            float doorZ = TraceDoors(doorTrace, p.pos, dirZ, p.radius);
            if (doorZ >= 0.0f && (distZ < 0.0f || doorZ < distZ)) distZ = doorZ;
        }
        if (distZ != -1.0f && distZ < p.radius) { 
            p.pos.z += (distZ - p.radius) * dirZ.z; 
            p.vel.z = 0.0f; 
        }
        else { p.pos.z += dz; }
    }

    // 同步给物理刚体
    MoveKinematicBody(player_body, p.pos.x, p.pos.y, p.pos.z);
}

void PlayerPushEntities(PlayerState& p, float dt, 
                        JPH::BodyInterface& body_interface,
                        const std::vector<JPH::BodyID>& physboxIds,
                        std::vector<ExplosiveBarrel>& barrels,
                        const std::vector<glm::vec3>& physboxMins,
                        const std::vector<glm::vec3>& physboxMaxs) {
    glm::vec3 playerVelH(p.vel.x, 0.0f, p.vel.z);
    float playerSpeedH = glm::length(playerVelH);
    if (playerSpeedH <= 0.01f) return;

    // 推箱子
    for (size_t i = 0; i < physboxIds.size(); i++) {
        JPH::RVec3 box_pos = body_interface.GetCenterOfMassPosition(physboxIds[i]);
        glm::vec3 bpos((float)box_pos.GetX(), (float)box_pos.GetY(), (float)box_pos.GetZ());

        glm::vec3 diff = p.pos - bpos;
        diff.y = 0.0f;
        float horizontalDist = glm::length(diff);

        glm::vec3 halfSize = (physboxMaxs[i] - physboxMins[i]) * 0.5f;
        float contactRange = p.radius + std::max(halfSize.x, halfSize.z) + 6.0f;

        if (horizontalDist < contactRange) {
            glm::vec3 pushDir = glm::normalize(-diff);
            float dot = glm::dot(glm::normalize(playerVelH), pushDir);
            if (dot > 0.1f) {
                JPH::Vec3 v = body_interface.GetLinearVelocity(physboxIds[i]);
                glm::vec3 boxVel(v.GetX(), 0.0f, v.GetZ());
                float boxSpeedAlongPush = glm::dot(boxVel, pushDir);
                float desiredSpeed = playerSpeedH * dot * 0.8f;
                if (boxSpeedAlongPush < desiredSpeed) {
                    float add = desiredSpeed - boxSpeedAlongPush;
                    JPH::Vec3 newV(v.GetX() + pushDir.x * add, v.GetY(), v.GetZ() + pushDir.z * add);
                    body_interface.SetLinearVelocity(physboxIds[i], newV);
                    body_interface.ActivateBody(physboxIds[i]);
                }
            }
        }
    }

    // 推爆炸桶
    for (auto& b : barrels) {
        if (b.exploded) continue;
        JPH::RVec3 bpos_j = body_interface.GetCenterOfMassPosition(b.bodyId);
        glm::vec3 bpos((float)bpos_j.GetX(), (float)bpos_j.GetY(), (float)bpos_j.GetZ());

        glm::vec3 diff = p.pos - bpos;
        diff.y = 0.0f;
        float horizontalDist = glm::length(diff);
        float contactRange = p.radius + 12.0f + 6.0f;

        if (horizontalDist < contactRange) {
            glm::vec3 pushDir = glm::normalize(-diff);
            float dot = glm::dot(glm::normalize(playerVelH), pushDir);
            if (dot > 0.1f) {
                JPH::Vec3 v = body_interface.GetLinearVelocity(b.bodyId);
                glm::vec3 boxVel(v.GetX(), 0.0f, v.GetZ());
                float boxSpeedAlongPush = glm::dot(boxVel, pushDir);
                float desiredSpeed = playerSpeedH * dot * 0.8f;
                if (boxSpeedAlongPush < desiredSpeed) {
                    float add = desiredSpeed - boxSpeedAlongPush;
                    JPH::Vec3 newV(v.GetX() + pushDir.x * add, v.GetY(), v.GetZ() + pushDir.z * add);
                    body_interface.SetLinearVelocity(b.bodyId, newV);
                    body_interface.ActivateBody(b.bodyId);
                }
            }
        }
    }
}