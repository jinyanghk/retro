#include "MapCompiler.h"
#include <fstream>
#include <iostream>
#include <cstring>

static void AppendLumpRaw(std::vector<char>& outBuffer,
                          std::vector<LumpDir>& dirs,
                          uint32_t type, const std::vector<char>& raw)
{
    printf("[Packer] Appending lump type: %u, size: %zu\n", type, raw.size()); // 👈 加上这行！
    LumpDir d;
    d.type   = type;
    d.offset = (uint32_t)outBuffer.size();
    d.size   = (uint32_t)raw.size();
    outBuffer.insert(outBuffer.end(), raw.begin(), raw.end());
    while (outBuffer.size() % 4) outBuffer.push_back(0);
    dirs.push_back(d);
}

bool PackLumps(const MapData& map,
               const MeshBuffers& renderMesh,
               const MeshBuffers& collisionMesh,
               const std::vector<float>& triggerBounds,
               const std::vector<std::string>& triggerTargets,
               const std::vector<float>& physboxBounds,
               const std::vector<float>& doorBounds,
               const std::vector<std::string>& doorNames,
               const std::vector<float>& zombieSpawns,
               const std::vector<float>& barrelSpawns,
               const NavGrid& navGrid,
               const std::vector<NodeRecord>& nodeRecs, 
               const std::vector<uint32_t>& nodeLinks,                
               const char* outPath)
{
    std::vector<char> outBuffer;
    std::vector<LumpDir> dirs;

    size_t headerStart = outBuffer.size();
    outBuffer.resize(12 + 12 * sizeof(LumpDir), 0); // v5

    // ---- LUMP_GEOMETRY (stride 9) ----
    {
        std::vector<char> buf;
        auto PushU32 = [&](uint32_t v){ buf.insert(buf.end(), (char*)&v, (char*)&v + 4); };
        uint32_t nv = (uint32_t)(renderMesh.verts.size() / 9);
        uint32_t ni = (uint32_t)renderMesh.indices.size();
        PushU32(nv); PushU32(ni);
        buf.insert(buf.end(), (const char*)renderMesh.verts.data(),
                   (const char*)renderMesh.verts.data() + renderMesh.verts.size()*4);
        buf.insert(buf.end(), (const char*)renderMesh.indices.data(),
                   (const char*)renderMesh.indices.data() + renderMesh.indices.size()*4);
        AppendLumpRaw(outBuffer, dirs, LUMP_GEOMETRY, buf);
    }

    // ---- LUMP_COLLISION ----
    {
        std::vector<char> buf;
        auto PushU32 = [&](uint32_t v){ buf.insert(buf.end(), (char*)&v, (char*)&v + 4); };
        uint32_t cnv = (uint32_t)(collisionMesh.verts.size() / 3);
        uint32_t cni = (uint32_t)collisionMesh.indices.size();
        PushU32(cnv); PushU32(cni);
        buf.insert(buf.end(), (const char*)collisionMesh.verts.data(),
                   (const char*)collisionMesh.verts.data() + collisionMesh.verts.size()*4);
        buf.insert(buf.end(), (const char*)collisionMesh.indices.data(),
                   (const char*)collisionMesh.indices.data() + collisionMesh.indices.size()*4);
        AppendLumpRaw(outBuffer, dirs, LUMP_COLLISION, buf);
    }

    // ---- LUMP_SPAWN ----
    {
        std::vector<char> buf;
        buf.insert(buf.end(), (char*)map.spawnPos,    (char*)map.spawnPos + 12);
        buf.insert(buf.end(), (char*)map.spawnAngles, (char*)map.spawnAngles + 12);
        AppendLumpRaw(outBuffer, dirs, LUMP_SPAWN, buf);
    }

    // ---- LUMP_TRIGGERS ----
    {
        std::vector<TriggerRecord> records;
        for (size_t i = 0; i < triggerBounds.size() / 6; i++) {
            TriggerRecord r{};
            r.mins[0]=triggerBounds[i*6+0]; r.mins[1]=triggerBounds[i*6+1]; r.mins[2]=triggerBounds[i*6+2];
            r.maxs[0]=triggerBounds[i*6+3]; r.maxs[1]=triggerBounds[i*6+4]; r.maxs[2]=triggerBounds[i*6+5];
            std::string t = (i < triggerTargets.size()) ? triggerTargets[i] : "";
            strncpy(r.target, t.c_str(), 63);
            records.push_back(r);
        }
        std::vector<char> buf;
        uint32_t count = (uint32_t)records.size();
        buf.insert(buf.end(), (char*)&count, (char*)&count + 4);
        buf.insert(buf.end(), (char*)records.data(),
                   (char*)records.data() + records.size()*sizeof(TriggerRecord));
        AppendLumpRaw(outBuffer, dirs, LUMP_TRIGGERS, buf);
    }

    // ---- LUMP_PHYSBOX ----
    {
        std::vector<char> buf;
        uint32_t count = (uint32_t)(physboxBounds.size() / 6);
        buf.insert(buf.end(), (char*)&count, (char*)&count + 4);
        buf.insert(buf.end(), (char*)physboxBounds.data(),
                   (char*)physboxBounds.data() + physboxBounds.size()*4);
        AppendLumpRaw(outBuffer, dirs, LUMP_PHYSBOX, buf);
    }

    // ---- LUMP_DOORS ----
    {
        std::vector<DoorRecord> records;
        for (size_t i = 0; i < doorBounds.size() / 12; i++) {
            DoorRecord r{};
            r.mins[0]=doorBounds[i*12+0]; r.mins[1]=doorBounds[i*12+1]; r.mins[2]=doorBounds[i*12+2];
            r.maxs[0]=doorBounds[i*12+3]; r.maxs[1]=doorBounds[i*12+4]; r.maxs[2]=doorBounds[i*12+5];
            r.dir[0] =doorBounds[i*12+6]; r.dir[1] =doorBounds[i*12+7]; r.dir[2] =doorBounds[i*12+8];
            r.dist   =doorBounds[i*12+9];
            r.speed  =doorBounds[i*12+10];
            r.wait   =doorBounds[i*12+11];
            std::string n = (i < doorNames.size()) ? doorNames[i] : "";
            strncpy(r.targetname, n.c_str(), 63);
            records.push_back(r);
        }
        std::vector<char> buf;
        uint32_t count = (uint32_t)records.size();
        buf.insert(buf.end(), (char*)&count, (char*)&count + 4);
        buf.insert(buf.end(), (char*)records.data(),
                   (char*)records.data() + records.size()*sizeof(DoorRecord));
        AppendLumpRaw(outBuffer, dirs, LUMP_DOORS, buf);
    }

    // ---- LUMP_ZOMBIES ----
    {
        std::vector<char> buf;
        uint32_t count = (uint32_t)(zombieSpawns.size() / 3);
        buf.insert(buf.end(), (char*)&count, (char*)&count + 4);
        buf.insert(buf.end(), (char*)zombieSpawns.data(),
                   (char*)zombieSpawns.data() + zombieSpawns.size()*4);
        AppendLumpRaw(outBuffer, dirs, LUMP_ZOMBIES, buf);
    }

    // ---- LUMP_BARRELS ----
    {
        std::vector<char> buf;
        uint32_t count = (uint32_t)(barrelSpawns.size() / 3);
        buf.insert(buf.end(), (char*)&count, (char*)&count + 4);
        buf.insert(buf.end(), (char*)barrelSpawns.data(),
                   (char*)barrelSpawns.data() + barrelSpawns.size()*4);
        AppendLumpRaw(outBuffer, dirs, LUMP_BARRELS, buf);
    }

    // ---- LUMP_ENTITIES (v3) ----
    // 布局: u32 count, 然后 count 次 [EntityRecord + kvJson bytes]
    {
        std::vector<char> buf;
        uint32_t count = (uint32_t)map.entities.size();
        buf.insert(buf.end(), (char*)&count, (char*)&count + 4);

        for (const auto& e : map.entities) {
            EntityRecord r{};
            strncpy(r.classname, e.classname.c_str(), 63);
            r.origin[0]=e.origin.x;   r.origin[1]=e.origin.y;   r.origin[2]=e.origin.z;
            r.angles[0]=e.angles.x;   r.angles[1]=e.angles.y;   r.angles[2]=e.angles.z;
            r.bboxMins[0]=e.bboxMins.x; r.bboxMins[1]=e.bboxMins.y; r.bboxMins[2]=e.bboxMins.z;
            r.bboxMaxs[0]=e.bboxMaxs.x; r.bboxMaxs[1]=e.bboxMaxs.y; r.bboxMaxs[2]=e.bboxMaxs.z;

            // 把 properties + customs + connections 合并成一个 JSON 对象
            nlohmann::json kv = nlohmann::json::object();
            if (e.properties.is_object()) kv.update(e.properties);
            if (e.customs.is_object() && !e.customs.empty()) kv["customs"] = e.customs;
            if (e.connections.is_array() && !e.connections.empty()) kv["connections"] = e.connections;
            std::string s = kv.dump();
            r.kvJsonSize = (uint32_t)s.size();

            buf.insert(buf.end(), (char*)&r, (char*)&r + sizeof(EntityRecord));
            buf.insert(buf.end(), s.data(), s.data() + s.size());
        }
        AppendLumpRaw(outBuffer, dirs, LUMP_ENTITIES, buf);
    }

    // ---- LUMP_NAVGRID ----
    if (navGrid.cols > 0 && navGrid.rows > 0) {
        std::vector<char> buf;
        NavGridHeader hdr;
        hdr.minX = navGrid.minX;
        hdr.minZ = navGrid.minZ;
        hdr.cellSize = navGrid.cellSize;
        hdr.cols = navGrid.cols;
        hdr.rows = navGrid.rows;
        buf.insert(buf.end(), (char*)&hdr, (char*)&hdr + sizeof(NavGridHeader));
        buf.insert(buf.end(), navGrid.walkable.begin(), navGrid.walkable.end());
        // 👈 别忘了这行
        buf.insert(buf.end(), (char*)navGrid.heights.data(), (char*)navGrid.heights.data() + navGrid.heights.size() * 4);        
        AppendLumpRaw(outBuffer, dirs, LUMP_NAVGRID, buf);
    }

    // 👈 新增 LUMP_NODEGRAPH 打包
    if (!nodeRecs.empty()) {
        std::vector<char> buf;
        uint32_t nodeCount = (uint32_t)nodeRecs.size();
        uint32_t linkCount = (uint32_t)nodeLinks.size();
        
        buf.insert(buf.end(), (char*)&nodeCount, (char*)&nodeCount + 4);
        buf.insert(buf.end(), (char*)&linkCount, (char*)&linkCount + 4);
        buf.insert(buf.end(), (char*)nodeRecs.data(), (char*)nodeRecs.data() + nodeRecs.size() * sizeof(NodeRecord));
        buf.insert(buf.end(), (char*)nodeLinks.data(), (char*)nodeLinks.data() + nodeLinks.size() * 4);
        
        AppendLumpRaw(outBuffer, dirs, LUMP_NODEGRAPH, buf);
    }  

    // ---- 回填头部 ----
    uint32_t lumpCount = (uint32_t)dirs.size();
    memcpy(&outBuffer[headerStart + 0], "MBVH", 4);
    uint32_t version = MAP_VERSION;
    memcpy(&outBuffer[headerStart + 4], &version, 4);
    memcpy(&outBuffer[headerStart + 8], &lumpCount, 4);
    memcpy(&outBuffer[headerStart + 12], dirs.data(), lumpCount * sizeof(LumpDir));

    std::ofstream out(outPath, std::ios::binary);
    if (!out.is_open()) { std::cerr << "Cannot write " << outPath << "\n"; return false; }
    out.write(outBuffer.data(), outBuffer.size());
    out.close();

    printf("Compiled %s: Version=%u, Lumps=%u, Entities=%zu\n",
           outPath, version, lumpCount, map.entities.size());
    return true;
}