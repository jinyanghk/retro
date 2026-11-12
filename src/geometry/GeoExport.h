#pragma once

// 跨平台动态库导出/导入宏
// 用法：
// 1. 在要被导出的类或函数前加 GEO_API
// 2. 在 CMake 里，编译 core 库时定义 GEO_EXPORTS 宏
#ifdef _WIN32
    #ifdef GEO_EXPORTS
        #define GEO_API __declspec(dllexport)
    #else
        #define GEO_API __declspec(dllimport)
    #endif
#else
    #ifdef GEO_EXPORTS
        #define GEO_API __attribute__((visibility("default")))
    #else
        #define GEO_API
    #endif
#endif