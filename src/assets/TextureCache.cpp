// src/assets/TextureCache.cpp
#include "TextureCache.h"
#include "FileSystem.h"
#include <GL/glew.h>
#define STB_IMAGE_IMPLEMENTATION   // ⚠️ 全项目唯一一处，Render.cpp 必须删掉它
#include "stb_image.h"
#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <glm/glm.hpp>

std::unordered_map<std::string, unsigned int> TextureCache::s_cache;

void TextureCache::Init() {
    s_cache.clear();
}

void TextureCache::Shutdown() {
    for (auto& kv : s_cache) {
        if (kv.second) glDeleteTextures(1, &kv.second);
    }
    s_cache.clear();
}

// 内部：加载失败时生成棋盘格（从 Render.cpp 搬过来）
static unsigned int MakeCheckerTexture(const std::string& name) {
    uint32_t hash = 0;
    for (char c : name) hash = hash * 131 + (uint8_t)c;
    glm::vec3 base(0.55f, 0.50f, 0.42f);

    const int SZ = 64;
    std::vector<uint8_t> pix(SZ * SZ * 4);
    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            bool checker = ((x / 16) + (y / 16)) & 1;
            float k = checker ? 1.0f : 0.82f;
            int idx = (y * SZ + x) * 4;
            pix[idx+0] = (uint8_t)(base.r * k * 255);
            pix[idx+1] = (uint8_t)(base.g * k * 255);
            pix[idx+2] = (uint8_t)(base.b * k * 255);
            pix[idx+3] = 255;
        }
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SZ, SZ, 0, GL_RGBA, GL_UNSIGNED_BYTE, pix.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return tex;
}

unsigned int TextureCache::GetTexture(const std::string& materialName) {
    // 1. 缓存命中
    auto it = s_cache.find(materialName);
    if (it != s_cache.end()) return it->second;

    // 2. 解析路径（FileSystem 内部已有 MISSING 日志）
    std::string path = FileSystem::ResolveMaterial(materialName);

    // 3. 加载 PNG → RGBA8（强制 4 通道，P1 alphatest 要用 alpha）
    int w = 0, h = 0, channels = 0;
    stbi_set_flip_vertically_on_load(true);  // ⚠️ 先开；若画面上下颠倒，改成 false
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);

    GLuint tex = 0;
    if (pixels) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        stbi_image_free(pixels);
    } else {
        std::cerr << "[TextureCache] Failed to load: " << path << std::endl;
        tex = MakeCheckerTexture(materialName);
    }

    s_cache[materialName] = tex;
    return tex;
}

unsigned int TextureCache::GetCubemap(const std::vector<std::string>& facePaths) {
    std::string cacheKey = "cubemap:" + facePaths[0]; // 用第一张图做键
    if (s_cache.find(cacheKey) != s_cache.end()) return s_cache[cacheKey];

    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texID);

    int width, height, nrChannels;
    stbi_set_flip_vertically_on_load(false); // 👈 Cubemap 不能翻转 Y 轴！
    for (unsigned int i = 0; i < facePaths.size(); ++i) {
        std::string fullPath = FileSystem::ResolveMaterial(facePaths[i]);
        unsigned char* data = stbi_load(fullPath.c_str(), &width, &height, &nrChannels, 4);
        if (data) {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
            stbi_image_free(data);
        } else {
            std::cerr << "[TextureCache] Cubemap face failed: " << fullPath << std::endl;
        }
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    s_cache[cacheKey] = texID;
    return texID;
}