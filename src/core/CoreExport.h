#pragma once

// 跨平台动态库导出/导入宏
// 用法：
// 1. 在要被导出的类或函数前加 CORE_API
// 2. 在 CMake 里，编译 core 库时定义 CORE_EXPORTS 宏
#ifdef _WIN32
    #ifdef CORE_EXPORTS
        #define CORE_API __declspec(dllexport)
    #else
        #define CORE_API __declspec(dllimport)
    #endif
#else
    #ifdef CORE_EXPORTS
        #define CORE_API __attribute__((visibility("default")))
    #else
        #define CORE_API
    #endif
#endif