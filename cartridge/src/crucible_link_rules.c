/* The rules of a link FIGHT (docs/fight-system.md 9.2): the host picks, the guest sees every byte.
 * In the lobby, with MODE at FIGHT, two rows take the place of TIME and GOAL:
 *   RULES  FAIR / STRENGTH / CHAOS / CUSTOM (LEFT/RIGHT the presets; A opens the custom rows)
 *   EDGE   the suggested handicap, on or off (from both sides' attribute power, a line of arithmetic)
 * The custom rows (UP/DOWN, LEFT/RIGHT or A to change, B back): attributes, best of, pips, HP and the toggles
 * (RANDOM bags, FOG: their bag hidden, MIRROR: both bring the host's); any change makes the preset CUSTOM. The guest's SELECT asks for a change (the host sees a ? by its name).
 * The host's last rules are kept in its record (pl.rules) and come back the next time it hosts. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_data.h"
#include "crucible_ui.h"
#include "crucible_state.h"
#include "crucible_link.h"
#include "crucible_player.h"
#define T_CREAM 7u
#define T_BRASS 15u
#define ROWS 8u
#define WIN 6u
uint8_t lr_at, lr_top; /* the custom page's cursor and window (the harness reads them) */
static const char PRESET[4][9] = {"FAIR", "STRENGTH", "CHAOS", "CUSTOM"};
static const char LABEL[ROWS][9] = {
    "ATTRS",  "BEST OF", "PIPS",   "HP",
    "RANDOM", "FOG",     "MIRROR", "BACK"}; /* RANDOM bags, FOG their bag hidden, MIRROR both bring the host's bag */
static const uint8_t BITS[3] = {3, 5, 6}; /* R3 bits of rows 4..6 */
static const char ATTRS[3][7] = {"NORMAL", "CAPPED", "FULL"};
static const uint8_t BEST[3] = {1, 3, 5}, PIPS[4] = {6, 4, 3, 2};
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
  if (n >= 10u) {
    o[0] = (char)('0' + n / 10u);
    o[1] = (char)('0' + n % 10u);
    o[2] = 0;
  } else {
    o[0] = (char)('0' + n);
    o[1] = 0;
  }
}
static const uint8_t BIT8[8] = {1, 2, 4, 8, 16, 32, 64, 128};

/* ---- presets ---- */
/* R1: preset:2 | attributes:2 << 2 | best-of:2 << 4 | timer:2 << 6 */
void lr_preset(uint8_t p) BANKED {
  link_rules[2] = 2u; /* HP 12 */
  link_rules[4] = link_rules[5] = 0;
  link_rules[6] = 0;
  link_rules[7] = 0;
  if (p == 0u) {
    link_rules[1] = 0x50u;
    link_rules[3] = 0;
  } /* FAIR: NORMALISED, best of 3, 4 pips */
  else if (p == 1u) {
    link_rules[1] = (uint8_t)(0x50u | 1u | (2u << 2));
    link_rules[3] = 0;
  } /* TRUE STRENGTH: FULL (the edge shown, off) */
  else if (p == 2u) {
    link_rules[1] = (uint8_t)(0x50u | 2u | (1u << 2));
    link_rules[3] = 8u | 32u;
  } /* CHAOS: CAPPED, random bags, in fog */
  else
    link_rules[1] = (uint8_t)((link_rules[1] & 0xfcu) | 3u);
}
/* the suggested handicap (9.2): power = GRIT + 2 FOCUS + REACH + 2 (slots - 1); the weaker side gets HP
 * min(8, 2 gap / 3) (the HP field holds 7 at most). who: 0 the host is weaker, 1 the guest. 0 HP: none. */
static uint8_t power(uint8_t a) {
  return (uint8_t)((a & 7u) + (((a >> 3) & 3u) << 1) + ((a >> 5) & 3u) + ((a & 0x80u) ? 2u : 0u));
}
uint8_t lr_suggest(uint8_t *who) BANKED {
  uint8_t mine = power(pl.attrs), theirs = power(link_partner_attrs()), gap, hp, host_weak;
  host_weak = link_role == LINK_HOST ? (uint8_t)(mine < theirs) : (uint8_t)(theirs < mine);
  gap = mine > theirs ? (uint8_t)(mine - theirs) : (uint8_t)(theirs - mine);
  hp = 0;
  while (gap >= 3u) {
    gap = (uint8_t)(gap - 3u);
    hp = (uint8_t)(hp + 2u);
  }
  if (gap == 2u) hp++; /* 2 gap / 3 without a divide */
  if (hp > 7u) hp = 7u;
  *who = host_weak ? 0u : 1u;
  return hp;
}
uint8_t lr_edge_on(void) BANKED { return (link_rules[6] & 0x80u) ? 1u : 0u; }
void lr_edge(uint8_t on) BANKED {
  uint8_t who, hp = lr_suggest(&who);
  link_rules[4] &= (uint8_t)~7u;
  link_rules[5] &= (uint8_t)~7u;
  link_rules[6] &= 0x7fu;
  if (on && hp) {
    link_rules[4u + who] |= hp;
    link_rules[6] |= 0x80u;
  }
}
/* the lobby's two FIGHT rows: row 2 RULES, row 3 EDGE (label and value, value[9]) */
void lr_lobby_row(uint8_t row, char *label, char *value) BANKED {
  if (row == 2u) {
    strcpy(label, "RULES");
    strcpy(value, PRESET[link_rules[1] & 3u]);
    return;
  }
  {
    uint8_t who, hp = lr_suggest(&who), mine = (uint8_t)(who == (link_role == LINK_HOST ? 0u : 1u));
    strcpy(label, "EDGE");
    if (!hp) {
      strcpy(value, "EVEN");
      return;
    }
    value[0] = '+';
    num_(value + 1, hp);
    strcat(value, mine ? " YOU" : " THEM");
    if (!lr_edge_on()) strcat(value, "?");
  }
}
/* LEFT/RIGHT/A on those rows; 1: the custom page opens */
uint8_t lr_lobby_change(uint8_t row, uint8_t pressed) BANKED {
  if (row == 2u) {
    if (pressed & J_A) {
      lr_at = 0;
      lr_top = 0;
      return 1;
    }
    {
      uint8_t p = (uint8_t)(link_rules[1] & 3u);
      p = (pressed & J_LEFT) ? (uint8_t)((p + 3u) & 3u) : (uint8_t)((p + 1u) & 3u);
      lr_preset(p);
    }
    return 0;
  }
  lr_edge(!lr_edge_on());
  return 0;
}
/* ---- the custom page ---- */
static void value_of(uint8_t r, char *v) {
  uint8_t r1 = link_rules[1], r3 = link_rules[3];
  v[0] = 0;
  switch (r) {
  case 0: strcpy(v, ATTRS[((r1 >> 2) & 3u) < 3u ? (r1 >> 2) & 3u : 0u]); break;
  case 1: num_(v, BEST[((r1 >> 4) & 3u) < 3u ? (r1 >> 4) & 3u : 1u]); break;
  case 2: num_(v, PIPS[(r1 >> 6) & 3u]); break;
  case 3: num_(v, (uint8_t)(8u + ((link_rules[2] & 7u) << 1))); break;
  case 7: break;
  default: strcpy(v, (r3 & BIT8[BITS[(uint8_t)(r - 4u) < 3u ? r - 4u : 0u]]) ? "ON" : "OFF");
  }
}
void lr_page_draw(uint8_t row0) BANKED {
  uint8_t i, r;
  char v[9];
  text_(3, row0, " ", 14, T_CREAM);
  text_(4, row0, "RULES", 5, T_BRASS);
  text_(10, row0, PRESET[link_rules[1] & 3u], (uint8_t)strlen(PRESET[link_rules[1] & 3u]), T_BRASS);
  for (i = 0; i < WIN; i++) {
    r = (uint8_t)(lr_top + i);
    text_(3, (uint8_t)(row0 + 1u + i), " ", 14, T_CREAM);
    if (r >= ROWS) continue;
    if (r == lr_at) put_(3, (uint8_t)(row0 + 1u + i), GLYPH('>'), T_CREAM);
    text_(4, (uint8_t)(row0 + 1u + i), LABEL[r], (uint8_t)strlen(LABEL[r]), r == lr_at ? T_CREAM : T_BRASS);
    value_of(r, v);
    text_((uint8_t)(17u - strlen(v)), (uint8_t)(row0 + 1u + i), v, (uint8_t)strlen(v), T_BRASS);
  }
}
static void change(uint8_t r, uint8_t back) {
  uint8_t r1 = link_rules[1], v;
  switch (r) {
  case 0:
    v = (uint8_t)((r1 >> 2) & 3u);
    v = back ? (uint8_t)(v ? v - 1u : 2u) : (uint8_t)(v < 2u ? v + 1u : 0u);
    link_rules[1] = (uint8_t)((r1 & 0xf3u) | (uint8_t)(v << 2));
    break;
  case 1:
    v = (uint8_t)((r1 >> 4) & 3u);
    v = back ? (uint8_t)(v ? v - 1u : 2u) : (uint8_t)(v < 2u ? v + 1u : 0u);
    link_rules[1] = (uint8_t)((r1 & 0xcfu) | (uint8_t)(v << 4));
    break;
  case 2:
    v = (uint8_t)((r1 >> 6) & 3u);
    v = (uint8_t)((v + (back ? 3u : 1u)) & 3u);
    link_rules[1] = (uint8_t)((r1 & 0x3fu) | (uint8_t)(v << 6));
    break;
  case 3:
    v = (uint8_t)(link_rules[2] & 7u);
    v = (uint8_t)((v + (back ? 7u : 1u)) & 7u);
    link_rules[2] = (uint8_t)((link_rules[2] & 0xf8u) | v);
    break;
  default:
    if ((uint8_t)(r - 4u) < 3u) link_rules[3] ^= BIT8[BITS[r - 4u]];
  }
  link_rules[1] = (uint8_t)((link_rules[1] & 0xfcu) | 3u); /* any change: CUSTOM */
}
/* one press on the custom page; 1: back to the lobby */
uint8_t lr_page(uint8_t pressed, uint8_t row0) BANKED {
  if ((pressed & J_B) || ((pressed & J_A) && lr_at == ROWS - 1u)) return 1;
  if (pressed & J_UP) {
    lr_at = lr_at ? (uint8_t)(lr_at - 1u) : (uint8_t)(ROWS - 1u);
  } else if (pressed & J_DOWN) {
    lr_at = (uint8_t)(lr_at + 1u < ROWS ? lr_at + 1u : 0u);
  } else if (pressed & (J_A | J_LEFT | J_RIGHT)) {
    if (lr_at < ROWS - 1u) change(lr_at, (pressed & J_LEFT) ? 1u : 0u);
  } else
    return 0;
  if (lr_at < lr_top)
    lr_top = lr_at;
  else if (lr_at >= lr_top + WIN)
    lr_top = (uint8_t)(lr_at - WIN + 1u);
  sound_play(SFX_MOVE);
  lr_page_draw(row0);
  return 0;
}
/* the host's last rules: from its record when it opens a lobby, back into it at START */
void lr_load(void) BANKED {
  uint8_t i;
  link_mode = (uint8_t)(pl.rules[0] & 15u);
  if (link_mode > 2u) link_mode = 2u;
  link_time = (uint8_t)((pl.rules[0] >> 4) & 3u);
  for (i = 1; i < 8u; i++) link_rules[i] = pl.rules[i];
  link_rules[6] &= 0x7fu;
  link_rules[4] &= (uint8_t)~7u;
  link_rules[5] &= (uint8_t)~7u;
}
void lr_save(void) BANKED {
  uint8_t i;
  pl.rules[0] = (uint8_t)(link_mode | (link_time << 4));
  for (i = 1; i < 8u; i++) pl.rules[i] = link_rules[i];
  player_save();
}
