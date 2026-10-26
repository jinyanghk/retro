// MapExport.h
#ifndef MAP_EXPORT_H
#define MAP_EXPORT_H
#include <string>
bool ExportMapJson(const std::string& path);
bool ImportMapJson(const std::string& path);
#endif