// MapExport.cpp
#include "MapExport.h"
#include "SceneGlobals.h"
#include "EditorPanels.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdio>

using json = nlohmann::json;

static json Vec3ToJson(const glm::vec3& v)
{
    return json::array({v.x, v.y, v.z});
}

bool ExportMapJson(const std::string& path)
{
    json root;
    root["format"] = "retro_map_v1";

    json brushes = json::array();
    for (const auto& b : g_brushes) {
        json jb;
        jb["material"] = b.m_szMaterial;
        jb["position"] = Vec3ToJson(b.m_vecPos);
        jb["halfSize"] = Vec3ToJson(b.m_vecSize);
        jb["angles"]   = Vec3ToJson(b.m_angRot);

        json verts = json::array();
        for (const auto& v : b.m_vertices) verts.push_back(Vec3ToJson(v));
        jb["vertices"] = verts;

        json faces = json::array();
        for (int f = 0; f < (int)b.m_faces.size(); f++) {
            const auto& face = b.m_faces[f];
            json jf;
            jf["indices"]  = json::array({face.m_nVertexIndices[0], face.m_nVertexIndices[1],
                                          face.m_nVertexIndices[2], face.m_nVertexIndices[3]});
            jf["normal"]   = Vec3ToJson(face.m_vecNormal);
            jf["material"] = face.m_szMaterial;
            faces.push_back(jf);
        }
        jb["faces"] = faces;
        brushes.push_back(jb);
    }
    root["brushes"] = brushes;

    json entities = json::array();
    for (const auto& e : g_entities) {
        json je;
        je["classname"] = e.m_szName;
        je["origin"]    = Vec3ToJson(e.m_vecPos);
        je["angles"]    = Vec3ToJson(e.m_angRot);
        je["type"]      = (int)e.m_iType;
        je["bbox_mins"]  = Vec3ToJson(e.m_vecBBoxMins);   // ★ 新增
        je["bbox_maxs"]  = Vec3ToJson(e.m_vecBBoxMaxs);   // ★ 新增

        json props = json::object();
        for (const auto& kv : e.m_properties) props[kv.first] = kv.second;
        je["properties"] = props;

        // 🔥 新增：导出 I/O 连接
        json outs = json::array();
        for (const auto& out : e.m_outputs) {
            json jo;
            jo["output"]  = out.outputName;
            jo["target"]  = out.targetName;
            jo["input"]   = out.inputName;
            jo["param"]   = out.parameter;
            jo["delay"]   = out.delay;
            outs.push_back(jo);
        }
        je["outputs"] = outs;

        entities.push_back(je);
    }
    root["entities"] = entities;

    std::ofstream f(path);
    if (!f.is_open()) {
        char b[256]; snprintf(b, sizeof(b), "[MAP] FAILED to open %s", path.c_str()); LogAdd(b);
        return false;
    }
    f << root;//.dump(2);
    f.close();

    char b[256]; snprintf(b, sizeof(b), "[MAP] exported %s (%zu brushes, %zu entities)",
        path.c_str(), g_brushes.size(), g_entities.size());
    LogAdd(b);
    return true;
}

bool ImportMapJson(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) { LogAdd("[MAP] FAILED to open map.json"); return false; }
    json root;
    try { f >> root; } catch (const std::exception& e) {
        char b[256]; snprintf(b, sizeof(b), "[MAP] json parse error: %s", e.what()); LogAdd(b);
        return false;
    }

    g_brushes.clear();
    g_entities.clear();

    auto ParseVec3 = [](const json& j) {
        return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
    };

    if (root.contains("brushes")) {
        for (const auto& jb : root["brushes"]) {
            CBrush b;
            b.m_szMaterial = jb.value("material", "DEV/DEV_MEASUREGENERIC01");
            b.m_vecPos     = ParseVec3(jb["position"]);
            b.m_vecSize    = ParseVec3(jb["halfSize"]);
            b.m_angRot     = ParseVec3(jb["angles"]);
            b.m_vertices.clear();
            for (const auto& v : jb["vertices"]) b.m_vertices.push_back(ParseVec3(v));
            b.m_faces.resize(6);
            int fi = 0;
            for (const auto& jf : jb["faces"]) {
                if (fi >= 6) break;
                for (int k = 0; k < 4; k++)
                    b.m_faces[fi].m_nVertexIndices[k] = jf["indices"][k].get<int>();
                b.m_faces[fi].m_vecNormal  = ParseVec3(jf["normal"]);
                b.m_faces[fi].m_szMaterial = jf.value("material", "");
                fi++;
            }
            if (b.m_vertices.size() == 8) b.RecalcFaceNormals();
            else                          b.RebuildPrimitiveMesh();
            g_brushes.push_back(b);
        }
    }

    if (root.contains("entities")) {
        for (const auto& je : root["entities"]) {
            CEntity e;
            e.m_szName = je.value("classname", "info_player_start");
            e.m_vecPos = ParseVec3(je["origin"]);
            e.m_angRot = ParseVec3(je["angles"]);
            e.m_iType  = (EntityType)je.value("type", (int)ENTITY_MODEL);

            if (je.contains("bbox_mins") && je.contains("bbox_maxs")) {
                e.m_vecBBoxMins = ParseVec3(je["bbox_mins"]);
                e.m_vecBBoxMaxs = ParseVec3(je["bbox_maxs"]);
            }

            if (je.contains("properties") && je["properties"].is_object()) {
                for (auto it = je["properties"].begin(); it != je["properties"].end(); ++it)
                    e.m_properties[it.key()] = it.value().get<std::string>();
            }

            // 🔥 新增：读取 I/O 连接
            if (je.contains("outputs") && je["outputs"].is_array()) {
                for (const auto& jo : je["outputs"]) {
                    EntityOutput out;
                    out.outputName = jo.value("output", "");
                    out.targetName = jo.value("target", "");
                    out.inputName  = jo.value("input", "");
                    out.parameter  = jo.value("param", "");
                    out.delay      = jo.value("delay", 0.0f);
                    e.m_outputs.push_back(out);
                }
            }
                        
            // 如果 bbox 没从 json 里读到，才走 UpdateEntityBBox
            if (!je.contains("bbox_mins")) UpdateEntityBBox(e);
            g_entities.push_back(e);
        }
    }
    return true;
}