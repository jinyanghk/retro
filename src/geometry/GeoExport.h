#pragma once

#ifdef _WIN32
    #ifdef GEOMETRY_EXPORTS
        #define GEO_API __declspec(dllexport)
    #else
        #define GEO_API __declspec(dllimport)
    #endif
#else
    #ifdef GEOMETRY_EXPORTS
        #define GEO_API __attribute__((visibility("default")))
    #else
        #define GEO_API
    #endif
#endif