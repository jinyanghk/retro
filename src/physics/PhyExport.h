#pragma once

#ifdef _WIN32
    #ifdef PHYSICS_EXPORTS
        #define PHYSICS_API __declspec(dllexport)
    #else
        #define PHYSICS_API __declspec(dllimport)
    #endif
#else
    #ifdef PHYSICS_EXPORTS
        #define PHYSICS_API __attribute__((visibility("default")))
    #else
        #define PHYSICS_API
    #endif
#endif