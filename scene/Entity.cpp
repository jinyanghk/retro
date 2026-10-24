#include "scene/Entity.h"
#include "fgd/FgdDatabase.h"

void UpdateEntityBBox(CEntity& ent)
{
    // ★ 对于 trigger 和 func_physbox，完全保留用户在 2D 视图里拉出来的尺寸
    if (ent.m_szName.find("trigger") != std::string::npos ||
        ent.m_szName == "func_physbox") {
        glm::vec3 halfSize = (ent.m_vecBBoxMaxs - ent.m_vecBBoxMins) * 0.5f;
        ent.m_vecBBoxMins = ent.m_vecPos - halfSize;
        ent.m_vecBBoxMaxs = ent.m_vecPos + halfSize;
        return;
    }

    // 默认 FGD 逻辑
    glm::vec3 mins, maxs;
    if (FgdDatabase::Instance().GetBBox(ent.m_szName, mins, maxs)) {
        ent.m_vecBBoxMins = ent.m_vecPos + mins;
        ent.m_vecBBoxMaxs = ent.m_vecPos + maxs;
    } else {
        ent.m_vecBBoxMins = ent.m_vecPos + glm::vec3(-8);
        ent.m_vecBBoxMaxs = ent.m_vecPos + glm::vec3( 8);
    }
}