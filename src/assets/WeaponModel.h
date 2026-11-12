#pragma once
#include <string>
#include <vector>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include "AssetExport.h"

struct ASSET_API WeaponMesh {
    unsigned int VAO = 0, VBO = 0, EBO = 0;
    unsigned int indexCount = 0;
    unsigned int textureID = 0;
};

struct ASSET_API WeaponModelData {
    std::vector<WeaponMesh> meshes;
};

// 加载 glb/gltf 模型
ASSET_API WeaponModelData LoadWeaponModel(const std::string& path);