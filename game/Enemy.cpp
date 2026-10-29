#include "Enemy.h"
#include "BVH.h"
#include "AStar.h"
#include <cstdlib>

void UpdateEnemies(std::vector<EnemyInfo>& enemies, PlayerState& player, float dt,
                   JPH::BodyInterface& body_interface,
                   const MapData& mapData,
                   const std::vector<JPH::BodyID>& physboxIds,
                   std::vector<ExplosiveBarrel>& barrels,
                   const std::vector<DoorStateForTrace>& doorTrace,
                   const std::vector<glm::vec3>& physboxMins,
                   const std::vector<glm::vec3>& physboxMaxs,
                   const GravityGunState& gravityGun, // 👈 新增
                   Mix_Chunk* sndHit) {
    
    glm::vec3 dummyNormal;

    for (size_t eIdx = 0; eIdx < enemies.size(); eIdx++) {
        auto& enemy = enemies[eIdx];
        if (enemy.state == EnemyState::DEAD) {
            enemy.deathTimer += dt;
            continue;
        }

        glm::vec3 oldPos = enemy.pos; 
        float distToPlayer = glm::length(player.pos - enemy.pos);

        // ==========================================
        // 第一部分：感知与状态切换
        // ==========================================
        switch (enemy.state) {
            case EnemyState::IDLE:
                enemy.vel.x = 0.0f; enemy.vel.z = 0.0f;
                if (distToPlayer < 800.0f) enemy.state = EnemyState::CHASE;
                break;
                
            case EnemyState::CHASE:
                if (distToPlayer < 60.0f) enemy.state = EnemyState::ATTACK;
                else if (distToPlayer > 1200.0f) enemy.state = EnemyState::IDLE;
                break;
                
            case EnemyState::ATTACK:
                if (distToPlayer > 80.0f) enemy.state = EnemyState::CHASE;
                break;

            case EnemyState::STUNNED:
                enemy.stunTimer -= dt;
                if (enemy.stunTimer <= 0.0f) enemy.state = EnemyState::CHASE;
                break;

            default: break;
        }

        // ==========================================
        // 第二部分：动作执行
        // ==========================================
        if (enemy.state == EnemyState::CHASE) {
            enemy.pathTimer -= dt;
            if (enemy.pathTimer <= 0.0f) {
                enemy.path = FindPath(mapData, enemy.pos, player.pos);
                if (enemy.path.empty() || enemy.pathIndex >= (int)enemy.path.size()) {
                    enemy.pathIndex = 0;
                }
                enemy.pathTimer = 0.5f; 
            }

            glm::vec3 moveTarget = player.pos;
            if (!enemy.path.empty() && enemy.pathIndex >= 0 && enemy.pathIndex < (int)enemy.path.size()) {
                moveTarget = enemy.path[enemy.pathIndex];
                
                glm::vec2 enemy2D(enemy.pos.x, enemy.pos.z);
                glm::vec2 target2D(moveTarget.x, moveTarget.z);
                if (glm::length(target2D - enemy2D) < 24.0f) {
                    enemy.pathIndex++;
                }
                if (enemy.pathIndex >= (int)enemy.path.size()) {
                    moveTarget = player.pos;
                }
            }

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
        } 
        else if (enemy.state == EnemyState::ATTACK) {
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
        }
        else if (enemy.state == EnemyState::STUNNED) {
            enemy.vel.x *= 0.9f;
            enemy.vel.z *= 0.9f;
        }

        // --- 交给物理引擎 ---
        if (!enemy.bodyId.IsInvalid()) {
            JPH::Vec3 currentVel = body_interface.GetLinearVelocity(enemy.bodyId);
            body_interface.SetLinearVelocity(enemy.bodyId, JPH::Vec3(enemy.vel.x, currentVel.GetY(), enemy.vel.z));
            body_interface.ActivateBody(enemy.bodyId);
            
            JPH::RVec3 physPos = body_interface.GetCenterOfMassPosition(enemy.bodyId);
            enemy.pos = glm::vec3((float)physPos.GetX(), (float)physPos.GetY(), (float)physPos.GetZ());
        }

        // --- 推箱子与桶 ---
        glm::vec3 enemyVelH(enemy.vel.x, 0.0f, enemy.vel.z);
        float enemySpeedH = glm::length(enemyVelH);
        if (enemySpeedH > 0.01f) {
            for (size_t i = 0; i < physboxIds.size(); i++) {
                // 如果箱子悬空（Y > 400），绝对不推它（防止空中陷阱被横向推走）
                glm::vec3 boxCenter = (physboxMins[i] + physboxMaxs[i]) * 0.5f;
                if (boxCenter.y > 400.0f) continue;

                JPH::RVec3 box_pos = body_interface.GetCenterOfMassPosition(physboxIds[i]);
                glm::vec3 bpos((float)box_pos.GetX(), (float)box_pos.GetY(), (float)box_pos.GetZ());
                glm::vec3 diff = enemy.pos - bpos; diff.y = 0.0f;
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
                glm::vec3 diff = enemy.pos - bpos; diff.y = 0.0f;
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

        // ==========================================================
        // 👈 统一物理撞击检测（重力枪砸 / 机关掉落）
        // ==========================================================
        for (size_t i = 0; i < physboxIds.size(); i++) {
            
            // 如果这个箱子正被重力枪抓着，绝对不能造成伤害！
            if (gravityGun.holding && gravityGun.heldBody == physboxIds[i]) {
                continue;
            }

            JPH::RVec3 boxPosJolt = body_interface.GetCenterOfMassPosition(physboxIds[i]);
            glm::vec3 bpos((float)boxPosJolt.GetX(), (float)boxPosJolt.GetY(), (float)boxPosJolt.GetZ());
            JPH::Vec3 boxVel = body_interface.GetLinearVelocity(physboxIds[i]);
            float speedSq = boxVel.LengthSq();

            glm::vec3 diff = enemy.pos - bpos;
            bool isHit = (std::abs(diff.x) < 60.0f && std::abs(diff.z) < 60.0f && std::abs(diff.y) < 70.0f);

            if (isHit) {
                // 👈 情况 1：高空陷阱砸落（高度 > 200，速度向下）
                bool isFallingTrap = (bpos.y > 200.0f && boxVel.GetY() < -100.0f);
                
                // 👈 情况 2：重力枪抛掷飞弹（速度极快）
                bool isThrownFast = (speedSq > 400.0f * 400.0f);

                if (isFallingTrap || isThrownFast) {
                    // 1. 秒杀敌人
                    enemy.hp = 0.0f;
                    enemy.state = EnemyState::DEAD;
                    
                    // 2. 摧毁敌人的物理刚体
                    if (!enemy.bodyId.IsInvalid()) {
                        body_interface.RemoveBody(enemy.bodyId);
                        body_interface.DestroyBody(enemy.bodyId);
                        enemy.bodyId = JPH::BodyID(); 
                    }
                    
                    // 3. 如果是陷阱大铁块，让它停在半空变成静态墙
                    if (isFallingTrap) {
                        body_interface.SetLinearVelocity(physboxIds[i], JPH::Vec3(0.0f, 0.0f, 0.0f));
                        body_interface.SetAngularVelocity(physboxIds[i], JPH::Vec3(0.0f, 0.0f, 0.0f));
                        body_interface.SetMotionType(physboxIds[i], JPH::EMotionType::Static, JPH::EActivation::Activate);
                        printf("[TRAP] Enemy crushed by falling block!\n");
                    } else {
                        printf("[PHYSICS] Enemy killed by flying object!\n");
                    }
                    
                    break; 
                }
            }
        }

        // --- 卡死检测 ---
        float distMoved = glm::length(enemy.pos - oldPos); 
        if (enemy.state == EnemyState::CHASE && (enemy.vel.x != 0 || enemy.vel.z != 0)) {
            if (distMoved < 0.5f) {
                enemy.stuckTimer += dt;
                if (enemy.stuckTimer > 1.5f) {
                    enemy.pathTimer = 0.0f;
                    enemy.stuckTimer = 0.0f;
                    enemy.vel.x += (rand() % 100 - 50);
                    enemy.vel.z += (rand() % 100 - 50);
                }
            } else {
                enemy.stuckTimer = 0.0f;
            }
        }        
    }
}