#ifndef SCENE_ENTITY_H
#define SCENE_ENTITY_H

#include <glm/glm.hpp>
#include <string>
#include <map>

enum EntityType {
    ENTITY_MODEL = 0,
    ENTITY_PLAYER_START,
    ENTITY_LIGHT,
};

struct CEntity {
    EntityType  m_iType = ENTITY_MODEL;
    std::string m_szName;              // classname，如 "info_player_start" / "light"
    glm::vec3   m_vecPos = glm::vec3(0);
    glm::vec3   m_angRot = glm::vec3(0);   // pitch,yaw,roll

    // GameDatabase keyvalues（字符串存储）
    std::map<std::string, std::string> m_properties;

    // bbox（由 UpdateEntityBBox 每帧算）
    glm::vec3 m_vecBBoxMins = glm::vec3(-8);
    glm::vec3 m_vecBBoxMaxs = glm::vec3( 8);

    glm::vec3 GetCenter() const { return (m_vecBBoxMins + m_vecBBoxMaxs) * 0.5f; }
};

// GameDatabase bbox 或默认 ±8；retro 没有 MDL，所以永远走这条
void UpdateEntityBBox(CEntity& ent);

#endif