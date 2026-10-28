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

struct EntityConnection {
    std::string connectionName;  // 例如 "OnTrigger"
    std::string targetName;  // 例如 "door1"
    std::string inputName;   // 例如 "Open"
    std::string parameter;   // 参数，默认空
    float       delay = 0;   // 延迟，默认0
};

struct CEntity {
    EntityType  m_iType = ENTITY_MODEL;
    std::string m_szName;              // classname，如 "info_player_start" / "light"
    glm::vec3   m_vecPos = glm::vec3(0);
    glm::vec3   m_angRot = glm::vec3(0);   // pitch,yaw,roll

    // GameDatabase keyvalues（字符串存储）
    std::map<std::string, std::string> m_properties;

    // 🔥 新增：完全自由的自定义属性，不受 GameDatabase 约束
    std::map<std::string, std::string> m_customProperties;

    // bbox（由 UpdateEntityBBox 每帧算）
    glm::vec3 m_vecBBoxMins = glm::vec3(-8);
    glm::vec3 m_vecBBoxMaxs = glm::vec3( 8);

    // 🔥 新增：ImGuizmo Graph Editor 用于存储 I/O 连接
    std::vector<EntityConnection> m_connections;

    glm::vec3 GetCenter() const { return (m_vecBBoxMins + m_vecBBoxMaxs) * 0.5f; }
};

// GameDatabase bbox 或默认 ±8；retro 没有 MDL，所以永远走这条
void UpdateEntityBBox(CEntity& ent);

#endif