#include "AStar.h"
#include <queue>
#include <unordered_map>
#include <algorithm>

struct AStarNode {
    uint32_t index;
    float gScore;
    float fScore;
    bool operator>(const AStarNode& other) const { return fScore > other.fScore; }
};

// 寻找离当前位置最近的导航节点
static int GetClosestNode(const MapData& map, const glm::vec3& pos) {
    int best = -1;
    float bestDist = 1e9f;
    for (size_t i = 0; i < map.navNodes.size(); ++i) {
        float d = glm::distance(pos, map.navNodes[i].pos);
        if (d < bestDist) { bestDist = d; best = (int)i; }
    }
    return best;
}

std::vector<glm::vec3> FindPath(const MapData& map, const glm::vec3& startPos, const glm::vec3& endPos) {
    std::vector<glm::vec3> path;
    
    // 如果地图里没有节点图，直接直线冲向目标
    if (map.navNodes.empty()) {
        path.push_back(endPos);
        return path;
    }

    int startNode = GetClosestNode(map, startPos);
    int endNode = GetClosestNode(map, endPos);

    if (startNode == -1 || endNode == -1) {
        path.push_back(endPos);
        return path;
    }

    // 如果起点和终点是同一个节点，或者距离太近，直接走过去
    if (startNode == endNode) {
        path.push_back(endPos);
        return path;
    }

    std::priority_queue<AStarNode, std::vector<AStarNode>, std::greater<AStarNode>> openSet;
    std::unordered_map<uint32_t, float> gScore;
    std::unordered_map<uint32_t, int> cameFrom;
    std::vector<bool> closedSet(map.navNodes.size(), false);

    // 初始化代价表
    for (size_t i = 0; i < map.navNodes.size(); ++i) gScore[i] = 1e9f;

    gScore[startNode] = 0.0f;
    openSet.push({ (uint32_t)startNode, 0.0f, glm::distance(map.navNodes[startNode].pos, endPos) });

    while (!openSet.empty()) {
        uint32_t current = openSet.top().index;
        openSet.pop();

        // 找到终点节点，重建路径
        if (current == (uint32_t)endNode) {
            uint32_t curr = current;
            while (curr != (uint32_t)startNode) {
                path.push_back(map.navNodes[curr].pos);
                curr = cameFrom[curr];
            }
            path.push_back(map.navNodes[startNode].pos);
            std::reverse(path.begin(), path.end());
            path.push_back(endPos); // 最后加上玩家的真实位置
            return path;
        }

        if (closedSet[current]) continue;
        closedSet[current] = true;

        for (uint32_t neighbor : map.navNodes[current].links) {
            if (closedSet[neighbor]) continue;

            float dist = glm::distance(map.navNodes[current].pos, map.navNodes[neighbor].pos);
            float tentativeG = gScore[current] + dist;

            if (tentativeG < gScore[neighbor]) {
                cameFrom[neighbor] = current;
                gScore[neighbor] = tentativeG;
                float f = tentativeG + glm::distance(map.navNodes[neighbor].pos, endPos);
                openSet.push({ neighbor, tentativeG, f });
            }
        }
    }

    // 如果没找到路径（图断了），回退到直接冲锋
    path.push_back(endPos);
    return path;
}