#pragma once
#include "Common.h"

struct AudioAssets {
    Mix_Chunk* sndShoot = nullptr;
    Mix_Chunk* sndHit = nullptr;
    Mix_Chunk* sndDoor = nullptr;
    Mix_Chunk* sndExplosion = nullptr;
};

// 初始化音频系统并加载所有音效
void InitAudio(AudioAssets& audio);

// 释放音频资源
void ShutdownAudio(AudioAssets& audio);