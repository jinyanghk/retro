#pragma once
#include <cstdint>
#include <cstring>
#include <string>

static constexpr uint32_t MAP_MAGIC   = 0x4856424D; // "MBVH"
static constexpr uint32_t MAP_VERSION = 7; // v7: stride 11 + real UV

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
    LUMP_NODEGRAPH  = 11, // v5
    LUMP_MATERIALS  = 12, // v6: 材质分组（渲染分批用）
    LUMP_SKYNAME    = 13, // 👈 新增
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

struct EntityRecord {
    char     classname[64];
    float    origin[3];
    float    angles[3];
    float    bboxMins[3];
    float    bboxMaxs[3];
    uint32_t kvJsonSize;
    // 紧跟 kvJsonSize 字节 UTF-8
};

struct NavGridHeader {
    float minX, minZ;
    float cellSize;
    uint32_t cols, rows;
    // 紧跟 cols * rows 个 uint8_t
};

struct NodeRecord {
    float pos[3];
    uint32_t firstLink;
    uint32_t numLinks;
};

// v6: 材质分组记录
// 布局: u32 groupCount, [MaterialGroupRecord] × groupCount
struct MaterialGroupRecord {
    char     name[64];       // 材质名
    uint32_t indexOffset;    // 在 LUMP_GEOMETRY.indices 里的起始下标
    uint32_t indexCount;     // 索引个数（6 的倍数）
};

#pragma pack(pop)

inline std::string LumpString(const char* arr, size_t maxLen) {
    size_t n = strnlen(arr, maxLen);
    return std::string(arr, n);
}