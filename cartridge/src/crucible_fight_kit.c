/* The bag and the level card (docs/fight-system.md). The bag is what you bring to the crucible: eight of
 * your things. Six can be pinned here (one per save); the rest, and any pin you lose, come from the auto bag (what you
 * own of two stars, then one, by how often you used it). Things of three stars go in only pinned, REACH of them; four
 * stars never. Opened from the bench (SELECT, then START on the filter screen: TO BAG, the bench's focus as the first
 * candidate) or with SELECT during a fight's opening.
 *   LEFT/RIGHT  a pin (then your attribute points when you have some)     UP/DOWN  what you own that fits
 *   A           pin it (a point: spend it)        B  clear the pin        START  done (saved)
 * A pin marked '+' makes something with another pin or with one of the four: you can forge it in a fight.
 * The level card comes after a fight that raised a level: points go to GRIT (+1 HP), FOCUS (+1 pip: more time a
 * turn) or REACH (one more thing of three stars). */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_data.h"
#include "crucible_ui.h"
#include "crucible_scene.h"
#include "crucible_state.h"
#include "crucible_crux.h"
#include "crucible_player.h"
#define T_CREAM 7u
#define T_BRASS 15u
#define Y0 4u
static const char ANAME[3][6] = {"GRIT", "FOCUS", "REACH"};
uint16_t kit_edit[6]; /* the pins being edited (harness) */
#define kit_ kit_edit
static uint16_t cand_;
static uint8_t at_, where_;
uint8_t kit_cursor, kit_open_now; /* harness */
uint8_t cri_owned(crucible_core *c, uint16_t id) BANKED;
static void put_(uint8_t x, uint8_t y, uint8_t tile, uint8_t attr) {
  VBK_REG = 1;
  set_bkg_tiles(x, y, 1, 1, &attr);
  VBK_REG = 0;
  set_bkg_tiles(x, y, 1, 1, &tile);
}
/* local: a string literal belongs to this bank (ui_text in another bank would read the wrong bytes) */
static void text_(uint8_t x, uint8_t y, const char *s, uint8_t width, uint8_t attr) {
  uint8_t tiles[20], attrs[20], i = 0, c;
  if (!width) return;
  if (width > 20u) width = 20u;
  memset(attrs, attr, width);
  while (i < width) {
    c = (uint8_t)*s;
    if (c) s++;
    tiles[i++] = GLYPH(c);
  }
  VBK_REG = 1;
  set_bkg_tiles(x, y, width, 1, attrs);
  VBK_REG = 0;
  set_bkg_tiles(x, y, width, 1, tiles);
}
static void num_(char *o, uint16_t n, uint8_t w) {
  o[w] = 0;
  while (w) {
    o[--w] = (char)('0' + n % 10u);
    n /= 10u;
  }
}
static uint8_t slots_(void) { return (uint8_t)(6u + (pl.pts ? 3u : 0u)); }
static uint8_t threes_(uint8_t skip) {
  uint8_t k, n = 0;
  for (k = 0; k < 6u; k++)
    if (k != skip && kit_[k] != CRU_NONE && cx_stars(kit_[k]) == 3u) n++;
  return n;
}
static uint8_t fits_(uint8_t slot, uint16_t id) {
  uint8_t s, k;
  if (id < 4u || id >= core.items) return 0;
  s = cx_stars(id);
  if (s > 3u || (s == 3u && threes_(slot) >= player_attr(PA_REACH))) return 0;
  for (k = 0; k < 6u; k++)
    if (k != slot && kit_[k] == id) return 0;
  return 1;
}
static void card_row(uint8_t k) {
  char s[21], n[14];
  uint16_t id = k == at_ && cand_ != CRU_NONE ? cand_ : kit_[k];
  uint8_t j, st;
  memset(s, ' ', 20);
  s[20] = 0;
  s[0] = (char)(k == at_ ? '>' : ' ');
  if (id != CRU_NONE) {
    crucible_get_name(id, n);
    memcpy(s + 2, n, strlen(n) > 12u ? 12u : strlen(n));
    st = cx_stars(id);
    for (j = 0; j < st; j++) s[15u + j] = '*';
    for (j = 0; j < 6u; j++)
      if (j != k && kit_[j] != CRU_NONE && cru_recipe(&core, id, kit_[j]) != CRU_NONE) {
        s[19] = '+';
        break;
      }
    if (s[19] != '+')
      for (j = 0; j < 4u; j++)
        if (cru_recipe(&core, id, j) != CRU_NONE) {
          s[19] = '+';
          break;
        }
  } else
    memcpy(s + 2, "- AUTO -", 8);
  text_(0, (uint8_t)(Y0 + k), s, 20, k == at_ ? T_CREAM : T_BRASS);
}
static void head(void) {
  char s[8];
  uint8_t i;
  text_(0, 0, " ", 20, T_CREAM);
  text_(1, 0, "YOUR BAG", 8, T_CREAM);
  strcpy(s, "LV ");
  num_(s + 3, pl.level, 2);
  text_(14, 0, s, 5, T_BRASS);
  text_(1, 2, "PINNED, THEN AUTO", 17, T_BRASS);
  for (i = 0; i < 3u; i++) {
    char a[8];
    memcpy(a, ANAME[i], 2);
    a[2] = ' ';
    num_(a + 3, player_attr(i), 1);
    a[4] = 0;
    text_((uint8_t)(1u + i * 6u), 11, a, 4, T_BRASS);
  }
}
static void points_rows(void) {
  uint8_t i;
  char s[21], n[3];
  if (!pl.pts) {
    text_(0, 13, " ", 20, T_CREAM);
    return;
  }
  memset(s, ' ', 20);
  s[20] = 0;
  memcpy(s, "PTS", 3);
  s[3] = (char)('0' + (pl.pts > 9u ? 9u : pl.pts));
  for (i = 0; i < 3u; i++) {
    uint8_t x = (uint8_t)(5u + i * 5u);
    s[x - 1u] = (char)(at_ == 6u + i ? '>' : ' ');
    memcpy(s + x, ANAME[i], 2);
    num_(n, player_attr(i), 1);
    s[x + 2u] = n[0];
  }
  text_(0, 13, s, 20, at_ >= 6u ? T_CREAM : T_BRASS);
}
static void hint(void) {
  text_(0, 17, " ", 20, T_CREAM);
  if (at_ < 6u) {
    text_(1, 17, "UD", 2, T_BRASS);
    put_(4, 17, UI_A, T_CREAM);
    text_(5, 17, "PIN", 3, T_CREAM);
    put_(9, 17, UI_B, T_CREAM);
    text_(10, 17, "CLEAR", 5, T_CREAM);
    text_(16, 17, "ST", 2, T_BRASS);
  } else {
    put_(1, 17, UI_A, T_CREAM);
    text_(2, 17, "SPEND", 5, T_CREAM);
    text_(16, 17, "ST", 2, T_BRASS);
  }
}
static void draw(void) {
  uint8_t k;
  head();
  for (k = 0; k < 6u; k++) card_row(k);
  points_rows();
  hint();
}
/* where 1: a fight's opening (the bag is rebuilt after); 0: the bench, the focus as a candidate */
uint8_t kit_open(uint8_t where) BANKED {
  uint8_t y;
  where_ = where;
  at_ = 0;
  kit_open_now = 1;
  kit_cursor = 0;
  memcpy(kit_, pl.kit, sizeof kit_);
  for (y = 0; y < 6u; y++)
    if (kit_[y] >= core.items || kit_[y] < 4u || !cri_owned(&core, kit_[y]) || cx_stars(kit_[y]) > 3u)
      kit_[y] = CRU_NONE;
  cand_ = CRU_NONE;
  if (!where && core.focus < core.items && cru_owned(&core, core.focus) && fits_(0, core.focus))
    cand_ = core.focus; /* TO BAG: the bench's focus */
  if (!where) scene_draw(SCENE_RECORDS);
  for (y = 0; y < 18u; y++) text_(0, y, " ", 20, T_CREAM);
  draw();
  return 1;
}
static void step_card(int8_t d) {
  uint16_t id = cand_ != CRU_NONE ? cand_ : kit_[at_] != CRU_NONE ? kit_[at_] : core.focus, start;
  uint16_t n;
  if (id >= core.items) id = 0;
  start = id;
  for (n = 0; n < 6000u; n++) {
    id = cru_shelf_step(&core, id, d);
    if (id == start) break;
    if (fits_(at_, id)) {
      cand_ = id;
      return;
    }
  }
}
/* 1 when done (the pins are saved) */
uint8_t kit_tick(uint8_t pressed) BANKED {
  uint8_t n = slots_();
  if (pressed & J_START) {
    memcpy(pl.kit, kit_, sizeof kit_);
    player_save();
    kit_open_now = 0;
    sound_play(SFX_CLOSE);
    return 1;
  }
  if (pressed & (J_LEFT | J_RIGHT)) {
    at_ = (uint8_t)((pressed & J_LEFT) ? (at_ ? at_ - 1u : n - 1u) : (at_ + 1u >= n ? 0u : at_ + 1u));
    cand_ = CRU_NONE;
    sound_play(SFX_MOVE);
  } else if (pressed & (J_UP | J_DOWN)) {
    if (at_ < 6u) step_card((pressed & J_UP) ? -1 : 1);
    sound_play(SFX_MOVE);
  } else if (pressed & J_A) {
    if (at_ < 6u) {
      if (cand_ != CRU_NONE) {
        kit_[at_] = cand_;
        cand_ = CRU_NONE;
        sound_play(SFX_PICK);
      }
    } else if (player_spend((uint8_t)(at_ - 6u)))
      sound_play(SFX_NEW);
    else
      sound_play(SFX_DENY);
    if (at_ >= slots_()) at_ = 0;
  } else if (pressed & J_B) {
    if (at_ < 6u) {
      kit_[at_] = CRU_NONE;
      cand_ = CRU_NONE;
      sound_play(SFX_UNDO);
    }
  } else
    return 0;
  kit_cursor = at_;
  draw();
  return 0;
}

/* ---- the level card ---- */
static uint8_t lat_;
uint8_t level_tick(uint8_t pressed) BANKED;
uint8_t level_open(void) BANKED {
  uint8_t y;
  char s[12];
  for (y = 0; y < 18u; y++) text_(0, y, " ", 20, T_CREAM);
  text_(1, 0, "THE GRID REMEMBERS YOU", 18, T_CREAM);
  strcpy(s, "LEVEL ");
  num_(s + 6, pl.level, 2);
  text_(6, 9, s, 8, T_CREAM);
  text_(2, 10, "SOMETHING NEW OPENS", 19, T_BRASS);
  lat_ = 0;
  sound_play(SFX_NEW);
  if (!pl.pts) {
    put_(4, 16, UI_A, T_CREAM);
    text_(6, 16, "GO ON", 5, T_CREAM);
    return 1;
  }
  (void)level_tick(0);
  return 1;
}
uint8_t level_tick(uint8_t pressed) BANKED {
  uint8_t i;
  char s[21];
  static const uint8_t CAP[3] = {4, 2, 3};
  if (!pl.pts) return (pressed & (J_A | J_B | J_START)) ? 1u : 0u;
  if (pressed & (J_UP | J_LEFT))
    lat_ = lat_ ? (uint8_t)(lat_ - 1u) : 2u;
  else if (pressed & (J_DOWN | J_RIGHT))
    lat_ = lat_ >= 2u ? 0u : (uint8_t)(lat_ + 1u);
  else if (pressed & J_A) {
    if (player_spend(lat_))
      sound_play(SFX_PICK);
    else
      sound_play(SFX_DENY);
    if (!pl.pts) return 1;
  } else if (pressed & (J_B | J_START))
    return 1; /* later: the points wait in the bag screen */
  strcpy(s, "POINTS ");
  num_(s + 7, pl.pts, 1);
  text_(6, 12, s, 8, T_CREAM);
  for (i = 0; i < 3u; i++) {
    memset(s, ' ', 16);
    s[16] = 0;
    s[0] = (char)(lat_ == i ? '>' : ' ');
    memcpy(s + 2, ANAME[i], strlen(ANAME[i]));
    num_(s + 9, player_attr(i), 1);
    s[10] = '/';
    num_(s + 11, CAP[i], 1);
    text_(3, (uint8_t)(13u + i), s, 16, lat_ == i ? T_CREAM : T_BRASS);
  }
  put_(1, 17, UI_A, T_CREAM);
  text_(2, 17, "SPEND", 5, T_CREAM);
  put_(9, 17, UI_B, T_CREAM);
  text_(10, 17, "LATER", 5, T_CREAM);
  return 0;
}
