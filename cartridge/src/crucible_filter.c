/* Filters (SELECT on the bench and in the book): one screen, one grid. ALL and the seven types in the left column,
 * the twelve traits in the right, all twenty on screen at once. The arrows move the cursor (LEFT/RIGHT switch columns),
 * the row under it lights up and the line below says how many of its things are found out of how many exist, so a
 * filter is also a hint ("HOT 3/9"). A shows only that, B goes back unchanged, SELECT clears it (ALL). The star marks the
 * filter in use, which the bench and book headers also show; B on the bench (nothing held) clears it too. The choice
 * narrows the bench shelf and the recipe book and is kept with the save. The counts and the rules are the core's
 * (cru_filter_*); this file draws the grid. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_data.h"
#include "crucible_ui.h"
#include "crucible_scene.h"
#include "crucible_state.h"
#define T_INK 0u
#define T_DIM 8u
#define T_CREAM 7u
#define T_BRASS 15u
#define TYPES 8u /* ALL + seven types: the left column; traits fill the right */
#define Y0 2u
static const char trait_names[CRUCIBLE_TRAITS][6] = {"HOT",   "COLD",  "WET",   "AIRY", "STONE", "SHINY",
                                                     "GLOWS", "ALIVE", "GREEN", "MADE", "BIG",   "MAGIC"};
static uint8_t at, kit;
uint8_t kit_open(uint8_t where) BANKED; /* crucible_fight_kit.c */
uint8_t kit_tick(uint8_t pressed) BANKED;
/* Local text helpers: literals live in this bank, so they must not be passed to code in another bank. */
static void put_(uint8_t x, uint8_t y, uint8_t tile, uint8_t attr) {
  VBK_REG = 1;
  set_bkg_tiles(x, y, 1, 1, &attr);
  VBK_REG = 0;
  set_bkg_tiles(x, y, 1, 1, &tile);
}
static void text_(uint8_t x, uint8_t y, const char *s, uint8_t width, uint8_t attr) {
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
void filter_label(uint8_t f, char *out) BANKED {
  if (!f)
    strcpy(out, "ALL");
  else if (f < 8u)
    crucible_category_name(f - 1u, out);
  else
    strcpy(out, trait_names[f - 8u]);
}
/* where filter f sits: column x (its label; the star one left, the cursor two left) and row y */
static uint8_t fx(uint8_t f) { return f < TYPES ? 3u : 12u; }
static uint8_t fy(uint8_t f) { return (uint8_t)(Y0 + (f < TYPES ? f : f - TYPES)); }
/* one entry: the cursor's row lit (cream), else ink when any is found, dim when none */
static void entry(uint8_t f) {
  char s[12];
  uint8_t x = fx(f), y = fy(f);
  text_(x - 1u, y, " ", 8, T_INK);
  if (f == core.filter) put_(x - 1u, y, UI_STAR, T_INK);
  filter_label(f, s);
  text_(x, y, s, (uint8_t)strlen(s), f == at ? T_CREAM : core.found[f] ? T_INK : T_DIM);
}
static void cursor(void) {
  set_sprite_tile(39, SPR_ARROW_R);
  set_sprite_prop(39, 0);
  move_sprite(39, (uint8_t)((fx(at) - 2u) * 8u + 8u), (uint8_t)(fy(at) * 8u + 16u));
}
/* the line under the grid: what is under the cursor, found/total */
static void detail(void) {
  char s[20], n[6];
  uint16_t f = core.found[at], t = core.total[at];
  filter_label(at, s);
  if (!f)
    strcat(s, ": NONE YET");
  else {
    strcat(s, " ");
    ui_number(n, f, f >= 1000u ? 4u : f >= 100u ? 3u : 2u);
    strcat(s, n);
    strcat(s, "/");
    ui_number(n, t, t >= 1000u ? 4u : t >= 100u ? 3u : 2u);
    strcat(s, n);
  }
  text_(1, 14, " ", 18, T_DIM);
  text_((uint8_t)(1u + (18u - strlen(s)) / 2u), 14, s, (uint8_t)strlen(s), T_DIM);
  cursor();
}
void filter_open(void) BANKED {
  uint8_t f;
  char l[10]; /* crucible_category_name writes 10 bytes */
  scene_draw(SCENE_RECORDS);
  at = core.filter < FILTERS ? core.filter : 0u;
  text_(1, 0, " ", 18, T_CREAM);
  text_(1, 0, "FILTER", 6, T_CREAM);
  filter_label(core.filter, l);
  text_((uint8_t)(19u - strlen(l)), 0, l, (uint8_t)strlen(l), T_BRASS);
  text_(1, 1, " ", 18, T_BRASS);
  text_(3, 1, "TYPE", 4, T_BRASS);
  text_(12, 1, "TRAIT", 5, T_BRASS);
  for (f = 0; f < FILTERS; f++) entry(f);
  text_(0, 16, " ", 20, T_CREAM);
  put_(0, 16, UI_A, T_CREAM);
  text_(1, 16, "SHOW", 4, T_CREAM);
  put_(6, 16, UI_B, T_CREAM);
  text_(7, 16, "BACK", 4, T_CREAM);
  text_(13, 16, "SE ALL", 6, T_BRASS);
  text_(0, 17, " ", 20, T_CREAM);
  text_(1, 17, "ST TO KIT", 9, T_BRASS);
  kit = 0;
  detail();
}
/* Returns 0 while open, 1 when a filter was applied (SELECT: ALL), 2 when closed unchanged. */
uint8_t filter_tick(uint8_t pressed) BANKED {
  uint8_t was = at, r;
  if (kit) return kit_tick(pressed) ? 2u : 0u; /* the kit screen, opened from here (START: TO KIT) */
  if (pressed & J_START) {
    move_sprite(39, 0, 0);
    sound_play(SFX_OPEN);
    kit = 1;
    kit_open(0);
    return 0;
  }
  if (pressed & J_B) {
    move_sprite(39, 0, 0);
    sound_play(SFX_CLOSE);
    return 2;
  }
  if (pressed & J_SELECT) {
    cru_filter_apply(&core, 0);
    move_sprite(39, 0, 0);
    sound_play(SFX_UNDO);
    return 1;
  }
  if (pressed & J_A) {
    if (!cru_filter_apply(&core, at)) {
      sound_play(SFX_DENY);
      return 0;
    }
    move_sprite(39, 0, 0);
    sound_play(SFX_OPEN);
    return 1;
  }
  r = at < TYPES ? at : (uint8_t)(at - TYPES);
  if (pressed & J_UP) {
    if (!r) return 0;
    at--;
  } else if (pressed & J_DOWN) {
    if (at == TYPES - 1u || at + 1u >= FILTERS) return 0;
    at++;
  } else if (pressed & (J_LEFT | J_RIGHT)) {
    at = at < TYPES ? (uint8_t)(TYPES + r) : (uint8_t)(r < TYPES ? r : TYPES - 1u);
  } else
    return 0;
  entry(was);
  entry(at);
  sound_play(SFX_MOVE);
  detail();
  return 0;
}
