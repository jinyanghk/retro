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

    for (size_t eIdx = 0; eIdx < enemies.size(); eIdx++) {
        auto& enemy = enemies[eIdx];
        if (enemy.state == EnemyInfo::DEAD) {
            enemy.deathTimer += dt;
            continue;
        }

        glm::vec3 toPlayer = player.pos - enemy.pos;
        float distToPlayer = glm::length(toPlayer);
        
        // 1. 状态切换
        if (distToPlayer < 60.0f) {
            enemy.state = EnemyInfo::ATTACK;
        } else if (distToPlayer < 800.0f) {
            enemy.state = EnemyInfo::CHASE;
        } else {
            enemy.state = EnemyInfo::IDLE;
        }

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

        glm::vec3 newPos = enemy.pos;

        // --- 辅助函数 1：检查实体碰撞 (墙/箱子/桶/门/其他敌人) ---
        auto IsBlocked = [&](glm::vec3 testPos) -> bool {
            float halfH = enemy.height * 0.5f;
            glm::vec3 eMin = testPos - glm::vec3(enemy.radius, halfH, enemy.radius);
            glm::vec3 eMax = testPos + glm::vec3(enemy.radius, halfH, enemy.radius);

            for (size_t i = 0; i < physboxIds.size(); i++) {
                JPH::RMat44 t = body_interface.GetWorldTransform(physboxIds[i]);
                JPH::AABox aabb = body_interface.GetShape(physboxIds[i])->GetWorldSpaceBounds(t, JPH::Vec3(1,1,1));
                glm::vec3 bMin((float)aabb.mMin.GetX(), (float)aabb.mMin.GetY(), (float)aabb.mMin.GetZ());
                glm::vec3 bMax((float)aabb.mMax.GetX(), (float)aabb.mMax.GetY(), (float)aabb.mMax.GetZ());
                if (eMax.x > bMin.x && eMin.x < bMax.x &&
                    eMax.y > bMin.y && eMin.y < bMax.y &&
                    eMax.z > bMin.z && eMin.z < bMax.z) return true;
            }

            for (auto& b : barrels) {
                if (b.exploded) continue;
                JPH::RMat44 t = body_interface.GetWorldTransform(b.bodyId);
                JPH::AABox aabb = body_interface.GetShape(b.bodyId)->GetWorldSpaceBounds(t, JPH::Vec3(1,1,1));
                glm::vec3 bMin((float)aabb.mMin.GetX(), (float)aabb.mMin.GetY(), (float)aabb.mMin.GetZ());
                glm::vec3 bMax((float)aabb.mMax.GetX(), (float)aabb.mMax.GetY(), (float)aabb.mMax.GetZ());
                if (eMax.x > bMin.x && eMin.x < bMax.x &&
                    eMax.y > bMin.y && eMin.y < bMax.y &&
                    eMax.z > bMin.z && eMin.z < bMax.z) return true;
            }

            for (auto& d : doorTrace) {
                if (d.openAmount > 0.5f) continue;
                glm::vec3 off = d.openDir * (d.openDist * d.openAmount);
                glm::vec3 dMin = d.mins + off;
                glm::vec3 dMax = d.maxs + off;
                if (eMax.x > dMin.x && eMin.x < dMax.x &&
                    eMax.y > dMin.y && eMin.y < dMax.y &&
                    eMax.z > dMin.z && eMin.z < dMax.z) return true;
            }

            for (size_t j = 0; j < enemies.size(); j++) {
                if (j == eIdx || enemies[j].state == EnemyInfo::DEAD) continue;
                glm::vec3 oPos = enemies[j].pos;
                float oHalfH = enemies[j].height * 0.5f;
                if (testPos.x + enemy.radius > oPos.x - enemies[j].radius && testPos.x - enemy.radius < oPos.x + enemies[j].radius &&
                    testPos.y + halfH > oPos.y - oHalfH && testPos.y - halfH < oPos.y + oHalfH &&
                    testPos.z + enemy.radius > oPos.z - enemies[j].radius && testPos.z - enemy.radius < oPos.z + enemies[j].radius) {
                    return true;
                }
            }
            return false;
        };

        // --- 辅助函数 3：防跳崖检测 ---
        auto IsCliffAhead = [&](glm::vec3 testPos) -> bool {
            // 如果它没站在地上，先交给重力去处理，不判定悬崖（否则会卡在半空）
            if (!enemy.isGrounded) return false;

            float testRayY = testPos.y - enemy.height * 0.5f + 4.0f;
            float frontFloor = TraceBVH(glm::vec3(testPos.x, testRayY, testPos.z), glm::vec3(0, -1, 0), dummyNormal);
            
            if (frontFloor < 0.0f || frontFloor > 200.0f) return true;
            
            float curRayY = enemy.pos.y - enemy.height * 0.5f + 4.0f;
            float curFloor = TraceBVH(glm::vec3(enemy.pos.x, curRayY, enemy.pos.z), glm::vec3(0, -1, 0), dummyNormal);
            
            if (curFloor >= 0.0f && curFloor < 50.0f) {
                if (frontFloor - curFloor > 16.0f) return true; // 落差超过 16 就是悬崖
            }
            return false;
        };

        // --- 组合移动逻辑 (包含爬楼梯) ---
        glm::vec3 moveDelta(enemy.vel.x * dt, 0, enemy.vel.z * dt);
        glm::vec3 targetPos = newPos + moveDelta;

        if (glm::length(moveDelta) > 0.01f) {
            bool isCliff = IsCliffAhead(targetPos);
            bool isHit = IsBlocked(targetPos);

            if (!isCliff && !isHit) {
                newPos = targetPos;
            } else if (!isCliff && isHit) {
                // 撞到了东西（墙或台阶），尝试像楼梯一样爬上去
                glm::vec3 moveDir = glm::normalize(moveDelta);
                
                // 在向前 1 个身体宽度的位置探测是否有 16 高度的台阶
                glm::vec3 stepCheckPos = newPos + moveDir * (enemy.radius + 2.0f);
                float stepFloor = TraceBVH(glm::vec3(stepCheckPos.x, stepCheckPos.y + 16.0f - enemy.height * 0.5f + 4.0f, stepCheckPos.z), glm::vec3(0, -1, 0), dummyNormal);
                float curFloor = TraceBVH(glm::vec3(newPos.x, newPos.y - enemy.height * 0.5f + 4.0f, newPos.z), glm::vec3(0, -1, 0), dummyNormal);

                // 如果上方有地，且高度差在 16 左右（16~20），说明是台阶
                if (stepFloor >= 0.0f && curFloor >= 0.0f && (curFloor - stepFloor) >= 12.0f && (curFloor - stepFloor) <= 20.0f) {
                    // 🔥 这里恢复平滑上台阶：直接把目标位置的高度抬升
                    targetPos.y += 16.0f;
                    newPos = targetPos;
                } else {
                    // 确实是墙，不是台阶，停下
                    enemy.vel.x = 0;
                    enemy.vel.z = 0;
                }
            } else {
                // 悬崖或者双重判定，停下
                enemy.vel.x = 0;
                enemy.vel.z = 0;
            }
        }

        // --- Y 轴（重力与贴地） ---
        enemy.vel.y -= 800.0f * dt;
        if (enemy.vel.y < -1500.0f) enemy.vel.y = -1500.0f;

        glm::vec3 testY = newPos + glm::vec3(0, enemy.vel.y * dt, 0);
        glm::vec3 rayStart = testY - glm::vec3(0, enemy.height * 0.5f, 0);
        // 放宽探测距离到 40，保证台阶吸附绝对稳
        float maxProbeDist = std::abs(enemy.vel.y * dt) + 40.0f; 
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

        enemy.pos = newPos;

        // --- 推箱子与桶 ---
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