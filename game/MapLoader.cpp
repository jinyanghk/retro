#include "MapLoader.h"
#include <cstring>
#include <cstdio>

bool LoadMap(const char* filename, MapData& outData) {
    std::ifstream f(filename, std::ios::binary);
    if (!f.is_open()) { printf("Cannot open %s\n", filename); return false; }

    char magic[4]; f.read(magic, 4);
    if (strncmp(magic, "MBVH", 4) != 0) { printf("Invalid map format\n"); return false; }

    uint32_t version = 0, lumpCount = 0;
    f.read((char*)&version, 4);
    f.read((char*)&lumpCount, 4);

    if (version < 3) {
        printf("Map version %u < 3, please recompile map.bvh\n", version);
        return false;
    }

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

    // 2) GEOMETRY (v3: stride 9)
    if (auto d = FindLump(LUMP_GEOMETRY)) {
        f.seekg(d->offset);
        f.read((char*)&outData.nv, 4);
        f.read((char*)&outData.ni, 4);
        outData.verts.resize(outData.nv * 9);
        outData.indices.resize(outData.ni);
        f.read((char*)outData.verts.data(),   outData.nv * 9 * 4);
        f.read((char*)outData.indices.data(), outData.ni * 4);
    }

    // 3) COLLISION
    if (auto d = FindLump(LUMP_COLLISION)) {
        f.seekg(d->offset);
        f.read((char*)&outData.cnv, 4);
        f.read((char*)&outData.cni, 4);
        outData.collisionVerts.resize(outData.cnv * 3);
        outData.collisionIndices.resize(outData.cni);
        f.read((char*)outData.collisionVerts.data(),   outData.cnv * 3 * 4);
        f.read((char*)outData.collisionIndices.data(), outData.cni * 4);
    } else {
        // 兜底：从 GEOMETRY 抽 pos（stride 9 → 3）
        outData.cnv = outData.nv; outData.cni = outData.ni;
        outData.collisionVerts.resize(outData.nv * 3);
        for (uint32_t i = 0; i < outData.nv; i++) {
            outData.collisionVerts[i*3+0] = outData.verts[i*9+0];
            outData.collisionVerts[i*3+1] = outData.verts[i*9+1];
            outData.collisionVerts[i*3+2] = outData.verts[i*9+2];
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

    // 9) ENTITIES (v3) —— 变长记录，顺序读
    if (auto d = FindLump(LUMP_ENTITIES)) {
        f.seekg(d->offset);
        uint32_t count = 0; f.read((char*)&count, 4);
        outData.entities.reserve(count);
        for (uint32_t i = 0; i < count; i++) {
            EntityRecord r{};
            f.read((char*)&r, sizeof(EntityRecord));
            std::string kvStr(r.kvJsonSize, '\0');
            if (r.kvJsonSize) f.read(&kvStr[0], r.kvJsonSize);

            MapData::EntityKV e;
            e.classname = LumpString(r.classname, 64);
            e.origin    = glm::vec3(r.origin[0], r.origin[1], r.origin[2]);
            e.angles    = glm::vec3(r.angles[0], r.angles[1], r.angles[2]);
            e.bboxMins  = glm::vec3(r.bboxMins[0], r.bboxMins[1], r.bboxMins[2]);
            e.bboxMaxs  = glm::vec3(r.bboxMaxs[0], r.bboxMaxs[1], r.bboxMaxs[2]);
            try {
                e.kv = kvStr.empty() ? nlohmann::json::object()
                                     : nlohmann::json::parse(kvStr);
            } catch (const std::exception& ex) {
                printf("Entity %s kv parse error: %s\n", e.classname.c_str(), ex.what());
                e.kv = nlohmann::json::object();
            }
            outData.entities.push_back(std::move(e));
        }

        // 验证打印（跑通后可删）
        for (auto& e : outData.entities) {
            if (e.kv.contains("my_custom_data"))
                printf("Entity %s has my_custom_data=%s\n",
                       e.classname.c_str(), e.kv["my_custom_data"].dump().c_str());
        }
    }

    printf("Loaded map: Version=%u, Lumps=%u, Entities=%zu\n",
           version, lumpCount, outData.entities.size());
    return true;
}