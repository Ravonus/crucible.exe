/* CRUCIBLE, native CGB. Choose two things, watch their bodies merge, discover what they make, fill the book.
 * The room is vaporwave: two specimens and a mystery slot float in a pastel pink sky between holographic columns,
 * three glass cases stand on the checkerboard floor below. KEEL compiled every object to directional animated tiles; this cartridge
 * only streams them. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "interrupts.h"
#include "crucible_data.h"
#include "crucible_ui.h"
#include "crucible_scene.h"
#include "crucible_state.h"
#include "crucible_avatar.h"
#include "crucible_eggs.h"
#include "crucible_storyrun.h"
#include "crucible_fight.h"
#include "crucible_link.h"
#include "crucible_fusion.h"
#include "crucible_predict.h"
#include "keel_retro_clock.h"
#include "crucible_palette_lease.h"
#include "crucible_overlay.h"
#include "crucible_room.h"
#include "crucible_flow.h"
#include "crucible_sky.h"
void reveal_predict(uint16_t a, uint16_t b) BANKED;
void reveal_prepare(uint16_t id, uint8_t variant) BANKED; /* crucible_reveal.c: the result turns out of the glitch */
uint8_t reveal_turn(void) BANKED;
#define REPEAT_DELAY 15u
#define REPEAT_EVERY 5u
#define STAGE_X 80u
#define STAGE_Y 48u
#define LIST_ROWS 13u
#define LOOP_FRAMES 576u
/* text attributes: ink or dim (magenta) on the pastel wall and pages, pale cyan or neon pink on the void */
#define T_INK 0u
#define T_DIM 8u
#define T_CREAM 7u
#define T_BRASS 15u
/* OAM: 0-23 material overlay (0-35 fusion, burst and busts in merges and talks), 24-31 cloud, 32-33 motes, 34-36 rooms,
 * 37 loading spark, 38 encounter hint (crucible_flow.c), 39 cursor */
#define OAM_MOTE 32u
#define OAM_FLAME 34u
#define OAM_CURSOR 39u
enum { BENCH, MERGE, REVEAL, BOOK, RECORDS, MENU, FILTER, TALK, FIGHT, LINKEND };
typedef char room_screens_match
    [(BENCH == ROOM_S_BENCH && MERGE == ROOM_S_MERGE && REVEAL == ROOM_S_REVEAL && FIGHT == ROOM_S_FIGHT) ? 1 : -1];
/* turn speed setting: frames per view step (normal, slow, fast) */
static const uint8_t turn_ticks[3] = {11, 16, 7};
enum {
  K_BLANK,
  K_ITEM,
  K_EMPTY,
  K_QUESTION,
  K_TRIED
}; /* and the core's CRU_K_GLITCH (5): drawn as ?, torn by crucible_lost.c */
enum { CA, CB, CR, CL, CF, CN, CELLS };
static const uint8_t bench_x[CELLS] = {1, 8, 15, 2, 8, 14}, bench_y[CELLS] = {4, 4, 4, 10, 10, 10},
                     cell_pal[CELLS] = {1, 5, 3, 4, 5, 4};
static const uint8_t holds[4] = {3, 2, 2, 1};
static const uint16_t WHITE = 0x7fffu, DUSK = (14u | (8u << 5) | (22u << 10));
/* holographic: glow silhouettes cyan to white; sparks cyan, white and magenta */
static const uint16_t GLOW[4] = {0, (8u | (25u << 5) | (31u << 10)), 0x7fffu, 0x7fffu},
                      SPARK[4] = {0, (8u | (25u << 5) | (31u << 10)), 0x7fffu, (26u | (6u << 5) | (22u << 10))};
static const uint16_t CANDLE[4] = {0, (10u | (6u << 5) | (2u << 10)), (31u | (17u << 5) | (4u << 10)),
                                   (31u | (29u << 5) | (16u << 10))},
                      CLOUD[4] = {0, (25u | (19u << 5) | (30u << 10)), 0x7fffu, 0};
typedef struct {
  uint8_t kind, rotate, animate, bank, swap, drawn, x, y, shown, ready;
  uint16_t id, shade;
  uint8_t ticks;
  kr_frame_clock playback;
} cell_t;
static cell_t cells[CELLS];
/* the merge on screen (a copy of core.mix: the core clears its slots when the mix commits) */
static uint16_t mix_a, mix_b, result, book_at, book_top, book_n;
static uint8_t screen, outcome, awarded;
static uint8_t held, pressed, repeat_dir, repeat_frames, rot_step, rot_clock, load_turn;
static uint8_t merge_fuse, appr, light, dt, book_back, filter_back;
/* the recipe book's rows under the current filter (shelf order) */
static uint16_t book_ids[LIST_ROWS];
static uint16_t clock, t, anim;
/* the plaster and the palette-dirty flag are shared with the living rooms (crucible_room.c), which write base colours */
#define plaster room_plaster
#define pal_changed room_dirty
static char label[17];
/* ---- palettes: base colours in bank-2 scratch; fades use a per-frame difference table; VBlank writes ---- */
/* SRAM addresses (0xA000-0xBFFF): identity on the cartridge; a host build maps them onto the switched bank (SRAM_PTR). */
#ifndef SRAM_PTR
#define SRAM_PTR(a) (a)
#endif
#define BASE ((uint16_t *)SRAM_PTR(0xbd00u))
/* Bank 2 layout: fusion buffers end at B1FF; base palettes BD00..BD7F;
 * lease backup BD80..BD89; v3 migration borrows BE00..BF07 at boot only.
 * The lease adds SRAM scratch, not another WRAM palette or sprite layer. */
#define PAL_LEASE ((keel_cgb_palette_lease_t *)SRAM_PTR(0xbd80u))
static uint16_t out_bg[32], out_sp[32], fade_to_bg, fade_to_sp;
static uint8_t fade_bg, fade_sp, pal_ready;
static int8_t lut_bg[63], lut_sp[63];
static void build_lut(int8_t *lut, uint8_t k) {
  int16_t acc = -31 * (int16_t)k;
  uint8_t i;
  for (i = 0; i < 63u; i++) {
    lut[i] = (int8_t)(acc >> 4);
    acc += k;
  }
}
static uint16_t mix(uint16_t a, uint16_t b, const int8_t *lut) {
  uint8_t r = a & 31u, g = (a >> 5) & 31u, l = (a >> 10) & 31u;
  r += lut[(uint8_t)((b & 31u) + 31u - r)];
  g += lut[(uint8_t)(((b >> 5) & 31u) + 31u - g)];
  l += lut[(uint8_t)(((b >> 10) & 31u) + 31u - l)];
  return (uint16_t)r | ((uint16_t)g << 5) | ((uint16_t)l << 10);
}
static void pal_set(uint8_t at, const uint16_t *p) {
  ENABLE_RAM;
  SWITCH_RAM(2);
  memcpy(BASE + at, p, 8);
  DISABLE_RAM;
  pal_changed = 1;
}
#define pal_bg(slot, p) pal_set((slot) * 4u, p)
#define pal_sp(slot, p) pal_set(32u + (slot) * 4u, p)
static void pal_lease_init(void) {
  ENABLE_RAM;
  SWITCH_RAM(2);
  crucible_pal_lease_init(PAL_LEASE);
  DISABLE_RAM;
}
static void pal_lease_begin(void) {
  ENABLE_RAM;
  SWITCH_RAM(2);
  crucible_pal_lease_begin(PAL_LEASE, BASE, PAL_SKY);
  DISABLE_RAM;
}
static void pal_lease_end(void) {
  ENABLE_RAM;
  SWITCH_RAM(2);
  if (crucible_pal_lease_end(PAL_LEASE, BASE)) pal_changed = 1;
  DISABLE_RAM;
}
/* Bench B is the focus, so it shares palette 5. In a fight B is an opponent
 * ingredient; the independently chosen answer leases the unused sky slot. */
static uint8_t cell_palette(uint8_t c) { return screen == FIGHT && c == CF ? PAL_SKY : cell_pal[c]; }
static void pal_item(uint8_t slot, uint16_t id, uint8_t sprite) {
  uint16_t p[4];
  crucible_get_palette(id, VARIANT(id), p);
  if (sprite)
    pal_sp(slot, p);
  else
    pal_bg(slot, p);
}
static void fade(uint8_t bg, uint16_t to_bg, uint8_t sp, uint16_t to_sp) {
  if (fade_bg == bg && fade_sp == sp && fade_to_bg == to_bg && fade_to_sp == to_sp) return;
  fade_bg = bg;
  fade_to_bg = to_bg;
  fade_sp = sp;
  fade_to_sp = to_sp;
  pal_changed = 1;
}
/* The sky behind every specimen: colour 0 of palettes 0,1,3,5,6 (kept in one place so the room can relight it). */
static void prepare_palettes(void) {
  uint8_t i;
  uint16_t c;
  if (!pal_changed) return;
  if (fade_bg) build_lut(lut_bg, fade_bg);
  if (fade_sp) build_lut(lut_sp, fade_sp);
  ENABLE_RAM;
  SWITCH_RAM(2);
  for (i = 0; i < 32u; i++) {
    c = BASE[i];
    if ((i & 3u) == 0u && (i != 8u || screen == FIGHT) && i != 16u && i != 28u) c = plaster;
    out_bg[i] = fade_bg ? mix(c, fade_to_bg, lut_bg) : c;
    c = BASE[32u + i];
    out_sp[i] = fade_sp ? mix(c, fade_to_sp, lut_sp) : c;
  }
  DISABLE_RAM;
  if (egg_dmg()) egg_dmg_filter(out_bg, out_sp); /* the DMG egg: four greens */
  pal_changed = 0;
  pal_ready = 1;
}
/* ---- map helpers ---- */
static void put(uint8_t x, uint8_t y, uint8_t tile, uint8_t attr) {
  VBK_REG = 1;
  set_bkg_tiles(x, y, 1, 1, &attr);
  VBK_REG = 0;
  set_bkg_tiles(x, y, 1, 1, &tile);
}
static void text(uint8_t x, uint8_t y, const char *s, uint8_t width, uint8_t attr) {
  uint8_t tiles[20], attrs[20], i = 0, c;
  if (!width) return;
  if (width > 20u) width = 20u;
  memset(attrs, attr, width);
  while (i < width) {
    c = *s;
    if (c) s++;
    tiles[i++] = GLYPH(c);
  }
  VBK_REG = 1;
  set_bkg_tiles(x, y, width, 1, attrs);
  VBK_REG = 0;
  set_bkg_tiles(x, y, width, 1, tiles);
}
static void field(uint8_t x, uint8_t y, uint8_t width, const char *s, uint8_t attr) {
  uint8_t n = strlen(s);
  if (n > width) n = width;
  text(x, y, " ", width, attr);
  text(x + (width - n) / 2u, y, s, n, attr);
}
static void number(char *out, uint16_t n, uint8_t width) {
  out[width] = 0;
  while (width) {
    out[--width] = '0' + (uint8_t)(n % 10u);
    n /= 10u;
  }
}
/* digits of the catalogue's size: found counts and specimen numbers are printed that wide */
static uint8_t digits(void) { return core.items > 999u ? 4u : core.items > 99u ? 3u : 2u; }
/* found/total while it fits in five tiles (a catalogue under 100), else just found */
static uint8_t count(char *n, uint16_t found, uint16_t total) {
  uint8_t d = digits();
  if (d > 2u) {
    number(n, found, d);
    return d;
  }
  number(n, found, 2);
  n[2] = '/';
  number(n + 3, total, 2);
  return 5u;
}
static void header(const char *title) {
  char n[8];
  uint8_t w = count(n, core.found[0], core.items);
  if (title) {
    text(1, 0, title, 8, T_CREAM);
    text(18u - w, 0, n, w, T_CREAM);
  } else {
    put(1, 0, UI_BOOK, T_CREAM);
    text(2, 0, n, w, T_CREAM);
    put(14, 0, UI_STAR, T_CREAM);
    number(n, core.points > 9999u ? 9999u : core.points, 4);
    text(15, 0, n, 4, T_CREAM);
    /* the active filter sits between the counters */
    text(7, 0, " ", 7, T_BRASS);
    if (core.filter) {
      filter_label(core.filter, label);
      field(7, 0, 7, label, T_BRASS);
    }
  }
}
/* ---- cells: 4x4-tile object windows, double-buffered across both VRAM banks (swapped in VBlank) ---- */
static void cells_place(uint8_t book) {
  uint8_t c;
  for (c = 0; c < CELLS; c++) {
    cells[c].x = bench_x[c];
    cells[c].y = bench_y[c];
  }
  if (book) {
    cells[CA].x = 3;
    cells[CA].y = 3;
  }
}
static void cell_set(uint8_t c, uint8_t kind, uint16_t id, uint8_t rotate, uint8_t animate) {
  cell_t *p = &cells[c];
  uint8_t pal = cell_palette(c);
  if (p->kind == kind && p->id == id && p->rotate == rotate && p->animate == animate) return;
  p->kind = kind;
  p->id = id;
  p->rotate = rotate;
  p->animate = animate;
  p->shown = 0xffu;
  p->drawn = 0;
  p->swap = 0;
  p->ready = 0;
  kr_clock_reset(&p->playback);
  if (kind == K_ITEM) {
    p->ready = crucible_art_ready(id, rotate, 0) == 255u; /* a cached loop plays from the first frame */
    p->ticks = crucible_frame_ticks(id);
    if (pal != PAL_MUTED) pal_item(pal, id, 0);
    if (rotate) {
      rot_step = 0;
      rot_clock = 0;
    }
  }
}
static void cell_map(uint8_t c) {
  cell_t *p = &cells[c];
  uint8_t map[16], attrs[16], i, r, col, base = c * 16u, pal = cell_palette(c), muted = pal == PAL_MUTED;
  uint16_t shade = p->shade;
  for (i = 0; i < 16u; i++, shade >>= 1) {
    r = i >> 2;
    col = i & 3u;
    if (p->kind == K_ITEM) {
      map[i] = base + i;
      attrs[i] = ((!muted && (shade & 1u)) ? 6u : pal) | (p->bank ? 8u : 0u);
    } else if (p->kind == K_BLANK) {
      map[i] = UI_BLANK;
      attrs[i] = 0;
    } else if ((r == 1u || r == 2u) && (col == 1u || col == 2u)) {
      map[i] =
          p->kind == K_EMPTY ? UI_BLANK : (p->kind == K_TRIED ? UI_CROSS : UI_QUESTION) + (r - 1u) * 2u + (col - 1u);
      attrs[i] = 0;
    } else {
      map[i] = (r == 0u || r == 3u) ? ((col == 0u || col == 3u) ? UI_RINGC : UI_RINGH) : UI_RINGV;
      attrs[i] = (col >= 2u ? 0x20u : 0u) | (r >= 2u ? 0x40u : 0u);
    }
  }
  VBK_REG = 1;
  set_bkg_tiles(p->x, p->y, 4, 4, attrs);
  VBK_REG = 0;
  set_bkg_tiles(p->x, p->y, 4, 4, map);
  p->drawn = 1;
}
static uint8_t focus_load = 255u;
static uint8_t cell_load(uint8_t c) {
  cell_t *p = &cells[c];
  uint8_t turn, index;
  if (p->kind != K_ITEM || p->swap) return 0;
  /* a turning object plays its turntable (its pose follows the view); at rest it plays its resting loop in place */
  turn = p->rotate ? 1u : 0u;
  index = turn ? rot_step : p->animate ? kr_clock_frame(&p->playback, anim, p->ticks, CRUCIBLE_POSES - 1u) : 0u;
  /* a loop plays only once all of it is cached: until then the object stands still on its keyframe (never hopping
  * between decoded and missing frames), and the focus's progress shows on the sign */
  if (turn || p->animate) {
    if (!p->ready &&
        !(((uint8_t)sys_time + c) & 7u)) { /* real frames, not loop turns: the round robin can never lock it out */
      uint8_t r = crucible_art_ready(p->id, turn, c == CF ? 120u : c < CL ? 100u : 80u);
      if (r == 255u) p->ready = 1;
      if (c == CF) focus_load = r;
    }
    if (!p->ready)
      index = 0;
    else if (c == CF)
      focus_load = 255u;
  }
  /* the cell shows (id, shown frame); shown carries the turn flag in bit 7 and 0xff before the first draw */
  if (p->drawn && p->shown == (index | (turn << 7))) return 0;
  /* Decode into the hidden bank; the visible bank keeps showing until the VBlank swap. Streams: slots A, B,
  * result and the focus animate (decoding a few rows a frame, then from cache); the side cases are keyframes.
  * While a stream is still decoding the cell keeps its frame, or shows the new object's keyframe at once. */
  if (!crucible_art(c < CL    ? c
                    : c == CF ? 3u
                              : CRUCIBLE_ART_KEY,
                    p->id, turn, index, VARIANT(p->id), c * 16u, 255u, p->bank ? 0u : CRUCIBLE_ART_BANK1))
    return 0;
  p->shown = crucible_art_shown | (turn << 7);
  crucible_overlay_cell(c, p->id, p->shown, p->x, p->y);
  p->shade = crucible_shade_mask;
  if (p->drawn)
    p->swap = 1;
  else {
    p->bank ^= 1u;
    cell_map(c);
  }
  return 1;
}
static void cells_vblank(void) {
  uint8_t c;
  for (c = 0; c < CELLS; c++)
    if (cells[c].swap) {
      cells[c].bank ^= 1u;
      cells[c].swap = 0;
      cell_map(c);
    }
  crucible_overlay_sync();
}
static void cells_update(uint8_t budget) {
  uint8_t c, i, n = 0;
  for (c = 0; c < CELLS; c++)
    if (cells[c].kind != K_ITEM && cells[c].kind != 255u && !cells[c].drawn) cell_map(c);
  for (i = 0; i < CELLS && n < budget; i++) {
    c = load_turn;
    if (++load_turn >= CELLS) load_turn = 0;
    n += cell_load(c);
  }
}
static void cells_reset(void) {
  uint8_t c;
  for (c = 0; c < CELLS; c++) {
    cells[c].kind = 255u;
    cells[c].swap = 0;
    cells[c].drawn = 0;
  }
}
/* ---- sky: random sprite clouds drift across the bench (behind the floating things); on the title two sky
 * bands scroll at different speeds and spawn random cloud chunks into map columns just before they scroll in ---- */
#define CLOUDS 4u
static const uint8_t cloud_w[3] = {2, 3, 4}, cloud_h[3] = {1, 2, 2},
                     cloud_tile[3] = {SPR_CLOUD_S0, SPR_CLOUD_M0, SPR_CLOUD_L0}, cloud_lane[CLOUDS] = {9, 27, 45, 19};
static uint16_t cloud_x[CLOUDS], rng16 = 0x5eedu;
static uint8_t cloud_shape[CLOUDS], cloud_speed[CLOUDS], sky;
static uint8_t rnd8(void) {
  rng16 ^= rng16 << 7;
  rng16 ^= rng16 >> 9;
  rng16 ^= rng16 << 8;
  return (uint8_t)rng16;
}
/* Cloud x is kept in sixteenths of a pixel, offset by 64 pixels so it can sit off the left edge. */
static void cloud_tiles(uint8_t i) {
  uint8_t ty, tx, s = cloud_shape[i];
  if (i < 3u) return; /* OAM 0..23 belong to the material overlay: only the last cloud is a sprite */
  for (ty = 0; ty < 2u; ty++)
    for (tx = 0; tx < 4u; tx++) {
      set_sprite_tile(i * 8u + ty * 4u + tx, cloud_tile[s] + ty * cloud_w[s] + tx);
      set_sprite_prop(i * 8u + ty * 4u + tx, 0x80u | 7u);
    }
}
static void cloud_spawn(uint8_t i, uint8_t anywhere) {
  cloud_shape[i] = i == 3u ? 0u : (uint8_t)(rnd8() % 3u);
  cloud_speed[i] = 2u + (rnd8() & 7u);
  cloud_x[i] = (uint16_t)(anywhere ? 24u + rnd8() % 180u : 232u + (rnd8() & 63u)) << 4;
  cloud_tiles(i);
}
static void clouds_setup(void) {
  uint8_t i;
  for (i = 0; i < CLOUDS; i++) cloud_tiles(i);
}
static void clouds_draw(uint8_t on) {
  uint8_t i, tx, ty, s;
  int16_t x, sx;
  for (i = 3u; i < CLOUDS; i++) {
    s = cloud_shape[i];
    x = (int16_t)(cloud_x[i] >> 4) - 64;
    for (ty = 0; ty < 2u; ty++)
      for (tx = 0; tx < 4u; tx++) {
        sx = x + (int16_t)(tx * 8u);
        if (!on || ty >= cloud_h[s] || tx >= cloud_w[s] || sx < -7 || sx > 167)
          move_sprite(i * 8u + ty * 4u + tx, 0, 0);
        else
          move_sprite(i * 8u + ty * 4u + tx, (uint8_t)(sx + 8), (uint8_t)(cloud_lane[i] + ty * 8u + 16u));
      }
  }
}
static void clouds_tick(void) {
  uint8_t i;
  uint16_t d;
  for (i = 0; i < CLOUDS; i++) {
    d = (uint16_t)cloud_speed[i] * dt;
    if (cloud_x[i] < (24u << 4) + d)
      cloud_spawn(i, 0);
    else
      cloud_x[i] -= d;
  }
  clouds_draw(1);
}
/* Title bands: rows 4..6 drift slowly, rows 7..9 faster; chunk ids per band (small far, big near). */
/* Scanline events per screen: 1 bench (toast cut and sprite gap), 2 title (two scrolling bands), 0 none. */
/* The toast card shows lines 8..8+h-1; sprites (clouds) are off behind it, and the window is cut below it. The height
 * is kept so that any rebuild of the bench's events (sky_mode) keeps the cut: without it the window covers the screen. */
static uint8_t toast_h;
/* the toast's card hides sprites behind it; whatever cuts it, the second event always turns them back on (otherwise a cut
 * between the two events would leave them off for good and every later merge would play invisible) */
static void toast_cut(uint8_t h) {
  toast_h = h;
  if (sky != 1u) return;
  CRITICAL {
    lcd_kind[0] = h ? LCD_OBJ_OFF : LCD_OBJ_ON;
    lcd_line[1] = h ? (uint8_t)(7u + h) : 8u;
    lcd_kind[1] = h ? (LCD_WIN_OFF | LCD_OBJ_ON) : LCD_OBJ_ON;
  }
}
/* off the bench nothing turns sprites back on, so a toast's cut that fired just before the list changed would leave them off (a menu with no cursor) */
static void sky_mode(uint8_t m) {
  uint8_t line[4], kind[4];
  sky = m;
  if (m != 1u) LCDC_REG |= LCDCF_OBJON;
  if (m == 1u) {
    line[0] = 7;
    kind[0] = 0;
    line[1] = 8;
    kind[1] = 0;
    line[2] = 150;
    kind[2] = LCD_SCX;
    lcd_events(line, kind, 3);
    toast_cut(toast_h);
  } else if (m == 2u) {
    line[0] = 31;
    line[1] = 55;
    line[2] = 79;
    line[3] = 150;
    kind[0] = kind[1] = kind[2] = kind[3] = LCD_SCX;
    lcd_events(line, kind, 4);
  } else {
    line[0] = 150;
    kind[0] = LCD_SCX;
    lcd_events(line, kind, 1);
  }
}
/* Scanline glitch: for a few frames the bench's formula rows (lines 33..62) tear sideways at random, the
 * corrupted-video cut of the title busts, used when a combination resolves. */
static uint8_t glitch_t;
static void glitch_start(uint8_t frames) {
  uint8_t line[7] = {33, 37, 42, 47, 52, 58, 150}, kind[7], i;
  for (i = 0; i < 7u; i++) kind[i] = LCD_SCX;
  lcd_events(line, kind, 7);
  sky = 3;
  glitch_t = frames;
}
static void glitch_tick(void) {
  uint8_t i;
  if (!glitch_t) return;
  if (--glitch_t) {
    for (i = 0; i < 6u; i++) lcd_value[i] = (rnd8() & 3u) ? 0u : (uint8_t)((rnd8() & 7u) - 3u);
  } else
    sky_mode(screen == MENU ? 2u : screen == BENCH || screen == REVEAL ? 1u : 0u);
}
/* Sprite glitch: two random rows of a 4x4 sprite object (OAM first..first+15) jump sideways. */
static void glitch_rows(uint8_t first) {
  uint8_t i, r1 = rnd8() & 3u, r2 = rnd8() & 3u;
  int8_t d = (int8_t)((rnd8() & 7u) - 3);
  for (i = 0; i < 16u; i++)
    if (((i >> 2) == r1 || (i >> 2) == r2) && d) scroll_sprite(first + i, d * 2, 0);
}

static void room(uint8_t on) {
  uint8_t i;
  if (!on) {
    for (i = 0; i < 32u; i++) move_sprite(i, 0, 0);
    for (i = OAM_FLAME; i < OAM_CURSOR; i++) move_sprite(i, 0, 0);
    return;
  }
  if (screen == BENCH) clouds_tick();
  if ((clock & 15u) == 0u && screen != BOOK) scene_twinkle((uint8_t)(clock >> 4));
}
/* ---- title bust: four marble heads turn as sprites (OAM 0..35, double-buffered across VRAM banks); each full
 * turn ends in a glitch cut to the next head ---- */
#define BUST_X 56u
#define BUST_Y 22u
static const uint16_t GLITCH[4] = {0, (8u | (25u << 5) | (31u << 10)), (28u | (6u << 5) | (24u << 10)), 0x7fffu};
static uint8_t bust_id, bust_view, bust_bank, bust_clock, bust_glitch, bust_job,
    bust_hold; /* hold: a talking character keeps its bust */
#define AVATAR 0xffu /* bust_id: a procedural face (crucible_avatar.c) instead of a baked bust */
static uint8_t bust_want, talk_back; /* the avatar changed while a stream was running */
static void bust_place(void) {
  uint8_t i, row, x, by = (uint8_t)(BUST_Y + (bust_id == AVATAR ? avatar_bob() : 0u));
  int8_t jog;
  wait_vbl_done();
  for (i = 0; i < 36u; i++) { /* placed whole in one frame: a half-moved bob put 12 sprites on a line */
    row = i / 6u;
    jog = bust_glitch ? (int8_t)((rnd8() & 7u) - 3) : 0;
    x = (uint8_t)((int8_t)(BUST_X + (i % 6u) * 8u + 8u) +
                  (bust_glitch && (row == ((bust_glitch >> 1) % 6u) || row == ((bust_glitch + 3u) % 6u)) ? jog * 2
                                                                                                         : 0));
    move_sprite(i, x, (uint8_t)(by + row * 8u + 16u));
  }
}
static void bust_show(void) {
  uint8_t i;
  for (i = 0; i < 36u; i++) {
    set_sprite_tile(i, i);
    set_sprite_prop(i, 1u | (bust_bank ? 8u : 0u));
  }
}
static void bust_tint(void) {
  uint16_t p[4];
  if (bust_id == AVATAR)
    avatar_palette(p);
  else
    crucible_bust_palette(bust_id, p);
  pal_sp(1, p);
}
static void bust_chunk(uint8_t c, uint8_t bank) {
  if (bust_id == AVATAR)
    avatar_chunk(c, 0, bank);
  else
    crucible_bust(bust_id, bust_view, c, 0, bank);
}
static void bust_open(void) {
  uint8_t c;
  bust_clock = 0;
  bust_glitch = 0;
  bust_bank = 0;
  bust_job = 0;
  bust_want = 0;
  for (c = 0; c < 3u; c++) bust_chunk(c, 0);
  bust_show();
  bust_place();
  bust_tint();
}
/* A new view decodes into the hidden VRAM bank one third per frame (jobs 1..3), then the sprites flip banks. */
static void bust_tick(void) {
  if (bust_job) {
    if (bust_job <= 3u) {
      bust_chunk(bust_job - 1u, bust_bank ^ 1u);
      bust_job++;
    } else {
      bust_bank ^= 1u;
      bust_show();
      bust_job = 0;
    }
  }
  bust_clock += dt;
  if (bust_id == AVATAR) {
    uint8_t b = avatar_bob();
    if (avatar_tick(dt)) bust_want = 1;
    if (bust_want && !bust_job) {
      bust_want = 0;
      bust_job = 1;
    }
    if (bust_glitch) {
      bust_glitch--;
      if (!bust_glitch) bust_tint();
      bust_place();
    } else if (b != avatar_bob())
      bust_place();
    return;
  }
  if (bust_glitch) {
    bust_glitch--;
    if (bust_glitch == 8u && !bust_hold) {
      uint8_t n = (uint8_t)(rnd8() % CRUCIBLE_BUSTS);
      bust_id = n == bust_id ? (uint8_t)((n + 1u) % CRUCIBLE_BUSTS) : n; /* a random icon next */
      bust_view = 0;
      bust_job = 1;
    }
    if (!bust_glitch) bust_tint();
    bust_place();
    return;
  }
  if (bust_clock >= 8u && !bust_job) {
    bust_clock = 0;
    bust_view = (bust_view + 1u) & 15u;
    if (!bust_view && !bust_hold) {
      bust_view = 15u;
      bust_glitch = 12;
      pal_sp(1, GLITCH);
      return;
    }
    bust_job = 1;
  }
}
static void bust_hide(void) {
  uint8_t i;
  for (i = 0; i < 36u; i++) move_sprite(i, 0, 0);
}
/* a talker arrives through the glitch: its rows settle in, the sprites jog in the glitch palette, the scene tears */
static uint8_t talk_out; /* frames left of a talker glitching away */
static void arrow(uint8_t show);
static void to_bench(void);
static void fight_go(uint8_t f, uint8_t level, uint8_t nemesis);
static uint8_t talk_kind; /* 0 a talk, 1 the wake-up of a new story run, 2 the run's end */
static void talk_go(uint8_t back, uint8_t kind) {
  talk_back = back;
  talk_kind = kind;
  bust_hide();
  sky_mode(0);
  screen = TALK;
  cells_reset();
  arrow(0);
}
static void talk_enter(void);
/* the story run reacts to a mix: a loss or the end at once; visitors and champions go to the director (crucible_flow.c) */
static void story_react(uint8_t r) {
  r = flow_after_mix(r);
  if (r == STORY_LOSS) {
    talk_go(BENCH, 0);
    talk_event(STORY_LOSS, story_lost());
    talk_enter();
  } else if (r == STORY_OVER) {
    talk_go(MENU, 2);
    talk_event(STORY_OVER, story_lost());
    talk_enter();
  }
}
/* ---- FIGHT: a boss's face above, what it is combining in three cells, your answer below ---- */
static uint8_t flash_t;
static void bust_open(void);
static void fight_go(uint8_t f, uint8_t level, uint8_t nemesis) {
  bust_hide();
  sky_mode(0);
  pal_lease_begin();
  screen = FIGHT;
  cells_reset();
  arrow(0);
  fight_open(f, level, nemesis);
  cells[CA].x = 1;
  cells[CA].y = 9;
  cells[CB].x = 8;
  cells[CB].y = 9;
  cells[CR].x = 15;
  cells[CR].y = 9;
  cells[CF].x = 8;
  cells[CF].y = 13;
  bust_id = AVATAR;
  bust_view = 0;
  bust_hold = 1;
  bust_open();
  avatar_glitch(1);
  bust_glitch = 16;
  pal_sp(1, GLITCH);
  glitch_start(14);
  music_mood(1);
  flash_t = 0;
} /* the champion song plays the fight (crucible_sound.c) */
static void fight_end(uint8_t r) {
  pal_lease_end();
  bust_hold = 0;
  bust_hide();
  bust_id = (uint8_t)(rnd8() % CRUCIBLE_BUSTS);
  fade(0, 0, 0, 0);
  if (r == FIGHT_OVER) {
    talk_go(MENU, 2);
    talk_event(STORY_OVER, fight_lost());
    talk_enter();
  } else
    to_bench();
}
static void tick_fight(void) {
  uint8_t r = fight_tick(pressed, dt), fx, c;
  crucible_cells v;
  fight_view(&v);
  for (c = 0; c < CELLS; c++)
    if (v.kind[c] < 8u)
      cell_set(c, v.kind[c], v.id[c], (v.message >> c) & 1u, 1); /* the fight says what each cell shows */
  fx = fight_fx();
  if (fx & FIGHT_FX_FLASH) flash_t = 12u; /* the attack lands: white */
  if (fx & (FIGHT_FX_SHAKE | FIGHT_FX_CHARGE)) {
    bust_glitch = fx & FIGHT_FX_SHAKE ? 12u : 6u;
    pal_sp(1, GLITCH);
  } /* jolt / charge */
  if (fx & FIGHT_FX_TEAR) glitch_start(10);
  if (fx & FIGHT_FX_PHASE) {
    avatar_glitch(1);
    glitch_start(20);
  }
  if (flash_t) {
    flash_t = flash_t > dt ? (uint8_t)(flash_t - dt) : 0u;
    fade((uint8_t)(flash_t + 4u > 16u ? 16u : flash_t + 4u), WHITE, (uint8_t)(flash_t), WHITE);
    if (!flash_t) fade(0, 0, 0, 0);
  }
  /* the "?" shimmers while the attack charges */
  if (v.sign && !(clock & 7u)) {
    uint16_t p[4];
    fight_shimmer(p);
    pal_bg(3, p);
  }
  bust_tick();
  if (r != FIGHT_GOING) fight_end(r);
}
/* the link cable's news */
static void bench_view(void);
static void link_event(uint8_t ev) {
  if (ev == LINK_EV_GO) {
    link_bring();
    link_begin();
    to_bench();
    glitch_start(16);
  } /* the session tears the bench in */
  else if (ev == LINK_EV_GIFT) {
    uint16_t id = link_inbox();
    if (id < core.items && cru_grant(&core, id, 0)) {
      sound_play(SFX_NEW);
      glitch_start(8);
      if (screen == BENCH) {
        header(0);
        bench_view();
      }
    }
  } /* the partner's find lands here too */
  else if (ev == LINK_EV_END) {
    bust_hide();
    sky_mode(0);
    cells_reset();
    arrow(0);
    screen = LINKEND;
    link_result_draw();
    glitch_start(24);
  } else if (ev && screen == MENU)
    menu_link_refresh();
}
static void talk_enter(void) {
  bust_id = talk_bust();
  bust_view = 0;
  bust_hold = 1;
  bust_open();
  if (bust_id == AVATAR) avatar_glitch(1);
  bust_glitch = 16;
  pal_sp(1, GLITCH);
  glitch_start(10);
  sound_play(SFX_SWAP);
}
/* an encounter the director started (the view has panned to it): a champion, or a visitor */
static void encounter(uint8_t e) {
  if (e == FLOW_FIGHT)
    fight_go(flow_arg & 15u, talk_saga()->chapter, flow_arg >> 7);
  else {
    talk_go(BENCH, 0);
    talk_open((uint16_t)(core.rng ^ sys_time), (uint8_t)(clock & 3u));
    talk_enter();
  }
  SCY_REG = 0;
}
/* ---- shelf: grouped by category; up/down jump between groups ---- */
/* The shelf, the slots, the result preview and the name sign are the core's (cru_bench_*); this draws them. */
static void cursor(uint8_t x, uint8_t y, uint8_t tile) {
  set_sprite_tile(OAM_CURSOR, tile);
  set_sprite_prop(OAM_CURSOR, 0);
  move_sprite(OAM_CURSOR, x, y);
}
static void arrow(uint8_t show) {
  if (show)
    cursor(80u + 4u, (uint8_t)(72u + 16u + ((clock >> 4) & 1u)), SPR_ARROW0);
  else
    move_sprite(OAM_CURSOR, 0, 0);
}
/* the cue rows (A choose, B back, START menu, SELECT filter): crucible_flow.c */
#define cues() flow_cues(screen)
/* the six cells and the name sign: slots, result and the focus's neighbours play their resting loops, the focus turns */
static void bench_view(void) {
  crucible_cells v;
  uint8_t c;
  cru_bench_cells(&core, &v);
  /* Identity feedback precedes palette/cache work and remains visible while
  * every art job is queued, partial or unavailable. Outcome has its own row. */
  crucible_get_name(core.focus, label);
  field(1, 15, 18, label, T_CREAM);
  flow_sign(v.message, v.sign);
  crucible_art_cursor(core.focus, 0);
  reveal_predict(core.slot_a, core.focus);
  for (c = 0; c < CELLS; c++) cell_set(c, v.kind[c], v.id[c], c == CF, 1);
  {
    uint16_t ids[CELLS];
    uint8_t n = 0;
    for (c = 0; c < CELLS; c++)
      if (v.kind[c] == CRU_K_ITEM) ids[n++] = v.id[c];
    crucible_art_visible(ids, n);
  }
}
static void draw_bench(void) {
  music_mood(1);
  sky_mode(1);
  scene_draw(SCENE_BENCH);
  header(0);
  cells_place(0);
  cells_reset();
  bench_view();
  cues();
  clouds_setup();
  hide_sprites = 0;
}
/* ---- merge: the real bodies meet, join into one, flicker between fused and new, flash, resolve ---- */
static const uint8_t smooth[17] = {0, 1, 4, 9, 16, 24, 33, 43, 54, 64, 75, 84, 92, 99, 104, 107, 108};
static uint16_t ease(uint16_t a, uint16_t b, uint8_t k, uint8_t n) {
  int16_t d = (int16_t)b - (int16_t)a;
  uint8_t q;
  if (k >= n) return b;
  q = smooth[(uint8_t)((uint16_t)k * 16u / n)];
  return (uint16_t)((int16_t)a + (int16_t)(d * (int16_t)q / 108));
}
static void place_pair(int16_t ax, int16_t bx, int16_t y) {
  fusion_place(0, ax, y, 1);
  fusion_place(1, bx, y, 2);
}
/* cru_bench_a decided the outcome (and rolled a NEW result's tint); the animation plays from core.mix */
static void begin_merge(void) {
  mix_a = core.mix.a;
  mix_b = core.mix.b;
  result = core.mix.result;
  outcome = core.mix.outcome;
  merge_fuse = outcome == CRU_NEW ? 24u : outcome == CRU_ROUTE ? 18u : 12u;
  appr = 8u;
  /* the merge starts from each object's keyframe: no stream to decode, so the mix never stalls */
  crucible_art(CRUCIBLE_ART_KEY, mix_a, 0, 0, VARIANT(mix_a), 0, 255u, CRUCIBLE_ART_CAPTURE);
  crucible_art(CRUCIBLE_ART_KEY, mix_b, 0, 0, VARIANT(mix_b), 16, 255u, CRUCIBLE_ART_CAPTURE);
  if (result != CRU_NONE) {
    crucible_art(CRUCIBLE_ART_KEY, result, 0, 0, core.mix.variant, 32, 255u, CRUCIBLE_ART_CAPTURE);
    pal_item(3, result, 1);
  } else
    fusion_union();
  reveal_prepare(outcome == CRU_NEW || outcome == CRU_ROUTE ? result : CRU_NONE,
                 core.mix.variant); /* its turntable decodes through the merge */
  pal_item(1, mix_a, 1);
  pal_item(2, mix_b, 1);
  fusion_frame(0, STAGE_X, STAGE_Y, 16);
  fusion_hide();
  arrow(0);
  /* Sprites appear next frame, once their palettes reach the hardware in VBlank. */
  clouds_draw(0);
  toast_cut(0);
  LCDC_REG |= LCDCF_OBJON;
  hide_sprites = 0;
  screen = MERGE;
  t = 0;
  music_mood(3);
  sound_play(SFX_MIX);
}
static void burst(uint8_t age) {
  uint8_t i, xy[2];
  for (i = 0; i < BURST_PARTS; i++) {
    set_sprite_tile(i, age < 14u ? SPR_SPARK0 : age < 28u ? SPR_SPARK1 : SPR_SPARK2);
    set_sprite_prop(i, 4);
    if (age < BURST_FRAMES) {
      crucible_burst_xy(age, i, xy);
      move_sprite(i, xy[0] + 4u, xy[1] + 12u + 8u);
    } else
      move_sprite(i, 0, 0);
  }
}
static void motes(uint8_t on) {
  uint8_t i, x, y;
  for (i = 0; i < 2u; i++) {
    set_sprite_tile(OAM_MOTE + i, SPR_BUBBLE1);
    set_sprite_prop(OAM_MOTE + i, 4);
    if (on) {
      x = STAGE_X - 4u + i * 6u + (uint8_t)(((t >> 2) + i * 3u) % 5u);
      y = (uint8_t)(STAGE_Y + 12u - ((t * 2u + i * 11u) % 24u));
      move_sprite(OAM_MOTE + i, x + 8u, y + 16u);
    } else
      move_sprite(OAM_MOTE + i, 0, 0);
  }
}
static void draw_reveal(void) {
  char n[10], b[12];
  uint8_t len;
  scene_draw(SCENE_REVEAL);
  header(0);
  cells_place(0);
  cells_reset();
  field(2, 10, 16, outcome == CRU_NEW ? "NEW DISCOVERY" : outcome == CRU_ROUTE ? "NEW RECIPE" : "KNOWN", T_BRASS);
  crucible_get_name(result, label);
  field(2, 11, 16, label, T_CREAM);
  crucible_get_category(result, label);
  field(2, 12, 16, label, T_BRASS);
  cell_set(CB, K_ITEM, result, 1, 1);
  crucible_get_name(mix_a, label);
  crucible_get_name(mix_b, b);
  len = strlen(label);
  if (len + 3u + strlen(b) <= 16u) {
    strcpy(label + len, " + ");
    strcpy(label + len + 3u, b);
  }
  field(2, 13, 16, label, T_BRASS);
  if (awarded) {
    n[0] = '+';
    number(n + 1, awarded, awarded >= 10u ? 2u : 1u);
    strcat(n, " PTS");
    field(2, 14, 16, n, T_CREAM);
  } else
    field(2, 14, 16, "", T_CREAM);
  cues();
}
/* the core commits the mix: ownership, tried bits, points, feats and the save (a known or empty pair lands on the bench) */
static void finish_merge(void) { awarded = cru_mix_finish(&core); }
/* Timeline in real frames (sys_time), so a slow render step skips poses instead of slowing the merge. */
/* alternation: holds 6,4,3,2 plus 1..4 swaps of 4 frames each */
#define ALT_FRAMES 28u
static uint16_t prev_t;
static uint8_t crossed(uint16_t at) { return prev_t < at && t >= at; }
static void tick_merge(void) {
  uint8_t step, k, round, swaps, used, show_new, j, A = appr;
  uint16_t end_fuse = A + merge_fuse, end_alt = end_fuse + (outcome == CRU_NEW ? ALT_FRAMES : 0u);
  if (t == 0u) {
    t = 1;
    prev_t = 0;
    place_pair(8, 64, 32);
    cell_set(CA, K_BLANK, 0, 0, 0);
    cell_set(CB, K_BLANK, 0, 0, 0);
    cell_set(CR, K_BLANK, 0, 0, 0);
    return;
  }
  if (t <= A)
    fade((uint8_t)(t * 11u / A), DUSK, 0, 0);
  else if (t < end_alt)
    fade(11, DUSK, 0, 0);
  if (t <= A) {
    if (outcome == CRU_NEW || outcome == CRU_ROUTE) {
      crucible_art_urgent = 1;
      crucible_art_tick();
    }
    place_pair((int16_t)ease(8, STAGE_X - 32u, (uint8_t)t, A), (int16_t)ease(64, STAGE_X, (uint8_t)t, A), 32);
    return;
  }
  if (outcome == CRU_NOTHING) {
    /* They touch, half-join and part: nothing new forms, and the first pick stays on the bench. */
    k = t - A > 255u ? 255u : (uint8_t)(t - A);
    if (k <= 28u) {
      step = k < 14u ? k / 3u : (28u - k) / 3u;
      fusion_frame(step, STAGE_X, STAGE_Y, (uint8_t)(16u - step));
      if (crossed(30u)) sound_play(SFX_NOTHING);
      return;
    }
    k -= 28u;
    fade((uint8_t)(k < 16u ? (16u - k) * 11u / 16u : 0u), DUSK, 0, 0);
    place_pair((int16_t)ease(STAGE_X - 32u, 8, k, 16), (int16_t)ease(STAGE_X, 64, k, 16), 32);
    if (k >= 16u) {
      uint16_t a = core.mix.a, b = core.mix.b;
      fade(0, 0, 0, 0);
      finish_merge();
      fusion_hide();
      screen = BENCH;
      music_mood(1);
      clouds_setup();
      bench_view();
      cues();
      arrow(1);
      story_react(story_after_mix(a, b, 0));
    }
    return;
  }
  if (t <= end_fuse) {
    step = (uint8_t)((uint16_t)(t - A) * FUSION_STEPS / merge_fuse);
    fusion_frame(step, STAGE_X, STAGE_Y, (uint8_t)(16u - step));
    if (outcome == CRU_KNOWN) {
      motes(0);
      return;
    }
    if (((prev_t - A) >> 3) != ((t - A) >> 3)) {
      sound_play(SFX_BUBBLE);
      sound_drone(step);
    }
    /* the two ingredients' voices answer each other as they fuse */
    if (((prev_t - A) >> 4) != ((t - A) >> 4))
      sound_voice(((t - A) >> 4) & 1u ? mix_b : mix_a, crucible_category(((t - A) >> 4) & 1u ? mix_b : mix_a), 5,
                  (uint8_t)(step >> 1));
    motes(1);
    return;
  }
  motes(0);
  /* Something already on the shelf: no flash, no card. The result lands in the slot with its own chime and the
  * first pick stays, so the next pair is one press away. */
  if (outcome == CRU_KNOWN) {
    fusion_hide();
    fade(0, 0, 0, 0);
    finish_merge();
    screen = BENCH;
    music_mood(1);
    sky_mode(1);
    clouds_setup();
    bench_view();
    cues();
    arrow(1);
    sound_play(SFX_KNOWN);
    glitch_start(6);
    story_react(story_after_mix(result, result, 1));
    return;
  }
  if (t < end_alt) {
    /* Fused body and new form alternate as glowing silhouettes: holds shrink while bursts lengthen. */
    crucible_art_urgent = 1;
    crucible_art_tick();
    (void)crucible_overlay_ahead(result);
    k = (uint8_t)(t - end_fuse);
    used = 0;
    show_new = 0;
    if (prev_t <= end_fuse) {
      pal_sp(1, GLOW);
      pal_sp(2, GLOW);
      pal_sp(3, GLITCH);
      pal_sp(6, GLOW);
      fade(14, DUSK, 0, 0);
    }
    for (round = 0; round < 4u; round++) {
      swaps = round + 1u;
      if (k < used + holds[round]) break;
      used += holds[round];
      if (k < used + swaps * 2u) {
        j = (uint8_t)(k - used);
        show_new = (j & 1u) == 0u;
        if (crossed(end_fuse + used + j))
          sound_voice(show_new ? result : mix_a, crucible_category(show_new ? result : mix_a), 5,
                      (uint8_t)(round * 4u + j));
        break;
      }
      used += swaps * 2u;
    }
    if (show_new) {
      fusion_place(2, STAGE_X - 16u, STAGE_Y - 16u, 3);
      if (rnd8() & 1u) glitch_rows(0);
      for (j = 16; j < 32u; j++) move_sprite(j, 0, 0);
    } else
      place_pair(STAGE_X - 16u, STAGE_X - 16u, STAGE_Y - 16u);
    return;
  }
  /* the result turns out of the glitch until its turntable and overlay are decoded (crucible_reveal.c); then the flash */
  if (!(k = reveal_turn())) return;
  k--;
  if (k > 8u) k = 8u;
  fade(16, WHITE, (uint8_t)(k * 2u), WHITE);
  if (!k) sound_play(SFX_FLASH);
  if (k >= 8u) {
    uint16_t p[12];
    fusion_hide();
    finish_merge();
    crucible_ui_palettes(p);
    pal_sp(6, p + 4);
    screen = REVEAL;
    t = 0;
    draw_reveal();
    glitch_start(12);
  }
}
static void back_to_bench(void) {
  egg_made(result);
  if (outcome == CRU_NEW) link_found(result);
  talk_notice(result);
  burst(BURST_FRAMES);
  cru_reveal_close(&core);
  screen = BENCH;
  fade(0, 0, 0, 0);
  draw_bench();
  arrow(1);
  story_react(story_after_mix(result, result, 1));
}
static void tick_reveal(void) {
  uint16_t before = t;
  t += dt > 2u ? 2u : dt;
  if (t > 255u) t = 255u;
  if (before == 0u) {
    music_mood(0);
    sound_voice(result, crucible_category(result), outcome == CRU_NEW ? 2u : outcome == CRU_ROUTE ? 3u : 4u, 0);
  }
  fade(t < 24u ? (uint8_t)(16u - (t * 16u / 24u)) : 0u, WHITE, t < 24u ? (uint8_t)(16u - (t * 16u / 24u)) : 0u, WHITE);
  burst(t <= BURST_FRAMES && outcome != CRU_KNOWN ? (uint8_t)(t - 1u) : BURST_FRAMES);
  if (t > 20u && (pressed & (J_A | J_B))) {
    sound_play(SFX_CLOSE);
    back_to_bench();
  }
}
/* ---- book: the specimen plate on the left page, the numbered ledger on the right ---- */
static void book_fill(void) { book_n = cru_book_count(&core); }
static void book_list(void) {
  uint8_t i;
  uint16_t at, id;
  cru_book_rows_from(&core, book_top, book_ids, LIST_ROWS);
  for (i = 0; i < LIST_ROWS; i++) {
    at = book_top + i;
    id = at < book_n ? book_ids[i] : CRU_NONE;
    if (id == CRU_NONE)
      text(10, 2u + i, " ", 9, T_INK);
    else if (OWNED(id)) {
      crucible_get_name(id, label);
      text(10, 2u + i, " ", 9, T_INK);
      text(10, 2u + i, label, (uint8_t)(strlen(label) > 9u ? 9u : strlen(label)), T_INK);
    } else
      text(10, 2u + i, "---------", 9, T_DIM);
  }
  cursor(10u * 8u - 6u + 8u, (uint8_t)((2u + book_at - book_top) * 8u + 16u), SPR_ARROW_R);
}
/* the plate: the specimen turning, its name, category, catalogue number (native id, stable as the catalogue grows)
 * and the first recipe tried that makes it */
static void book_detail(void) {
  uint8_t k;
  uint16_t id = book_ids[book_at - book_top], ab[2];
  char a[13], n[8];
  n[0] = 'N';
  n[1] = 'O';
  n[2] = '.';
  number(n + 3, id + 1u, digits());
  if (OWNED(id)) {
    crucible_get_name(id, label);
    field(1, 8, 9, label, T_INK);
    field(1, 9, 9, strlen(label) > 9u ? (const char *)label + 9 : "", T_INK);
    crucible_art_cursor(id, 1);
    cell_set(CA, K_ITEM, id, 1, 1);
    crucible_get_category(id, label);
    field(1, 10, 9, label, T_DIM);
    field(1, 11, 9, n, T_DIM);
    k = cru_book_route(&core, id, ab);
    if (k == CRU_BOOK_RECIPE) {
      crucible_get_name(ab[0], a);
      field(1, 12, 9, a, T_INK);
      crucible_get_name(ab[1], label);
      a[0] = '+';
      strcpy(a + 1, label);
      field(1, 13, 9, a, T_INK);
    } else {
      field(1, 12, 9, k == CRU_BOOK_ELEMENT ? "ELEMENT" : "SHARED", T_DIM);
      field(1, 13, 9, "", T_DIM);
    }
  } else {
    field(1, 8, 9, "UNKNOWN", T_DIM);
    field(1, 9, 9, "", T_DIM);
    cell_set(CA, K_QUESTION, 0, 0, 0);
    field(1, 10, 9, "", T_DIM);
    field(1, 11, 9, n, T_DIM);
    field(1, 12, 9, "", T_DIM);
    field(1, 13, 9, "", T_DIM);
  }
}
static void draw_book(void) {
  uint8_t w;
  char c[10];
  music_mood(2);
  scene_draw(SCENE_BOOK);
  /* header: BOOK, or the active filter, with found/total of the shown rows */
  if (core.filter) {
    filter_label(core.filter, label);
    header(label);
    w = count(c, cru_filter_found(&core, core.filter), cru_filter_total(&core, core.filter));
    text(13, 0, " ", 5, T_CREAM);
    text(18u - w, 0, c, w, T_CREAM);
  } else
    header("BOOK");
  cells_place(1);
  cells_reset();
  book_list();
  book_detail();
  cues();
}
/* ---- menu: borrows object palettes 1, 3 and 5 for the chrome logo, the marble bust and the katakana ---- */
static void open_menu(uint8_t title) {
  uint16_t p[12];
  screen = MENU;
  room(0);
  arrow(0);
  fusion_hide();
  burst(BURST_FRAMES);
  cells_reset();
  scene_menu_palettes(p);
  pal_bg(1, p);
  pal_bg(3, p + 4);
  pal_bg(5, p + 8);
  music_mood(2);
  hide_sprites = 0;
  sky_mode(2);
  menu_open(title);
  bands_setup();
  bust_id = (uint8_t)(rnd8() % CRUCIBLE_BUSTS);
  bust_view = 0;
  bust_open();
}
static void open_book(uint8_t back) {
  bust_hide();
  sky_mode(0);
  screen = BOOK;
  book_back = back;
  book_fill();
  book_at = cru_book_index(&core, core.focus);
  if (book_at == CRU_NONE) book_at = 0;
  book_top = book_at >= LIST_ROWS / 2u ? book_at - LIST_ROWS / 2u : 0;
  if (book_top + LIST_ROWS > book_n) book_top = book_n > LIST_ROWS ? book_n - LIST_ROWS : 0;
  cru_book_open(&core);
  room(0);
  arrow(0);
  light = 0;
  plaster = crucible_plaster(0);
  pal_changed = 1;
  draw_book();
}
static void open_filter(uint8_t back) {
  screen = FILTER;
  filter_back = back;
  room(0);
  arrow(0);
  cells_reset();
  sky_mode(0);
  filter_open();
}
static void to_bench(void) {
  bust_hide();
  flow_back(0);
  screen = BENCH;
  light = 0;
  plaster = crucible_plaster(0);
  pal_changed = 1;
  draw_bench();
  arrow(1);
  flow_back(1);
}
static void book_move(int16_t d) {
  int16_t n = (int16_t)book_at + d;
  if (n >= (int16_t)book_n) n = (int16_t)book_n - 1;
  if (n < 0) n = 0;
  if (n == (int16_t)book_at) return;
  book_at = (uint16_t)n;
  if (book_at < book_top) book_top = book_at;
  if (book_at >= book_top + LIST_ROWS) book_top = book_at - LIST_ROWS + 1u;
  sound_play(SFX_MOVE);
  book_list();
  book_detail();
  cru_book_view(&core);
}
/* ---- input: press, then repeat after 15 frames every 5 (handheld menu convention); presses are latched in
 * VBlank (the only edge detector: a second one here would count a press twice when it lands between the VBlank
 * sample and this read), so a slow frame never swallows one ---- */
static void read_input(void) {
  uint8_t dir;
  held = joypad();
  pressed = input_take();
  dir = held & (J_LEFT | J_RIGHT | J_UP | J_DOWN);
  if (dir && dir == repeat_dir) {
    if (++repeat_frames >= REPEAT_DELAY + REPEAT_EVERY) {
      repeat_frames = REPEAT_DELAY;
      pressed |= dir;
    }
  } else {
    repeat_dir = dir;
    repeat_frames = 0;
  }
  cru_stir(&core, DIV_REG + held);
}
static void tick_bench(void) {
  uint8_t d = (pressed & J_LEFT)    ? CRU_LEFT
              : (pressed & J_RIGHT) ? CRU_RIGHT
              : (pressed & J_UP)    ? CRU_UP
              : (pressed & J_DOWN)  ? CRU_DOWN
                                    : 0u;
  if (d && cru_bench_move(&core, d)) {
    sound_voice(core.focus, crucible_category(core.focus), 0, 0);
    bench_view();
  }
  if (pressed & J_A) {
    d = cru_bench_a(&core, DIV_REG);
    if (d == CRU_PICKED) {
      sound_voice(core.slot_a, crucible_category(core.slot_a), 1, 0);
      bench_view();
      cues();
    } else if (d == CRU_DENY)
      sound_play(SFX_DENY);
    else {
      begin_merge();
      return;
    }
  }
  if ((pressed & J_B) && (cru_bench_b(&core) || flow_unfilter())) {
    sound_play(SFX_UNDO);
    header(0);
    bench_view();
    cues();
  }
  if (pressed & J_START) {
    sound_play(SFX_OPEN);
    open_menu(0);
    return;
  }
  if (pressed & J_SELECT) {
    sound_play(SFX_OPEN);
    open_filter(BENCH);
    return;
  }
  arrow(1);
}
static uint8_t load_k; /* a game start waiting behind the sign loader (crucible_sky.c) */
static void tick_menu(void) {
  uint8_t k;
  if (load_k) {
    if (!sky_loader_tick()) return;
    k = load_k;
    load_k = 0;
  } else {
    k = menu_tick(pressed);
    if (k == MENU_FREE || k == MENU_STORY_LOAD || k == MENU_STORY_NEW) {
      load_k = k;
      bust_hide();
      sky_loader_open();
      return;
    }
  }
  if (k == MENU_RESUME)
    to_bench();
  else if (k == MENU_FREE) {
    story_leave();
    to_bench();
  } else if (k == MENU_LINK_GO) {
    link_bring();
    link_go();
    to_bench();
    glitch_start(16);
  } else if (k == MENU_SEND) {
    link_gift(core.focus);
    sound_play(SFX_NEW);
    to_bench();
  } else if (k == MENU_STORY_LOAD) {
    story_leave();
    story_resume(menu_slot);
    to_bench();
  } else if (k == MENU_STORY_NEW) {
    story_leave();
    talk_go(MENU, 1);
    talk_intro((uint16_t)(sys_time ^ core.rng));
    talk_enter();
  } else if (k == MENU_BOOK)
    open_book(MENU);
  else if (k != MENU_STAY) {
    bust_hide();
    sky_mode(0);
    screen = RECORDS;
    cells_reset();
    records_open(k == MENU_TITLES ? 1u : 2u);
  }
}
static void tick_filter(void) {
  uint8_t k = filter_tick(pressed);
  if (!k) return;
  cru_filter_close(&core);
  if (filter_back == BOOK)
    open_book(book_back);
  else
    to_bench();
}
static void tick_book(void) {
  if (pressed & J_SELECT) {
    sound_play(SFX_OPEN);
    open_filter(BOOK);
    return;
  }
  if (pressed & J_UP)
    book_move(-1);
  else if (pressed & J_DOWN)
    book_move(1);
  else if (pressed & J_LEFT)
    book_move(-(int16_t)LIST_ROWS);
  else if (pressed & J_RIGHT)
    book_move(LIST_ROWS);
  if (pressed & (J_B | J_START)) {
    sound_play(SFX_CLOSE);
    if (book_back == MENU)
      open_menu(0);
    else
      to_bench();
  } else if (pressed & J_A) {
    if (book_n && cru_book_use(&core, book_ids[book_at - book_top])) {
      sound_voice(core.focus, crucible_category(core.focus), 1, 0);
      to_bench();
    } else
      sound_play(SFX_DENY);
  }
}
void crucible_run(void) BANKED {
  uint16_t last, chrome[12], room_pal[16];
  uint8_t i, audio_scene = 255u;
  DISPLAY_OFF;
  hide_sprites = 1;
  HIDE_SPRITES;
  HIDE_WIN;
  VBK_REG = 0;
  /* The serial interrupt stays off: this cartridge drives the link port itself (crucible_link.c). */
  IE_REG &= ~SIO_IFLAG;
  crucible_load_font();
  crucible_load_ui();
  save_boot();
  egg_boot();
  sound_init();
  crucible_ui_palettes(chrome);
  scene_palettes(room_pal);
  plaster = room_pal[0];
  for (i = 0; i < 8u; i++) {
    pal_bg(i, room_pal);
    pal_sp(i, chrome + 8);
  }
  pal_bg(PAL_SKY, room_pal + 4);
  pal_bg(PAL_WOOD, room_pal + 8);
  pal_bg(PAL_MUTED, room_pal + 12);
  pal_bg(6, chrome + 4);
  pal_sp(6, chrome + 4);
  pal_sp(4, SPARK);
  pal_sp(5, CANDLE);
  pal_sp(7, CLOUD);
  pal_lease_init();
  prepare_palettes();
  set_bkg_palette(0, 8, out_bg);
  set_sprite_palette(0, 8, out_sp);
  pal_ready = 0;
  screen = BENCH;
  ENABLE_OAM_DMA;
  SPRITES_8x8;
  for (i = 0; i < 40u; i++) move_sprite(i, 0, 0);
  rng16 ^= core.rng | 1u;
  for (i = 0; i < CLOUDS; i++) cloud_spawn(i, 1);
  lcd_install();
  /* Power-on shows the title card; CONTINUE opens the bench. */
  open_menu(1);
  prepare_palettes();
  set_bkg_palette(0, 8, out_bg);
  set_sprite_palette(0, 8, out_sp);
  pal_ready = 0;
  SHOW_BKG;
  DISPLAY_ON;
  SHOW_SPRITES;
  last = sys_time;
  anim = 0;
  clock = 0;
  while (1) {
    vsync();
    if (pal_ready) {
      set_bkg_palette(0, 8, out_bg);
      set_sprite_palette(0, 8, out_sp);
      pal_ready = 0;
    }
    cells_vblank();
    dt = (uint8_t)((uint16_t)(sys_time - last) > 8u ? 8u : (uint16_t)(sys_time - last));
    anim = (anim + dt) % LOOP_FRAMES;
    last = sys_time;
    clock++;
    if (screen == MERGE) {
      prev_t = t;
      if (t) t += dt;
    } /* real elapsed frames: decoding never stretches the choreography */
    /* the turntable runs on real time (frames elapsed), one view per step, so a busy frame never slows it */
    rot_clock = (uint8_t)(rot_clock + dt);
    if (rot_clock >= turn_ticks[(core.options >> 3) & 3u]) {
      rot_clock = (uint8_t)(rot_clock - turn_ticks[(core.options >> 3) & 3u]);
      if (rot_clock >= turn_ticks[(core.options >> 3) & 3u]) rot_clock = 0;
      if (++rot_step >= CRUCIBLE_VIEWS) rot_step = 0;
    }
    {
      uint8_t a = (uint8_t)(screen | (link_on ? 0x80u : 0u));
      if (audio_scene != a) {
        audio_scene = a;
        music_scene(screen);
      }
    } /* the screen picks the song; a cable opened or closed on the same screen (the lobby) too */
    sound_tick();
    read_input();
    /* the classic soft reset: A+B+START+SELECT together restarts the cartridge (the save is untouched) */
    if ((held & (J_A | J_B | J_START | J_SELECT)) == (J_A | J_B | J_START | J_SELECT)) reset();
    cru_tick(&core, dt);
    if (link_on) {
      link_event(link_poll(dt));
      if (link_started && screen == BENCH && !(clock & 31u)) link_hud();
    }
    if (story_on && screen == BENCH) story_hud(dt, 0);
    egg_input(pressed, screen == MENU, dt);
    if ((screen == MENU || screen == BENCH) && !glitch_t) {
      uint8_t e = egg_take();
      if (e) { /* a secret: its visitor steps in */
        pal_changed = 1;
        talk_back = screen;
        bust_hide();
        sky_mode(0);
        screen = TALK;
        cells_reset();
        arrow(0);
        talk_egg((uint8_t)(e - 1u));
        talk_enter();
      }
    }
    {
      uint8_t e = flow_tick(screen, &pressed);
      if (e) encounter(e);
    } /* encounters come to the bench (crucible_flow.c) */
    pressed = room_tick(screen, pressed); /* the living room: palette states, life, secrets */
    if (screen == TALK) {
      if (talk_out) {
        bust_tick();
        if (talk_out > dt)
          talk_out -= dt;
        else {
          uint8_t kind = talk_kind;
          talk_out = 0;
          talk_kind = 0;
          bust_hold = 0;
          bust_hide();
          bust_id = (uint8_t)(rnd8() % CRUCIBLE_BUSTS);
          if (kind == 1u) {
            talk_wait();
            wait_vbl_done();
            story_begin(menu_slot, talk_intro_scale(), talk_intro_hash(), talk_intro_name());
            to_bench();
          } /* the run begins */
          else if (kind == 2u) {
            story_end();
            open_menu(1);
          } /* lost for good */
          else if (talk_back == BENCH) {
            talk_back = MENU;
            story_save();
            to_bench();
          } else {
            story_save();
            open_menu(core.made ? 0u : 1u);
          }
        }
      } else if (talk_tick(pressed)) { /* it glitches out before the screen changes */
        talk_out = 22;
        if (bust_id == AVATAR) avatar_glitch(0);
        bust_glitch = 22;
        pal_sp(1, GLITCH);
        glitch_start(12);
      } else {
        if (talk_tear()) {
          bust_glitch = 12;
          pal_sp(1, GLITCH);
        }
        bust_tick();
      }
    } else if (screen == FIGHT)
      tick_fight();
    else if (screen == LINKEND) {
      if (pressed & J_A) {
        link_close();
        open_menu(1);
      }
    } else if (screen == BENCH)
      tick_bench();
    else if (screen == MERGE)
      tick_merge();
    else if (screen == REVEAL)
      tick_reveal();
    else if (screen == BOOK)
      tick_book();
    else if (screen == MENU)
      tick_menu();
    else if (screen == FILTER)
      tick_filter();
    else if (records_tick(pressed))
      open_menu(0);
    if (screen != MERGE)
      crucible_art_tick(); /* the merge has the CPU to itself (its turn decodes the result: crucible_reveal.c) */
    glitch_tick();
    {
      uint8_t h = feats_toast(screen == BENCH && sky == 1u);
      if (sky == 1u) toast_cut(h);
    }
    if (screen == BENCH || screen == MERGE || screen == REVEAL)
      room(1);
    else if (screen == MENU) {
      if (!load_k) bust_tick();
      bands_tick(dt);
    }
    cells_update(screen == MERGE ? 0u : 2u);
    crucible_overlay_tick(screen, focus_load);
    {
      uint8_t c, u = 0;
      for (c = 0; c < CELLS; c++)
        if (cells[c].kind == K_ITEM && (cells[c].rotate || cells[c].animate) && !cells[c].ready) u = 1;
      crucible_art_urgent = u;
    }
    /* Also restore an unexpected scene exit (e.g. a link-end event); normal
   * fight_end already restored it. The existing scene byte avoids a poll. */
    if (audio_scene == FIGHT && screen != FIGHT) pal_lease_end();
    prepare_palettes();
  }
}
