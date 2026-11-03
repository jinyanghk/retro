#include "Log.h"

std::deque<std::string> g_logLines;
void LogAdd(const char* s) {
    g_logLines.push_back(s ? s : "");
    if (g_logLines.size() > 1000) g_logLines.pop_front();
}
const std::deque<std::string>& LogLines() { return g_logLines; }