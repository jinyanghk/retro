#include "Enemy.h"
#include "BVH.h"

void UpdateEnemies(std::vector<EnemyInfo>& enemies, PlayerState& player, float dt,
                   JPH::BodyInterface& body_interface,
                   const std::vector<JPH::BodyID>& physboxIds,
                   std::vector<ExplosiveBarrel>& barrels,
                   const std::vector<DoorStateForTrace>& doorTrace,
                   const std::vector<glm::vec3>& physboxMins,
                   const std::vector<glm::vec3>& physboxMaxs,
                   Mix_Chunk* sndHit) {
    
    glm::vec3 dummyNormal;

    for (auto& enemy : enemies) {
        if (enemy.state == EnemyInfo::DEAD) {
            enemy.deathTimer += dt;
            continue;
        }

        glm::vec3 toPlayer = player.pos - enemy.pos;
        float distToPlayer = glm::length(toPlayer);
        
        // 1. 状态切换
        if (distToPlayer < 60.0f) enemy.state = EnemyInfo::ATTACK;
        else if (distToPlayer < 800.0f) enemy.state = EnemyInfo::CHASE;
        else enemy.state = EnemyInfo::IDLE;

        // 2. 执行状态
        if (enemy.state == EnemyInfo::CHASE) {
            glm::vec3 dir = glm::normalize(glm::vec3(toPlayer.x, 0, toPlayer.z));
            enemy.vel.x = dir.x * 150.0f;
            enemy.vel.z = dir.z * 150.0f;   
        } else if (enemy.state == EnemyInfo::ATTACK) {
            enemy.vel.x = 0.0f;
            enemy.vel.z = 0.0f;
            enemy.attackTimer -= dt;
            if (enemy.attackTimer <= 0.0f) {
                player.hp -= 10.0f;
                player.damageTimer = 0.2f;
                enemy.attackTimer = 1.0f;
                if (sndHit) Mix_PlayChannel(-1, sndHit, 0);
                printf("[AI] Enemy attacked player! HP: %.1f\n", player.hp);
            }
        } else {
            enemy.vel.x = 0.0f;
            enemy.vel.z = 0.0f;
        }

        // 3. 重力与碰撞
        glm::vec3 newPos = enemy.pos;

        auto CheckEnemyAABBCollision = [&](glm::vec3 testPos) -> int {
            float halfH = enemy.height * 0.5f;
            glm::vec3 eMin = testPos - glm::vec3(enemy.radius, halfH, enemy.radius);
            glm::vec3 eMax = testPos + glm::vec3(enemy.radius, halfH, enemy.radius);

            // 1. 测物理箱子
            for (size_t i = 0; i < physboxIds.size(); i++) {
                JPH::RMat44 t = body_interface.GetWorldTransform(physboxIds[i]);
                JPH::AABox aabb = body_interface.GetShape(physboxIds[i])->GetWorldSpaceBounds(t, JPH::Vec3(1,1,1));
                glm::vec3 bMin((float)aabb.mMin.GetX(), (float)aabb.mMin.GetY(), (float)aabb.mMin.GetZ());
                glm::vec3 bMax((float)aabb.mMax.GetX(), (float)aabb.mMax.GetY(), (float)aabb.mMax.GetZ());
                if (eMax.x > bMin.x && eMin.x < bMax.x &&
                    eMax.y > bMin.y && eMin.y < bMax.y &&
                    eMax.z > bMin.z && eMin.z < bMax.z) return 1;
            }

            // 2. 测门
            for (auto& d : doorTrace) {
                if (d.openAmount > 0.5f) continue;
                glm::vec3 off = d.openDir * (d.openDist * d.openAmount);
                glm::vec3 dMin = d.mins + off;
                glm::vec3 dMax = d.maxs + off;
                if (eMax.x > dMin.x && eMin.x < dMax.x &&
                    eMax.y > dMin.y && eMin.y < dMax.y &&
                    eMax.z > dMin.z && eMin.z < dMax.z) return 1;
            }

            // 3. 测静态地图（精确支持 16 高度台阶）
            float r = enemy.radius;
            glm::vec3 dirs[4] = { {r, 0, 0}, {-r, 0, 0}, {0, 0, r}, {0, 0, -r} };
            for (int i = 0; i < 4; i++) {
                glm::vec3 dir = glm::normalize(dirs[i]);
                glm::vec3 rayStart = testPos + glm::vec3(0, -halfH + 8.0f, 0);
                float d = TraceBVH(rayStart, dir, dummyNormal);
                if (d < 0.0f || d >= r + 2.0f) continue;

                glm::vec3 stepStart = rayStart + dir * d;
                float headClearance = TraceBVH(stepStart, glm::vec3(0, 1.0f, 0), dummyNormal);
                if (headClearance >= 0.0f && headClearance < 20.0f) return 1;

                // 精确 16 单位台阶检测
                glm::vec3 upPos = testPos;
                upPos.y += 16.0f; 
                glm::vec3 upRay = upPos - glm::vec3(0, halfH - 2.0f, 0);
                float upD = TraceBVH(upRay, glm::vec3(0, -1, 0), dummyNormal);
                if (upD >= 0.0f && upD <= 2.0f) return 2; // 可跨越台阶

                // 前方悬空 → 跳崖
                glm::vec3 frontGroundPos = testPos + dir * (r + 4.0f);
                glm::vec3 fgRay = frontGroundPos - glm::vec3(0, halfH - 2.0f, 0);
                float fgD = TraceBVH(fgRay, glm::vec3(0, -1, 0), dummyNormal);
                if (fgD < 0.0f) continue;
                float drop = fgD - (upRay.y - testPos.y + upD);
                if (drop > 0.0f && drop <= 40.0f) continue;

                return 1;
            }
            return 0;
        };

        auto isGoingDownStep = [&](glm::vec3 pos, glm::vec3 dir) -> bool {
            glm::vec3 curRay = pos - glm::vec3(0, enemy.height * 0.5f - 2.0f, 0);
            float curD = TraceBVH(curRay, glm::vec3(0, -1, 0), dummyNormal);
            if (curD < 0.0f || curD > 5.0f) return false;
            glm::vec3 frontPos = pos + dir * (enemy.radius * 2.0f + 8.0f);
            glm::vec3 rayStart = frontPos - glm::vec3(0, enemy.height * 0.5f - 2.0f, 0);
            float frontD = TraceBVH(rayStart, glm::vec3(0, -1, 0), dummyNormal);
            if (frontD < 0.0f) return true;
            float heightDiff = frontD - curD;
            if (heightDiff < 0.0f && heightDiff > -1000.0f) return true;
            return false;
        };

        if (enemy.stepCooldown > 0.0f) enemy.stepCooldown -= dt;
        bool canStep = (enemy.stepCooldown <= 0.0f);

        // X轴移动
        glm::vec3 dirX(enemy.vel.x > 0 ? 1.0f : -1.0f, 0, 0);
        glm::vec3 testX = newPos + glm::vec3(enemy.vel.x * dt, 0, 0);
        int hitX = CheckEnemyAABBCollision(testX);
        if (hitX == 0) {
            newPos.x = testX.x;
        } else if (hitX == 2 && canStep) {
            newPos.x = testX.x;
            newPos.y += 16.0f;
            enemy.vel.y = 0.0f;
            enemy.stepCooldown = 0.25f;
        } else if (isGoingDownStep(newPos, dirX)) {
            newPos.x = testX.x;
        } else {
            enemy.vel.x = 0;
        }

        // Z轴移动
        glm::vec3 dirZ(0, 0, enemy.vel.z > 0 ? 1.0f : -1.0f);
        glm::vec3 testZ = newPos + glm::vec3(0, 0, enemy.vel.z * dt);
        int hitZ = CheckEnemyAABBCollision(testZ);
        if (hitZ == 0) {
            newPos.z = testZ.z;
        } else if (hitZ == 2 && canStep) {
            newPos.z = testZ.z;
            newPos.y += 16.0f;
            enemy.vel.y = 0.0f;
            enemy.stepCooldown = 0.25f;
        } else if (isGoingDownStep(newPos, dirZ)) {
            newPos.z = testZ.z;
        } else {
            enemy.vel.z = 0;
        }

        // Y轴重力与贴地
        enemy.vel.y -= 800.0f * dt;
        if (enemy.vel.y < -1500.0f) enemy.vel.y = -1500.0f;

        glm::vec3 testY = newPos + glm::vec3(0, enemy.vel.y * dt, 0);
        glm::vec3 rayStart = testY - glm::vec3(0, enemy.height * 0.5f, 0);
        float maxProbeDist = std::abs(enemy.vel.y * dt) + 10.0f;
        float floorDist = TraceBVH(rayStart, glm::vec3(0, -1, 0), dummyNormal);

        if (floorDist >= 0.0f && floorDist <= maxProbeDist) {
            float groundY = testY.y - enemy.height * 0.5f + floorDist;
            testY.y = groundY + enemy.height * 0.5f;
            enemy.vel.y = 0.0f;
            enemy.isGrounded = true;
        } else {
            enemy.isGrounded = false;
        }
        newPos.y = testY.y;

        if (newPos.y < -500.0f) {
            newPos = player.pos + glm::vec3((rand() % 100 - 50), 100.0f, (rand() % 100 - 50));
            enemy.vel = glm::vec3(0.0f);
        }
        enemy.pos = newPos;

        // 敌人推箱子与桶
        glm::vec3 enemyVelH(enemy.vel.x, 0.0f, enemy.vel.z);
        float enemySpeedH = glm::length(enemyVelH);
        if (enemySpeedH > 0.01f) {
            for (size_t i = 0; i < physboxIds.size(); i++) {
                JPH::RVec3 box_pos = body_interface.GetCenterOfMassPosition(physboxIds[i]);
                glm::vec3 bpos((float)box_pos.GetX(), (float)box_pos.GetY(), (float)box_pos.GetZ());
                glm::vec3 diff = newPos - bpos; diff.y = 0.0f;
                float horizontalDist = glm::length(diff);
                glm::vec3 halfSize = (physboxMaxs[i] - physboxMins[i]) * 0.5f;
                float contactRange = enemy.radius + std::max(halfSize.x, halfSize.z) + 6.0f;
                if (horizontalDist < contactRange) {
                    glm::vec3 pushDir = glm::normalize(-diff);
                    float dot = glm::dot(glm::normalize(enemyVelH), pushDir);
                    if (dot > 0.1f) {
                        JPH::Vec3 v = body_interface.GetLinearVelocity(physboxIds[i]);
                        glm::vec3 boxVel(v.GetX(), 0.0f, v.GetZ());
                        float boxSpeedAlongPush = glm::dot(boxVel, pushDir);
                        float desiredSpeed = enemySpeedH * dot * 0.5f; 
                        if (boxSpeedAlongPush < desiredSpeed) {
                            float add = desiredSpeed - boxSpeedAlongPush;
                            JPH::Vec3 newV(v.GetX() + pushDir.x * add, v.GetY(), v.GetZ() + pushDir.z * add);
                            body_interface.SetLinearVelocity(physboxIds[i], newV);
                            body_interface.ActivateBody(physboxIds[i]);
                        }
                    }
                }
            }
            for (auto& b : barrels) {
                if (b.exploded) continue;
                JPH::RVec3 barrel_pos = body_interface.GetCenterOfMassPosition(b.bodyId);
                glm::vec3 bpos((float)barrel_pos.GetX(), (float)barrel_pos.GetY(), (float)barrel_pos.GetZ());
                glm::vec3 diff = newPos - bpos; diff.y = 0.0f;
                float horizontalDist = glm::length(diff);
                float contactRange = enemy.radius + 12.0f + 6.0f;
                if (horizontalDist < contactRange) {
                    glm::vec3 pushDir = glm::normalize(-diff);
                    float dot = glm::dot(glm::normalize(enemyVelH), pushDir);
                    if (dot > 0.1f) {
                        JPH::Vec3 v = body_interface.GetLinearVelocity(b.bodyId);
                        glm::vec3 boxVel(v.GetX(), 0.0f, v.GetZ());
                        float boxSpeedAlongPush = glm::dot(boxVel, pushDir);
                        float desiredSpeed = enemySpeedH * dot * 0.5f; 
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
    }
}