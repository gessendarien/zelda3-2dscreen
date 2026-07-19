// Bottom screen second screen for the 3DS port.
// The 3DS bottom screen is 320x240 (touch-enabled). We render the minimap
// and item inventory here, mirroring the Android MinimapView functionality.
//
// Layout (320x240):
//   Left  192px: world map viewport (192x240) when outdoors, or the dungeon
//                floor (80x80→160x160) + floor strip (B1 1F 2F...) indoors
//   Right 128px: 4×5 grid of item icons (20 usable items)
//                + 40px equipped-item display at bottom
//
// Touch: right panel touch → equip item at that slot (SS_EquipSlot)

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "../../types.h"
#include "../../second_screen_tables.h"
#include "../linux/ss_sheets.h"

// API from second_screen.c
int  SS_GetLinkX(void);
int  SS_GetLinkY(void);
int  SS_GetModule(void);
int  SS_GetArea(void);
bool SS_IsIndoors(void);
void SS_ReadSram(uint8 *out, int n);
int  SS_GetEquippedSlot(void);
int  SS_GetDungeon(void);
int  SS_GetDungeonLayout(int palace, uint8 *out, int cap);
void SS_ReadDungFlags(uint8 *out, int n);
bool SS_RenderIconSheet(uint32 *px);
bool SS_RenderWorldMap(uint32 *px, bool dark);
bool SS_RenderDungeonFloor(int palace, int floorIdx, uint32 *px);
void SS_EquipSlot(int slot);

// Bottom screen physical dimensions (column-major, same as top)
#define BOT_W 320
#define BOT_H 240

// Layout constants
#define MAP_W  192          // map panel width
#define MAP_SZ 192          // map/dungeon viewport size
#define STATUS_H 48         // status bar height below map
#define ITEM_COLS 4         // item grid columns
#define ITEM_ROWS 5         // item grid rows
#define ITEM_CELL_W (128 / ITEM_COLS)   // 32px
#define ITEM_CELL_H ((BOT_H - 40) / ITEM_ROWS)  // 40px
#define EQUIP_H 40          // equipped-item strip height

// Colour palette
#define COL(r,g,b) (0xff000000u | ((uint32)(r) << 16) | ((uint32)(g) << 8) | (b))
enum {
  C_BG_MAP   = COL(20, 20, 30),
  C_BG_ITEMS = COL(15, 15, 20),
  C_DIVIDER  = COL(60, 60, 80),
  C_CELL_BG  = COL(30, 30, 45),
  C_CELL_SEL = COL(50, 100, 180),
  C_EMPTY    = COL(20, 20, 30),
  C_EQUIP_HL = COL(60,  120, 200),
  C_BORDER   = COL(80,  80,  100),
};

// Persistent render state
static uint32 g_screen[BOT_W * BOT_H];      // 320×240 ARGB draw buffer
static uint32 g_world_map[512 * 512];        // 512×512 world map (light+dark cached)
static uint32 g_dung_map[80 * 80];          // 80×80 dungeon floor
static uint32 g_icon_sheet[160 * 128];      // icon sprite sheet (10×8 × 16px)

static bool g_map_dirty = true;
static bool g_icons_dirty = true;
static bool g_dark_world_cached = false;
static uint32 g_frame;                       // update counter (30 Hz) for blinking
static int  g_last_out_x = 2048, g_last_out_y = 2048;  // Link's last outdoor spot
static bool g_in_cinema;                     // title/cutscene: triforce screen

// Floor-plaque touch state: tapping a plaque previews that floor for ~6 s.
static int    g_view_floor;            // signed floor being previewed
static int    g_view_palace = -1;
static uint32 g_view_touch_frame;      // g_frame of last tap (0 = inactive)
// Plaque geometry from the last draw, for the touch handler. g_strip_fl
// lists the signed floor of each drawn plaque (only revealed floors).
static bool g_strip_active;
static int  g_strip_x0, g_strip_pw, g_strip_y0, g_strip_ph;
static int  g_strip_count, g_strip_palace;
static int8_t g_strip_fl[16];

// SRAM snapshot (g_ram 0xF300..0xF400: items, hearts, map/compass bits...)
static uint8 g_sram[256];

static int sram16(int off) { return g_sram[off] | (g_sram[off + 1] << 8); }

// ── Software draw helpers ───────────────────────────────────────────────────

static inline void set_px(int x, int y, uint32 c) {
  if ((unsigned)x < BOT_W && (unsigned)y < BOT_H)
    g_screen[y * BOT_W + x] = c;
}

static void fill_rect(int x, int y, int w, int h, uint32 c) {
  for (int dy = 0; dy < h; dy++)
    for (int dx = 0; dx < w; dx++)
      set_px(x + dx, y + dy, c);
}

// Blit a region from a source buffer (srcW-wide) to the screen.
static void blit(const uint32 *src, int src_w,
                 int sx, int sy, int w, int h,
                 int dx, int dy) {
  for (int row = 0; row < h; row++) {
    const uint32 *s = src + (sy + row) * src_w + sx;
    for (int col = 0; col < w; col++) {
      uint32 p = s[col];
      if (p & 0xFF000000)  // skip fully transparent
        set_px(dx + col, dy + row, p);
    }
  }
}

// Blit with 2× nearest-neighbour upscale.
static void blit2x(const uint32 *src, int src_w,
                   int sx, int sy, int w, int h,
                   int dx, int dy) {
  for (int row = 0; row < h; row++) {
    const uint32 *s = src + (sy + row) * src_w + sx;
    for (int col = 0; col < w; col++) {
      uint32 p = s[col];
      set_px(dx + col * 2,     dy + row * 2,     p);
      set_px(dx + col * 2 + 1, dy + row * 2,     p);
      set_px(dx + col * 2,     dy + row * 2 + 1, p);
      set_px(dx + col * 2 + 1, dy + row * 2 + 1, p);
    }
  }
}

// 3×5 glyphs, 3 bits per row, top row in the highest octal digit.
static const uint16 kDigitFont[10] = {
  075557, 026227, 071747, 071717, 055711,
  074717, 074757, 071122, 075757, 075717,
};
enum { kGlyphB = 065656, kGlyphF = 074644 };

static void draw_glyph2x(int x, int y, uint16 glyph, uint32 c) {
  for (int r = 0; r < 5; r++) {
    int row = (glyph >> ((4 - r) * 3)) & 7;
    for (int b = 0; b < 3; b++)
      if (row & (4 >> b))
        fill_rect(x + b * 2, y + r * 2, 2, 2, c);
  }
}

static void draw_number2x(int x, int y, int val, uint32 c) {
  if (val < 0) return;
  if (val > 9999) val = 9999;
  char buf[8];
  int n = sprintf(buf, "%d", val);
  for (int i = 0; i < n; i++)
    draw_glyph2x(x + i * 8, y, kDigitFont[buf[i] - '0'], c);
}

// ── Triforce / cinema screen ────────────────────────────────────────────────

// 0x00-0x05 = boot/title/file select, 0x12/0x14/0x17/0x18-0x1A = cutscenes.
// Same mapping as mode_for_module in the SDL second screen.
static bool module_is_cinema(int m) {
  return m <= 0x05 || m == 0x12 || m == 0x14 || m == 0x17 ||
         (m >= 0x18 && m <= 0x1A);
}

// Upward triangle, apex at (cx, y_top), height s (side slope ~0.58 = tan 30°).
static void tri_up(int cx, int y_top, int s, uint32 c) {
  for (int r = 0; r <= s; r++) {
    int half = r * 149 / 256;
    fill_rect(cx - half, y_top + r, half * 2 + 1, 1, c);
  }
}

static void draw_triforce(int cx, int cy, int s, uint32 c) {
  tri_up(cx, cy - s, s, c);
  tri_up(cx - s * 149 / 256, cy, s, c);
  tri_up(cx + s * 149 / 256, cy, s, c);
}

// Title screen / cutscenes: whole bottom screen black with a softly
// pulsing dim triforce.
static void draw_cinema_screen(void) {
  fill_rect(0, 0, BOT_W, BOT_H, COL(0, 0, 0));
  int ph = g_frame & 63;
  int tri = ph < 32 ? ph : 64 - ph;   // 0..32 triangle wave
  int g = 90 + tri * 2;
  draw_triforce(BOT_W / 2, BOT_H / 2, 40, COL(g, g * 83 / 100, g * 41 / 100));
}

// ── Map rendering ───────────────────────────────────────────────────────────

static void refresh_world_map(bool dark) {
  if (!g_map_dirty && g_dark_world_cached == dark) return;
  if (SS_RenderWorldMap(g_world_map, dark)) {
    g_map_dirty = false;
    g_dark_world_cached = dark;
  }
}

// Overworld: MAP_W×view_h viewport of the 512×512 map centred on
// (link_x, link_y). Scale from SDL code: map_px = 128 + (link_coord / 4096.0) * 256
static void draw_overworld_view(int link_x, int link_y, bool with_marker, int view_h) {
  int map_cx = 128 + (link_x * 256 / 4096);
  int map_cy = 128 + (link_y * 256 / 4096);
  // Dark world: link_x above ~4096 means dark world half of the combined map.
  bool dark = (link_x >= 4096) || (g_sram[0x7B] != 0);
  refresh_world_map(dark);

  int vp_x = map_cx - MAP_W / 2;
  int vp_y = map_cy - view_h / 2;
  if (vp_x < 0) vp_x = 0;
  if (vp_y < 0) vp_y = 0;
  if (vp_x > 512 - MAP_W) vp_x = 512 - MAP_W;
  if (vp_y > 512 - view_h) vp_y = 512 - view_h;

  blit(g_world_map, 512, vp_x, vp_y, MAP_W, view_h, 0, 0);

  if (with_marker) {
    // Draw Link marker (4×4 yellow dot)
    int dot_x = (map_cx - vp_x) - 2;
    int dot_y = (map_cy - vp_y) - 2;
    fill_rect(dot_x, dot_y, 4, 4, COL(255, 255, 0));
  }
}

// Floor plaques below the dungeon map (B2 B1 1F 2F ...), only the floors
// already revealed (fls, deepest basement first). The previewed floor is
// highlighted; when previewing another floor, the floor Link is actually on
// keeps a small yellow marker. Tappable (see HandleTouch).
static void draw_floor_strip(const int8_t *fls, int n, int cur_floor,
                             int view_floor, int palace) {
  if (n <= 0) return;
  int pw = (MAP_W - 8) / n;
  if (pw > 30) pw = 30;
  int ph = 16;
  int x0 = (MAP_W - pw * n) / 2;
  int y0 = MAP_SZ + (STATUS_H - ph) / 2;

  g_strip_active = true;
  g_strip_x0 = x0; g_strip_pw = pw; g_strip_y0 = y0; g_strip_ph = ph;
  g_strip_count = n; g_strip_palace = palace;
  memcpy(g_strip_fl, fls, n);

  for (int f = 0; f < n; f++) {
    int fl = fls[f];
    int x = x0 + f * pw;
    if (fl < -9 || fl > 8) continue;  // keep glyph lookups in range
    bool sel = (fl == view_floor);
    fill_rect(x + 1, y0, pw - 2, ph, sel ? COL(60, 110, 190) : C_CELL_BG);
    uint32 c = sel ? COL(255, 255, 255) : COL(150, 150, 170);
    int lx = x + (pw - 13) / 2, ly = y0 + (ph - 10) / 2;
    if (fl < 0) {
      draw_glyph2x(lx, ly, kGlyphB, c);
      draw_glyph2x(lx + 7, ly, kDigitFont[-fl], c);
    } else {
      draw_glyph2x(lx, ly, kDigitFont[fl + 1], c);
      draw_glyph2x(lx + 7, ly, kGlyphF, c);
    }
    if (fl == cur_floor && cur_floor != view_floor)
      fill_rect(x + 2, y0 + 1, 4, 4, COL(255, 220, 60));
  }
}

static void draw_map_panel(bool indoors, int link_x, int link_y, int dungeon_info) {
  fill_rect(0, 0, MAP_W, BOT_H, C_BG_MAP);
  g_strip_active = false;

  if (!indoors) {
    draw_overworld_view(link_x, link_y, true, BOT_H);
    return;
  }

  // SS_GetDungeon() returns palace (0xFF outside palaces) | signed floor << 8
  int palace_idx = dungeon_info & 0xFF;
  int floor      = (int8_t)((dungeon_info >> 8) & 0xFF);

  if (palace_idx == 0xFF) {
    // Houses/caves have no map of their own: show a quiet triforce panel.
    draw_triforce(MAP_W / 2, BOT_H / 2, 24, COL(70, 58, 29));
    return;
  }

  // Tapping a plaque previews that floor; revert after ~6 s (180 updates).
  int view = floor;
  if (g_view_touch_frame && g_view_palace == palace_idx &&
      g_frame - g_view_touch_frame < 180)
    view = g_view_floor;
  else
    g_view_touch_frame = 0;

  // SS_RenderDungeonFloor wants a layout index (0..floors-1), not the
  // signed in-game floor (B1 = -1): offset by the basement count.
  uint8 lay[16 * 25];
  int r = SS_GetDungeonLayout(palace_idx, lay, sizeof(lay));
  if (r < 0) return;
  int floors    = r & 0xFF;
  int basements = (r >> 8) & 0xFF;
  if (floors > 16) floors = 16;
  int li = view + basements;
  if (li < 0) li = 0;
  if (li > floors - 1) li = floors - 1;
  view = li - basements;

  bool have_map     = (sram16(0x68) & (0x8000 >> palace_idx)) != 0;
  bool have_compass = (sram16(0x64) & (0x8000 >> palace_idx)) != 0;

  // Zelda-style progressive reveal: a floor is listed once Link has stepped
  // on it (any visited room); the dungeon map item reveals the full list.
  int8_t vis_fl[16];
  int vis_n = 0;
  if (have_map) {
    for (int f = 0; f < floors; f++)
      vis_fl[vis_n++] = (int8_t)(f - basements);
  } else {
    uint8 dflags[0x500];
    SS_ReadDungFlags(dflags, sizeof(dflags));
    for (int f = 0; f < floors; f++) {
      int fl = f - basements;
      bool vis = (fl == floor);  // the floor Link is on is always known
      for (int i = 0; i < 25 && !vis; i++) {
        uint8 v = lay[f * 25 + i];
        if (v != 0x0F && (dflags[v * 2] & 0x0F))
          vis = true;
      }
      if (vis) vis_fl[vis_n++] = (int8_t)fl;
    }
  }

  // Re-render every update: visited rooms and the map item change while
  // playing, so a cached bitmap would freeze the automap.
  if (!SS_RenderDungeonFloor(palace_idx, li, g_dung_map)) return;

  // Dungeon floor: 80×80 scaled 2× = 160×160, centred in the 192×192 area
  int off_x = (MAP_SZ - 160) / 2;
  int off_y = (MAP_SZ - 160) / 2;
  blit2x(g_dung_map, 80, 0, 0, 80, 80, off_x, off_y);

  // Blinking marker on the current room (each room = 16px → 32px after 2x).
  // Needs this dungeon's compass, and only shows on Link's actual floor.
  if (have_compass && view == floor && (g_frame & 4)) {
    int room = SS_GetArea() & 0xFF;
    for (int i = 0; i < 25; i++) {
      if (lay[li * 25 + i] == room && room != 0x0F) {
        fill_rect(off_x + (i % 5) * 32 + 12, off_y + (i / 5) * 32 + 12, 8, 8,
                  COL(255, 80, 80));
        break;
      }
    }
  }

  draw_floor_strip(vis_fl, vis_n, floor, view, palace_idx);
}

// ── Icon sheet + item grid ──────────────────────────────────────────────────

static void refresh_icons(void) {
  if (!g_icons_dirty) return;
  if (SS_RenderIconSheet(g_icon_sheet)) g_icons_dirty = false;
}

// Blit a 16×16 icon (cell index into icon sheet) to the screen at (dx, dy).
static void draw_icon_cell(int cell, int dx, int dy) {
  if (cell < 0) return;
  int sx = (cell % SS_ICON_COLS) * 16;
  int sy = (cell / SS_ICON_COLS) * 16;
  blit(g_icon_sheet, SS_ICON_COLS * 16, sx, sy, 16, 16, dx, dy);
}

static void draw_item_grid(void) {
  refresh_icons();

  int grid_x = MAP_W;   // right of map panel
  int grid_w = BOT_W - grid_x;  // 128px

  // Equipped item highlight strip at bottom
  fill_rect(grid_x, BOT_H - EQUIP_H, grid_w, EQUIP_H, COL(20, 20, 35));
  fill_rect(grid_x, BOT_H - EQUIP_H, grid_w, 1, C_BORDER);

  int equipped = SS_GetEquippedSlot();  // 1-based; 0 = none

  // Draw 4×5 item grid
  for (int slot = 0; slot < 20; slot++) {
    int col = slot % ITEM_COLS;
    int row = slot / ITEM_COLS;
    int cx  = grid_x + col * ITEM_CELL_W;
    int cy  = row * ITEM_CELL_H;

    bool is_sel = (slot + 1 == equipped);
    fill_rect(cx, cy, ITEM_CELL_W, ITEM_CELL_H, is_sel ? C_CELL_SEL : C_CELL_BG);
    // cell border
    fill_rect(cx, cy, ITEM_CELL_W, 1, C_BORDER);
    fill_rect(cx, cy, 1, ITEM_CELL_H, C_BORDER);

    // Item icon: look up the item level from sram and map to icon cell index
    uint8 level = (slot < 20) ? g_sram[0x40 + slot] : 0;
    int max_lv = kSS_ItemMaxLevel[slot];
    if (level > max_lv) level = max_lv;
    int icell = kSS_ItemCell[slot][level];
    if (icell >= 0) {
      int ix = cx + (ITEM_CELL_W - 16) / 2;
      int iy = cy + (ITEM_CELL_H - 16) / 2;
      draw_icon_cell(icell, ix, iy);
    }
  }

  // Equipped item (large, centred in the bottom strip)
  if (equipped > 0 && equipped <= 20) {
    int slot  = equipped - 1;
    uint8 lv  = g_sram[0x40 + slot];
    int max_lv = kSS_ItemMaxLevel[slot];
    if (lv > max_lv) lv = max_lv;
    int icell = kSS_ItemCell[slot][lv];
    if (icell >= 0) {
      // 2× upscale centred in 128×40 strip
      int ex = grid_x + (grid_w - 32) / 2;
      int ey = BOT_H - EQUIP_H + (EQUIP_H - 32) / 2;
      blit2x(g_icon_sheet, SS_ICON_COLS * 16,
             (icell % SS_ICON_COLS) * 16, (icell / SS_ICON_COLS) * 16,
             16, 16, ex, ey);
    }
  }

  // Right-edge divider
  fill_rect(grid_x, 0, 1, BOT_H, C_DIVIDER);
}

// ── Perf overlay ────────────────────────────────────────────────────────────
// Six rows: logic, ppu, blit, second screen, audio (tenths of ms per frame)
// and rendered fps. Values are pushed once per second from main3ds.c.

static int g_perf_vals[6] = {-1, -1, -1, -1, -1, -1};

void SecondScreen3DS_SetPerf(const int vals[6]) {
  memcpy(g_perf_vals, vals, sizeof(g_perf_vals));
}

static void draw_perf_overlay(void) {
  if (g_perf_vals[5] < 0) return;
  static const uint32 kPerfColors[6] = {
    COL(230, 60, 60),    // logic
    COL(60, 220, 60),    // ppu render
    COL(80, 200, 230),   // top blit
    COL(230, 220, 60),   // second screen
    COL(230, 80, 230),   // audio
    COL(255, 255, 255),  // fps
  };
  fill_rect(0, 0, 48, 70, COL(0, 0, 0));
  for (int i = 0; i < 6; i++) {
    fill_rect(2, 5 + i * 11, 4, 4, kPerfColors[i]);
    draw_number2x(10, 2 + i * 11, g_perf_vals[i], kPerfColors[i]);
  }
}

// ── Hand the composed screen buffer to the GPU presenter ────────────────────

// render3ds.c: converts to RGB565 and tiles it into the bottom texture.
void N3DS_UploadBottomScreen(const uint32 *argb);

static void flush_to_bottom_screen(void) {
  N3DS_UploadBottomScreen(g_screen);
}

// ── Public API ──────────────────────────────────────────────────────────────

bool SecondScreen3DS_Init(void) {
  memset(g_screen,     0, sizeof(g_screen));
  memset(g_world_map,  0, sizeof(g_world_map));
  memset(g_dung_map,   0, sizeof(g_dung_map));
  memset(g_icon_sheet, 0, sizeof(g_icon_sheet));
  g_map_dirty   = true;
  g_icons_dirty = true;
  return true;
}

void SecondScreen3DS_Update(void) {
  // Snapshot live game state
  SS_ReadSram(g_sram, 256);

  g_frame++;
  bool indoors = SS_IsIndoors();
  int  link_x  = SS_GetLinkX();
  int  link_y  = SS_GetLinkY();
  int  palace  = SS_GetDungeon();

  // Title screen and cutscenes: no game state worth showing — black screen
  // with the triforce instead of map + items.
  g_in_cinema = module_is_cinema(SS_GetModule() & 0xFF);
  if (g_in_cinema) {
    g_strip_active = false;
    draw_cinema_screen();
    draw_perf_overlay();
    flush_to_bottom_screen();
    return;
  }

  // Remember where Link last stood outdoors (used by the follow map).
  if (!indoors) {
    g_last_out_x = link_x;
    g_last_out_y = link_y;
  }

  // Clear screen
  fill_rect(0, 0, BOT_W, BOT_H, C_BG_MAP);

  draw_map_panel(indoors, link_x, link_y, palace);
  draw_item_grid();
  draw_perf_overlay();

  flush_to_bottom_screen();
}

// Handle a touch event on the bottom screen.
// Returns true if an action was taken.
bool SecondScreen3DS_HandleTouch(touchPosition *pos, bool touched) {
  if (!touched || g_in_cinema) return false;

  int tx = pos->px;
  int ty = pos->py;

  // Right panel → item grid
  if (tx >= MAP_W) {
    int col = (tx - MAP_W) / ITEM_CELL_W;
    int row = ty / ITEM_CELL_H;
    if (col < ITEM_COLS && row < ITEM_ROWS) {
      int slot = row * ITEM_COLS + col + 1;  // 1-based
      SS_EquipSlot(slot);
      return true;
    }
    return false;
  }

  // Left panel: tap a floor plaque to preview that floor for a few seconds
  // (slightly padded hitbox for finger-sized taps).
  if (g_strip_active && ty >= g_strip_y0 - 6 &&
      ty < g_strip_y0 + g_strip_ph + 6 && tx >= g_strip_x0) {
    int idx = (tx - g_strip_x0) / g_strip_pw;
    if (idx < g_strip_count) {
      g_view_floor = g_strip_fl[idx];
      g_view_palace = g_strip_palace;
      g_view_touch_frame = g_frame | 1;  // nonzero marks the preview active
      return true;
    }
  }
  return false;
}

void SecondScreen3DS_Shutdown(void) {
  // Nothing to free (all static/global buffers)
}
