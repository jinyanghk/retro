// src/assets/FileSystem.h
#pragma once
#include <string>
#include <vector>
#include "AssetExport.h"


class ASSET_API FileSystem {
public:
    // 初始化，设置基准目录（以后可以扩展成读取 pak 文件）
    static void Init(const std::string& baseDir);

    // 读取整个文本文件（比如 JSON, VMT）
    static std::string ReadFile(const std::string& path);

    // 检查文件是否存在
    static bool Exists(const std::string& path);

    // 解析材质路径（高仿 HL2 的材质系统）
    // 以后你可以在这里把 "BRICK/BRICKWALL001A" 映射到真实贴图路径
    static std::string ResolveMaterial(const std::string& materialName);

private:
    static std::string s_baseDir;
};