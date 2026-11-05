#include "Audio.h"

void InitAudio(AudioAssets& audio) {
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        printf("SDL_mixer could not initialize! SDL_mixer Error: %s\n", Mix_GetError());
    }
    audio.sndShoot = Mix_LoadWAV("sounds/shoot.wav");
    audio.sndHit = Mix_LoadWAV("sounds/hit.wav");
    audio.sndDoor = Mix_LoadWAV("sounds/door.wav");
    audio.sndExplosion = Mix_LoadWAV("sounds/explosion.wav");
}

void ShutdownAudio(AudioAssets& audio) {
    if (audio.sndShoot) Mix_FreeChunk(audio.sndShoot);
    if (audio.sndHit) Mix_FreeChunk(audio.sndHit);
    if (audio.sndDoor) Mix_FreeChunk(audio.sndDoor);
    if (audio.sndExplosion) Mix_FreeChunk(audio.sndExplosion);
    Mix_CloseAudio();
}