// src/assets/Audio.cpp
#include "Audio.h"
#include <stdio.h>

AudioAssets* g_pGlobalAudio = nullptr;

void InitAudio(AudioAssets& audio) {
    g_pGlobalAudio = &audio; // 👈 绑定全局指针
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        printf("SDL_mixer could not initialize! SDL_mixer Error: %s\n", Mix_GetError());
    }
    audio.sndShoot = Mix_LoadWAV("sound/weapons/pistol/pistol_fire2.wav");
    audio.sndHit = Mix_LoadWAV("sound/npc/zombie/zombie_hit.wav");
    audio.sndDoor = Mix_LoadWAV("sound/doors/door1_move.wav");
    audio.sndExplosion = Mix_LoadWAV("sound/weapons/explode3.wav");
}

void ShutdownAudio(AudioAssets& audio) {
    if (audio.sndShoot) Mix_FreeChunk(audio.sndShoot);
    if (audio.sndHit) Mix_FreeChunk(audio.sndHit);
    if (audio.sndDoor) Mix_FreeChunk(audio.sndDoor);
    if (audio.sndExplosion) Mix_FreeChunk(audio.sndExplosion);
    Mix_CloseAudio();
}