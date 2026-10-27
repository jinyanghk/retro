#pragma once
#include <cstdint>
#include <cstring>
#include <string>

static constexpr uint32_t MAP_MAGIC = 0x4856424D; // "MBVH"
static constexpr uint32_t MAP_VERSION = 2;

enum LumpType : uint32_t {
    LUMP_GEOMETRY   = 1,
    LUMP_COLLISION  = 2,
    LUMP_SPAWN      = 3,
    LUMP_TRIGGERS   = 4,
    LUMP_PHYSBOX    = 5,
    LUMP_DOORS      = 6,
    LUMP_ZOMBIES    = 7,
    LUMP_BARRELS    = 8,
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
#pragma pack(pop)

inline std::string LumpString(const char* arr, size_t maxLen) {
    size_t n = strnlen(arr, maxLen);
    return std::string(arr, n);
}