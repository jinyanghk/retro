// TextureCache.h
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "Export.h"

class ASSET_API TextureCache {
public:
    static void Init();
    static void Shutdown();
    
    // 传入材质名，返回 OpenGL 纹理 ID
    // 如果已经加载过，直接返回缓存的 ID
    static unsigned int GetTexture(const std::string& materialName);
    static unsigned int GetCubemap(const std::vector<std::string>& facePaths);
    // 👈 新增：按真实文件路径加载（用于 FBX / GLTF 模型贴图）
    static unsigned int GetTextureByPath(const std::string& fullPath);
private:
    static std::unordered_map<std::string, unsigned int> s_cache;
};