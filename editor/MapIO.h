// MapIO.h
#ifndef MAP_IO_H
#define MAP_IO_H
#include <string>
bool ExportMapJson(const std::string& path);
bool ImportMapJson(const std::string& path);
#endif