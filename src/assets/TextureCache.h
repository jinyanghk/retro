// TextureCache.h
#pragma once
#include <string>
#include <unordered_map>
#include "Export.h"

class ASSET_API TextureCache {
public:
    static void Init();
    static void Shutdown();
    
    // 传入材质名，返回 OpenGL 纹理 ID
    // 如果已经加载过，直接返回缓存的 ID
    static unsigned int GetTexture(const std::string& materialName);

private:
    static std::unordered_map<std::string, unsigned int> s_cache;
};