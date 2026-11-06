#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "Export.h"

struct ASSET_API Mesh {
    unsigned int VAO = 0, VBO = 0, EBO = 0;
    unsigned int indexCount = 0;
    unsigned int textureID = 0; // Diffuse 贴图 ID
};

class ASSET_API ModelLoader {
public:
    // 加载单个模型，返回包含所有 Mesh 的数组
    static std::vector<Mesh> LoadModel(const std::string& path);
};