#pragma once
#include "MapLoader.h"
#include <glm/glm.hpp>
#include <vector>

// 👈 不要 declare GridToWorld/WorldToGrid 到外部
std::vector<glm::vec3> FindPath(const MapData& map, const glm::vec3& startPos, const glm::vec3& endPos);