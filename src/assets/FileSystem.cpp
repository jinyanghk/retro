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
    // 这里可以先写死一个逻辑，或者直接返回原始名字
    // 比如 HL2 里 "DEV/DEV_MEASUREGENERIC01" 对应 "materials/dev/dev_measuregeneric01.vmt"
    // 我们目前先按你的引擎需求，简单返回材质名
    return materialName;
}