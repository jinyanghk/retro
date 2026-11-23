#pragma once
#include <glm/glm.hpp>
#include "MapLoader.h"

void Draw3DView(bool& outClickedLeft, bool& outClickedRight, bool& outClickedMiddle);

// 暴露给 Main 里的 OnLoadBvh 和 CheckNavCollisions 用
extern MapData g_loadedMapData;
extern bool g_bLoadedBvh;
extern std::vector<MapData::NodeData> g_editorNavNodes;

struct BadNavLink { glm::vec3 start, end; };
struct BadNavNode { glm::vec3 pos; };
extern std::vector<BadNavLink> g_badNavLinks;
extern std::vector<BadNavNode> g_badNavNodes;