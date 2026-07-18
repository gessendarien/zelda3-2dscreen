// 3DS main entry point with dual-screen support.
// Top screen (400×240):  game display (SNES PPU output)
// Bottom screen (320×240, touch):  second screen – map, inventory, touch-equip
//
// Build: cd src/platform/3ds && make
// Assets: copy zelda3_assets.dat to sd:/3ds/zelda3/zelda3_assets.dat
//         (not bundled into the executable)
// Data dir: sd:/3ds/zelda3/  (assets, optional zelda3.ini, saves/)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <3ds.h>

#include "snes/ppu.h"
#include "types.h"
#include "variables.h"
#include "zelda_rtl.h"
#include "zelda_cpu_infra.h"
#include "config.h"
#include "assets.h"
#include "load_gfx.h"
#include "util.h"
#include "audio.h"

// ── Forward declarations ────────────────────────────────────────────────────

// render3ds.c
void N3DS_TopScreen_Create(struct RendererFuncs *funcs);

// second_screen_3ds.c
bool SecondScreen3DS_Init(void);
void SecondScreen3DS_Update(void);
bool SecondScreen3DS_HandleTouch(touchPosition *pos, bool touched);
void SecondScreen3DS_Shutdown(void);

// audio_3ds.c
void Audio3DS_Init(void);
void Audio3DS_Update(void);
void Audio3DS_Shutdown(void);

// ── Globals ─────────────────────────────────────────────────────────────────

static int  g_input1_state;
static bool g_paused;
static bool g_turbo;
static int  g_ppu_render_flags;
static int  g_snes_width, g_snes_height;
static struct RendererFuncs g_renderer_funcs;
static uint32 g_gamepad_modifiers;
static uint16 g_gamepad_last_cmd[kGamepadBtn_Count];
static bool g_display_perf;

// ── Required stubs (normally in main.c with SDL mutex) ──────────────────────

void NORETURN Die(const char *error) {
  fprintf(stderr, "Fatal: %s\n", error);
  // Show error on bottom screen via console before exit
  consoleClear();
  printf("\x1b[1;1HFATAL ERROR:\x1b[K");
  printf("\x1b[2;1H%s\x1b[K", error);
  printf("\x1b[4;1HPress START to exit.\x1b[K");
  while (aptMainLoop()) {
    hidScanInput();
    if (hidKeysDown() & KEY_START) break;
    gspWaitForVBlank();
  }
  exit(1);
}

// Audio mutex stubs — audio buffers are filled on the main thread, the DSP
// consumes them via DMA, so no locking is needed.
void ZeldaApuLock(void)   {}
void ZeldaApuUnlock(void) {}

// ── Input mapping ───────────────────────────────────────────────────────────

static int Remap3dsButton(u32 bit) {
  switch (bit) {
    case KEY_A:      return kGamepadBtn_A;
    case KEY_B:      return kGamepadBtn_B;
    case KEY_X:      return kGamepadBtn_X;
    case KEY_Y:      return kGamepadBtn_Y;
    case KEY_L:      return kGamepadBtn_L1;
    case KEY_R:      return kGamepadBtn_R1;
    case KEY_SELECT: return kGamepadBtn_Guide;
    case KEY_START:  return kGamepadBtn_Start;
    case KEY_DUP:    return kGamepadBtn_DpadUp;
    case KEY_DDOWN:  return kGamepadBtn_DpadDown;
    case KEY_DLEFT:  return kGamepadBtn_DpadLeft;
    case KEY_DRIGHT: return kGamepadBtn_DpadRight;
    case KEY_UP:     return kGamepadBtn_DpadUp;
    case KEY_DOWN:   return kGamepadBtn_DpadDown;
    case KEY_LEFT:   return kGamepadBtn_DpadLeft;
    case KEY_RIGHT:  return kGamepadBtn_DpadRight;
    default:         return -1;
  }
}

static void HandleCommand(uint32 j, bool pressed);
static void HandleCommand_Locked(uint32 j, bool pressed);

static void HandleGamepadInput(int button, bool pressed) {
  if (button < 0) return;
  if (!!(g_gamepad_modifiers & (1u << button)) == pressed) return;
  g_gamepad_modifiers ^= (1u << button);
  if (pressed)
    g_gamepad_last_cmd[button] = FindCmdForGamepadButton(button, g_gamepad_modifiers);
  if (g_gamepad_last_cmd[button] != 0)
    HandleCommand(g_gamepad_last_cmd[button], pressed);
}

static void HandleCommand(uint32 j, bool pressed) {
  if (j <= kKeys_Controls_Last) {
    static const uint8 kKbdRemap[] = {0, 4, 5, 6, 7, 2, 3, 8, 0, 9, 1, 10, 11};
    if (pressed) g_input1_state |=  (1 << kKbdRemap[j]);
    else         g_input1_state &= ~(1 << kKbdRemap[j]);
    return;
  }
  if (j == kKeys_Turbo) { g_turbo = pressed; return; }
  HandleCommand_Locked(j, pressed);
}

static void HandleCommand_Locked(uint32 j, bool pressed) {
  if (!pressed) return;
  if (j <= kKeys_Load_Last)      { SaveLoadSlot(kSaveLoad_Load,   j - kKeys_Load);          return; }
  if (j <= kKeys_Save_Last)      { SaveLoadSlot(kSaveLoad_Save,   j - kKeys_Save);          return; }
  if (j <= kKeys_Replay_Last)    { SaveLoadSlot(kSaveLoad_Replay, j - kKeys_Replay);        return; }
  if (j <= kKeys_LoadRef_Last)   { SaveLoadSlot(kSaveLoad_Load,   256 + j - kKeys_LoadRef); return; }
  if (j <= kKeys_ReplayRef_Last) { SaveLoadSlot(kSaveLoad_Replay, 256 + j - kKeys_ReplayRef); return; }
  switch (j) {
    case kKeys_CheatLife:              PatchCommand('w'); break;
    case kKeys_CheatEquipment:         PatchCommand('W'); break;
    case kKeys_CheatKeys:              PatchCommand('o'); break;
    case kKeys_CheatWalkThroughWalls:  PatchCommand('E'); break;
    case kKeys_ClearKeyLog:            PatchCommand('k'); break;
    case kKeys_StopReplay:             PatchCommand('l'); break;
    case kKeys_Reset:                  ZeldaReset(true);   break;
    case kKeys_Pause:                  g_paused = !g_paused; break;
    case kKeys_PauseDimmed:            g_paused = !g_paused; break;
    case kKeys_DisplayPerf:            g_display_perf ^= 1; break;
    default: break;
  }
}

// ── Data directory & asset loading ──────────────────────────────────────────

#define DATA_DIR "sdmc:/3ds/zelda3"

// All game data lives on the SD card: assets, optional zelda3.ini and saves/.
// chdir makes the relative "saves/..." paths in zelda_rtl.c land on the SD.
static void SetupDataDirectory(void) {
  mkdir("sdmc:/3ds", 0777);
  mkdir(DATA_DIR, 0777);
  mkdir(DATA_DIR "/saves", 0777);
  chdir(DATA_DIR);
}

const uint8 *g_asset_ptrs[kNumberOfAssets];
uint32       g_asset_sizes[kNumberOfAssets];

// The assets file is intentionally NOT bundled into the .3dsx: it is read
// from the SD card, next to the saves (or next to the .3dsx as a fallback).
static void LoadAssets(const char *argv0) {
  size_t length = 0;
  uint8 *data = ReadWholeFile("zelda3_assets.dat", &length);  // cwd = DATA_DIR
  if (!data && argv0 && *argv0) {
    // Fallback: same directory as the .3dsx
    char path[512];
    strncpy(path, argv0, sizeof(path) - 32);
    path[sizeof(path) - 32] = '\0';
    char *slash = strrchr(path, '/');
    if (slash) {
      strcpy(slash + 1, "zelda3_assets.dat");
      data = ReadWholeFile(path, &length);
    }
  }
  if (!data) Die("zelda3_assets.dat not found.\n"
                 "Copy it to sd:/3ds/zelda3/");

  static const char kAssetsSig[] = {kAssets_Sig};
  if (length < 16 + 32 + 32 + 8 + kNumberOfAssets * 4 ||
      memcmp(data, kAssetsSig, 48) != 0 ||
      *(uint32 *)(data + 80) != kNumberOfAssets)
    Die("Invalid or corrupt zelda3_assets.dat");

  uint32 offset = 88 + kNumberOfAssets * 4 + *(uint32 *)(data + 84);
  for (size_t i = 0; i < kNumberOfAssets; i++) {
    uint32 size = *(uint32 *)(data + 88 + i * 4);
    offset = (offset + 3) & ~3u;
    if ((uint64)offset + size > length) Die("Assets file truncated");
    g_asset_sizes[i] = size;
    g_asset_ptrs[i]  = data + offset;
    offset += size;
  }
}

static bool ParseLinkGraphics(uint8 *file, size_t length) {
  if (length < 27 || memcmp(file, "ZSPR", 4) != 0) return false;
  uint32 pixel_offs   = DWORD(file[9]);
  uint32 pixel_length = WORD(file[13]);
  uint32 palette_offs = DWORD(file[15]);
  uint32 palette_length = WORD(file[19]);
  if ((uint64)pixel_offs + pixel_length > length ||
      (uint64)palette_offs + palette_length > length ||
      pixel_length != 0x7000) return false;
  memcpy(kLinkGraphics, file + pixel_offs, 0x7000);
  if (palette_length >= 120) memcpy(kPalette_ArmorAndGloves, file + palette_offs, 120);
  if (palette_length >= 124) memcpy(kGlovesColor, file + palette_offs + 120, 4);
  return true;
}

MemBlk FindInAssetArray(int asset, int idx) {
  return FindIndexInMemblk((MemBlk){ g_asset_ptrs[asset], g_asset_sizes[asset] }, idx);
}

static void LoadLinkGraphics(void) {
  if (!g_config.link_graphics) return;
  size_t length = 0;
  uint8 *file = ReadWholeFile(g_config.link_graphics, &length);
  if (!file || !ParseLinkGraphics(file, length)) Die("Unable to load link graphics");
  free(file);
}

// ── 3DS-specific config ─────────────────────────────────────────────────────

// Widescreen3DS key in zelda3.ini: auto (default) = widescreen on New 3DS
// only, on = always 400×240, off = always 256×240. Parsed here instead of
// config.c to keep the key platform-local (unknown keys only warn there).
static int ParseWidescreen3DSKey(void) {
  FILE *f = fopen("zelda3.ini", "rb");
  if (!f) return -1;
  char line[256];
  int result = -1;
  while (fgets(line, sizeof(line), f)) {
    char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (strncasecmp(p, "Widescreen3DS", 13) != 0) continue;
    p = strchr(p, '=');
    if (!p) continue;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (strncasecmp(p, "on", 2) == 0 || *p == '1') result = 1;
    else if (strncasecmp(p, "off", 3) == 0 || *p == '0') result = 0;
    break;
  }
  fclose(f);
  return result;
}

// ── Render ───────────────────────────────────────────────────────────────────

static void DrawFrame(void) {
  int scale = PpuGetCurrentRenderScale(g_zenv.ppu, g_ppu_render_flags);
  uint8 *pixels = NULL;
  int    pitch  = 0;
  g_renderer_funcs.BeginDraw(g_snes_width * scale, g_snes_height * scale,
                             &pixels, &pitch);
  ZeldaDrawPpuFrame(pixels, pitch, g_ppu_render_flags);
  g_renderer_funcs.EndDraw();
}

// ── Entry point ──────────────────────────────────────────────────────────────

int main(int argc, char **argv) {
  // RGBA8_OES: 32-bit pixels in ABGR byte order (what the 3DS hardware expects).
  gfxInit(GSP_RGBA8_OES, GSP_RGBA8_OES, false);
  gfxSetDoubleBuffering(GFX_TOP,    true);
  gfxSetDoubleBuffering(GFX_BOTTOM, true);

  // New 3DS: unlock the 804 MHz clock + L2 cache.
  bool is_new3ds = false;
  APT_CheckNew3DS(&is_new3ds);
  if (is_new3ds)
    osSetSpeedupEnable(true);

  // Use the bottom screen as a console during startup only.
  PrintConsole console;
  consoleInit(GFX_BOTTOM, &console);
  consoleClear();
  printf("\x1b[1;1HZelda3 3DS starting...\x1b[K");

  // Load config and assets from sd:/3ds/zelda3/
  SetupDataDirectory();
  ParseConfigFile(NULL);
  printf("\x1b[2;1HLoading assets...\x1b[K");
  LoadAssets(argc > 0 ? argv[0] : NULL);
  LoadLinkGraphics();

  printf("\x1b[3;1HInitialising engine...\x1b[K");
  ZeldaInitialize();

  // Adaptive resolution: the widescreen PPU render (400 px wide) costs ~36%
  // more than the native 256 px one, so it defaults to New 3DS only.
  // extraLeftRight = (400-256)/2 = 72; extend_y renders 240 rows (no bars).
  int wide_override = ParseWidescreen3DSKey();
  bool use_wide = (wide_override < 0) ? is_new3ds : (wide_override != 0);
  int extra_lr = use_wide ? 72 : 0;

  g_config.extended_aspect_ratio = extra_lr;
  g_config.extend_y = true;
  if (use_wide)
    g_config.features0 |= kFeatures0_ExtendScreen64 | kFeatures0_WidescreenVisualFixes;
  g_zenv.ppu->extraLeftRight = extra_lr;
  g_snes_width  = 256 + extra_lr * 2;
  g_snes_height = 240;

  g_wanted_zelda_features = g_config.features0;
  g_config.new_renderer   = 1;

  g_ppu_render_flags = kPpuRenderFlags_NewRenderer |
                       g_config.enhanced_mode7   * kPpuRenderFlags_4x4Mode7     |
                       kPpuRenderFlags_Height240  |
                       g_config.no_sprite_limits * kPpuRenderFlags_NoSpriteLimits;

  ZeldaEnableMsu(g_config.enable_msu);
  ZeldaReadSram();

  // Start audio (soft-fails if DSP firmware is unavailable)
  Audio3DS_Init();

  // Set up top screen renderer
  N3DS_TopScreen_Create(&g_renderer_funcs);

  // Set up bottom screen second screen (takes over the console framebuffer).
  // consoleInit switched the bottom screen to RGB565; restore RGBA8 so the
  // 32-bit writes in second_screen_3ds.c come out right. Single-buffered:
  // the panel only redraws at 30 Hz, so alternating buffers would flicker.
  printf("\x1b[4;1HInitialising second screen...\x1b[K");
  gfxSetScreenFormat(GFX_BOTTOM, GSP_RGBA8_OES);
  gfxSetDoubleBuffering(GFX_BOTTOM, false);
  SecondScreen3DS_Init();

  while (aptMainLoop()) {
    // ── Input ──
    hidScanInput();

    u32 kDown = hidKeysDown();
    u32 kUp   = hidKeysUp();

    // Map each set bitmask bit to a gamepad command
    for (u32 bit = 1; bit; bit <<= 1) {
      if (kDown & bit) HandleGamepadInput(Remap3dsButton(bit), true);
      if (kUp   & bit) HandleGamepadInput(Remap3dsButton(bit), false);
    }

    // Bottom screen touch → second screen
    if (hidKeysHeld() & KEY_TOUCH) {
      touchPosition pos;
      hidTouchRead(&pos);
      SecondScreen3DS_HandleTouch(&pos, true);
    }

    // ── Audio ──
    Audio3DS_Update();

    // ── Game logic ──
    if (!g_paused) {
      int inputs = g_input1_state;
      ZeldaRunFrame(inputs);
    }

    // ── Rendering ──
    DrawFrame();
    // The map/inventory panel is near-static: 30 Hz updates halve its cost.
    static uint32 frame_ctr;
    if (frame_ctr++ & 1)
      SecondScreen3DS_Update();

    // ── Frame timing: target 60 fps ──
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();

    // Turbo: run an extra logic frame without rendering
    if (g_turbo && !g_paused) {
      ZeldaRunFrame(g_input1_state);
    }
  }

  SecondScreen3DS_Shutdown();
  Audio3DS_Shutdown();
  g_renderer_funcs.Destroy();
  gfxExit();
  return 0;
}
