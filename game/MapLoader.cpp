#include "MapLoader.h"

bool LoadMap(const char* filename, MapData& outData) {
    std::ifstream f(filename, std::ios::binary);
    if (!f.is_open()) {
        printf("Cannot open %s\n", filename);
        return false;
    }

    char magic[4]; f.read(magic, 4);
    if (strncmp(magic, "MBVH", 4) != 0) {
        printf("Invalid map format\n");
        return false;
    }

    // 读取地图 brush
    f.read((char*)&outData.nv, 4); 
    f.read((char*)&outData.ni, 4);
    outData.verts.resize(outData.nv * 6); 
    f.read((char*)outData.verts.data(), outData.nv * 6 * 4);
    outData.indices.resize(outData.ni); 
    f.read((char*)outData.indices.data(), outData.ni * 4);

    // 出生点
    if (f.read((char*)outData.spawnPos, sizeof(outData.spawnPos))) {
        f.read((char*)outData.spawnAngles, sizeof(outData.spawnAngles));
    }

    // Trigger
    uint32_t numTriggers = 0;
    if (f.read((char*)&numTriggers, 4)) {
        std::vector<float> tb(numTriggers * 6);
        f.read((char*)tb.data(), tb.size() * 4);
        for (uint32_t i = 0; i < numTriggers; i++) {
            outData.triggerMins.push_back(glm::vec3(tb[i*6+0], tb[i*6+1], tb[i*6+2]));
            outData.triggerMaxs.push_back(glm::vec3(tb[i*6+3], tb[i*6+4], tb[i*6+5]));
        }
    }

    // Physbox
    uint32_t numPhysboxes = 0;
    if (f.read((char*)&numPhysboxes, 4)) {
        std::vector<float> pb(numPhysboxes * 6);
        f.read((char*)pb.data(), pb.size() * 4);
        for (uint32_t i = 0; i < numPhysboxes; i++) {
            outData.physboxMins.push_back(glm::vec3(pb[i*6+0], pb[i*6+1], pb[i*6+2]));
            outData.physboxMaxs.push_back(glm::vec3(pb[i*6+3], pb[i*6+4], pb[i*6+5]));
        }
        printf("Loaded %u physbox zones.\n", numPhysboxes);
    }

    // Doors
    uint32_t numDoors = 0;
    if (f.read((char*)&numDoors, 4)) {
        std::vector<float> db(numDoors * 12);
        f.read((char*)db.data(), db.size() * 4);
        for (uint32_t i = 0; i < numDoors; i++) {
            DoorInfo d;
            d.mins     = glm::vec3(db[i*12+0], db[i*12+1], db[i*12+2]);
            d.maxs     = glm::vec3(db[i*12+3], db[i*12+4], db[i*12+5]);
            d.openDir  = glm::vec3(db[i*12+6], db[i*12+7], db[i*12+8]);
            d.openDist = db[i*12+9];
            d.speed    = db[i*12+10];
            d.waitTime = db[i*12+11];
            outData.doors.push_back(d);
        }
        printf("Loaded %u doors.\n", numDoors);
    }

    // Zombie Spawns
    uint32_t numZombies = 0;
    if (f.read((char*)&numZombies, 4)) {
        std::vector<float> zb(numZombies * 3);
        f.read((char*)zb.data(), zb.size() * 4);
        for (uint32_t i = 0; i < numZombies; i++) {
            outData.zombieSpawns.push_back(glm::vec3(zb[i*3+0], zb[i*3+1], zb[i*3+2]));
        }
        printf("Loaded %u zombie spawn points.\n", numZombies);
    }

    // Barrels
    uint32_t numBarrels = 0;
    if (f.read((char*)&numBarrels, 4)) {
        std::vector<float> bb(numBarrels * 3);
        f.read((char*)bb.data(), bb.size() * 4);
        for (uint32_t i = 0; i < numBarrels; i++) {
            outData.barrelSpawns.push_back(glm::vec3(bb[i*3+0], bb[i*3+1], bb[i*3+2]));
        }
        printf("Loaded %u explosive barrels.\n", numBarrels);
    }

    return true;
}