// MapIO.cpp
#include "MapIO.h"
#include "Globals.h"
#include "Panels.h"
#include "Log.h"
#include "FileSystem.h"

#include <map> // for gcc13 + nlohmann_json
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdio>

using json = nlohmann::json;

std::string currentMap = "";

static json Vec3ToJson(const glm::vec3& v)
{
    return json::array({v.x, v.y, v.z});
}

bool ExportMapJson(const std::string& path)
{
    json root;
    
    // ================= 1. 导出 Brushes =================
    json brushes = json::array();
    for (size_t i = 0; i < g_brushes.size(); i++) {
        if (i < g_brushVisible.size() && g_brushVisible[i] == 0) continue; // 隐藏的不导
        
        const auto& b = g_brushes[i];
        json jb;
        jb["id"] = b.m_id; // 👈 保存 ID
        jb["material"] = b.m_szMaterial;
        jb["position"] = Vec3ToJson(b.m_vecPos);
        jb["halfSize"] = Vec3ToJson(b.m_vecSize);
        jb["angles"]   = Vec3ToJson(b.m_angRot);

        json faces = json::array();
        for (int f = 0; f < (int)b.m_faces.size(); f++) {
            const auto& face = b.m_faces[f];
            json jf;
            jf["normal"]   = Vec3ToJson(face.m_vecNormal);
            jf["material"] = face.m_szMaterial;
            
            json face_verts = json::array();
            for (int k = 0; k < 4; k++) {
                int idx = face.m_nVertexIndices[k];
                if (idx >= 0 && idx < (int)b.m_vertices.size()) {
                    face_verts.push_back(Vec3ToJson(b.m_vertices[idx]));
                }
            }
            jf["vertices"] = face_verts;
            jf["uaxis"] = face.m_szUAxis;
            jf["vaxis"] = face.m_szVAxis;
            faces.push_back(jf);
        }
        jb["faces"] = faces;
        brushes.push_back(jb);
    }
    root["brushes"] = brushes;

    // ================= 2. 导出 Entities =================
    json entities = json::array();
    for (size_t i = 0; i < g_entities.size(); i++) {
        if (i < g_entityVisible.size() && g_entityVisible[i] == 0) continue; // 跳过隐藏
        
        const auto& e = g_entities[i];
        json je;
        je["id"] = e.m_id; // 👈 保存 ID
        je["classname"] = e.m_szName;
        je["origin"]    = Vec3ToJson(e.m_vecPos);
        je["angles"]    = Vec3ToJson(e.m_angRot);
        je["type"]      = (int)e.m_iType;
        je["bbox_mins"] = Vec3ToJson(e.m_vecBBoxMins - e.m_vecPos);
        je["bbox_maxs"] = Vec3ToJson(e.m_vecBBoxMaxs - e.m_vecPos);

        json props = json::object();
        for (const auto& kv : e.m_properties) props[kv.first] = kv.second;
        je["properties"] = props;

        json customs = json::object();
        for (const auto& kv : e.m_customProperties) customs[kv.first] = kv.second;
        je["customs"] = customs;

        json connections = json::array();
        for (const auto& conn : e.m_connections) {
            json jo;
            jo["name"]  = conn.connectionName;
            jo["target"]  = conn.targetName;
            jo["input"]   = conn.inputName;
            jo["param"]   = conn.parameter;
            jo["delay"]   = conn.delay;
            connections.push_back(jo);
        }
        je["connections"] = connections;
        entities.push_back(je);
    }
    root["entities"] = entities;

    // ================= 3. 导出 Groups =================
    json groups = json::array();
    for (const auto& grp : g_brushGroups) {
        json jg;
        jg["name"] = grp.name;
        jg["visible"] = grp.visible;

        json brushes_arr = json::array();
        for (uint64_t bid : grp.brushIds) {
            // 因为存的是 ID，只要找到该 ID 对应的物体没被隐藏，就存下来
            bool isVisible = false;
            for (size_t i = 0; i < g_brushes.size(); i++) {
                if (g_brushes[i].m_id == bid) {
                    if (i < g_brushVisible.size() && g_brushVisible[i] != 0) isVisible = true;
                    break;
                }
            }
            if (isVisible) brushes_arr.push_back(bid);
        }
        jg["brushes"] = brushes_arr;

        json entities_arr = json::array();
        for (uint64_t eid : grp.entityIds) {
            bool isVisible = false;
            for (size_t i = 0; i < g_entities.size(); i++) {
                if (g_entities[i].m_id == eid) {
                    if (i < g_entityVisible.size() && g_entityVisible[i] != 0) isVisible = true;
                    break;
                }
            }
            if (isVisible) entities_arr.push_back(eid);
        }
        jg["entities"] = entities_arr;

        if (!brushes_arr.empty() || !entities_arr.empty()) {
            groups.push_back(jg);
        }
    }
    root["groups"] = groups;

    // ================= 4. 写文件 =================
    std::ofstream f(path);
    if (!f.is_open()) {
        char b[256]; snprintf(b, sizeof(b), "[MAP] FAILED to open %s", path.c_str()); LogAdd(b);
        return false;
    }
    f << root;
    f.close();

    char b[256]; snprintf(b, sizeof(b), "[MAP] exported %s (%zu brushes, %zu entities, %zu groups)",
        path.c_str(), brushes.size(), entities.size(), groups.size());
    LogAdd(b);
    
    // 👈 重要：导出成功后，为了保证编辑器内存里和文件完全一致，建议立刻重新加载一下
    // 否则你还要在编辑器里连续删东西，内存里的旧索引还是会和文件有偏差
    ImportMapJson(path); 
    
    return true;
}

bool ImportMapJson(const std::string& path)
{
    std::string content = FileSystem::ReadFile(path);
    if (content.empty()) 
    { 
        LogAdd("[MAP] FAILED to open map"); 
        return false; 
    }
    json root;
    try { 
        root = json::parse(content);  
    } catch (const std::exception& e) {
        char b[256]; snprintf(b, sizeof(b), "[MAP] json parse error: %s", e.what()); LogAdd(b);
        return false;
    }

    g_brushes.clear();
    g_entities.clear();

    auto ParseVec3 = [](const json& j, glm::vec3 def = glm::vec3(0.0f)) {
        if (!j.is_array() || j.size() < 3) return def;
        return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
    };

    if (root.contains("brushes")) {
        for (const auto& jb : root["brushes"]) {
            CBrush b;
            b.m_id = jb.value("id", (uint64_t)0);
            b.m_szMaterial = jb.value("material", "DEV/DEV_MEASUREGENERIC01");
            
            b.m_vecPos  = jb.contains("position") ? ParseVec3(jb["position"]) : glm::vec3(0.0f);
            b.m_vecSize = jb.contains("halfSize") ? ParseVec3(jb["halfSize"]) : glm::vec3(1.0f);
            b.m_angRot  = jb.contains("angles")   ? ParseVec3(jb["angles"])   : glm::vec3(0.0f);
            
            // 1. 优先读 brush 级别的 vertices（如果你编辑器里自己生成的）
            b.m_vertices.clear();
            if (jb.contains("vertices")) {
                for (const auto& v : jb["vertices"]) b.m_vertices.push_back(ParseVec3(v));
            }
            
            b.m_faces.resize(6);
            int fi = 0;
            
            // 2. 读 faces
            if (jb.contains("faces") && jb["faces"].is_array()) {
                for (const auto& jf : jb["faces"]) {
                    if (fi >= 6) break;
                    
                    // 2.1 如果 faces 里有 vertices，优先用它构建 m_vertices
                    if (jf.contains("vertices") && jf["vertices"].is_array()) {
                        for (const auto& v : jf["vertices"]) {
                            b.m_vertices.push_back(ParseVec3(v));
                        }
                    }
                    
                    // 2.2 读取 indices
                    if (jf.contains("indices") && jf["indices"].is_array() && jf["indices"].size() >= 4) {
                        for (int k = 0; k < 4; k++)
                            b.m_faces[fi].m_nVertexIndices[k] = jf["indices"][k].get<int>();
                    } else {
                        // 如果没 indices，用 default 顺序（0,1,2,3），并假定 vertices 是刚刚平铺进去的
                        int base = (fi * 4);
                        // 👈 安全防护：如果顶点不足 4 个，强行补足或者修正索引
                        int currentVerts = (int)b.m_vertices.size();
                        for (int k = 0; k < 4; k++) {
                            int idx = base + k;
                            // 如果算出来的索引越界了，就指回最后一个有效的顶点
                            if (idx >= currentVerts) {
                                idx = (currentVerts > 0) ? currentVerts - 1 : 0;
                            }
                            b.m_faces[fi].m_nVertexIndices[k] = idx;
                        }
                    }
                    
                    b.m_faces[fi].m_vecNormal  = jf.contains("normal") ? ParseVec3(jf["normal"]) : glm::vec3(0.0f, 1.0f, 0.0f);
                    b.m_faces[fi].m_szMaterial = jf.value("material", b.m_szMaterial);

                    b.m_faces[fi].m_szUAxis = jf.value("uaxis", "[1 0 0 0] 1");
                    b.m_faces[fi].m_szVAxis = jf.value("vaxis", "[0 1 0 0] 1");

                    fi++;
                }
            }
            
            // 3. 兜底：如果没有任何 vertices，就自动生成 AABB 的 8 个顶点
            if (b.m_vertices.empty()) {
                glm::vec3 h = b.m_vecSize;
                b.m_vertices = {
                    b.m_vecPos + glm::vec3(-h.x, -h.y, -h.z),
                    b.m_vecPos + glm::vec3( h.x, -h.y, -h.z),
                    b.m_vecPos + glm::vec3( h.x,  h.y, -h.z),
                    b.m_vecPos + glm::vec3(-h.x,  h.y, -h.z),
                    b.m_vecPos + glm::vec3(-h.x, -h.y,  h.z),
                    b.m_vecPos + glm::vec3( h.x, -h.y,  h.z),
                    b.m_vecPos + glm::vec3( h.x,  h.y,  h.z),
                    b.m_vecPos + glm::vec3(-h.x,  h.y,  h.z),
                };
            }
            
            b.RecalcFaceNormals();
            g_brushes.push_back(b);
        }
    }

    if (root.contains("entities")) {
        for (const auto& je : root["entities"]) {
            CEntity e;
            e.m_id = je.value("id", (uint64_t)0);
            e.m_szName = je.value("classname", "info_player_start");
            
            e.m_vecPos = je.contains("origin") ? ParseVec3(je["origin"]) : glm::vec3(0.0f);
            e.m_angRot = je.contains("angles") ? ParseVec3(je["angles"]) : glm::vec3(0.0f);
            e.m_iType  = (EntityType)je.value("type", (int)ENTITY_MODEL);

            if (je.contains("bbox_mins") && je.contains("bbox_maxs")) {
                glm::vec3 rel_mins = ParseVec3(je["bbox_mins"]);
                glm::vec3 rel_maxs = ParseVec3(je["bbox_maxs"]);
                e.m_vecBBoxMins = e.m_vecPos + rel_mins;
                e.m_vecBBoxMaxs = e.m_vecPos + rel_maxs;
            }

            if (je.contains("properties") && je["properties"].is_object()) {
                for (auto it = je["properties"].begin(); it != je["properties"].end(); ++it)
                    e.m_properties[it.key()] = it.value().get<std::string>();
            }

            if (je.contains("customs") && je["customs"].is_object()) {
                for (auto it = je["customs"].begin(); it != je["customs"].end(); ++it)
                    e.m_customProperties[it.key()] = it.value().get<std::string>();
            }

            if (je.contains("connections") && je["connections"].is_array()) {
                for (const auto& jo : je["connections"]) {
                    EntityConnection conn;
                    conn.connectionName = jo.value("name", "");
                    conn.targetName = jo.value("target", "");
                    conn.inputName  = jo.value("input", "");
                    conn.parameter  = jo.value("param", "");
                    conn.delay      = jo.value("delay", 0.0f);
                    e.m_connections.push_back(conn);
                }
            }
            
            if (!je.contains("bbox_mins")) UpdateEntityBBox(e);
            g_entities.push_back(e);
        }
    }

    // ================= 导入 Groups =================
    g_brushGroups.clear(); // 先清空旧的组
    if (root.contains("groups") && root["groups"].is_array()) {
        for (const auto& jg : root["groups"]) {
            BrushGroup grp;
            grp.name = jg.value("name", "New_Group");
            grp.visible = jg.value("visible", 1);

            if (jg.contains("brushes") && jg["brushes"].is_array()) {
                for (const auto& jb : jg["brushes"]) {
                    grp.brushIds.push_back(jb.get<uint64_t>());
                }
            }
            if (jg.contains("entities") && jg["entities"].is_array()) {
                for (const auto& je : jg["entities"]) {
                    grp.entityIds.push_back(je.get<uint64_t>());
                }
            }
            g_brushGroups.push_back(grp);
        }
    }

    // 确保可见性数组和物体数量一致
    g_brushVisible.assign(g_brushes.size(), 1);
    g_entityVisible.assign(g_entities.size(), 1);

    return true;
}