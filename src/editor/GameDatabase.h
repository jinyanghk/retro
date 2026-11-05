#ifndef GAME_DATABASE_H
#define GAME_DATABASE_H

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <map>

struct GdbKeyValue {
    std::string name;
    std::string type;
    std::string description;
    std::string defaultValue;
    std::string longDesc;
    struct Choice { std::string value; std::string caption; };
    std::vector<Choice> choices;
};

struct GdbClass {
    std::string kind;       // PointClass / SolidClass / BaseClass / NPCClass / ...
    std::string description;
    std::string studio;     // models/editor/playerstart.mdl
    glm::vec3   color      = glm::vec3(0);
    glm::vec3   sizeMins   = glm::vec3(-8);
    glm::vec3   sizeMaxs   = glm::vec3( 8);
    bool        hasSize    = false;
    std::vector<GdbKeyValue> keyValues;
};

class GameDatabase {
public:
    static GameDatabase& Instance();

    bool LoadFromFile(const std::string& jsonPath);
    void Clear();

    bool HasClass(const std::string& classname) const;
    const GdbClass* GetClass(const std::string& classname) const;
    std::vector<std::string> GetClassList() const;

    bool GetKeyValues(const std::string& classname, std::vector<GdbKeyValue>& out) const;
    bool GetBBox(const std::string& classname, glm::vec3& mins, glm::vec3& maxs) const;
    bool GetModelPath(const std::string& classname, std::string& outPath) const;

private:
    GameDatabase() = default;
    std::map<std::string, GdbClass> m_classes;
};

#endif