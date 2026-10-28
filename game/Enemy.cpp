#include "Enemy.h"
#include "BVH.h"
#include "AStar.h"
#include <cstdlib> // 用于 rand()

void UpdateEnemies(std::vector<EnemyInfo>& enemies, PlayerState& player, float dt,
                   JPH::BodyInterface& body_interface,
                   const MapData& mapData,
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

        glm::vec3 oldPos = enemy.pos; // 👈 记录上一帧位置，用于卡死检测

        glm::vec3 toPlayer = player.pos - enemy.pos;
        float distToPlayer = glm::length(toPlayer);
        
        // 1. 状态切换
        if (distToPlayer < 60.0f) {
            enemy.state = EnemyInfo::ATTACK;
        } else if (distToPlayer < 1200.0f) {
            enemy.state = EnemyInfo::CHASE;
        } else {
            enemy.state = EnemyInfo::IDLE;
        }

        // 2. 执行状态 (寻路与目标)
        if (enemy.state == EnemyInfo::CHASE) {
            // 1. 寻路冷却
            enemy.pathTimer -= dt;
            if (enemy.pathTimer <= 0.0f) {
                enemy.path = FindPath(mapData, enemy.pos, player.pos);
                
                // 👈 核心修复：只有在路径发生明显变化时，才重置 pathIndex
                // 如果路径没变或者只变了一点点，就保留当前的 pathIndex，防止左右横跳
                if (enemy.path.empty()) {
                    enemy.pathIndex = 0;
                } else if (enemy.pathIndex >= (int)enemy.path.size()) {
                    enemy.pathIndex = 0;
                }
                
                enemy.pathTimer = 0.5f; 
            }

            glm::vec3 moveTarget = player.pos;
            
            if (!enemy.path.empty() && enemy.pathIndex >= 0 && enemy.pathIndex < (int)enemy.path.size()) {
                moveTarget = enemy.path[enemy.pathIndex];
                
                // 2. 节点推进
                // 👈 核心修复：把 64.0f 缩小到 24.0f。如果 64 意味着还没走到就切换，
                // 那它在两个节点中间来回走，就会永远切不过去。
                glm::vec2 enemy2D(enemy.pos.x, enemy.pos.z);
                glm::vec2 target2D(moveTarget.x, moveTarget.z);
                if (glm::length(target2D - enemy2D) < 24.0f) {
                    enemy.pathIndex++;
                }
                
                if (enemy.pathIndex >= (int)enemy.path.size()) {
                    moveTarget = player.pos; // 所有节点都走完了，直接冲玩家
                }
            }

            // 3. 计算速度
            glm::vec3 moveDir = moveTarget - enemy.pos;
            moveDir.y = 0.0f;
            if (glm::length(moveDir) > 0.1f) {
                moveDir = glm::normalize(moveDir);
                enemy.vel.x = moveDir.x * 150.0f;
                enemy.vel.z = moveDir.z * 150.0f;
            } else {
                enemy.vel.x = 0.0f;
                enemy.vel.z = 0.0f;
            }
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

        // --- 辅助函数：检查碰撞 (只保留水平方向) ---
        auto IsBlocked = [&](glm::vec3 testPos) -> bool {
            float halfH = enemy.height * 0.5f;
            glm::vec3 eMin = testPos - glm::vec3(enemy.radius, halfH, enemy.radius);
            glm::vec3 eMax = testPos + glm::vec3(enemy.radius, halfH, enemy.radius);

            // 1. 物理箱子和桶
            for (size_t i = 0; i < physboxIds.size(); i++) {
                JPH::RMat44 t = body_interface.GetWorldTransform(physboxIds[i]);
                JPH::AABox aabb = body_interface.GetShape(physboxIds[i])->GetWorldSpaceBounds(t, JPH::Vec3(1,1,1));
                glm::vec3 bMin((float)aabb.mMin.GetX(), (float)aabb.mMin.GetY(), (float)aabb.mMin.GetZ());
                glm::vec3 bMax((float)aabb.mMax.GetX(), (float)aabb.mMax.GetY(), (float)aabb.mMax.GetZ());
                if (eMax.x > bMin.x && eMin.x < bMax.x && eMax.y > bMin.y && eMin.y < bMax.y && eMax.z > bMin.z && eMin.z < bMax.z) return true;
            }
            for (auto& b : barrels) {
                if (b.exploded) continue;
                JPH::RMat44 t = body_interface.GetWorldTransform(b.bodyId);
                JPH::AABox aabb = body_interface.GetShape(b.bodyId)->GetWorldSpaceBounds(t, JPH::Vec3(1,1,1));
                glm::vec3 bMin((float)aabb.mMin.GetX(), (float)aabb.mMin.GetY(), (float)aabb.mMin.GetZ());
                glm::vec3 bMax((float)aabb.mMax.GetX(), (float)aabb.mMax.GetY(), (float)aabb.mMax.GetZ());
                if (eMax.x > bMin.x && eMin.x < bMax.x && eMax.y > bMin.y && eMin.y < bMax.y && eMax.z > bMin.z && eMin.z < bMax.z) return true;
            }
            for (auto& d : doorTrace) {
                if (d.openAmount > 0.5f) continue;
                glm::vec3 off = d.openDir * (d.openDist * d.openAmount);
                glm::vec3 dMin = d.mins + off;
                glm::vec3 dMax = d.maxs + off;
                if (eMax.x > dMin.x && eMin.x < dMax.x && eMax.y > dMin.y && eMin.y < dMax.y && eMax.z > dMin.z && eMin.z < dMax.z) return true;
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

            // 2. 静态墙壁
            if (glm::length(enemy.vel) > 0.01f) {
                glm::vec3 forward = glm::normalize(glm::vec3(enemy.vel.x, 0, enemy.vel.z));
                glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
                glm::vec3 origin = testPos + glm::vec3(0, 4.0f, 0);
                glm::vec3 dirs[3] = { forward, glm::normalize(forward + right * 0.7f), glm::normalize(forward - right * 0.7f) };

                for (int d = 0; d < 3; d++) {
                    float dist = TraceBVH(origin, dirs[d], dummyNormal);
                    if (dist >= 0.0f && dist <= 12.0f) return true; // 12的探测距离
                }
            }

            return false;
        };

        // --- 纯平地移动与滑行逻辑 ---
        glm::vec3 moveDelta(enemy.vel.x * dt, 0, enemy.vel.z * dt);
        glm::vec3 targetPos = newPos + moveDelta;

        // --- 纯平地移动逻辑（交给物理引擎） ---
        // 你只需要根据寻路结果，把期望的水平速度算出来
        glm::vec3 desiredVel(enemy.vel.x, 0.0f, enemy.vel.z);
        
        // --- 移动与物理交互 ---
        // 1. 把我们算出的意图速度（enemy.vel）写给 Jolt
        if (!enemy.bodyId.IsInvalid()) {
            JPH::Vec3 currentVel = body_interface.GetLinearVelocity(enemy.bodyId);
            // 保留 Y 轴原本的物理速度（重力），只覆盖 X 和 Z 的速度
            body_interface.SetLinearVelocity(enemy.bodyId, JPH::Vec3(enemy.vel.x, currentVel.GetY(), enemy.vel.z));
            body_interface.ActivateBody(enemy.bodyId);
            
            // 2. 从 Jolt 读取它真正到达的位置（物理引擎已经帮你处理了滑墙和碰撞）
            JPH::RVec3 physPos = body_interface.GetCenterOfMassPosition(enemy.bodyId);
            enemy.pos = glm::vec3((float)physPos.GetX(), (float)physPos.GetY(), (float)physPos.GetZ());
        }

        // --- 推箱子与桶 (保持不变) ---
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

        // --- 卡死检测 ---
        float distMoved = glm::length(enemy.pos - oldPos); 
        if (enemy.state == EnemyInfo::CHASE && (enemy.vel.x != 0 || enemy.vel.z != 0)) {
            if (distMoved < 0.5f) {
                enemy.stuckTimer += dt;
                if (enemy.stuckTimer > 1.5f) {
                    enemy.pathTimer = 0.0f; // 重新算路径
                    enemy.stuckTimer = 0.0f;
                    enemy.vel.x += (rand() % 100 - 50); // 给个随机推力脱困
                    enemy.vel.z += (rand() % 100 - 50);
                }
            } else {
                enemy.stuckTimer = 0.0f;
            }
        }        
    }
}