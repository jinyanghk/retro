#pragma once
#include <cstdint>
#include <cstring>
#include <string>

static constexpr uint32_t MAP_MAGIC   = 0x4856424D; // "MBVH"
static constexpr uint32_t MAP_VERSION = 4; // v4: +LUMP_NAVGRID

enum LumpType : uint32_t {
    LUMP_GEOMETRY   = 1,
    LUMP_COLLISION  = 2,
    LUMP_SPAWN      = 3,
    LUMP_TRIGGERS   = 4,
    LUMP_PHYSBOX    = 5,
    LUMP_DOORS      = 6,
    LUMP_ZOMBIES    = 7,
    LUMP_BARRELS    = 8,
    LUMP_ENTITIES   = 9,
    LUMP_NAVGRID    = 10,
};

#pragma pack(push, 1)
struct LumpDir {
    uint32_t type;
    uint32_t offset;
    uint32_t size;
};

struct TriggerRecord {
    float mins[3];
    float maxs[3];
    char  target[64];
};

struct DoorRecord {
    float mins[3];
    float maxs[3];
    float dir[3];
    float dist;
    float speed;
    float wait;
    char  targetname[64];
};

// v3: 变长实体记录
// 布局: u32 count, 然后 count 条 EntityRecord
//       每条 EntityRecord 之后紧跟 kvJsonSize 字节的 UTF-8 JSON
//       所有 EntityRecord 先连续存放，再放所有 kvJson（见注释图）
// 为避免对齐问题，我们采用「交错存」更简单：
//   [count]
//   [EntityRecord][kvJson bytes][padding?] ... 循环 count 次
// 读取时按 kvJsonSize 自增指针即可。
struct EntityRecord {
    char     classname[64];
    float    origin[3];
    float    angles[3];
    float    bboxMins[3];
    float    bboxMaxs[3];
    uint32_t kvJsonSize;
    // 紧跟 kvJsonSize 字节 UTF-8
};

// v3: GEOMETRY 顶点 stride = 9 float (pos3 + normal3 + color3)
// 读取: u32 nv, u32 ni, float verts[nv*9], u32 indices[ni]

struct NavGridHeader {
    float minX, minZ;       // 网格左下角原点
    float cellSize;         // 每个格子的大小 (比如 32.0f)
    uint32_t cols, rows;    // 网格列数和行数
    // 紧跟 cols * rows 个 uint8_t，0为不可走，1为可走
};

#pragma pack(pop)

inline std::string LumpString(const char* arr, size_t maxLen) {
    size_t n = strnlen(arr, maxLen);
    return std::string(arr, n);
}