#include "AStar.h"
#include <queue>
#include <unordered_map>
#include <cmath>
#include <algorithm>

glm::ivec2 WorldToGrid(const MapData& map, const glm::vec3& pos) {
    int cx = std::floor((pos.x - map.navMinX) / map.navCellSize);
    int cy = std::floor((pos.z - map.navMinZ) / map.navCellSize);
    return glm::ivec2(cx, cy);
}

glm::vec3 GridToWorld(const MapData& map, int cx, int cy) {
    float x = map.navMinX + cx * map.navCellSize + map.navCellSize * 0.5f;
    float z = map.navMinZ + cy * map.navCellSize + map.navCellSize * 0.5f;
    // TODO: 如果你想让僵尸贴着地面走，这里 y 应该是该格子的地面高度。
    // 目前先简单粗暴用 0，或者让 Enemy 自己处理重力。
    return glm::vec3(x, 0.0f, z); 
}

struct Node {
    int x, y;
    float g, h;
    float f() const { return g + h; }
    bool operator>(const Node& other) const { return f() > other.f(); }
};

std::vector<glm::vec3> FindPath(const MapData& map, const glm::vec3& startPos, const glm::vec3& endPos) {
    std::vector<glm::vec3> path;
    if (map.navWalkable.empty()) return path;

    glm::ivec2 start = WorldToGrid(map, startPos);
    glm::ivec2 end = WorldToGrid(map, endPos);

    // 限制在地图边界内
    auto isValid = [&](int x, int y) {
        return x >= 0 && x < (int)map.navCols && y >= 0 && y < (int)map.navRows;
    };

    if (!isValid(start.x, start.y) || !isValid(end.x, end.y)) return path;
    
    // 如果终点本身不可走（比如在墙里），我们就找离终点最近的可走格子
    if (map.navWalkable[end.y * map.navCols + end.x] == 0) {
        // 简单兜底：这里可以加个 BFS 找最近格子，先略过
        return path; 
    }

    int W = map.navCols;
    int H = map.navRows;

    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> openList;
    std::vector<float> gScore(W * H, 1e9f);
    std::vector<int> cameFrom(W * H, -1);
    std::vector<bool> closed(W * H, false);

    auto heuristic = [](int x1, int y1, int x2, int y2) {
        return std::abs(x1 - x2) + std::abs(y1 - y2); // 曼哈顿距离
    };

    int startIdx = start.y * W + start.x;
    int endIdx = end.y * W + end.x;

    gScore[startIdx] = 0;
    openList.push({start.x, start.y, 0.0f, (float)heuristic(start.x, start.y, end.x, end.y)});

    // 8方向寻路（上下左右 + 斜角）
    int dx[] = {1, -1, 0, 0, 1, 1, -1, -1};
    int dy[] = {0, 0, 1, -1, 1, -1, 1, -1};

    while (!openList.empty()) {
        Node current = openList.top();
        openList.pop();

        int idx = current.y * W + current.x;
        if (closed[idx]) continue;
        closed[idx] = true;

        if (idx == endIdx) {
            // 重建路径
            int curr = endIdx;
            while (curr != startIdx && curr != -1) {
                int cx = curr % W;
                int cy = curr / W;
                path.push_back(GridToWorld(map, cx, cy));
                curr = cameFrom[curr];
            }
            std::reverse(path.begin(), path.end());
            return path;
        }

        for (int i = 0; i < 8; i++) {
            int nx = current.x + dx[i];
            int ny = current.y + dy[i];

            if (!isValid(nx, ny)) continue;
            if (map.navWalkable[ny * W + nx] == 0) continue; // 撞墙

            // 防止斜角穿墙（如果两侧都是墙，斜着不能走）
            if (i >= 4) {
                if (map.navWalkable[current.y * W + nx] == 0 || 
                    map.navWalkable[ny * W + current.x] == 0) continue;
            }

            int nIdx = ny * W + nx;
            float tentativeG = gScore[idx] + (i >= 4 ? 1.414f : 1.0f); // 斜角代价更大

            if (tentativeG < gScore[nIdx]) {
                cameFrom[nIdx] = idx;
                gScore[nIdx] = tentativeG;
                float h = heuristic(nx, ny, end.x, end.y);
                openList.push({nx, ny, tentativeG, h});
            }
        }
    }

    return path; // 没路
}