// src/assets/FileSystem.cpp
#include "FileSystem.h"
#include <fstream>
#include <iostream>
#include <algorithm>

std::string FileSystem::s_baseDir = "";

void FileSystem::Init(const std::string& baseDir) {
    s_baseDir = baseDir;
    if (!s_baseDir.empty() && s_baseDir.back() != '/' && s_baseDir.back() != '\\') {
        s_baseDir += "/";
    }
    std::cout << "[FileSystem] Initialized with base dir: " << s_baseDir << std::endl;
}

std::string FileSystem::ReadFile(const std::string& path) {
    std::ifstream f(s_baseDir + path, std::ios::binary);
    if (!f.is_open()) return "";
    
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
                         
    // 👈 新增：如果是在 Windows 下读的文本，去掉 \r 防止后续字符串比对出错
    content.erase(std::remove(content.begin(), content.end(), '\r'), content.end());
    
    return content;
}

bool FileSystem::Exists(const std::string& path) {
    std::ifstream f(s_baseDir + path);
    return f.good();
}

std::string FileSystem::ResolveMaterial(const std::string& materialName) {
    std::string name = materialName;
    
    // 统一转小写，替换反斜杠
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c){ return std::tolower(c); });
    std::replace(name.begin(), name.end(), '\\', '/');

    // 提取文件名（DEV/xxx -> xxx）
    size_t slashPos = name.find_last_of('/');
    if (slashPos != std::string::npos) {
        name = name.substr(slashPos + 1);
    }

    // 👈 核心修复：加上 "assets/" 前缀
    std::string relativePath = "assets/materials/dev/" + name + ".png";
    
    return s_baseDir + relativePath;
}