#pragma once

#ifdef _WIN32
    #ifdef ASSET_EXPORTS
        #define ASSET_API __declspec(dllexport)
    #else
        #define ASSET_API __declspec(dllimport)
    #endif
#else
    #ifdef ASSET_EXPORTS
        #define ASSET_API __attribute__((visibility("default")))
    #else
        #define ASSET_API
    #endif
#endif