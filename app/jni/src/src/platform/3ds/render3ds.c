// Top screen renderer for the 3DS port.
// The 3DS top screen is 400x240 pixels, but the framebuffer is stored in
// column-major order (240 rows per column, 400 columns). Drawing to it
// requires a 90° rotation. We maintain an intermediate ARGB8888 buffer that
// the PPU renders into, then blit+rotate it to the framebuffer each frame.
#include <3ds.h>
#include <stdlib.h>
#include <string.h>

#include "../../types.h"
#include "../../util.h"

// Framebuffer physical dimensions (column-major: height=240, width=400)
#define FB_H 240
#define FB_W 400

static uint32 *g_draw_buf = NULL;
static size_t  g_draw_buf_px = 0;
static int     g_draw_w = 0, g_draw_h = 0;

static bool N3DS_Top_Init(SDL_Window *w) {
  (void)w;
  return true;
}

static void N3DS_Top_Destroy(void) {
  free(g_draw_buf);
  g_draw_buf = NULL;
  g_draw_buf_px = 0;
}

static void N3DS_Top_BeginDraw(int width, int height, uint8 **pixels, int *pitch) {
  size_t need = (size_t)width * height;
  if (need > g_draw_buf_px) {
    free(g_draw_buf);
    g_draw_buf = (uint32 *)malloc(need * 4);
    g_draw_buf_px = need;
  }
  g_draw_w = width;
  g_draw_h = height;
  *pixels = (uint8 *)g_draw_buf;
  *pitch  = width * 4;
}

static void N3DS_Top_EndDraw(void) {
  // Center the game image in the 400x240 top screen.
  // x_off: horizontal centering (columns), y_off: vertical centering (rows).
  int x_off = (FB_W - g_draw_w) / 2;
  int y_off = (FB_H - g_draw_h) / 2;
  if (x_off < 0) x_off = 0;
  if (y_off < 0) y_off = 0;

  uint32 *fb  = (uint32 *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
  uint32 *src = g_draw_buf;

  // x-outer / y-inner: for a fixed column x the framebuffer entries are
  // contiguous (column-major), so the writes stay sequential in memory.
  for (int x = 0; x < g_draw_w; x++) {
    uint32 *col = fb + (x + x_off) * FB_H + (FB_H - 1 - y_off);
    const uint32 *s = src + x;
    for (int y = 0; y < g_draw_h; y++, s += g_draw_w) {
      // Convert ARGB8888 (game) → RGBA8888 (3DS GSP_RGBA8_OES)
      uint32 p = *s;
      col[-y] = (p << 8) | (p >> 24);
    }
  }
}

void N3DS_TopScreen_Create(struct RendererFuncs *funcs) {
  funcs->Initialize = N3DS_Top_Init;
  funcs->Destroy    = N3DS_Top_Destroy;
  funcs->BeginDraw  = N3DS_Top_BeginDraw;
  funcs->EndDraw    = N3DS_Top_EndDraw;
}
