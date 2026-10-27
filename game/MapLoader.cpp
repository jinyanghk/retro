#include "MapLoader.h"
#include <cstring>

bool LoadMap(const char* filename, MapData& outData) {
    std::ifstream f(filename, std::ios::binary);
    if (!f.is_open()) { printf("Cannot open %s\n", filename); return false; }

    char magic[4]; f.read(magic, 4);
    if (strncmp(magic, "MBVH", 4) != 0) { printf("Invalid map format\n"); return false; }

    uint32_t version = 0, lumpCount = 0;
    f.read((char*)&version, 4);
    f.read((char*)&lumpCount, 4);

    std::vector<LumpDir> dirs(lumpCount);
    f.read((char*)dirs.data(), lumpCount * sizeof(LumpDir));

    auto FindLump = [&](uint32_t type) -> const LumpDir* {
        for (auto& d : dirs) if (d.type == type) return &d;
        return nullptr;
    };

    // 1) SPAWN
    if (auto d = FindLump(LUMP_SPAWN)) {
        f.seekg(d->offset);
        f.read((char*)outData.spawnPos, 12);
        f.read((char*)outData.spawnAngles, 12);
    }

    // 2) GEOMETRY
    if (auto d = FindLump(LUMP_GEOMETRY)) {
        f.seekg(d->offset);
        f.read((char*)&outData.nv, 4);
        f.read((char*)&outData.ni, 4);
        outData.verts.resize(outData.nv * 6);
        outData.indices.resize(outData.ni);
        f.read((char*)outData.verts.data(), outData.nv * 6 * 4);
        f.read((char*)outData.indices.data(), outData.ni * 4);
    }

    // 3) COLLISION
    if (auto d = FindLump(LUMP_COLLISION)) {
        f.seekg(d->offset);
        f.read((char*)&outData.cnv, 4);
        f.read((char*)&outData.cni, 4);
        outData.collisionVerts.resize(outData.cnv * 3);
        outData.collisionIndices.resize(outData.cni);
        f.read((char*)outData.collisionVerts.data(), outData.cnv * 3 * 4);
        f.read((char*)outData.collisionIndices.data(), outData.cni * 4);
    } else {
        // 兜底
        outData.cnv = outData.nv; outData.cni = outData.ni;
        outData.collisionVerts.resize(outData.nv * 3);
        for (uint32_t i = 0; i < outData.nv; i++) {
            outData.collisionVerts[i*3+0] = outData.verts[i*6+0];
            outData.collisionVerts[i*3+1] = outData.verts[i*6+1];
            outData.collisionVerts[i*3+2] = outData.verts[i*6+2];
        }
        outData.collisionIndices = outData.indices;
    }

    // 4) TRIGGERS
    if (auto d = FindLump(LUMP_TRIGGERS)) {
        f.seekg(d->offset);
        uint32_t count = 0; f.read((char*)&count, 4);
        std::vector<TriggerRecord> recs(count);
        f.read((char*)recs.data(), count * sizeof(TriggerRecord));
        for (auto& r : recs) {
            outData.triggerMins.push_back(glm::vec3(r.mins[0], r.mins[1], r.mins[2]));
            outData.triggerMaxs.push_back(glm::vec3(r.maxs[0], r.maxs[1], r.maxs[2]));
            outData.triggerTargets.push_back(LumpString(r.target, 64));
        }
    }

    // 5) PHYSBOX
    if (auto d = FindLump(LUMP_PHYSBOX)) {
        f.seekg(d->offset);
        uint32_t count = 0; f.read((char*)&count, 4);
        std::vector<float> buf(count * 6);
        f.read((char*)buf.data(), count * 6 * 4);
        for (uint32_t i = 0; i < count; i++) {
            outData.physboxMins.push_back(glm::vec3(buf[i*6+0], buf[i*6+1], buf[i*6+2]));
            outData.physboxMaxs.push_back(glm::vec3(buf[i*6+3], buf[i*6+4], buf[i*6+5]));
        }
    }

    // 6) DOORS
    if (auto d = FindLump(LUMP_DOORS)) {
        f.seekg(d->offset);
        uint32_t count = 0; f.read((char*)&count, 4);
        std::vector<DoorRecord> recs(count);
        f.read((char*)recs.data(), count * sizeof(DoorRecord));
        for (auto& r : recs) {
            MapData::DoorInfo di;
            di.mins     = glm::vec3(r.mins[0], r.mins[1], r.mins[2]);
            di.maxs     = glm::vec3(r.maxs[0], r.maxs[1], r.maxs[2]);
            di.openDir  = glm::vec3(r.dir[0],  r.dir[1],  r.dir[2]);
            di.openDist = r.dist;
            di.speed    = r.speed;
            di.waitTime = r.wait;
            di.targetname = LumpString(r.targetname, 64);
            outData.doors.push_back(di);
        }
    }

    // 7) ZOMBIES
    if (auto d = FindLump(LUMP_ZOMBIES)) {
        f.seekg(d->offset);
        uint32_t count = 0; f.read((char*)&count, 4);
        std::vector<float> buf(count * 3);
        f.read((char*)buf.data(), count * 3 * 4);
        for (uint32_t i = 0; i < count; i++)
            outData.zombieSpawns.push_back(glm::vec3(buf[i*3+0], buf[i*3+1], buf[i*3+2]));
    }

    // 8) BARRELS
    if (auto d = FindLump(LUMP_BARRELS)) {
        f.seekg(d->offset);
        uint32_t count = 0; f.read((char*)&count, 4);
        std::vector<float> buf(count * 3);
        f.read((char*)buf.data(), count * 3 * 4);
        for (uint32_t i = 0; i < count; i++)
            outData.barrelSpawns.push_back(glm::vec3(buf[i*3+0], buf[i*3+1], buf[i*3+2]));
    }

    printf("Loaded map: Version=%u, Lumps=%u\n", version, lumpCount);
    return true;
}