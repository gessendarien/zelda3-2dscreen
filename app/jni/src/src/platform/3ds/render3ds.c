// GPU presentation for the 3DS port (citro2d).
// The PPU still renders in software (ARGB8888). The CPU then does a cheap
// *sequential* ARGB→RGB565 conversion into a linear staging buffer, the GX
// DMA engine tiles it into a VRAM texture, and the GPU draws it to the
// screen (handling the 3DS's rotated framebuffers for free). Compared to
// the old CPU rotation blit into the column-major framebuffer this removes
// all the scattered writes (~2 ms/frame on real hardware). RGB565 is
// lossless here: SNES colour is 15-bit at the source.

#include <citro2d.h>
#include <stdlib.h>
#include <string.h>

#include "../../types.h"
#include "../../util.h"

#define TEX_W 512
#define TEX_H 256

static C3D_RenderTarget *g_target_top, *g_target_bot;
static C3D_Tex  g_tex_top, g_tex_bot;
static uint16  *g_stage_top, *g_stage_bot;  // linear staging buffers (RGB565)
static uint32  *g_draw_buf;                 // PPU output (ARGB8888)
static size_t   g_draw_buf_px;
static int      g_draw_w, g_draw_h;

// v goes 1 → 1-h/TEX_H because GX_DisplayTransfer fills the texture from
// the top row down while the GPU's v axis points up.
static const Tex3DS_SubTexture kSubtexWide = {
  400, 240, 0.0f, 1.0f, 400.0f / TEX_W, 1.0f - 240.0f / TEX_H,
};
static const Tex3DS_SubTexture kSubtexNarrow = {
  256, 240, 0.0f, 1.0f, 256.0f / TEX_W, 1.0f - 240.0f / TEX_H,
};
static const Tex3DS_SubTexture kSubtexBottom = {
  320, 240, 0.0f, 1.0f, 320.0f / TEX_W, 1.0f - 240.0f / TEX_H,
};

// Convert ARGB rows into the staging buffer and tile it into the texture.
static void UploadTex(const uint32 *src, int w, int h, int src_stride,
                      uint16 *stage, C3D_Tex *tex) {
  for (int y = 0; y < h; y++) {
    const uint32 *s = src + (size_t)y * src_stride;
    uint16 *d = stage + (size_t)y * TEX_W;
    for (int x = 0; x < w; x++) {
      uint32 p = s[x];
      d[x] = (uint16)(((p >> 8) & 0xF800) | ((p >> 5) & 0x07E0) |
                      ((p >> 3) & 0x1F));
    }
  }
  GSPGPU_FlushDataCache(stage, TEX_W * TEX_H * sizeof(uint16));
  // C3D_SyncDisplayTransfer coordinates with citro3d's GX queue; a raw
  // GX_DisplayTransfer + gspWaitForPPF races it (the PPF event is shared
  // with frame presentation), which showed up as speckle artifacts from
  // the GPU sampling half-transferred textures on hardware.
  C3D_SyncDisplayTransfer((u32 *)stage, GX_BUFFER_DIM(TEX_W, TEX_H),
                          (u32 *)tex->data, GX_BUFFER_DIM(TEX_W, TEX_H),
                          GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) |
                          GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
                          GX_TRANSFER_OUT_TILED(1));
}

// ── RendererFuncs ───────────────────────────────────────────────────────────

static bool N3DS_Init(SDL_Window *w) {
  (void)w;
  return true;
}

static void N3DS_Destroy(void) {
  free(g_draw_buf);
  g_draw_buf = NULL;
  g_draw_buf_px = 0;
  linearFree(g_stage_top);
  linearFree(g_stage_bot);
  C3D_TexDelete(&g_tex_top);
  C3D_TexDelete(&g_tex_bot);
  C2D_Fini();
  C3D_Fini();
}

static void N3DS_BeginDraw(int width, int height, uint8 **pixels, int *pitch) {
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

static void N3DS_EndDraw(void) {
  int w = g_draw_w > 400 ? 400 : g_draw_w;
  int h = g_draw_h > 240 ? 240 : g_draw_h;
  UploadTex(g_draw_buf, w, h, g_draw_w, g_stage_top, &g_tex_top);
}

void N3DS_Renderer_Create(struct RendererFuncs *funcs) {
  // The startup console switched the bottom screen to RGB565 and single
  // buffering; restore the defaults citro2d presents into.
  gfxSetScreenFormat(GFX_BOTTOM, GSP_BGR8_OES);
  gfxSetDoubleBuffering(GFX_BOTTOM, true);

  C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
  C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
  C2D_Prepare();

  g_target_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
  g_target_bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

  C3D_TexInitVRAM(&g_tex_top, TEX_W, TEX_H, GPU_RGB565);
  C3D_TexInitVRAM(&g_tex_bot, TEX_W, TEX_H, GPU_RGB565);
  C3D_TexSetFilter(&g_tex_top, GPU_NEAREST, GPU_NEAREST);
  C3D_TexSetFilter(&g_tex_bot, GPU_NEAREST, GPU_NEAREST);

  g_stage_top = (uint16 *)linearAlloc(TEX_W * TEX_H * sizeof(uint16));
  g_stage_bot = (uint16 *)linearAlloc(TEX_W * TEX_H * sizeof(uint16));
  memset(g_stage_top, 0, TEX_W * TEX_H * sizeof(uint16));
  memset(g_stage_bot, 0, TEX_W * TEX_H * sizeof(uint16));

  funcs->Initialize = N3DS_Init;
  funcs->Destroy    = N3DS_Destroy;
  funcs->BeginDraw  = N3DS_BeginDraw;
  funcs->EndDraw    = N3DS_EndDraw;
}

// Called by second_screen_3ds.c with its composed 320×240 ARGB buffer.
void N3DS_UploadBottomScreen(const uint32 *argb) {
  UploadTex(argb, 320, 240, 320, g_stage_bot, &g_tex_bot);
}

// Present both screens without waiting for vblank: the GSP swaps buffers
// at vblank on its own (no tearing), so a frame that takes slightly over
// one vblank shows at ~55 fps instead of being quantized down to 30.
// Pacing is handled by the main loop.
void N3DS_Present(int top_w) {
  C3D_FrameBegin(0);

  C2D_TargetClear(g_target_top, C2D_Color32(0, 0, 0, 255));
  C2D_SceneBegin(g_target_top);
  C2D_Image top = { &g_tex_top, top_w > 256 ? &kSubtexWide : &kSubtexNarrow };
  C2D_DrawImageAt(top, (400 - top.subtex->width) / 2.0f, 0.0f, 0.0f,
                  NULL, 1.0f, 1.0f);

  C2D_SceneBegin(g_target_bot);
  C2D_Image bot = { &g_tex_bot, &kSubtexBottom };
  C2D_DrawImageAt(bot, 0.0f, 0.0f, 0.0f, NULL, 1.0f, 1.0f);

  C3D_FrameEnd(0);
}
