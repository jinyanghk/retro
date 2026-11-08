// src/assets/ModelLoader.h
#pragma once
#include <string>
#include <vector>
#include <unordered_map>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>

#include "Export.h"

struct ASSET_API Vertex {
    glm::vec3 position;
    glm::vec2 uv;
    glm::ivec4 boneIDs;
    glm::vec4 weights;
    glm::vec3 normal;       // 👈 新增
    glm::vec3 tangent;      // 👈 新增
    glm::vec3 bitangent;    // 👈 新增
};

struct ASSET_API Mesh {
    unsigned int VAO = 0, VBO = 0, EBO = 0;
    unsigned int indexCount = 0;
    unsigned int textureID = 0;
    unsigned int normalMapID = 0;  // 👈 新增：法线贴图
};

struct ASSET_API BoneInfo {
    int id;
    glm::mat4 offset;
};

struct ASSET_API KeyPosition { float time; glm::vec3 value; };
struct ASSET_API KeyRotation { float time; glm::quat value; };
struct ASSET_API KeyScale    { float time; glm::vec3 value; };

struct ASSET_API BoneAnimation {
    std::string name;
    std::vector<KeyPosition> positions;
    std::vector<KeyRotation> rotations;
    std::vector<KeyScale>    scales;
};

struct ASSET_API Animation {
    std::string name;
    float duration;
    std::vector<BoneAnimation> boneAnimations;
};

class ASSET_API ModelLoader {
public:
    static Assimp::Importer s_importer;
    static const aiScene* s_scene; 
    static std::vector<Mesh> LoadModel(const std::string& path, 
                                    std::unordered_map<std::string, BoneInfo>& boneMap, 
                                    int& boneCounter,
                                    std::vector<Animation>& outAnimations);
};