// MapIO.cpp
#include "MapIO.h"
#include "SceneGlobals.h"
#include "EditorPanels.h"
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
    //root["format"] = "retro_map_v1";

    json brushes = json::array();
    for (const auto& b : g_brushes) {
        json jb;
        jb["material"] = b.m_szMaterial;
        jb["position"] = Vec3ToJson(b.m_vecPos);
        jb["halfSize"] = Vec3ToJson(b.m_vecSize);
        jb["angles"]   = Vec3ToJson(b.m_angRot);

        // 注意：不要写 jb["vertices"]，bvh.exe 不读它

        json faces = json::array();
        for (int f = 0; f < (int)b.m_faces.size(); f++) {
            const auto& face = b.m_faces[f];
            json jf;
            
            // 1. 正常导出 normal 和 material
            jf["normal"]   = Vec3ToJson(face.m_vecNormal);
            jf["material"] = face.m_szMaterial;
            
            // 2. 根据 indices 提取出实际的 3D 顶点坐标（bvh.exe 需要这个）
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

    // 实体部分保持不变
    json entities = json::array();
    for (const auto& e : g_entities) {
        json je;
        je["classname"] = e.m_szName;
        je["origin"]    = Vec3ToJson(e.m_vecPos);
        je["angles"]    = Vec3ToJson(e.m_angRot);
        je["type"]      = (int)e.m_iType;
        // 👈 存世界坐标，但写进 JSON 时转回相对坐标
        je["bbox_mins"]  = Vec3ToJson(e.m_vecBBoxMins - e.m_vecPos);
        je["bbox_maxs"]  = Vec3ToJson(e.m_vecBBoxMaxs - e.m_vecPos);

        json props = json::object();
        for (const auto& kv : e.m_properties) props[kv.first] = kv.second;
        je["properties"] = props;

        json customs = json::object();
        for (const auto& kv : e.m_customProperties) {
            customs[kv.first] = kv.second;
        }
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

    std::ofstream f(path);
    if (!f.is_open()) {
        char b[256]; snprintf(b, sizeof(b), "[MAP] FAILED to open %s", path.c_str()); LogAdd(b);
        return false;
    }
    f << root;
    f.close();

    char b[256]; snprintf(b, sizeof(b), "[MAP] exported %s (%zu brushes, %zu entities)",
        path.c_str(), g_brushes.size(), g_entities.size());
    LogAdd(b);
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
    return true;
}