#pragma once

#include <deque>
#include <string>

#include "Export.h"

extern CORE_API std::deque<std::string> g_logLines;
CORE_API void LogAdd(const char* s);