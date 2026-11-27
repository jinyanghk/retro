#include "Render.h"
#include "entities/EntityDoor.h"
#include "entities/EntityPhysbox.h"
#include "entities/EntityBarrel.h"
#include "entities/EntityZombie.h"

void RenderWorldEntities(const RenderContext& ctx, const World& world, const glm::mat4& viewProj) {
    for (const auto& entPtr : world.GetEntities()) {
        const Entity* ent = entPtr.get();
        if (!ent || ent->IsPendingKill()) continue;

        if (const auto* door = dynamic_cast<const EntityDoor*>(ent)) {
            glm::vec3 mins = door->GetCurrentMins();
            glm::vec3 maxs = door->GetCurrentMaxs();
            glm::vec3 center = (mins + maxs) * 0.5f;
            glm::vec3 halfExt = (maxs - mins) * 0.5f;
            // DrawDoorBox(ctx, viewProj, center, halfExt, glm::vec3(0.45f, 0.35f, 0.25f));
        } else if (const auto* barrel = dynamic_cast<const EntityBarrel*>(ent)) {
            glm::vec3 color = barrel->IsExplosive()
                ? glm::vec3(0.85f, 0.15f, 0.15f)  // Red explosive barrel
                : glm::vec3(0.35f, 0.55f, 0.75f); // Blue/grey standard prop
            // DrawBox(ctx, viewProj, barrel->GetPosition(), barrel->GetHalfExtents(), color);
        } else if (const auto* phys = dynamic_cast<const EntityPhysbox*>(ent)) {
            // DrawBox(ctx, viewProj, phys->GetPosition(), phys->GetHalfExtents(), glm::vec3(0.6f, 0.5f, 0.35f));
        } else if (const auto* zombie = dynamic_cast<const EntityZombie*>(ent)) {
            glm::vec3 center = zombie->GetPosition() + glm::vec3(0.0f, 36.0f, 0.0f);
            // DrawBox(ctx, viewProj, center, glm::vec3(16.0f, 36.0f, 16.0f), glm::vec3(0.2f, 0.65f, 0.25f));
        }
    }
}
