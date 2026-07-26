// SDL2 keycode compatibility stub for 3DS.
// Our shared codebase was written against SDL2; this header provides the
// SDL2 types and keysym definitions so config.c compiles.
// FindCmdForSdlKey / ParseKeyFromConfig are never called on 3DS (gamepad only),
// so the values don't need to be exact — they just need to compile.
#pragma once

// SDL2 key type aliases
typedef int SDL_Keycode;
typedef int SDL_Keymod;

// SDL2 introduces this mask for keys that map through scancodes.
// Config.c uses it in REMAP_SDL_KEYCODE(); never called on 3DS.
#define SDLK_SCANCODE_MASK (1 << 30)

// SDL2 compat constants missing from SDL1 (add only what config.c references)
#define SDLK_KP_ENTER 271

// Dummy constants for compilation
#define SDLK_UP 1
#define SDLK_DOWN 2
#define SDLK_LEFT 3
#define SDLK_RIGHT 4
#define SDLK_LSHIFT 5
#define SDLK_RSHIFT 6
#define SDLK_LCTRL 7
#define SDLK_RCTRL 8
#define SDLK_LALT 9
#define SDLK_RALT 10
#define SDLK_RETURN 11
#define SDLK_TAB 13
#define SDLK_UNKNOWN 14

#define SDLK_a 'a'
#define SDLK_c 'c'
#define SDLK_e 'e'
#define SDLK_f 'f'
#define SDLK_k 'k'
#define SDLK_l 'l'
#define SDLK_o 'o'
#define SDLK_p 'p'
#define SDLK_r 'r'
#define SDLK_s 's'
#define SDLK_t 't'
#define SDLK_v 'v'
#define SDLK_w 'w'
#define SDLK_x 'x'
#define SDLK_z 'z'

#define SDLK_F1 201
#define SDLK_F2 202
#define SDLK_F3 203
#define SDLK_F4 204
#define SDLK_F5 205
#define SDLK_F6 206
#define SDLK_F7 207
#define SDLK_F8 208
#define SDLK_F9 209
#define SDLK_F10 210

#define KMOD_SHIFT 1
#define KMOD_CTRL 2
#define KMOD_ALT 4
