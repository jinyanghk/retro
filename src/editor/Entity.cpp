#include "Entity.h"
#include "GameDatabase.h"

#include <cmath>

void UpdateEntityBBox(CEntity& ent)
{
    // ★ 对于 trigger / func_physbox / func_door，完全保留用户在 2D 视图里拉出来的尺寸
    if (ent.m_szName.find("trigger") != std::string::npos ||
        ent.m_szName == "func_physbox" ||
        ent.m_szName == "func_door") {
        glm::vec3 halfSize = (ent.m_vecBBoxMaxs - ent.m_vecBBoxMins) * 0.5f;
        // 首次创建时（半尺寸 ≈ 8 默认值）给门一个合理的默认尺寸
        if (ent.m_szName == "func_door" &&
            fabsf(halfSize.x - 8.0f) < 0.1f && fabsf(halfSize.y - 8.0f) < 0.1f) {
            halfSize = glm::vec3(32.0f, 64.0f, 8.0f);  // 门宽 64，高 128，厚 16
        }
        ent.m_vecBBoxMins = ent.m_vecPos - halfSize;
        ent.m_vecBBoxMaxs = ent.m_vecPos + halfSize;
        return;
    }

    // 默认 GameDatabase 逻辑
    glm::vec3 mins, maxs;
    if (GameDatabase::Instance().GetBBox(ent.m_szName, mins, maxs)) {
        ent.m_vecBBoxMins = ent.m_vecPos + mins;
        ent.m_vecBBoxMaxs = ent.m_vecPos + maxs;
    } else {
        ent.m_vecBBoxMins = ent.m_vecPos + glm::vec3(-8);
        ent.m_vecBBoxMaxs = ent.m_vecPos + glm::vec3( 8);
    }
}