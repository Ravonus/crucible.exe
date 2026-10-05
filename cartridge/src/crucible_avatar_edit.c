/* The avatar creator (docs/fight-system.md 8.2): a new story slot's face, built from the procedural generator.
 * It opens in the wake-up after the slot is named. The face is the talker's bust (the machine steps aside); nine rows
 * under it, each with what is open of it (3/8):
 *   UP/DOWN     a row (a row the style fixes is skipped: a human's eyes are its own)
 *   LEFT/RIGHT  that row's open options; the face regenerates, torn in as it changes
 *   SELECT      dream it: a seeded roll within the open options (the old generator)
 *   B           undo the last change; with nothing to undo, back to the name
 *   A or START  done: the genome (av_genome) is the run's face, read by story_begin
 * What is open is the slot's record: who you were in its last run stays (levels open options, secrets open styles).
 * Everything here comes from presses only (the seed is the wake-up's hash): a link partner regenerates the same face
 * from the six bytes. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_data.h"
#include "crucible_ui.h"
#include "crucible_state.h"
#include "crucible_avatar.h"
#include "crucible_player.h"
#include "crucible_fight_rules.h"
#define T_CREAM 7u
#define T_BRASS 15u
#define R0 8u /* the first row on screen */
uint8_t av_genome[6], av_genome_set;
/* the creator's own state lives in the fight engine's scratch (no fight runs during the wake-up): WRAM is tight */
#define prev_ (fr_scratch)
#define row_ (fr_scratch[6])
#define undo_ (fr_scratch[7])
#define n_ (fr_scratch[8])
#define seed_ (*(uint16_t *)(fr_scratch + 9))
static const char LABEL[AV_ROWS][6] = {"STYLE", "HEAD", "EYES", "MOUTH", "CROWN", "MARK", "HUE", "GRAIN", "AURA"};
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
static void num_(char *o, uint8_t n) {
  o[0] = n >= 10u ? (char)('0' + n / 10u) : ' ';
  o[1] = (char)('0' + n % 10u);
}
/* a row's value in the genome (8.1): 0 style | hue << 4; 1 head | eyes << 3; 2 mouth | crown << 3 | neck << 6;
 * 3 mark | grain << 3 | size << 6; 4 aura */
static uint8_t get_(uint8_t r) {
  uint8_t *g = av_genome;
  switch (r) {
  case 0: return (uint8_t)(g[0] & 15u);
  case 1: return (uint8_t)(g[1] & 7u);
  case 2: return (uint8_t)((g[1] >> 3) & 15u);
  case 3: return (uint8_t)(g[2] & 7u);
  case 4: return (uint8_t)((g[2] >> 3) & 7u);
  case 5: return (uint8_t)(g[3] & 7u);
  case 6: return (g[5] & 4u) ? 16u : (uint8_t)(g[0] >> 4); /* 16: the DMG green */
  case 7: return (uint8_t)((g[3] >> 3) & 7u);
  default: return (uint8_t)(g[4] & 7u);
  }
}
static void set_(uint8_t r, uint8_t v) {
  uint8_t *g = av_genome;
  switch (r) {
  case 0: g[0] = (uint8_t)((g[0] & 0xf0u) | v); break;
  case 1: g[1] = (uint8_t)((g[1] & 0xf8u) | v); break;
  case 2: g[1] = (uint8_t)((g[1] & 0x87u) | (uint8_t)(v << 3)); break;
  case 3: g[2] = (uint8_t)((g[2] & 0xf8u) | v); break;
  case 4: g[2] = (uint8_t)((g[2] & 0xc7u) | (uint8_t)(v << 3)); break;
  case 5: g[3] = (uint8_t)((g[3] & 0xf8u) | v); break;
  case 6:
    if (v == 16u)
      g[5] |= 4u;
    else {
      g[5] &= (uint8_t)~4u;
      g[0] = (uint8_t)((g[0] & 0x0fu) | (uint8_t)(v << 4));
    }
    break;
  case 7: g[3] = (uint8_t)((g[3] & 0xc7u) | (uint8_t)(v << 3)); break;
  default: g[4] = (uint8_t)((g[4] & 0xf8u) | v); break;
  }
}
/* a row the style decides by itself: a human's, a pixel face's and the great eye's eyes; the crown where the style
 * wears none of its own */
static uint8_t fixed_(uint8_t r) {
  uint8_t s = (uint8_t)(av_genome[0] & 15u);
  if (r == 2u) return s == 1u || s == 3u || s == 5u;
  if (r == 4u) return s == 2u || s == 3u || s == 7u || s == 8u;
  return 0;
}
/* the hue row has a 17th swatch once the DMG green was seen */
static uint8_t count_(uint8_t r) { return (uint8_t)(player_row_count(r) + (r == 6u && (pl.flags & PF_DMG) ? 1u : 0u)); }
static uint8_t open_(uint8_t r, uint8_t v) {
  return v >= player_row_count(r) ? 1u : player_unlocked((uint8_t)(player_row_base(r) + v));
}
static uint8_t open_count(uint8_t r) {
  uint8_t k, n = 0, c = count_(r);
  for (k = 0; k < c; k++)
    if (open_(r, k)) n++;
  return n;
}
static void row_draw(uint8_t r) {
  char s[18];
  uint8_t y = (uint8_t)(R0 + r), on = r == row_, a = on ? T_BRASS : T_CREAM;
  text_(0, y, " ", 20, T_CREAM);
  if (on) put_(1, y, GLYPH('>'), T_BRASS);
  text_(2, y, LABEL[r], 5, a);
  if (fixed_(r))
    text_(10, y, "--", 2, T_CREAM);
  else {
    if (on) {
      put_(9, y, UI_LEFT, T_BRASS);
      put_(12, y, UI_RIGHT, T_BRASS);
    }
    num_(s, (uint8_t)(get_(r) + 1u));
    text_(10, y, s, 2, a);
  }
  num_(s, open_count(r));
  s[2] = '/';
  num_(s + 3, count_(r));
  s[5] = 0;
  text_(14, y, s[0] == ' ' ? s + 1 : s, s[0] == ' ' ? 4u : 5u, T_CREAM);
}
static void draw_all(void) {
  uint8_t r;
  for (r = 0; r < AV_ROWS; r++) row_draw(r);
  text_(0, 17, " ", 20, T_CREAM);
  put_(0, 17, UI_A, T_CREAM);
  text_(1, 17, "OK", 2, T_CREAM);
  put_(4, 17, UI_B, T_CREAM);
  text_(5, 17, "UNDO", 4, T_CREAM);
  text_(10, 17, "SEL DREAM", 9, T_CREAM);
}
static void face_(void) {
  avatar_make_genome(av_genome);
  avatar_glitch(1);
}
/* the next open option of a row, either way, wrapping */
static uint8_t step_(uint8_t r, int8_t d) {
  uint8_t c = count_(r), v = get_(r), k;
  if (v >= c) v = 0;
  for (k = 0; k < c; k++) {
    v = d > 0 ? (uint8_t)(v + 1u == c ? 0u : v + 1u) : (uint8_t)(v ? v - 1u : c - 1u);
    if (open_(r, v)) return v;
  }
  return get_(r);
}
/* open: the slot's record (what is open), its last face to start from */
void av_edit_open(uint16_t seed) BANKED {
  player_story_load(menu_slot, seed);
  memcpy(av_genome, pl.genome, 6);
  row_ = 0;
  undo_ = 0;
  n_ = 0;
  seed_ = seed ? seed : 0x5eedu;
  av_genome_set = 0;
  face_();
  draw_all();
}
/* 0 still open, 1 done (av_genome_set), 2 back to the name */
uint8_t av_edit_tick(uint8_t pressed) BANKED {
  uint8_t r = row_, v;
  if (pressed & (J_A | J_START)) {
    av_genome_set = 1;
    sound_play(SFX_OPEN);
    return 1;
  }
  if (pressed & J_B) {
    if (!undo_) {
      sound_play(SFX_CLOSE);
      return 2;
    }
    memcpy(av_genome, prev_, 6);
    undo_ = 0;
    sound_play(SFX_UNDO);
    face_();
    draw_all();
    return 0;
  }
  if (pressed & (J_UP | J_DOWN)) {
    do {
      row_ = pressed & J_UP ? (uint8_t)(row_ ? row_ - 1u : AV_ROWS - 1u)
                            : (uint8_t)(row_ + 1u == AV_ROWS ? 0u : row_ + 1u);
    } while (fixed_(row_));
    sound_play(SFX_MOVE);
    row_draw(r);
    row_draw(row_);
    return 0;
  }
  if (pressed & (J_LEFT | J_RIGHT)) {
    if (fixed_(row_)) return 0;
    v = step_(row_, pressed & J_RIGHT ? 1 : -1);
    if (v == get_(row_)) {
      sound_play(SFX_DENY);
      return 0;
    }
    memcpy(prev_, av_genome, 6);
    undo_ = 1;
    set_(row_, v);
  } else if (pressed & J_SELECT) {
    memcpy(prev_, av_genome, 6);
    undo_ = 1;
    player_genome_roll(av_genome, (uint16_t)(seed_ ^ (uint16_t)((uint16_t)(++n_) * 0x9e37u)));
    av_genome[4] = (uint8_t)((av_genome[4] & 7u) | (prev_[4] & 0xf8u));
    av_genome[5] = prev_[5]; /* the mark and the secret bits are earned, never rolled */
  } else
    return 0;
  sound_play(SFX_SWAP);
  face_();
  draw_all();
  return 0;
}
