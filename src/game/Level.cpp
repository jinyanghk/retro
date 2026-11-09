#include "Level.h"

std::string currentMap = "";
std::string nextMap = "";
bool pendingMapSwitch = false;

LevelState currentState = LevelState::PLAYING;
float transitionTimer = 0.0f;

// 👈 辅助函数：清空当前地图的物理、渲染与敌人资源
void CleanupCurrentLevel(JPH::BodyInterface& body_interface,
                         std::vector<JPH::BodyID>& physboxIds,
                         std::vector<JPH::BodyID>& doorBodies,
                         std::vector<EnemyInfo>& enemies,
                         JPH::BodyID& player_body) 
{
    for (auto id : physboxIds) { if(!id.IsInvalid()) { body_interface.RemoveBody(id); body_interface.DestroyBody(id); } }
    physboxIds.clear();
    for (auto id : doorBodies) { if(!id.IsInvalid()) { body_interface.RemoveBody(id); body_interface.DestroyBody(id); } }
    doorBodies.clear();
    enemies.clear();
    if(!player_body.IsInvalid()) { body_interface.RemoveBody(player_body); body_interface.DestroyBody(player_body); }
    
    g_sortedTris.clear();
    g_bvh.clear();
}