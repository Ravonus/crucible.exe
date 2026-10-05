/* The kit and the level card (docs/fight-system.md 2, 8.3). The kit is persistent, one per save: six cards
 * on a budget of 12 (+REACH), and the passives you bring. It is edited from the bench (SELECT opens the filter screen;
 * START there is TO KIT, with the bench's focus as the first candidate) or by SELECT during a fight's opening tear.
 *   LEFT/RIGHT  a slot (six cards, two passives, then your attribute points when you have some)
 *   UP/DOWN     cycle what you own that fits the budget (a passive slot: the passives you know)
 *   A           set it (a point: spend it)        B  clear the slot (the auto-kit fills it in a fight)
 *   SELECT      the auto-kit (the balanced builder)        START  done (saved)
 * A copy of a card already in the kit costs one less: stacks are cheap, and each copy in the discard hits harder.
 * Cards that make something together are marked with a link: FUSE in a fight. The level card comes after a fight
 * that raised a level: the new option is named, and points go to GRIT, FOCUS or REACH. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_data.h"
#include "crucible_ui.h"
#include "crucible_scene.h"
#include "crucible_state.h"
#include "crucible_fight_rules.h"
#include "crucible_player.h"
#define T_CREAM 7u
#define T_BRASS 15u
#define Y0 9u
static const char STANCE_CH[3] = {'=', '#', '*'};
static const uint8_t BIT8[8] = {1, 2, 4, 8, 16, 32, 64, 128};
static const char PNAME[FP_COUNT][10] = {"ECHO",  "PRISM",    "SCAR",  "VIGIL",  "CATALYST",  "STALEMATE",
                                         "SALVE", "UNDERTOW", "LUCID", "NOCLIP", "OVERCLOCK", "MEMORY"};
static const char ANAME[3][6] = {"GRIT", "FOCUS", "REACH"};
uint16_t kit_edit[6]; /* the kit being edited (harness) */
#define kit_ kit_edit
static uint16_t cand_;
static uint8_t at_, where_, cpas_, dirty_;
uint8_t kit_cursor, kit_open_now; /* harness */
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
static void num_(char *o, uint16_t n, uint8_t w) {
  o[w] = 0;
  while (w) {
    o[--w] = (char)('0' + n % 10u);
    n /= 10u;
  }
}
static uint8_t slots_(void) { return (uint8_t)(8u + (pl.pts ? 3u : 0u)); }
static uint8_t cost_with(uint8_t slot, uint16_t id) {
  uint16_t k[6];
  memcpy(k, kit_, sizeof k);
  k[slot] = id;
  return fr_kit_cost(k, 6);
}
static uint8_t full(void) {
  uint8_t k;
  for (k = 0; k < 6u; k++)
    if (kit_[k] == CRU_NONE) return 0;
  return 1;
}
static uint8_t cost_now(void) {
  uint16_t k[6];
  uint8_t i;
  for (i = 0; i < 6u; i++) k[i] = kit_[i] == CRU_NONE ? kit_[0] : kit_[i];
  if (kit_[0] == CRU_NONE) return 0;
  return fr_kit_cost(k, 6);
}
static void card_row(uint8_t k) {
  char s[21], n[14];
  uint16_t id = k == at_ && cand_ != CRU_NONE ? cand_ : kit_[k];
  uint8_t m, st, j, c;
  memset(s, ' ', 20);
  s[20] = 0;
  s[0] = (char)(k == at_ ? '>' : ' ');
  if (id != CRU_NONE) {
    m = fr_stance(id);
    for (st = 0, j = 1; st < 3u; st++)
      if (m & BIT8[st]) s[j++] = STANCE_CH[st];
    s[3] = (char)('0' + fr_power(id));
    crucible_get_name(id, n);
    memcpy(s + 5, n, strlen(n) > 11u ? 11u : strlen(n));
    for (j = 0; j < 6u; j++)
      if (j != k && kit_[j] != CRU_NONE && cru_recipe(&core, id, kit_[j]) != CRU_NONE) {
        s[17] = '+';
        break;
      } /* a pair: FUSE */
    c = fr_cost(id);
    for (j = 0; j < k; j++)
      if (kit_[j] == id) {
        if (c > 1u) c--;
        break;
      }
    s[19] = (char)('0' + c);
  } else
    memcpy(s + 5, "- AUTO -", 8);
  text_(0, (uint8_t)(Y0 + k), s, 20, k == at_ ? T_CREAM : T_BRASS);
}
static void pas_row(void) {
  char s[21];
  uint8_t k, id, x;
  memset(s, ' ', 20);
  s[20] = 0;
  memcpy(s + 1, "BRING", 5);
  for (k = 0; k < 2u; k++) {
    x = (uint8_t)(7u + k * 7u);
    s[x - 1u] = (char)(at_ == 6u + k ? '>' : ' ');
    id = (uint8_t)(k ? pl.equip >> 4 : pl.equip & 15u);
    if (at_ == 6u + k) id = cpas_;
    if (k && player_slots() < 2u)
      memcpy(s + x, "LV 6", 4);
    else if (id < FP_COUNT)
      memcpy(s + x, PNAME[id], strlen(PNAME[id]) > 6u ? 6u : strlen(PNAME[id]));
    else
      memcpy(s + x, "--", 2);
  }
  text_(0, 15, s, 20, at_ >= 6u && at_ < 8u ? T_CREAM : T_BRASS);
}
static void head(void) {
  char s[8], n[4];
  uint8_t c = cost_now(), b = player_budget(), i;
  text_(0, 0, " ", 20, T_CREAM);
  text_(1, 0, "KIT", 3, T_CREAM);
  num_(n, c, 2);
  strcpy(s, n);
  strcat(s, "/");
  num_(n, b, 2);
  strcat(s, n);
  text_(14, 0, s, 5, c > b ? T_BRASS : T_CREAM);
  for (i = 0; i < 15u; i++)
    put_((uint8_t)(1u + i), 1, GLYPH(i < c ? '#' : i < b ? '.' : ' '), i < c ? (c > b ? T_BRASS : T_CREAM) : T_BRASS);
  strcpy(s, "LV ");
  num_(s + 3, pl.level, 2);
  text_(14, 3, s, 5, T_BRASS);
  for (i = 0; i < 3u; i++) {
    char a[8];
    memcpy(a, ANAME[i], 2);
    a[2] = ' ';
    num_(a + 3, player_attr(i), 1);
    a[4] = 0;
    text_(14, (uint8_t)(4u + i), a, 4, T_BRASS);
  }
}
static void points_rows(void) {
  uint8_t i;
  char s[21], n[3];
  if (!pl.pts) {
    text_(0, 16, " ", 20, T_CREAM);
    return;
  }
  memset(s, ' ', 20);
  s[20] = 0;
  memcpy(s, "PTS", 3);
  s[3] = (char)('0' + (pl.pts > 9u ? 9u : pl.pts));
  for (i = 0; i < 3u; i++) {
    uint8_t x = (uint8_t)(5u + i * 5u);
    s[x - 1u] = (char)(at_ == 8u + i ? '>' : ' ');
    memcpy(s + x, ANAME[i], 2);
    num_(n, player_attr(i), 1);
    s[x + 2u] = n[0];
  }
  text_(0, 16, s, 20, at_ >= 8u ? T_CREAM : T_BRASS);
}
static void hint(void) {
  text_(0, 17, " ", 20, T_CREAM);
  if (at_ < 8u) {
    text_(1, 17, "UD", 2, T_BRASS);
    put_(4, 17, UI_A, T_CREAM);
    text_(5, 17, "SET", 3, T_CREAM);
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
  pas_row();
  points_rows();
  hint();
}
/* where 1: a fight's opening (the kit it fights with is re-read after); 0: the bench, the focus as a candidate */
uint8_t kit_open(uint8_t where) BANKED {
  uint8_t y;
  where_ = where;
  at_ = 0;
  kit_open_now = 1;
  kit_cursor = 0;
  memcpy(kit_, pl.kit, sizeof kit_);
  for (y = 0; y < 6u; y++)
    if (kit_[y] >= core.items || !cru_owned(&core, kit_[y])) kit_[y] = CRU_NONE;
  if (!full() && kit_[0] == CRU_NONE) player_autokit(kit_, player_budget());
  cand_ = CRU_NONE;
  cpas_ = (uint8_t)(pl.equip & 15u);
  if (!where && core.focus < core.items && cru_owned(&core, core.focus))
    cand_ = core.focus; /* TO KIT: the bench's focus */
  if (!where) scene_draw(SCENE_RECORDS);
  for (y = 0; y < 18u; y++) text_(0, y, " ", 20, T_CREAM);
  draw();
  dirty_ = 0;
  return 1;
}
static void step_card(int8_t d) {
  uint16_t id = cand_ != CRU_NONE ? cand_ : kit_[at_] != CRU_NONE ? kit_[at_] : core.focus, start;
  uint16_t n;
  if (id >= core.items) id = core.focus;
  start = id;
  for (n = 0; n < 2000u; n++) {
    id = cru_shelf_step(&core, id, d);
    if (id == start) break;
    if (cost_with(at_, id) <= player_budget()) {
      cand_ = id;
      return;
    }
  }
}
static void step_pas(int8_t d) {
  uint8_t i, id = cpas_;
  for (i = 0; i < FP_COUNT + 1u; i++) {
    id = (uint8_t)(d > 0 ? (id >= FP_COUNT ? 0u : id + 1u)
                         : (id == 0u         ? FP_COUNT
                            : id >= FP_COUNT ? FP_COUNT - 1u
                                             : id - 1u));
    if (id == FP_COUNT) {
      cpas_ = 15u;
      return;
    }
    if (pl.known & player_bit(id)) {
      cpas_ = id;
      return;
    }
  }
}
/* 1 when done (the kit is saved) */
uint8_t kit_tick(uint8_t pressed) BANKED {
  uint8_t n = slots_(), was = at_;
  if (pressed & J_START) {
    memcpy(pl.kit, kit_, sizeof kit_);
    player_save();
    kit_open_now = 0;
    sound_play(SFX_CLOSE);
    return 1;
  }
  if (pressed & J_SELECT) {
    player_autokit(kit_, player_budget());
    cand_ = CRU_NONE;
    sound_play(SFX_SWAP);
    draw();
    return 0;
  }
  if (pressed & (J_LEFT | J_RIGHT)) {
    at_ = (uint8_t)((pressed & J_LEFT) ? (at_ ? at_ - 1u : n - 1u) : (at_ + 1u >= n ? 0u : at_ + 1u));
    cand_ = CRU_NONE;
    cpas_ = (uint8_t)(at_ == 7u ? pl.equip >> 4 : pl.equip & 15u);
    sound_play(SFX_MOVE);
  } else if (pressed & (J_UP | J_DOWN)) {
    if (at_ < 6u)
      step_card((pressed & J_UP) ? -1 : 1);
    else if (at_ < 8u && !(at_ == 7u && player_slots() < 2u))
      step_pas((pressed & J_UP) ? -1 : 1);
    sound_play(SFX_MOVE);
  } else if (pressed & J_A) {
    if (at_ < 6u) {
      if (cand_ != CRU_NONE) {
        kit_[at_] = cand_;
        cand_ = CRU_NONE;
        sound_play(SFX_PICK);
      }
    } else if (at_ < 8u) {
      if (!(at_ == 7u && player_slots() < 2u)) {
        pl.equip =
            at_ == 6u ? (uint8_t)((pl.equip & 0xF0u) | (cpas_ & 15u)) : (uint8_t)((pl.equip & 15u) | (cpas_ << 4));
        sound_play(SFX_PICK);
      }
    } else if (player_spend((uint8_t)(at_ - 8u)))
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
  (void)was;
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
  text_(6, Y0, s, 8, T_CREAM);
  text_(2, Y0 + 1u, "SOMETHING NEW OPENS", 19, T_BRASS);
  lat_ = 0;
  sound_play(SFX_NEW);
  if (!pl.pts) {
    text_(4, Y0 + 7u, "A", 1, T_CREAM);
    text_(6, Y0 + 7u, "GO ON", 5, T_CREAM);
    return 1;
  }
  (void)level_tick(0);
  return 1;
}
uint8_t level_tick(uint8_t pressed) BANKED {
  uint8_t i;
  char s[21];
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
    return 1; /* later: the points wait in the kit screen */
  strcpy(s, "POINTS ");
  num_(s + 7, pl.pts, 1);
  text_(6, Y0 + 3u, s, 8, T_CREAM);
  for (i = 0; i < 3u; i++) {
    static const uint8_t CAP[3] = {4, 2, 3};
    memset(s, ' ', 16);
    s[16] = 0;
    s[0] = (char)(lat_ == i ? '>' : ' ');
    memcpy(s + 2, ANAME[i], strlen(ANAME[i]));
    num_(s + 9, player_attr(i), 1);
    s[10] = '/';
    num_(s + 11, CAP[i], 1);
    text_(3, (uint8_t)(Y0 + 4u + i), s, 16, lat_ == i ? T_CREAM : T_BRASS);
  }
  put_(1, 17, UI_A, T_CREAM);
  text_(2, 17, "SPEND", 5, T_CREAM);
  put_(9, 17, UI_B, T_CREAM);
  text_(10, 17, "LATER", 5, T_CREAM);
  return 0;
}
