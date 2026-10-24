#include "scene/Entity.h"
#include "fgd/FgdDatabase.h"

void UpdateEntityBBox(CEntity& ent)
{
    // 如果它是 trigger，并且被手动调整过尺寸（或者直接豁免），就不重置
    if (ent.m_szName.find("trigger") != std::string::npos) {
        // Trigger 完全由用户在 2D 视图里自由拉伸，永远不走 FGD 默认尺寸
        // 只需要保证它跟着 m_vecPos 平移即可
        glm::vec3 halfSize = (ent.m_vecBBoxMaxs - ent.m_vecBBoxMins) * 0.5f;
        ent.m_vecBBoxMins = ent.m_vecPos - halfSize;
        ent.m_vecBBoxMaxs = ent.m_vecPos + halfSize;
        return;
    }

    // 默认走 FGD 尺寸
    glm::vec3 mins, maxs;
    if (FgdDatabase::Instance().GetBBox(ent.m_szName, mins, maxs)) {
        ent.m_vecBBoxMins = ent.m_vecPos + mins;
        ent.m_vecBBoxMaxs = ent.m_vecPos + maxs;
    } else {
        ent.m_vecBBoxMins = ent.m_vecPos + glm::vec3(-8);
        ent.m_vecBBoxMaxs = ent.m_vecPos + glm::vec3( 8);
    }
}