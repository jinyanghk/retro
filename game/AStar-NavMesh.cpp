#include "AStar.h"
#include <queue>
#include <cmath>
#include <algorithm>

static glm::ivec2 WorldToGrid(const MapData& map, const glm::vec3& pos) {
    int cx = std::floor((pos.x - map.navMinX) / map.navCellSize);
    int cy = std::floor((pos.z - map.navMinZ) / map.navCellSize);
    return glm::ivec2(cx, cy);
}

static glm::vec3 GridToWorld(const MapData& map, int cx, int cy) {
    float x = map.navMinX + cx * map.navCellSize + map.navCellSize * 0.5f;
    float z = map.navMinZ + cy * map.navCellSize + map.navCellSize * 0.5f;
    int idx = cy * map.navCols + cx;
    float y = (idx >= 0 && idx < (int)map.navHeights.size()) ? map.navHeights[idx] : 0.0f;
    return glm::vec3(x, y, z); 
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

    auto isValid = [&](int x, int y) {
        return x >= 0 && x < (int)map.navCols && y >= 0 && y < (int)map.navRows;
    };

    if (!isValid(start.x, start.y) || !isValid(end.x, end.y)) return path;
    
    // 终点不可走时，找最近的可走格
    if (map.navWalkable[end.y * map.navCols + end.x] == 0) {
        float minDist = 1e9f;
        int bestX = end.x, bestY = end.y;
        for (int dy = -3; dy <= 3; ++dy) {
            for (int dx = -3; dx <= 3; ++dx) {
                int nx = end.x + dx, ny = end.y + dy;
                if (isValid(nx, ny) && map.navWalkable[ny * map.navCols + nx]) {
                    float d = (float)(dx*dx + dy*dy);
                    if (d < minDist) { minDist = d; bestX = nx; bestY = ny; }
                }
            }
        }
        end = glm::ivec2(bestX, bestY);
    }

    int W = map.navCols;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> openList;
    std::vector<float> gScore(W * map.navRows, 1e9f);
    std::vector<int> cameFrom(W * map.navRows, -1);
    std::vector<bool> closed(W * map.navRows, false);

    auto heuristic = [](int x1, int y1, int x2, int y2) {
        return std::abs(x1 - x2) + std::abs(y1 - y2);
    };

    int startIdx = start.y * W + start.x;
    int endIdx = end.y * W + end.x;

    gScore[startIdx] = 0.0f;
    openList.push({start.x, start.y, 0.0f, (float)heuristic(start.x, start.y, end.x, end.y)});

    int dx[] = {1, -1, 0, 0, 1, 1, -1, -1};
    int dy[] = {0, 0, 1, -1, 1, -1, 1, -1};

    while (!openList.empty()) {
        Node current = openList.top();
        openList.pop();

        int idx = current.y * W + current.x;
        if (closed[idx]) continue;
        closed[idx] = true;

        if (idx == endIdx) {
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

            if (!isValid(nx, ny) || map.navWalkable[ny * W + nx] == 0) continue;

            if (i >= 4 && (map.navWalkable[current.y * W + nx] == 0 || 
                           map.navWalkable[ny * W + current.x] == 0)) continue;

            int nIdx = ny * W + nx;
            float tentativeG = gScore[idx] + (i >= 4 ? 1.414f : 1.0f);

            if (tentativeG < gScore[nIdx]) {
                cameFrom[nIdx] = idx;
                gScore[nIdx] = tentativeG;
                float h = (float)heuristic(nx, ny, end.x, end.y);
                openList.push({nx, ny, tentativeG, h});
            }
        }
    }
    return path;
}