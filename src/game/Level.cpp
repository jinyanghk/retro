#include "Level.h"
#include "World.h"
#include "Weapon.h"
#include "Player.h"
#include "Physics.h"
#include "entities/EntityDoor.h"
#include "entities/EntityPhysbox.h"
#include "entities/EntityBarrel.h"
#include "entities/EntityZombie.h"
#include <cstring>
#include <algorithm>
#include <vector>
#include <glm/gtc/type_ptr.hpp>

namespace {
    World g_world;
    WeaponState g_weapon;
    std::vector<GameRenderBox> g_renderBoxes;
    GameRenderSnapshot g_renderSnapshot{};

    glm::vec3 ComputeViewDir(float yawDeg, float pitchDeg) {
        float yaw = glm::radians(yawDeg);
        float pitch = glm::radians(pitchDeg);
        return glm::normalize(glm::vec3(
            std::cos(yaw) * std::cos(pitch),
            std::sin(pitch),
            std::sin(yaw) * std::cos(pitch)
        ));
    }

    void RebuildRenderSnapshot() {
        g_renderBoxes.clear();

        for (const auto& entPtr : g_world.GetEntities()) {
            const Entity* ent = entPtr.get();
            if (!ent || ent->IsPendingKill()) continue;

            GameRenderBox box{};
            if (const auto* door = dynamic_cast<const EntityDoor*>(ent)) {
                glm::vec3 mins = door->GetCurrentMins();
                glm::vec3 maxs = door->GetCurrentMaxs();
                glm::vec3 c = (mins + maxs) * 0.5f;
                glm::vec3 h = (maxs - mins) * 0.5f;
                box = {{c.x, c.y, c.z}, {h.x, h.y, h.z}, {0.45f, 0.35f, 0.25f}, 1};
            } else if (const auto* barrel = dynamic_cast<const EntityBarrel*>(ent)) {
                glm::vec3 c = barrel->GetPosition();
                glm::vec3 h = barrel->GetHalfExtents();
                glm::vec3 col = barrel->IsExplosive()
                    ? glm::vec3(0.85f, 0.15f, 0.15f)
                    : glm::vec3(0.35f, 0.55f, 0.75f);
                box = {{c.x, c.y, c.z}, {h.x, h.y, h.z}, {col.x, col.y, col.z}, 2};
            } else if (const auto* phys = dynamic_cast<const EntityPhysbox*>(ent)) {
                glm::vec3 c = phys->GetPosition();
                glm::vec3 h = phys->GetHalfExtents();
                box = {{c.x, c.y, c.z}, {h.x, h.y, h.z}, {0.60f, 0.50f, 0.35f}, 0};
            } else if (const auto* zombie = dynamic_cast<const EntityZombie*>(ent)) {
                glm::vec3 c = zombie->GetPosition() + glm::vec3(0.0f, 36.0f, 0.0f);
                box = {{c.x, c.y, c.z}, {16.0f, 36.0f, 16.0f}, {0.20f, 0.65f, 0.25f}, 3};
            } else {
                continue;
            }
            g_renderBoxes.push_back(box);
        }

        g_renderSnapshot.boxes = g_renderBoxes.data();
        g_renderSnapshot.boxCount = static_cast<int>(g_renderBoxes.size());
        g_renderSnapshot.activeWeapon = static_cast<int>(g_weapon.current);
        g_renderSnapshot.holdingObject = (g_weapon.heldEntityId != 0);
        g_renderSnapshot.recoilTimer = g_weapon.fireCooldown;
        g_renderSnapshot.recoilDuration = 0.25f;

        if (g_weapon.heldEntityId != 0) {
            if (Entity* held = g_world.GetEntityById(g_weapon.heldEntityId)) {
                glm::vec3 p = held->GetPosition();
                g_renderSnapshot.heldCenter = {p.x, p.y, p.z};
            }
        }
    }
}

void InitLevel(const MapData& mapData, PlayerState& outPlayer) {
    ReleaseHeldObject(g_weapon, g_world);
    g_weapon = WeaponState{};
    g_renderBoxes.clear();

    g_world.LoadFromMap(mapData);

    outPlayer.pos = glm::make_vec3(mapData.spawnPos) + glm::vec3(0.0f, 36.0f, 0.0f);
    outPlayer.camYaw = 0.0f;
    outPlayer.camPitch = 0.0f;
    outPlayer.vel = glm::vec3(0.0f);

    g_world.SetPlayerState(outPlayer.pos, outPlayer.health);
    RebuildRenderSnapshot();
}

void UpdateLevel(PlayerState& player, const LevelInput& input, float dt) {
    g_world.SetPlayerState(player.pos, player.health);

    if (input.switchWeaponSlot >= 0 && input.switchWeaponSlot <= 2) {
        g_weapon.current = static_cast<WeaponType>(input.switchWeaponSlot);
    }

    glm::vec3 eyePos = player.GetEyePos();
    glm::vec3 viewDir = player.GetForward();

    // 1. Player +use ('E' key)
    if (input.useJustPressed) {
        WorldRaycastHit useHit = g_world.Raycast(eyePos, viewDir, 85.0f);
        if (useHit.hit && useHit.entity) {
            useHit.entity->Use(g_world, nullptr);
        }
    }

    // 2. Active weapon & Gravity Gun
    UpdateWeapon(g_weapon, g_world, eyePos, viewDir,
                 input.primaryDown, input.primaryJustPressed, input.secondaryJustPressed, dt);

    // 3. Entity thinking & Hammer I/O
    g_world.Update(dt);

    // 4. Apply any damage dealt to the player this tick
    float dmg = g_world.ConsumePlayerDamage();
    if (dmg > 0.0f) {
        player.health = std::max(0.0f, player.health - dmg);
    }

    // 5. Cache POD render snapshot for retro executable
    RebuildRenderSnapshot();
}

void CleanupLevel() {
    ReleaseHeldObject(g_weapon, g_world);
    g_world.Clear();
    g_renderBoxes.clear();
    g_renderSnapshot = GameRenderSnapshot{};
}

GameRenderSnapshot GetGameRenderSnapshot() {
    return g_renderSnapshot;
}

bool PollLevelChange(char* outNextMap, size_t maxLen) {
    if (!g_world.HasPendingLevelChange() || !outNextMap || maxLen == 0) {
        return false;
    }
    std::string next = g_world.ConsumePendingLevelChange();
    std::strncpy(outNextMap, next.c_str(), maxLen - 1);
    outNextMap[maxLen - 1] = '\0';
    return true;
}
