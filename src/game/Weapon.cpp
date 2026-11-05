#include "Weapon.h"
#include "Trace.h"

void FireWeapon(PlayerState& p, WeaponState& w, GravityGunState& g, float dt,
                JPH::BodyInterface& body_interface,
                const std::vector<JPH::BodyID>& physboxIds,
                const std::vector<DoorStateForTrace>& doorTrace,
                std::vector<EnemyInfo>& enemies,
                std::vector<ExplosiveBarrel>& barrels,
                std::vector<struct BulletHole>& bulletHoles,
                Mix_Chunk* sndShoot, Mix_Chunk* sndHit, Mix_Chunk* sndExplosion) {
    w.recoilTimer = w.recoilDuration;
    if (sndShoot) Mix_PlayChannel(-1, sndShoot, 0);

    glm::vec3 eyePos = p.GetEyePos();
    glm::vec3 front = p.GetForward();

    glm::vec3 hitNormal;
    int hitType = 0, hitIndex = -1;
    float hitDist = TraceFire(eyePos, front, body_interface, physboxIds, 
        doorTrace, enemies, barrels, hitNormal, hitType, hitIndex);

    if (hitDist >= 0.0f) {
        glm::vec3 hitPos = eyePos + front * hitDist;
        bulletHoles.push_back({hitPos + hitNormal * 0.1f, hitNormal, 10.0f});
        w.crosshairHitTimer = 0.15f;

        if (hitType == 2 && hitIndex >= 0) {
            if (!(g.holding && physboxIds[hitIndex] == g.heldBody)) {
                body_interface.ActivateBody(physboxIds[hitIndex]);
                float shootForce = 800.0f;
                JPH::Vec3 newV(front.x * shootForce, 200.0f, front.z * shootForce);
                body_interface.SetLinearVelocity(physboxIds[hitIndex], newV);
            }
        } else if (hitType == 4 && hitIndex >= 0) {
            // 1. 基础伤害
            enemies[hitIndex].hp -= 34.0f;
            if (sndHit) Mix_PlayChannel(-1, sndHit, 0);
            
            if (enemies[hitIndex].hp <= 0.0f) {
                enemies[hitIndex].state = EnemyState::DEAD;
            } else {
                // 2. 👈 核心打法：没死的话，进入 STUNNED 状态，被打飞！
                enemies[hitIndex].state = EnemyState::STUNNED;
                enemies[hitIndex].stunTimer = 0.6f; // 眩晕 0.6 秒
                
                // 3. 沿着子弹方向给一个极强的物理推力
                glm::vec3 pushDir = glm::normalize(front);
                enemies[hitIndex].vel.x += pushDir.x * 300.0f;
                enemies[hitIndex].vel.z += pushDir.z * 300.0f;
            }
        } else if (hitType == 5 && hitIndex >= 0) {
            ExplodeBarrel(hitIndex, body_interface, barrels, (std::vector<JPH::BodyID>&)physboxIds, 
                          enemies, p.pos, p.hp, sndExplosion);
        }
    }
}

void UpdateGravityGun(PlayerState& p, GravityGunState& g, bool bGrabPressed, bool bThrowPressed, float dt,
                      JPH::BodyInterface& body_interface,
                      const std::vector<JPH::BodyID>& physboxIds,
                      const std::vector<DoorStateForTrace>& doorTrace,
                      const std::vector<EnemyInfo>& enemies,
                      std::vector<ExplosiveBarrel>& barrels,
                      Mix_Chunk* sndDoor, Mix_Chunk* sndShoot) {
    
    glm::vec3 eye = p.GetEyePos();
    glm::vec3 front = p.GetForward();

    // 持续拉取
    if (g.holding) {
        float dynamic_hold_dist = g.holdDistance;
        JPH::RVec3 jpos = body_interface.GetCenterOfMassPosition(g.heldBody);
        glm::vec3 cur((float)jpos.GetX(), (float)jpos.GetY(), (float)jpos.GetZ());
        float distToPlayer = glm::length(cur - eye);
        if (distToPlayer < 60.0f) dynamic_hold_dist = 60.0f; 

        glm::vec3 target = eye + front * dynamic_hold_dist;
        glm::vec3 delta = target - cur;
        float distToTarget = glm::length(delta);

        if (distToTarget > 600.0f) {
            body_interface.SetLinearVelocity(g.heldBody, JPH::Vec3(0,0,0));
            g.holding = false;
            g.heldBody = JPH::BodyID();
        } else {
            const float stiffness = 15.0f;
            const float maxSpeed  = 1200.0f;
            glm::vec3 desiredVel = delta * stiffness;
            float spd = glm::length(desiredVel);
            if (spd > maxSpeed) desiredVel *= (maxSpeed / spd);

            body_interface.ActivateBody(g.heldBody);
            body_interface.SetLinearVelocity(g.heldBody, JPH::Vec3(desiredVel.x, desiredVel.y, desiredVel.z));
            JPH::Vec3 av = body_interface.GetAngularVelocity(g.heldBody);
            body_interface.SetAngularVelocity(g.heldBody, av * 0.85f);
        }
    }

    // 抓取/放下
    if (bGrabPressed) {
        if (g.holding) {
            glm::vec3 safePos = eye + front * 50.0f;
            body_interface.SetPositionAndRotation(g.heldBody, JPH::RVec3(safePos.x, safePos.y, safePos.z), JPH::Quat::sIdentity(), JPH::EActivation::Activate);
            body_interface.SetLinearVelocity(g.heldBody, JPH::Vec3(p.vel.x * 0.5f, 0.0f, p.vel.z * 0.5f));
            body_interface.SetAngularVelocity(g.heldBody, JPH::Vec3(0,0,0));
            
            g.justReleasedBody = g.heldBody;
            g.releaseCooldown = 0.05f;
            g.holding = false;
            g.heldBody = JPH::BodyID();
            if (sndDoor) Mix_PlayChannel(-1, sndDoor, 0); 
        }
        else {
            glm::vec3 n; int hitType, hitIdx;
            float dist = TraceFire(eye, front, body_interface, physboxIds, doorTrace, enemies, barrels, n, hitType, hitIdx);
            if ((hitType == 2 || hitType == 5) && hitIdx >= 0 && dist >= 0.0f && dist < 400.0f) {
                if (hitType == 2) g.heldBody = physboxIds[hitIdx];
                else if (hitType == 5) g.heldBody = barrels[hitIdx].bodyId;
                
                g.holding = true;
                body_interface.ActivateBody(g.heldBody);
                if (sndDoor) Mix_PlayChannel(-1, sndDoor, 0);
            }
        }
    }

    // 投掷
    if (bThrowPressed && g.holding) {
        const float throwSpeed = 1800.0f;
        glm::vec3 throwPos = eye + front * 60.0f;
        body_interface.SetPositionAndRotation(g.heldBody, JPH::RVec3(throwPos.x, throwPos.y, throwPos.z), JPH::Quat::sIdentity(), JPH::EActivation::Activate);
        body_interface.SetLinearVelocity(g.heldBody, JPH::Vec3(front.x * throwSpeed, front.y * throwSpeed + 80.0f, front.z * throwSpeed));
        body_interface.SetAngularVelocity(g.heldBody, JPH::Vec3(0,0,0));
        
        g.holding = false;
        g.heldBody = JPH::BodyID();
        if (sndShoot) Mix_PlayChannel(-1, sndShoot, 0);
    }

    // 释放冷却
    if (g.releaseCooldown > 0.0f) {
        g.releaseCooldown -= dt;
        body_interface.SetLinearVelocity(g.justReleasedBody, JPH::Vec3(0, 0, 0));
        body_interface.SetAngularVelocity(g.justReleasedBody, JPH::Vec3(0, 0, 0));
        body_interface.ActivateBody(g.justReleasedBody);
        if (g.releaseCooldown <= 0.0f) {
            g.justReleasedBody = JPH::BodyID();
        }
    }
}