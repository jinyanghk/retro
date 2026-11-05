#pragma once

#ifdef _WIN32
    #ifdef GAME_EXPORTS
        #define GAME_API __declspec(dllexport)
    #else
        #define GAME_API __declspec(dllimport)
    #endif
#else
    #ifdef GAME_EXPORTS
        #define GAME_API __attribute__((visibility("default")))
    #else
        #define GAME_API
    #endif
#endif