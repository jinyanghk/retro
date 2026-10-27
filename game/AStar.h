#pragma once
#include "MapLoader.h"
#include <glm/glm.hpp>
#include <vector>

// 将世界坐标转换为网格坐标
glm::ivec2 WorldToGrid(const MapData& map, const glm::vec3& pos);
// 将网格坐标转换为世界坐标（格子中心）
glm::vec3 GridToWorld(const MapData& map, int cx, int cy);

// A* 寻路，返回世界坐标的路径点（不包含起点）
std::vector<glm::vec3> FindPath(const MapData& map, const glm::vec3& startPos, const glm::vec3& endPos);