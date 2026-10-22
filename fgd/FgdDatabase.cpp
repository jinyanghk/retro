#include "fgd/FgdDatabase.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <cstdio>

using json = nlohmann::json;

FgdDatabase& FgdDatabase::Instance() { static FgdDatabase i; return i; }

static glm::vec3 ParseVec3(const std::string& s, const glm::vec3& fallback)
{
    // "x y z" 或 "-16 -16 0, 16 16 72" 只取前三个
    std::vector<float> nums;
    std::stringstream ss(s);
    std::string token;
    while (ss >> token) {
        try { nums.push_back(std::stof(token)); } catch (...) { break; }
        if (nums.size() >= 3) break;
    }
    if (nums.size() < 3) return fallback;
    return glm::vec3(nums[0], nums[1], nums[2]);
}

void FgdDatabase::Clear() { m_classes.clear(); }

bool FgdDatabase::LoadFromFile(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) {
        printf("[FgdDatabase] cannot open %s\n", path.c_str());
        return false;
    }
    json j;
    try { f >> j; } catch (const std::exception& e) {
        printf("[FgdDatabase] json parse error: %s\n", e.what());
        return false;
    }
    if (!j.is_object()) { printf("[FgdDatabase] root not object\n"); return false; }

    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string& classname = it.key();
        const json& v = it.value();
        FgdClass c;
        c.kind        = v.value("kind", "PointClass");
        c.description = v.value("description", "");
        c.studio      = v.value("studio", "");
        if (v.contains("color") && v["color"].is_string())
            c.color = ParseVec3(v["color"].get<std::string>(), glm::vec3(0));

        if (v.contains("size") && v["size"].is_string()) {
            std::string sz = v["size"].get<std::string>();
            auto comma = sz.find(',');
            if (comma != std::string::npos) {
                c.sizeMins = ParseVec3(sz.substr(0, comma), glm::vec3(-8));
                c.sizeMaxs = ParseVec3(sz.substr(comma+1), glm::vec3( 8));
                c.hasSize  = true;
            }
        }
        if (v.contains("keyvalues") && v["keyvalues"].is_array()) {
            for (const auto& kvj : v["keyvalues"]) {
                FgdKeyValue kv;
                kv.name         = kvj.value("name", "");
                kv.type         = kvj.value("type", "");
                kv.description  = kvj.value("desc", "");
                kv.defaultValue = kvj.value("default", "");
                kv.longDesc     = kvj.value("long", "");
                if (kvj.contains("choices") && kvj["choices"].is_array()) {
                    for (const auto& ch : kvj["choices"]) {
                        FgdKeyValue::Choice c2;
                        c2.value   = ch.value("value", "");
                        c2.caption = ch.value("caption", c2.value);
                        kv.choices.push_back(c2);
                    }
                }
                c.keyValues.push_back(kv);
            }
        }
        m_classes[classname] = std::move(c);
    }
    printf("[FgdDatabase] loaded %zu classes from %s\n", m_classes.size(), path.c_str());
    return true;
}

bool FgdDatabase::HasClass(const std::string& n) const { return m_classes.count(n) > 0; }

const FgdClass* FgdDatabase::GetClass(const std::string& n) const {
    auto it = m_classes.find(n);
    return it == m_classes.end() ? nullptr : &it->second;
}

std::vector<std::string> FgdDatabase::GetClassList() const {
    std::vector<std::string> out;
    out.reserve(m_classes.size());
    for (auto& kv : m_classes) out.push_back(kv.first);
    return out;
}

bool FgdDatabase::GetKeyValues(const std::string& n, std::vector<FgdKeyValue>& out) const {
    auto* c = GetClass(n);
    if (!c) return false;
    out = c->keyValues;
    return true;
}

bool FgdDatabase::GetBBox(const std::string& n, glm::vec3& mins, glm::vec3& maxs) const {
    auto* c = GetClass(n);
    if (!c || !c->hasSize) return false;
    mins = c->sizeMins;
    maxs = c->sizeMaxs;
    return true;
}

bool FgdDatabase::GetModelPath(const std::string& n, std::string& outPath) const {
    auto* c = GetClass(n);
    if (!c || c->studio.empty()) return false;
    outPath = c->studio;
    return true;
}