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
    // 1. 统一转小写
    std::string lowerMat = materialName;
    std::transform(lowerMat.begin(), lowerMat.end(), lowerMat.begin(), ::tolower);

    // 2. C++17 手动判断后缀
    auto EndsWith = [](const std::string& str, const std::string& suffix) {
        if (str.size() < suffix.size()) return false;
        return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    };

    if (EndsWith(lowerMat, ".png") || EndsWith(lowerMat, ".tga") || EndsWith(lowerMat, ".dds")) {
        return s_baseDir + "materials/" + lowerMat;
    }

    // 3. 替换反斜杠为正斜杠（防止路径拼错）
    std::replace(lowerMat.begin(), lowerMat.end(), '\\', '/');

    // 4. 默认拼上 materials/ 前缀和 .png 后缀
    return s_baseDir + "materials/" + lowerMat + ".png";
}