// SDL2 compatibility wrapper for 3DS SDL1.
// This file is found first in the include path. It pulls in the real SDL1
// header, then adds the SDL2 declarations our shared code needs.
#pragma once

// SDL2 key types (built on top of SDL1 types in SDL_keycode.h)
#include "SDL_keycode.h"

// SDL2: SDL_GetKeyFromName — SDL1 only has SDL_GetKeyName (not the reverse).
// Returns SDLK_UNKNOWN because keyboard lookup is never used on 3DS.
#ifndef SDL_GetKeyFromName
static inline SDL_Keycode SDL_GetKeyFromName(const char *name) {
  (void)name;
  return SDLK_UNKNOWN;
}
#endif

// SDL2: SDL_GetModState / SDL_INIT_ values compat
#ifndef AUDIO_S16
#define AUDIO_S16 AUDIO_S16SYS
#endif

#include <stdio.h>
typedef FILE SDL_RWops;
#define SDL_RWread(f, ptr, size, n) fread(ptr, size, n, f)
#define SDL_RWwrite(f, ptr, size, n) fwrite(ptr, size, n, f)
#define SDL_RWclose(f) fclose(f)
#define SDL_RWFromFile(file, mode) fopen(file, mode)
