// src/editor/MapIO.h
#pragma once

#include <string>
bool ExportMapJson(const std::string& path);
bool ImportMapJson(const std::string& path);

extern std::string currentMap;
