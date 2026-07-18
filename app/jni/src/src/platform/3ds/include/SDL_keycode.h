// SDL2 keycode compatibility stub for 3DS (which ships SDL1).
// Our shared codebase was written against SDL2; this header provides the
// SDL2 types on top of SDL1's keysym definitions so config.c compiles.
// FindCmdForSdlKey / ParseKeyFromConfig are never called on 3DS (gamepad only),
// so the values don't need to be exact — they just need to compile.
#pragma once

#include <SDL/SDL_keysym.h>  // SDL1: SDLKey enum, SDLMod

// SDL2 key type aliases
typedef SDLKey   SDL_Keycode;
typedef SDLMod   SDL_Keymod;

// SDL2 introduces this mask for keys that map through scancodes.
// Config.c uses it in REMAP_SDL_KEYCODE(); never called on 3DS.
#ifndef SDLK_SCANCODE_MASK
#define SDLK_SCANCODE_MASK (1 << 30)
#endif

// SDL2 compat constants missing from SDL1 (add only what config.c references)
#ifndef SDLK_KP_ENTER
#define SDLK_KP_ENTER 271
#endif
