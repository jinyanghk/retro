#include "Trace.h"
#include <cstdio>

float TraceDynamicBoxes(JPH::BodyInterface& bi,
                        const std::vector<JPH::BodyID>& ids,
                        const std::vector<ExplosiveBarrel>& barrels,
                        JPH::BodyID ignoreBody,
                        const glm::vec3& start, const glm::vec3& dir,
                        float maxDist)
{
    float best = maxDist;
    bool hit = false;
    for (auto id : ids) {
        if (!ignoreBody.IsInvalid() && id == ignoreBody) continue;
        JPH::RMat44 t = bi.GetWorldTransform(id);
        JPH::AABox aabb = bi.GetShape(id)->GetWorldSpaceBounds(t, JPH::Vec3(1,1,1));
        glm::vec3 bmin((float)aabb.mMin.GetX(), (float)aabb.mMin.GetY(), (float)aabb.mMin.GetZ());
        glm::vec3 bmax((float)aabb.mMax.GetX(), (float)aabb.mMax.GetY(), (float)aabb.mMax.GetZ());
        float tmin, tmax;
        if (RayAABB(start, dir, bmin, bmax, tmin, tmax)) {
            if (tmin >= 0.0f && tmin < best) { best = tmin; hit = true; }
        }
    }

    for (auto& b : barrels) {
        if (b.exploded) continue;
        if (!ignoreBody.IsInvalid() && b.bodyId == ignoreBody) continue;
        JPH::RMat44 t = bi.GetWorldTransform(b.bodyId);
        JPH::AABox aabb = bi.GetShape(b.bodyId)->GetWorldSpaceBounds(t, JPH::Vec3(1,1,1));
        glm::vec3 bmin((float)aabb.mMin.GetX(), (float)aabb.mMin.GetY(), (float)aabb.mMin.GetZ());
        glm::vec3 bmax((float)aabb.mMax.GetX(), (float)aabb.mMax.GetY(), (float)aabb.mMax.GetZ());
        float tmin, tmax;
        if (RayAABB(start, dir, bmin, bmax, tmin, tmax)) {
            if (tmin >= 0.0f && tmin < best) { best = tmin; hit = true; }
        }
    }

    return hit ? best : -1.0f;
}

float TraceDoors(const std::vector<DoorStateForTrace>& ds,
                 const glm::vec3& start, const glm::vec3& dir, float maxDist)
{
    float best = maxDist; bool hit = false;
    for (auto& d : ds) {
        if (d.openAmount > 0.5f) continue;
        glm::vec3 off = d.openDir * (d.openDist * d.openAmount);
        glm::vec3 worldMin = d.mins + off;
        glm::vec3 worldMax = d.maxs + off;
        float tmin, tmax;
        if (RayAABB(start, dir, worldMin, worldMax, tmin, tmax)) {
            if (tmin >= 0.0f && tmin < best) { best = tmin; hit = true; }
        }
    }
    return hit ? best : -1.0f;
}

// ---------------------------------------------------------------------------
// 爆炸逻辑
// ---------------------------------------------------------------------------
void ExplodeBarrel(int idx, JPH::BodyInterface& bi,
                   std::vector<ExplosiveBarrel>& barrels,
                   std::vector<JPH::BodyID>& physboxIds,
                   std::vector<EnemyInfo>& enemies,
                   glm::vec3 playerPos, float& playerHP,
                   Mix_Chunk* sndHit) {
    if (idx < 0 || idx >= (int)barrels.size() || barrels[idx].exploded) return;
    barrels[idx].exploded = true;

    JPH::RVec3 jpos = bi.GetCenterOfMassPosition(barrels[idx].bodyId);
    glm::vec3 center((float)jpos.GetX(), (float)jpos.GetY(), (float)jpos.GetZ());
    const float radius = 250.0f;  // 原来是 180，改大一点
    const float force  = 6000.0f; // 原来是 2500，改为 6000 才炸得飞
    const float damage = 120.0f;

    // 1. 摧毁桶本体（使用移除功能，防止后续遍历读到无效 ID）
    bi.RemoveBody(barrels[idx].bodyId); 

    // 2. 对周围 physbox 施加“爆炸”效果（暴力直接给速度）
    for (size_t i = 0; i < physboxIds.size(); i++) {
        JPH::RVec3 bp = bi.GetCenterOfMassPosition(physboxIds[i]);
        glm::vec3 bpos((float)bp.GetX(), (float)bp.GetY(), (float)bp.GetZ());
        glm::vec3 diff = bpos - center;
        float dist = glm::length(diff);
        
        if (dist < radius * 1.5f) {
            float falloff = 1.0f - (dist / (radius * 1.5f));
            if (falloff < 0.0f) falloff = 0.0f;

            glm::vec3 dir = (dist > 0.01f) ? glm::normalize(diff) : glm::vec3(0, 1, 0);
            
            // 【关键修复1】强行唤醒刚体
            bi.ActivateBody(physboxIds[i]);

            // 【关键修复2】直接设置极大的线速度（无视质量）
            float speed = 2500.0f * falloff;
            JPH::Vec3 newVel(
                dir.x * speed,
                (dir.y + 0.8f) * speed + 300.0f, // 稍微往上抛一点
                dir.z * speed
            );
            bi.SetLinearVelocity(physboxIds[i], newVel);
            
            // 【关键修复3】给一点随机旋转速度，让它飞得更真实
            float spin = 20.0f * falloff;
            bi.SetAngularVelocity(physboxIds[i], JPH::Vec3(
                (rand() % 100 - 50) * spin,
                (rand() % 100 - 50) * spin,
                (rand() % 100 - 50) * spin
            ));

            printf("[BARREL] Pushed box %zu at dist %.1f with direct velocity\n", i, dist);
        }
    }

    // 3. 对敌人扣血（更大范围 + 更高伤害保底）
    const float damageRadius = radius * 2.0f; // 把半径放大到 500
    const float verticalRadius = 300.0f; // 垂直方向额外容忍

    for (auto& e : enemies) {
        if (e.state == EnemyInfo::DEAD) continue;
        glm::vec3 diff = e.pos - center;
        float horizDist = glm::length(glm::vec2(diff.x, diff.z)); // 只算水平
        float vertDist  = std::abs(diff.y);
        
        if (horizDist < damageRadius && vertDist < verticalRadius) {
            float falloff = 1.0f - (horizDist / damageRadius);
            if (falloff < 0.2f) falloff = 0.2f;
            e.hp -= 180.0f * falloff;
            
            printf("[BARREL] Damaged enemy! dist=%.1f dmg=%.1f hp_left=%.1f\n", horizDist, 180.0f * falloff, e.hp);

            if (e.hp <= 0.0f) {
                e.state = EnemyInfo::DEAD;
                printf("[BARREL] Enemy killed by explosion!\n");
            }
        }
    }

    // 4. 对玩家扣血（统一使用爆炸半径 1.5 倍，且近距离保底）
    float playerDist = glm::length(playerPos - center);
    if (playerDist < damageRadius) {
        // falloff 计算，clamp 到 0.1 保证贴脸至少有 10% 伤害
        float falloff = 1.0f - (playerDist / damageRadius);
        if (falloff < 0.1f) falloff = 0.1f;

        // 玩家爆炸伤害系数（可以调，0.5 代表只有 50% 的伤害）
        playerHP -= damage * falloff * 0.5f;
        printf("[BARREL] Player hit! dist=%.1f dmg=%.1f\n", playerDist, damage * falloff * 0.5f);
    }

    // 5. 连锁引爆周围的爆炸桶
    for (size_t i = 0; i < barrels.size(); i++) {
        if (i == (size_t)idx || barrels[i].exploded) continue;
        
        JPH::RVec3 jpos2 = bi.GetCenterOfMassPosition(barrels[i].bodyId);
        glm::vec3 bpos2((float)jpos2.GetX(), (float)jpos2.GetY(), (float)jpos2.GetZ());
        float dist2 = glm::length(bpos2 - center);
        
        if (dist2 < radius) {
            // 递归调用，炸飞它们！
            ExplodeBarrel(i, bi, barrels, physboxIds, enemies, playerPos, playerHP, sndHit);
        }
    }

    if (sndHit) Mix_PlayChannel(-1, sndHit, 0);
    printf("[BARREL] Exploded at %.1f %.1f %.1f\n", center.x, center.y, center.z);
}

// ---------------------------------------------------------------------------
// 武器开火检测
// type: 0=无, 1=BVH静态, 2=物理箱子, 3=门, 4=敌人, 5=爆炸桶
// ---------------------------------------------------------------------------
float TraceFire(const glm::vec3& start, const glm::vec3& dir,
                JPH::BodyInterface& bi, const std::vector<JPH::BodyID>& physboxIds,
                const std::vector<DoorStateForTrace>& doorTrace,
                const std::vector<EnemyInfo>& enemies,
                const std::vector<ExplosiveBarrel>& barrels,
                glm::vec3& outNormal, int& outHitType, int& outHitIndex)
{
    float closestDist = 2048.0f;
    outHitType = 0;
    outHitIndex = -1;
    outNormal = glm::vec3(0, 1, 0);

    // 1. BVH 静态几何
    glm::vec3 bvhNormal;
    float distBVH = TraceBVH(start, dir, bvhNormal);
    if (distBVH >= 0.0f && distBVH < closestDist) {
        closestDist = distBVH;
        outHitType = 1;
        outNormal = bvhNormal;
    }

    // 2. 物理箱子
    for (size_t i = 0; i < physboxIds.size(); i++) {
        JPH::RMat44 t = bi.GetWorldTransform(physboxIds[i]);
        JPH::AABox aabb = bi.GetShape(physboxIds[i])->GetWorldSpaceBounds(t, JPH::Vec3(1,1,1));
        glm::vec3 bmin((float)aabb.mMin.GetX(), (float)aabb.mMin.GetY(), (float)aabb.mMin.GetZ());
        glm::vec3 bmax((float)aabb.mMax.GetX(), (float)aabb.mMax.GetY(), (float)aabb.mMax.GetZ());
        
        float tmin, tmax;
        if (RayAABB(start, dir, bmin, bmax, tmin, tmax) && tmin >= 0.0f && tmin < closestDist) {
            closestDist = tmin;
            outHitType = 2;
            outHitIndex = (int)i;
            glm::vec3 hitPos = start + dir * tmin;
            glm::vec3 boxCenter = (bmin + bmax) * 0.5f;
            outNormal = glm::normalize(hitPos - boxCenter);
        }
    }

    // 3. 门
    for (size_t i = 0; i < doorTrace.size(); i++) {
        auto& d = doorTrace[i];
        glm::vec3 off = d.openDir * (d.openDist * d.openAmount);
        glm::vec3 wMin = d.mins + off;
        glm::vec3 wMax = d.maxs + off;
        
        float tmin, tmax;
        if (RayAABB(start, dir, wMin, wMax, tmin, tmax) && tmin >= 0.0f && tmin < closestDist) {
            closestDist = tmin;
            outHitType = 3;
            outHitIndex = (int)i;
            glm::vec3 hitPos = start + dir * tmin;
            glm::vec3 doorCenter = (wMin + wMax) * 0.5f;
            outNormal = glm::normalize(hitPos - doorCenter);
        }
    }

    // 4. 敌人
    for (size_t i = 0; i < enemies.size(); i++) {
        if (enemies[i].state == EnemyInfo::DEAD) continue;
        glm::vec3 bmin = enemies[i].pos - glm::vec3(enemies[i].radius, enemies[i].height * 0.5f, enemies[i].radius);
        glm::vec3 bmax = enemies[i].pos + glm::vec3(enemies[i].radius, enemies[i].height * 0.5f, enemies[i].radius);
        
        float tmin, tmax;
        if (RayAABB(start, dir, bmin, bmax, tmin, tmax) && tmin >= 0.0f && tmin < closestDist) {
            closestDist = tmin;
            outHitType = 4;
            outHitIndex = (int)i;
            glm::vec3 hitPos = start + dir * tmin;
            outNormal = glm::normalize(hitPos - enemies[i].pos);
        }
    }

    // 5. 爆炸桶
    for (size_t i = 0; i < barrels.size(); i++) {
        if (barrels[i].exploded) continue;
        
        // 使用 AABB 测距
        JPH::RMat44 t = bi.GetWorldTransform(barrels[i].bodyId);
        JPH::AABox aabb = bi.GetShape(barrels[i].bodyId)->GetWorldSpaceBounds(t, JPH::Vec3(1,1,1));
        glm::vec3 bmin((float)aabb.mMin.GetX(), (float)aabb.mMin.GetY(), (float)aabb.mMin.GetZ());
        glm::vec3 bmax((float)aabb.mMax.GetX(), (float)aabb.mMax.GetY(), (float)aabb.mMax.GetZ());
        
        float tmin, tmax;
        if (RayAABB(start, dir, bmin, bmax, tmin, tmax) && tmin >= 0.0f && tmin < closestDist) {
            closestDist = tmin;
            outHitType = 5;
            outHitIndex = (int)i;
            glm::vec3 hitPos = start + dir * tmin;
            glm::vec3 barrelCenter = (bmin + bmax) * 0.5f;
            outNormal = glm::normalize(hitPos - barrelCenter);
        }
    }

    return closestDist < 2048.0f ? closestDist : -1.0f;
}