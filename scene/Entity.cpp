#include "scene/Entity.h"
#include "fgd/FgdDatabase.h"

void UpdateEntityBBox(CEntity& ent)
{
    // retro 没有 MDL，全部走 FGD size
    glm::vec3 mins, maxs;
    if (FgdDatabase::Instance().GetBBox(ent.m_szName, mins, maxs)) {
        ent.m_vecBBoxMins = ent.m_vecPos + mins;
        ent.m_vecBBoxMaxs = ent.m_vecPos + maxs;
    } else {
        ent.m_vecBBoxMins = ent.m_vecPos + glm::vec3(-8);
        ent.m_vecBBoxMaxs = ent.m_vecPos + glm::vec3( 8);
    }
}