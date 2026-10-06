/* FIGHT: THE CRUCIBLE (docs/fight-system.md). One pot between you and them; every move is a real recipe.
 * Fights come to the bench from play (crucible_flow.c) or from the link's FIGHT mode, never from a menu. This file is
 * the screen and the turn for every kind: a story duel (a faction's figure and its AI), a boss (its twist, its
 * telegraph, its phases), a gauntlet (three figures, your HP carries) and the link versus (the same engine in
 * lockstep: only action words travel). crucible_crux.c is the engine, crucible_fight_story.c who comes and what it
 * gives and costs. crucible.c draws the six cells this reports (fight_view) on the bench's own room, the opponent's
 * face as sprites in the intro and at the end, and the effects raised here (fight_fx).
 *
 * The screen (20x18 tiles), the bench's layout:
 *   row 0      your HP and bar | round pips | theirs
 *   rows 1-2   the box: who they are, their bag and four; what the move in hand does (or what they just did)
 *   rows 4-7   the merge row: [yours] + [the pot] = [what it makes]
 *   row 8      the pot's tab: whose it is, its heat, its rule (locked / grows / the sky)
 *   rows 10-13 your bag on the carousel (SELECT: theirs)
 *   rows 15-17 the name and stars; the buttons; your four and the turn's pips
 * Buttons: LEFT/RIGHT your things; A adds the one in hand (an empty pot: A marks it, A on a second forges the two,
 * A on the same one sets it alone); B waits on their pot, pours yours, unmarks; SELECT looks at their bag. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_data.h"
#include "crucible_ui.h"
#include "crucible_scene.h"
#include "crucible_state.h"
#include "crucible_lines.h"
#include "crucible_avatar.h"
#include "crucible_storyrun.h"
#include "crucible_fight.h"
#include "crucible_fight_int.h"
#include "crucible_player.h"
#include "crucible_link.h"
#include "crucible_link_scene.h"
#include "crucible_time.h"
#define T_CREAM 7u
#define T_BRASS 15u
#define PIP_FRAMES 150u
#define SHOW_FRAMES 90u
#define THINK_MIN 40u
#define INTRO_FRAMES 150u
#define FLEE_FRAMES 60u
uint8_t fight_kind, fight_ui, fight_cursor, fight_dbg, fight_swap, fight_round, fight_tier, fight_gauntlet, fight_look;
static uint8_t fight_end;
uint8_t fight_wins[2], flow_gauntlet;
uint16_t fight_seed, fight_t0;
char fui_line[20];
uint8_t fui_line_attr, fui_fx;
uint8_t fight_mark = 0xffu, fight_pips, fight_turns_seen; /* harness */
uint16_t fight_word = CX_NONE; /* the last word played (harness) */
static uint8_t intro_k_, chain_best_, open_t_, partner_, tl_[16], nt_, think_, eye_tool_, eye_kind_, prev_cur_,
    prev_mark_, prev_look_, hold_b_, vs_got_;
static uint16_t show_t_, pip_t_, pot_was_, owner_was_, vs_word_, dealt_;
static uint8_t prev_phase_[2];
static const uint8_t BIT8[8] = {1, 2, 4, 8, 16, 32, 64, 128};
extern uint8_t win_pos_y; /* standalone/main.c: the toast window's position (144: off) */

/* ---- the fight's glyphs: twelve punctuation glyphs nobody types in a fight become icons; the font comes back at the
 * end (crucible_load_font). Flat one-colour marks, no lighting. ---- */
#define G_HEAT '$'
#define G_LOCK '&'
#define G_SKY '@'
#define G_GROW '\\'
#define G_EYE '^'
#define G_BOLT '_'
#define G_HPF '#'
#define G_HPH '%'
#define G_HPE '*'
#define G_STAR '"'
static const char ICON_CH[13] = {'<', ';', '>', '[', '$', '&', '@', '\\', '^', '_', '#', '%', '*'};
static const uint8_t ICONS[13][8] = {{0x00, 0x18, 0x3c, 0x7e, 0xff, 0xdb, 0xff, 0x00}, /* < EARTH: a mound */
                                     {0x10, 0x10, 0x38, 0x38, 0x7c, 0x6c, 0x38, 0x00}, /* ; WATER: a drop */
                                     {0x10, 0x32, 0x3a, 0x7e, 0x6e, 0x46, 0x3c, 0x00}, /* > FIRE */
                                     {0x00, 0x78, 0x84, 0x1a, 0x62, 0x9e, 0x00, 0x00}, /* [ AIR: a gust */
                                     {0x20, 0x30, 0x74, 0x7c, 0xdc, 0x8c, 0x78, 0x00}, /* $ HEAT: a flame */
                                     {0x38, 0x44, 0x44, 0xfe, 0xee, 0xee, 0xfe, 0x00}, /* & LOCK */
                                     {0x00, 0x30, 0x7a, 0xfe, 0xfe, 0x00, 0x00, 0x00}, /* @ SKY: a cloud */
                                     {0x0c, 0x12, 0x6c, 0x98, 0x68, 0x08, 0x1c, 0x00}, /* \ GROW: a sprout */
                                     {0x00, 0x3c, 0x42, 0x99, 0x99, 0x42, 0x3c, 0x00}, /* ^ EYE */
                                     {0x0c, 0x18, 0x3c, 0x78, 0x18, 0x30, 0x60, 0x00}, /* _ BOLT (the boss charges) */
                                     {0x00, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x00}, /* # HP: full */
                                     {0x00, 0x7e, 0x72, 0x72, 0x72, 0x72, 0x7e, 0x00}, /* % HP: half */
                                     {0x00, 0x7e, 0x42, 0x42, 0x42, 0x42, 0x7e, 0x00}}; /* * HP: empty */
static const uint8_t STAR_[8] = {0x10, 0x10, 0xfe, 0x7c, 0x38, 0x6c, 0x44, 0x00};
static void icon_load(char ch, const uint8_t *m) {
  uint8_t t[16], i, tile = GLYPH(ch);
  for (i = 0; i < 8u; i++) {
    t[i + i] = m[i];
    t[i + i + 1u] = m[i];
  } /* colour 3: the font's ink */
  VBK_REG = 0;
  set_bkg_data(tile, 1, t);
  for (i = 0; i < 8u; i++) {
    t[i + i] = 0;
    t[i + i + 1u] = m[i];
  } /* the dim font's copy: colour 2 */
  VBK_REG = 1;
  set_bkg_data(tile, 1, t);
  VBK_REG = 0;
}
static void icons_load(void) {
  uint8_t i;
  for (i = 0; i < 13u; i++) icon_load(ICON_CH[i], ICONS[i]);
  icon_load('"', STAR_);
}
static const char ELEM_CH[4] = {'<', ';', '>', '['};

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
static void row_clear(uint8_t y) { text_(0, y, " ", 20, T_CREAM); }
static void centre_(uint8_t y, char *s, uint8_t attr) {
  uint8_t n = (uint8_t)strlen(s);
  if (n > 20u) {
    n = 20u;
    s[20] = 0;
  }
  text_((uint8_t)((20u - n) / 2u), y, s, n, attr);
} /* (never wider than the screen) */
static uint8_t me_(void) { return 0; } /* this cartridge's player is side 0 (the guest's engine is swapped) */

/* ---- your things and theirs, in tool order (the bag, then the four) ---- */
static uint8_t tools_of(uint8_t k, uint8_t *out) {
  uint8_t i, n = 0;
  for (i = 0; i < cx.s[k].n; i++) out[n++] = i;
  for (i = 0; i < 4u; i++)
    if (cx.s[k].four & BIT8[i]) out[n++] = (uint8_t)(12u + i);
  return n;
}
static void tools_now(void) {
  nt_ = tools_of(fight_look ? 1u : 0u, tl_);
  if (fight_cursor >= nt_) fight_cursor = nt_ ? (uint8_t)(nt_ - 1u) : 0u;
}
static uint16_t hand_id(void) { return nt_ ? cx_tool(fight_look ? 1u : 0u, tl_[fight_cursor]) : CX_NONE; }
static uint8_t knows_(uint16_t a, uint16_t b) { return cru_tried(&core, a, b); }

/* ---- the header (row 0): HP, bars, round pips ---- */
static void bar(uint8_t x, int8_t hp, uint8_t max, uint8_t attr, uint8_t rtl) {
  uint8_t i, h, units, full;
  if (hp < 0) hp = 0;
  /* ten half-steps over five tiles: units = round(hp * 10 / max) */
  units = (uint8_t)(((uint16_t)(uint8_t)hp * 10u + max / 2u) / max);
  if (hp && !units) units = 1;
  for (i = 0; i < 5u; i++) {
    h = rtl ? (uint8_t)(4u - i) : i;
    full = (uint8_t)(h * 2u);
    put_((uint8_t)(x + i), 0, GLYPH(units >= full + 2u ? G_HPF : units == full + 1u ? G_HPH : G_HPE), attr);
  }
}
static void header(void) {
  char n[3];
  int8_t a = cx.s[0].hp, b = cx.s[1].hp;
  uint8_t need = 0;
  num_(n, a > 0 ? (uint8_t)a : 0u, 2);
  text_(0, 0, n, 2, T_BRASS);
  bar(2, a, cx.s[0].max, T_BRASS, 0);
  text_(7, 0, "      ", 6, T_CREAM);
  if (fight_kind == FK_VERSUS) {
    need = link_rule(1);
    need = (uint8_t)(((need >> 4) & 3u) == 2u ? 3u : ((need >> 4) & 3u) == 0u ? 1u : 2u);
  }
  if (need > 1u) {
    uint8_t i;
    for (i = 0; i < need && i < 3u; i++) {
      put_((uint8_t)(7u + i), 0, i < fight_wins[0] ? UI_PIP_ON : UI_PIP_OFF, T_BRASS);
      put_((uint8_t)(12u - i), 0, i < fight_wins[1] ? UI_PIP_ON : UI_PIP_OFF, T_CREAM);
    }
  }
  bar(13, b, cx.s[1].max, T_CREAM, 1);
  num_(n, b > 0 ? (uint8_t)b : 0u, 2);
  text_(18, 0, n, 2, T_CREAM);
}
/* ---- the box (rows 1-2) ---- */
static void four_draw(uint8_t x, uint8_t y, uint8_t four, uint8_t attr) {
  uint8_t e;
  for (e = 0; e < 4u; e++) put_((uint8_t)(x + e), y, GLYPH(four & BIT8[e] ? ELEM_CH[e] : ' '), attr);
}
static void box_top(void) {
  char s[8];
  row_clear(1);
  if (eye_tool_ != 0xffu && fight_ui == UI_CHOOSE &&
      !fight_look) { /* the eye: what of theirs could take what you would leave */
    char t[24], nm[14];
    crucible_get_name(cx_tool(1, eye_tool_), nm);
    t[0] = G_EYE;
    t[1] = ' ';
    strcpy(t + 2, eye_kind_ == 3u ? "TAKES:" : eye_kind_ == 2u ? "SPLITS:" : "BREAKS:");
    if (strlen(t) + strlen(nm) < 19u)
      strcat(t, " ");
    else if (strlen(t) + strlen(nm) > 19u)
      t[2] = 0;
    strcat(t, nm); /* (a long name: no space; longer still: the eye and the name) */
    text_(1, 1, t, (uint8_t)strlen(t) > 19u ? 19u : (uint8_t)strlen(t), T_CREAM);
    return;
  }
  text_(1, 1, fo_name, (uint8_t)strlen(fo_name) > 9u ? 9u : (uint8_t)strlen(fo_name), T_CREAM);
  strcpy(s, "BAG ");
  num_(s + 4, cx.s[1].n, cx.s[1].n >= 10u ? 2u : 1u);
  if (fight_ui == UI_INTRO && (fight_kind == FK_VERSUS ? !cx.s[1].n : !fs_ready))
    strcpy(s, "     "); /* (its bag not packed yet: no count) */
  if ((link_rule(3) & 32u) && fight_kind == FK_VERSUS) strcpy(s, "BAG ?"); /* FOG: their bag is hidden */
  text_(10, 1, s, 5, T_BRASS);
  four_draw(16, 1, cx.s[1].four, T_CREAM);
}
static void box_line(void) {
  row_clear(2);
  text_(1, 2, fui_line, (uint8_t)strlen(fui_line), fui_line_attr);
}
static void line(const char *s, uint8_t attr) {
  strncpy(fui_line, s, 19);
  fui_line[19] = 0;
  fui_line_attr = attr;
  box_line();
}
/* ---- the pot's tab (row 8) ---- */
static void tab(void) {
  char s[17];
  uint8_t i, n = 0, k = cx.owner, c;
  if (cx.pot != CX_NONE) {
    const char *who = k == me_() ? "YOU" : fo_name;
    for (i = 0; i < 8u && who[i]; i++) s[n++] = who[i];
    s[n++] = ' ';
    c = cx_cat(cx.pot);
    if (c == CX_C_PLACE)
      s[n++] = G_LOCK;
    else {
      for (i = 0; i < cx.heat; i++) s[n++] = G_HEAT;
      if (!cx.heat) s[n++] = '-';
    }
    if (c == CX_C_LIFE) s[n++] = G_GROW;
    if (cx.sky && (cx_traits(cx.pot) & cx.sky)) s[n++] = G_SKY;
    if ((cx.s[k].tw & CX_TW_LOCK) && cx_phase(k) >= 2u && k != me_()) s[n++] = G_LOCK;
    if (cx.s[k].tw & CX_TW_CHARGE) s[n++] = G_BOLT;
  } else if (fight_kind == FK_BOSS && fo_plan != CX_NONE && (fight_ui == UI_WAIT || fight_ui == UI_INTRO)) {
    strcpy(s, "NEXT IT FORGES");
    n = 14;
  } else if (fight_ui == UI_INTRO && fight_look) {
    strcpy(s, "THEIR BAG");
    n = 9;
  }
  text_(2, 8, " ", 16, T_CREAM);
  if (n) text_((uint8_t)(2u + (16u - n) / 2u), 8, s, n, k == me_() && cx.pot != CX_NONE ? T_BRASS : T_CREAM);
}
/* ---- the name row, the buttons, your four and the turn ---- */
static void name_row(void) {
  char nm[14], s[20];
  uint16_t id = hand_id();
  uint8_t n, st, i;
  row_clear(15);
  if (id == CX_NONE) return;
  crucible_get_name(id, nm);
  n = (uint8_t)strlen(nm);
  st = cx_stars(id);
  memcpy(s, nm, n);
  s[n++] = ' ';
  for (i = 0; i < st; i++) s[n++] = G_STAR;
  s[n] = 0;
  text_((uint8_t)((20u - n) / 2u), 15, s, n, fight_look ? T_CREAM : T_BRASS);
}
static void hints(void) {
  uint8_t mine = fight_ui == UI_CHOOSE && !fight_look;
  row_clear(16);
  if (fight_look) {
    put_(0, 16, UI_B, T_CREAM);
    text_(1, 16, "BACK", 4, T_CREAM);
    text_(7, 16, "THEIR BAG", 9, T_BRASS);
    return;
  }
  if (!mine) {
    text_(1, 16, fight_ui == UI_WAIT ? "THEIR TURN" : " ", 10, T_BRASS);
    text_(13, 16, "SE", 2, T_BRASS);
    text_(16, 16, "BAG", 3, T_BRASS);
    return;
  }
  put_(0, 16, UI_A, T_CREAM);
  if (cx.pot == CX_NONE) {
    text_(1, 16, fight_mark != 0xffu ? "FORGE" : "MARK", 5, T_CREAM);
    if (fight_mark != 0xffu) {
      put_(7, 16, UI_B, T_CREAM);
      text_(8, 16, "UNDO", 4, T_CREAM);
    }
  } else if (cx.owner == me_()) {
    text_(1, 16, cx_locked_to(0) ? "-" : "BUILD", 5, T_CREAM);
    put_(7, 16, UI_B, T_CREAM);
    text_(8, 16, "POUR", 4, T_CREAM);
  } else {
    text_(1, 16, "ADD", 3, T_CREAM);
    put_(7, 16, UI_B, T_CREAM);
    text_(8, 16, "WAIT", 4, T_CREAM);
  }
  text_(13, 16, "SE", 2, T_BRASS);
  text_(16, 16, "BAG", 3, T_BRASS);
}
static void foot(void) {
  uint8_t i;
  row_clear(17);
  text_(0, 17, "YOURS", 5, T_BRASS);
  four_draw(6, 17, cx.s[0].four, T_BRASS);
  if (fight_ui == UI_CHOOSE)
    for (i = 0; i < fight_pips && i < 6u; i++) put_((uint8_t)(19u - i), 17, UI_PIP_ON, T_CREAM);
}
static void arrow_(uint8_t on) {
  set_sprite_tile(39, SPR_ARROW0);
  set_sprite_prop(39, 0);
  move_sprite(39, on ? 84u : 0u, on ? (uint8_t)(88u + ((sys_time >> 4) & 1u)) : 0u);
}

/* ---- what the move in hand would do (the box's second row and the = cell) ---- */
static uint8_t pv_kind_; /* CX_* outcome, CX_EV_FORGE, or 0xff none */
static uint16_t pv_r_;
static uint8_t pv_known_;
static void preview(void) {
  uint16_t x = hand_id(), r = CX_NONE, y;
  uint8_t o, mk, e;
  char s[24], nm[14];
  pv_kind_ = 0xffu;
  pv_r_ = CX_NONE;
  pv_known_ = 0;
  eye_tool_ = 0xffu;
  eye_kind_ = 0;
  box_top();
  if (fight_ui != UI_CHOOSE || x == CX_NONE) return;
  if (fight_look) {
    strcpy(s, cx_cat(x) == CX_C_PLACE ? "LOCKED WHEN SET" : "THEIRS");
    line(s, T_CREAM);
    return;
  }
  if (cx.pot == CX_NONE && fight_mark != 0xffu &&
      fight_mark != tl_[fight_cursor]) { /* a forge: the marked one + this */
    y = cx_tool(0, fight_mark);
    r = cx_recipe(y, x);
    pv_kind_ = CX_EV_FORGE;
    pv_r_ = r;
    pv_known_ = r != CX_NONE && knows_(x, y);
    if (r == CX_NONE) {
      line("NOTHING", T_CREAM);
      return;
    }
    if (!pv_known_) {
      line("FORGE ?", T_BRASS);
      return;
    }
    crucible_get_name(r, nm);
    strcpy(s, "FORGE ");
    strcat(s, nm);
    line(s, T_BRASS);
  } else {
    o = cx_outcome(0, x, &r);
    pv_kind_ = o;
    pv_r_ = r;
    if (o == CX_BUILD || o == CX_HIJACK)
      pv_known_ = knows_(x, cx.pot);
    else if (o == CX_SPLIT)
      pv_known_ = knows_(x, r);
    else
      pv_known_ = 1;
    if (!pv_known_) {
      line(cx.owner == me_() ? "BUILD ?" : "TRY IT?", T_BRASS);
      return;
    } /* you don't know this pair: a try */
    switch (o) {
    case CX_START: line(fight_mark == tl_[fight_cursor] ? "SET IT ALONE" : "MARK, THEN FORGE", T_BRASS); return;
    case CX_BUILD:
      strcpy(s, "BUILD +");
      s[7] = G_HEAT;
      s[8] = 0;
      break;
    case CX_HIJACK:
      strcpy(s, "HIJACK +");
      s[8] = G_HEAT;
      s[9] = 0;
      break;
    case CX_SPLIT:
      crucible_get_name(r, nm);
      strcpy(s, strlen(nm) > 11u ? "+" : "SPLIT: +");
      strcat(s, nm);
      line(s, T_BRASS);
      return;
    case CX_BREAK: line("BREAK IT", T_BRASS); return;
    default:
      line(cx.owner == me_() ? "NOTHING: BURNS -" : "NOTHING: IT BURNS", T_CREAM);
      if (cx.owner == me_()) {
        fui_line[16] = G_HEAT;
        fui_line[17] = 0;
      }
      return;
    }
    line(s, T_BRASS);
  }
  /* the eye: the first of their things that could take what you would leave standing, as far as you know */
  {
    cx_state *bk = (cx_state *)fr_scratch;
    uint16_t w;
    uint8_t ti[16], n, k;
    if (sizeof(cx_state) > sizeof fr_scratch) return;
    if (pv_kind_ == CX_EV_FORGE) {
      uint8_t a = fight_mark, b = tl_[fight_cursor];
      w = CX_ACT(CX_FORGE, a < b ? a : b, a < b ? b : a);
    } else
      w = CX_ACT(CX_ADD, tl_[fight_cursor], 0);
    memcpy(bk, &cx, sizeof cx);
    cx_do(w);
    if (cx.pot != CX_NONE && cx.owner == 0) {
      n = tools_of(1, ti);
      for (k = 0; k < n; k++) {
        y = cx_tool(1, ti[k]);
        mk = cx_outcome(1, y, &r);
        e = mk == CX_HIJACK && knows_(y, cx.pot) ? 3u : mk == CX_SPLIT && knows_(y, r) ? 2u : mk == CX_BREAK ? 1u : 0u;
        if (e > eye_kind_) {
          eye_kind_ = e;
          eye_tool_ = ti[k];
          if (e == 3u) break;
        }
      }
    }
    memcpy(&cx, bk, sizeof cx);
  }
  box_top();
}

static void end_(uint8_t r);
/* ---- turns ---- */
static void redraw_all(void) {
  header();
  box_top();
  box_line();
  tab();
  name_row();
  hints();
  foot();
}
static uint8_t story_(void) { return fight_kind != FK_VERSUS; }
static void turn_start(void) {
  uint8_t k = cx.act;
  cx_begin_turn();
  fight_look = 0;
  fight_mark = 0xffu;
  tools_now();
  fight_cursor = 0;
  hold_b_ = 0;
  fight_pips = (uint8_t)(4u + (story_() ? player_attr(PA_FOCUS) : 0u));
  if (fight_kind == FK_VERSUS) fight_pips = link_fight_pips();
  pip_t_ = 0;
  partner_ = 0;
  if (k == 0 && fight_kind == FK_BOSS && link_scene_shared()) { /* a shared boss: who answers this turn */
    link_scene_round(cx.turn);
    if (!link_scene_mine()) {
      partner_ = 1;
      fight_ui = UI_WAIT;
      line("THEY ANSWER", T_BRASS);
      redraw_all();
      return;
    }
  }
  if (k == 0) {
    fight_ui = UI_CHOOSE;
    preview();
  } else {
    fight_ui = UI_WAIT;
    think_ = 0;
    vs_got_ = 0;
    if (story_()) {
      fo_plan = (fight_kind == FK_BOSS) ? fs_plan() : CX_NONE;
      if (cx.pot == CX_NONE && fo_plan != CX_NONE && cx_legal(1, fo_plan)) {
        cx_ai_word = fo_plan;
      } /* its telegraphed forge */
      else
        cx_ai_begin(1, fo_seed, fo_know, fo_skill);
    }
    line(fight_kind == FK_VERSUS ? "THEY THINK" : "IT THINKS", T_CREAM);
  }
  redraw_all();
  prev_cur_ = 0xffu;
}
/* a move's line: the long prefix when it fits the box's 19 columns, else the short one (a name is at most 13) */
static void say_(char *s, const char *lng, const char *sht, const char *nm) {
  strcpy(s, (uint8_t)(strlen(lng) + strlen(nm)) <= 19u ? lng : sht);
  strcat(s, nm);
}
/* the move is played: the engine turns, the screen shows what it did */
static void show_after(uint8_t k) {
  char s[24], nm[14];
  uint8_t ev = cx.ev, mine = k == me_(), ph;
  show_t_ = 0;
  fight_ui = UI_SHOW;
  fight_turns_seen++;
  nm[0] = 0;
  if (cx.er != CX_NONE && cx.er < core.items) crucible_get_name(cx.er, nm);
  s[0] = 0;
  switch (ev) {
  case CX_START:
    crucible_get_name(cx.ex, nm);
    say_(s, mine ? "SET: " : "IT SETS ", mine ? "SET:" : "SETS:", nm);
    sound_play(SFX_PICK);
    break;
  case CX_EV_FORGE:
    say_(s, mine ? "FORGED " : "IT MADE ", mine ? "MADE:" : "IT:", nm);
    fui_fx |= FIGHT_FX_TEAR;
    sound_play(SFX_MIX);
    if (mine) fs_note_made(cx.er);
    break;
  case CX_BUILD:
    say_(s, mine ? "BUILT " : "IT ADDS ", mine ? "BUILT:" : "ADDS:", nm);
    fui_fx |= FIGHT_FX_TEAR;
    sound_play(SFX_MIX);
    if (mine) fs_note_made(cx.er);
    break;
  case CX_HIJACK:
    if (!mine)
      fo_took = 1;
    else if (cx.chain > chain_best_)
      chain_best_ = cx.chain;
    say_(s, mine ? "HIJACK! " : "TAKEN! ", mine ? "MINE:" : "TAKEN:", nm);
    fui_fx |= FIGHT_FX_TEAR | (mine ? 0u : FIGHT_FX_CHARGE);
    sound_play(mine ? SFX_NEW : SFX_DENY);
    if (mine) fs_note_made(cx.er);
    if (cx.chain >= 2u) {
      uint8_t n = (uint8_t)strlen(s);
      if (n <= 16u) {
        s[n] = ' ';
        s[n + 1u] = 'X';
        s[n + 2u] = (char)('0' + cx.chain);
        s[n + 3u] = 0;
      }
    }
    break;
  case CX_SPLIT:
    say_(s, mine ? "SPLIT! +" : "IT TOOK ", mine ? "+" : "TOOK:", nm);
    fui_fx |= FIGHT_FX_TEAR;
    sound_play(SFX_SWAP);
    break;
  case CX_BREAK:
    strcpy(s, mine ? "BREAK!" : "IT BROKE YOURS");
    fui_fx |= FIGHT_FX_TEAR | FIGHT_FX_SHAKE;
    sound_play(SFX_CLOSE);
    break;
  case CX_MISS:
    strcpy(s, mine ? "NOTHING. IT BURNS" : "IT MISSES");
    sound_play(SFX_NOTHING);
    break;
  case CX_EV_POUR: {
    char n2[3];
    num_(n2, cx.dmg, cx.dmg >= 10u ? 2u : 1u);
    strcpy(s, mine ? "POUR! THEM -" : "IT POURS: -");
    strcat(s, n2);
  }
    if (mine) dealt_ = (uint16_t)(dealt_ + cx.dmg);
    fui_fx |= mine ? FIGHT_FX_SHAKE | FIGHT_FX_TEAR : FIGHT_FX_FLASH;
    sound_play(mine ? SFX_FLASH : SFX_DENY);
    break;
  default: strcpy(s, mine ? "YOU WAIT" : "IT WAITS"); break;
  }
  line(s, mine ? T_BRASS : T_CREAM);
  for (ph = 0; ph < 2u; ph++)
    if (cx.s[ph].tw && cx_phase(ph) != prev_phase_[ph]) {
      prev_phase_[ph] = cx_phase(ph);
      fui_fx |= FIGHT_FX_PHASE;
      if (ph == 1u) fs_say(SAY_HURT, CX_NONE);
    }
  fight_cursor = 0;
  fight_mark = 0xffu;
  fight_look = 0;
  tools_now();
  redraw_all();
}
static void hash_now(uint8_t *h) {
  if (fight_swap) cx_swap();
  *h = cx_hash();
  if (fight_swap) cx_swap();
}
static void play(uint16_t w) {
  uint8_t k = cx.act;
  pot_was_ = cx.pot;
  owner_was_ = cx.owner;
  fight_word = w;
  if (fight_kind == FK_VERSUS && k == 0) {
    uint8_t h;
    hash_now(&h);
    link_fturn(w, cx.turn, h);
  }
  if (fight_kind == FK_BOSS && k == 0 && !partner_ && link_scene_shared())
    link_scene_send((uint8_t)w, (uint8_t)(w >> 8), 0); /* the partner plays it too */
  cx_do(w);
  show_after(k);
}
/* my turn */
static void choose(uint8_t pressed, uint8_t dt) {
  uint8_t held = joypad(), t;
  if ((pressed & J_SELECT) && !(fight_kind == FK_VERSUS && (link_rule(3) & 32u))) {
    fight_look ^= 1u;
    fight_cursor = 0;
    fight_mark = 0xffu;
    tools_now();
    sound_play(SFX_SWAP);
    preview();
    name_row();
    hints();
    box_line();
    return;
  }
  if (pressed & (J_LEFT | J_RIGHT)) {
    if (nt_)
      fight_cursor = (uint8_t)((pressed & J_LEFT) ? (fight_cursor ? fight_cursor - 1u : nt_ - 1u)
                                                  : (fight_cursor + 1u >= nt_ ? 0u : fight_cursor + 1u));
    sound_play(SFX_MOVE);
    preview();
    name_row();
    box_line();
    return;
  }
  if (fight_look) {
    if (pressed & J_B) {
      fight_look = 0;
      fight_cursor = 0;
      tools_now();
      preview();
      name_row();
      hints();
      box_line();
    }
    return;
  }
  if ((pressed & J_A) && nt_) {
    t = tl_[fight_cursor];
    if (cx.pot == CX_NONE) {
      if (fight_mark == 0xffu) {
        fight_mark = t;
        sound_play(SFX_PICK);
        preview();
        hints();
        box_line();
        return;
      }
      if (fight_mark == t) {
        play(CX_ACT(CX_ADD, t, 0));
        return;
      }
      {
        uint8_t a = fight_mark < t ? fight_mark : t, b = fight_mark < t ? t : fight_mark;
        uint16_t w = CX_ACT(CX_FORGE, a, b);
        if (cx_legal(0, w)) {
          play(w);
          return;
        }
        sound_play(SFX_DENY);
        return;
      }
    }
    if (cx.owner == me_() && cx_locked_to(0)) {
      sound_play(SFX_DENY);
      return;
    }
    play(CX_ACT(CX_ADD, t, 0));
    return;
  }
  if (pressed & J_B) hold_b_ = 1;
  if (hold_b_) {
    if (held & J_B) {
      hold_b_ = (uint8_t)(hold_b_ + dt);
      if (hold_b_ >= FLEE_FRAMES && (fight_kind == FK_DUEL || fight_kind == FK_GAUNTLET) && cx.pot == CX_NONE &&
          fight_mark == 0xffu) {
        fight_end = 3;
        end_(3);
        return;
      }
    } else {
      hold_b_ = 0;
      if (cx.pot == CX_NONE) {
        if (fight_mark != 0xffu) {
          fight_mark = 0xffu;
          sound_play(SFX_UNDO);
          preview();
          hints();
          box_line();
        }
        return;
      }
      play(CX_ACT(cx.owner == me_() ? CX_POUR : CX_PASS, 0, 0));
      return;
    }
  }
  pip_t_ = (uint16_t)(pip_t_ + dt);
  if (pip_t_ >= PIP_FRAMES) {
    pip_t_ = 0;
    if (fight_pips) {
      fight_pips--;
      foot();
      sound_play(SFX_MOVE);
    }
    if (!fight_pips) { /* time: your own pot pours, theirs stands, an empty one takes the first thing you hold */
      fight_mark = 0xffu;
      if (cx.pot == CX_NONE) {
        uint8_t ti[16];
        if (tools_of(0, ti))
          play(CX_ACT(CX_ADD, ti[0], 0));
        else
          play(CX_ACT(CX_PASS, 0, 0));
      } else
        play(CX_ACT(cx.owner == me_() ? CX_POUR : CX_PASS, 0, 0));
    }
  }
}
/* their turn: the AI thinks a little each frame, or the partner's word arrives */
static void wait_(uint8_t dt) {
  uint8_t i;
  think_ = think_ < 250u ? (uint8_t)(think_ + dt) : think_;
  if (fight_kind == FK_VERSUS) {
    uint16_t w;
    uint8_t h, t = cx.turn;
    if (link_xturn_in(&w, t, &h)) {
      uint8_t mine;
      hash_now(&mine);
      if (h != mine) {
        link_fsync_ask();
        return;
      }
      if (!cx_legal(1, w)) w = CX_ACT(cx.pot == CX_NONE ? CX_PASS : cx.owner == 1u ? CX_POUR : CX_PASS, 0, 0);
      play(w);
    }
    return;
  }
  if (partner_) {
    uint8_t p0, p1, c;
    if (link_scene_take(&p0, &p1, &c)) {
      uint16_t w = (uint16_t)(p0 | ((uint16_t)p1 << 8));
      if (!cx_legal(0, w)) w = CX_ACT(cx.pot == CX_NONE ? CX_PASS : cx.owner == 0u ? CX_POUR : CX_PASS, 0, 0);
      play(w);
    }
    return;
  } /* (the watcher's SELECT and B: fight_tick) */
  for (i = 0; i < 3u; i++)
    if (cx_ai_step()) break;
  if (cx_ai_word != CX_NONE && think_ >= THINK_MIN) play(cx_ai_word);
}
/* ---- the end screen: big letters (2x high, made from the font into the merge row's cell tiles 0..47, free while
 * those cells are blank and the face is gone), what you made with NEW tags, a stat, the buttons ---- */
static void big_text(uint8_t y, const char *s, uint8_t colour) {
  char seen[24];
  uint8_t ns = 0, i, j, n = (uint8_t)strlen(s), x0, g[16], t[32], k, m, row[20], at[20];
  if (n > 20u) n = 20u;
  x0 = (uint8_t)((20u - n) / 2u);
  for (i = 0; i < n; i++) {
    char c = s[i];
    if (c == ' ') {
      row[i] = GLYPH(' ');
      at[i] = T_CREAM;
      continue;
    }
    for (j = 0; j < ns; j++)
      if (seen[j] == c) break;
    if (j == ns && ns < 24u) {
      seen[ns] = c;
      VBK_REG = 0;
      get_bkg_data(GLYPH(c), 1, g);
      for (k = 0; k < 16u; k++) {
        m = (uint8_t)(g[(k >> 1) * 2u] | g[(k >> 1) * 2u + 1u]);
        t[k * 2u] = colour & 1u ? m : 0u;
        t[k * 2u + 1u] = colour & 2u ? m : 0u;
      }
      VBK_REG = 0;
      set_bkg_data((uint8_t)(ns * 2u), 2, t);
      ns++;
    }
    row[i] = (uint8_t)(j * 2u);
    at[i] = 7u;
  }
  row_clear(y);
  row_clear((uint8_t)(y + 1u));
  VBK_REG = 1;
  set_bkg_tiles(x0, y, n, 1, at);
  set_bkg_tiles(x0, (uint8_t)(y + 1u), n, 1, at);
  VBK_REG = 0;
  set_bkg_tiles(x0, y, n, 1, row);
  for (i = 0; i < n; i++)
    if (s[i] != ' ') row[i]++;
  set_bkg_tiles(x0, (uint8_t)(y + 1u), n, 1, row);
}
static const uint8_t MADE_X[3] = {2, 8, 14};
static uint8_t made_cell(uint8_t i) {
  return fs_made_n == 1u ? 1u : fs_made_n == 2u ? (uint8_t)(i ? 2u : 0u) : i;
} /* 0 CL, 1 CF, 2 CN */
static void end_draw(void) {
  char s[20], n2[4];
  uint8_t i;
  fui_fx = (uint8_t)((fui_fx & (uint8_t)~FIGHT_FX_FACE) | FIGHT_FX_NOFACE);
  arrow_(0);
  for (i = 1; i < 18u; i++)
    if (i < 3u || i == 8u || i >= 14u || i == 9u) row_clear(i);
  header();
  big_text(1, fe_big, fe_win ? 2u : 3u);
  if (fight_kind == FK_VERSUS) {
    strcpy(fe_sub, "ROUNDS ");
    num_(n2, fight_wins[0], 1);
    strcat(fe_sub, n2);
    strcat(fe_sub, " TO ");
    num_(n2, fight_wins[1], 1);
    strcat(fe_sub, n2);
  }
  if (!fe_stat[0]) {
    strcpy(s, "POURED ");
    num_(n2, dealt_ > 99u ? 99u : dealt_, dealt_ >= 10u ? 2u : 1u);
    strcat(s, n2);
    if (chain_best_ >= 2u) {
      strcat(s, "  CHAIN ");
      n2[0] = (char)('0' + chain_best_);
      n2[1] = 0;
      strcat(s, n2);
    }
    strcpy(fe_stat, s);
  }
  text_(1, 9, fs_made_n ? "MADE IN THE FIGHT" : "NOTHING NEW MADE", 17, T_BRASS);
  for (i = 0; i < fs_made_n && i < 3u; i++) text_(MADE_X[made_cell(i)], 14, "NEW", 3, T_CREAM);
  centre_(15, fe_sub, T_CREAM);
  centre_(16, fe_stat, T_BRASS);
  if (fight_ui == UI_CHOICE) {
    crucible_story *st = talk_saga();
    uint8_t deep = st->lucid < 64u && (DIV_REG & 1u);
    put_(2, 17, UI_A, T_CREAM);
    put_(3, 17, GLYPH(deep ? '-' : '+'), T_CREAM);
    put_(8, 17, UI_B, T_CREAM);
    put_(9, 17, GLYPH(deep ? '+' : '-'), T_CREAM);
    text_(14, 17, "SE", 2, T_BRASS);
    put_(16, 17, GLYPH('/'), T_CREAM);
  } else if (fight_kind == FK_VERSUS) {
    put_(1, 17, UI_A, T_CREAM);
    text_(2, 17, "AGAIN", 5, T_CREAM);
    put_(12, 17, UI_B, T_CREAM);
    text_(13, 17, "LEAVE", 5, T_CREAM);
  } else {
    put_(1, 17, UI_A, T_CREAM);
    text_(2, 17, "OK", 2, T_CREAM);
  }
}
static void end_(uint8_t r) {
  show_t_ = 0;
  fs_end(r);
  end_draw();
}
/* the link's result card in the end screen's frame (the bench band, the pixel title): crucible_link.c */
void fight_card(const char *big, uint8_t win, const char *l1, const char *l2) BANKED {
  uint8_t y;
  char t[21];
  win_pos_y = 144u;
  SCY_REG = 0;
  scene_draw(SCENE_BENCH);
  for (y = 0; y < 18u; y++)
    if (y < 3u || y == 8u || y == 9u || y >= 14u) row_clear(y);
  strncpy(fe_big, big, 13);
  fe_big[13] = 0;
  big_text(1, fe_big, win ? 2u : 3u); /* (fe_big: what the card says, for a reader) */
  strncpy(t, l1, 20);
  t[20] = 0;
  centre_(9, t, T_BRASS);
  strncpy(t, l2, 20);
  t[20] = 0;
  centre_(15, t, T_CREAM);
  put_(1, 17, UI_A, T_CREAM);
  text_(2, 17, "MENU", 4, T_CREAM);
}

/* ---- rounds and the end ---- */
static void vs_sides(void) { /* the versus' sides for this round (host order), the guest swapped to see itself */
  uint16_t bag[CX_BAG], obag[CX_BAG];
  uint8_t n, on;
  {
    uint8_t first = link_fight_first(fight_round); /* in the host's order */
    link_bags(bag, &n, obag, &on); /* host's then guest's */
    cx_side_set(0, bag, n, (int8_t)link_fight_hp(0), 0);
    cx_side_set(1, obag, on, (int8_t)link_fight_hp(1), 0);
    cx_begin(first);
    link_round_reset();
    fight_swap = link_role == LINK_HOST ? 0u : 1u;
    if (fight_swap) cx_swap();
  }
}
static void round_open(void) {
  if (fight_kind == FK_VERSUS && (fight_round || intro_k_ != 2u)) vs_sides();
  intro_k_ = 0;
  fight_look = 0;
  prev_phase_[0] = prev_phase_[1] = 0;
  fui_fx = (uint8_t)((fui_fx & (uint8_t)~FIGHT_FX_FACE) | FIGHT_FX_NOFACE | FIGHT_FX_TEAR);
  turn_start();
}
static void round_over(uint8_t r) { /* r for side 0: 1 won, 2 lost, 3 a draw */
  char s[20];
  if (fight_kind == FK_VERSUS) {
    if (r == 1u)
      fight_wins[0]++;
    else if (r == 2u)
      fight_wins[1]++;
    if (link_fight_over(r)) {
      fight_round++;
      fight_ui = UI_ROUND;
      show_t_ = 0;
      strcpy(s, r == 1u ? "ROUND: YOURS" : r == 2u ? "ROUND: THEIRS" : "ROUND: EVEN");
      line(s, T_BRASS);
      redraw_all();
      return;
    }
    end_(fight_wins[0] > fight_wins[1] ? 1u : fight_wins[0] < fight_wins[1] ? 2u : 5u);
    return; /* the match's result (a drawn one: NEITHER FALLS) */
  }
  if (fight_kind == FK_GAUNTLET && r == 1u && fight_gauntlet < 3u) {
    fight_ui = UI_ROUND;
    show_t_ = 0;
    line("ANOTHER COMES", T_CREAM);
    redraw_all();
    return;
  }
  end_(r == 3u ? 5u : r);
}
static void intro_draw(void);
static uint8_t finish(uint8_t r) {
  crucible_load_font();
  arrow_(0);
  avatar_still = 0;
  if (fight_kind == FK_BOSS) link_scene_end();
  if (r == FIGHT_WON) time_mark_act();
  return r;
}

/* ---- the API crucible.c calls ---- */
void fight_open(uint8_t f, uint8_t level, uint8_t nemesis) BANKED {
  fight_dbg = 0;
  fui_fx = FIGHT_FX_FACE | FIGHT_FX_TEAR;
  avatar_still = 1;
  fight_end = 0;
  fight_round = 0;
  fight_wins[0] = fight_wins[1] = 0;
  fight_look = 0;
  fight_mark = 0xffu;
  fight_cursor = 0;
  fight_swap = 0;
  fs_made_n = 0;
  fight_turns_seen = 0;
  fui_gained = 0;
  intro_k_ = 0;
  chain_best_ = 0;
  dealt_ = 0;
  eye_tool_ = 0xffu;
  fe_lost = CX_NONE;
  fo_plan = CX_NONE;
  win_pos_y = 144u;
  SCY_REG = 0; /* the toast window off, the encounter's pan over: the fight's rows are its own */
  scene_draw(SCENE_BENCH);
  icons_load();
  fight_t0 = sys_time;
  fight_ui = UI_INTRO;
  memset(&cx, 0, sizeof cx);
  cx.pot = CX_NONE;
  if (link_scene_watching()) {
    fight_kind = FK_BOSS;
    fs_open_shared();
    intro_draw();
    fs_face();
    fight_t0 = sys_time;
    return;
  } /* pulled into the partner's boss (9.4) */
  else if (link_on && link_started && link_mode == LINK_FIGHT) {
    fight_kind = FK_VERSUS;
    link_peer(fo_name);
    fo_name[8] = 0;
    {
      uint8_t k = (uint8_t)strlen(fo_name);
      while (k && fo_name[k - 1u] == ' ') fo_name[--k] = 0;
    }
    if (!fo_name[0] || !strcmp(fo_name, "YOU")) strcpy(fo_name, "THEM");
    fo_faction = 3; /* (a name padded with spaces, or the default YOU: THEM) */
    strcpy(fui_line, "THE SIGNAL TUNES IN");
    fui_line_attr = T_CREAM;
    avatar_make((uint16_t)link_seed, 3);
  } else {
    if (flow_gauntlet && (f & FIGHT_DUEL)) {
      fight_kind = FK_GAUNTLET;
      fight_gauntlet = 1;
      flow_gauntlet = 0;
    } else
      fight_kind = (f & FIGHT_DUEL) ? FK_DUEL : FK_BOSS;
    fs_open_story((uint8_t)(f & 7u), level, nemesis, (uint8_t)(fight_kind != FK_BOSS));
    intro_draw();
    fs_open_bag();
    if (!fui_line[0]) {
      fs_say(SAY_TAUNT, CX_NONE);
      box_line();
    }
    fs_face();
    fight_t0 = sys_time;
    return; /* (its face has its moment from here) */
  }
  intro_draw();
}
/* the entrance: first who comes (its face, its name, its way), then what it brings (its bag on the carousel; a boss's
 * first forge on the merge row) */
static const char *const STYLE[6] = {"IT LEARNS YOUR POTS", "ITS POTS CHARGE",    "IT TURNS THE SKY",
                                     "IT READ YOUR BAG",    "WHAT IT SETS GROWS", "ITS POTS LOCK"};
static void intro_draw(void) {
  uint8_t y;
  const char *w;
  for (y = 1; y < 18u; y++)
    if (y < 3u || y == 8u || y >= 14u) row_clear(y);
  header();
  fight_look = 0;
  eye_tool_ = 0xffu;
  box_top();
  box_line();
  if (!intro_k_) {
    if (fight_kind != FK_VERSUS) { /* who it is: its faction and what kind of fight */
      char id[21];
      if (fight_kind == FK_GAUNTLET) {
        strcpy(id, "DOOR 1: ");
        id[5] = (char)('0' + fight_gauntlet);
        strcat(id, fo_name);
      } else {
        strcpy(id, fo_name);
        strcat(id, fight_kind == FK_DUEL ? " DUEL" : fo_nemesis ? " NEMESIS" : " BOSS");
      }
      centre_(14, id, T_CREAM);
    }
    w = fight_kind == FK_VERSUS     ? "FROM THE OTHER ROOM"
        : fight_kind == FK_GAUNTLET ? "THREE DOORS. ONE."
        : fight_kind == FK_BOSS     ? STYLE[fo_faction < 6u ? fo_faction : 0u]
                                    : "IT WANTS A FIGHT";
    text_((uint8_t)((20u - strlen(w)) / 2u), 15, w, (uint8_t)strlen(w), T_BRASS);
    put_(1, 16, UI_A, T_CREAM);
    text_(2, 16, "GO ON", 5, T_CREAM);
    if (fight_kind != FK_VERSUS) {
      text_(13, 16, "SE", 2, T_BRASS);
      text_(16, 16, "BAG", 3, T_BRASS);
    }
    return;
  }
  fight_look = 1;
  fight_cursor = 0;
  tools_now();
  tab();
  name_row();
  put_(1, 16, UI_A, T_CREAM);
  text_(2, 16, "FIGHT", 5, T_CREAM);
  text_(8, 16, "<> THEIR BAG", 12, T_BRASS);
  foot();
}
uint8_t fight_tick(uint8_t pressed, uint8_t dt) BANKED {
  uint8_t r;
  SCY_REG = 0; /* the encounter's pan is over: the fight is the whole screen */
  if (fight_kind == FK_VERSUS && fight_ui != UI_INTRO && link_synced()) {
    turn_start();
    return FIGHT_GOING;
  } /* the host's state was adopted: the turn again */
  switch (fight_ui) {
  case UI_INTRO:
    if (fight_kind == FK_VERSUS) {
      r = link_fight_ready();
      if (r == 2u) {
        line("THEIR ROOM DIFFERS", T_CREAM);
        link_end(LINK_LOST);
        return finish(FIGHT_FLED);
      }
      if (link_result()) return finish(FIGHT_FLED);
      if (!r) return FIGHT_GOING;
      if (!intro_k_) {
        uint8_t g[6];
        link_partner_genome(g);
        avatar_make_genome(g);
        fight_t0 = sys_time;
        intro_k_ = 1;
        vs_sides();
        line("THE SIGNAL HOLDS", T_CREAM);
        header();
        box_top();
        return FIGHT_GOING;
      }
    } else {
      if (!intro_k_) intro_k_ = 1;
      if (!fs_ready && fs_prep_step()) box_top();
    } /* its bag is chosen while you look at it */
    if ((pressed & J_SELECT) && fight_kind != FK_VERSUS) {
      fight_ui = UI_KIT;
      arrow_(0);
      open_t_ = 3;
      fui_fx |= FIGHT_FX_NOFACE;
      return FIGHT_GOING;
    }
    if (fight_kind == FK_BOSS && link_scene_wait()) return FIGHT_GOING;
    if (intro_k_ == 1u) { /* who comes */
      if (((uint16_t)(sys_time - fight_t0) >= INTRO_FRAMES || (pressed & J_A)) &&
          (fs_ready || fight_kind == FK_VERSUS)) {
        intro_k_ = 2;
        fight_t0 = sys_time;
        fui_fx = (uint8_t)((fui_fx & (uint8_t)~FIGHT_FX_FACE) | FIGHT_FX_NOFACE);
        intro_draw();
      }
      return FIGHT_GOING;
    }
    if (pressed & (J_LEFT | J_RIGHT)) {
      if (nt_)
        fight_cursor = (uint8_t)((pressed & J_LEFT) ? (fight_cursor ? fight_cursor - 1u : nt_ - 1u)
                                                    : (fight_cursor + 1u >= nt_ ? 0u : fight_cursor + 1u));
      sound_play(SFX_MOVE);
      name_row();
    }
    if ((uint16_t)(sys_time - fight_t0) >= 360u || (pressed & J_A))
      round_open(); /* what it brings: until A (6 s at most) */
    return FIGHT_GOING;
  case UI_KIT: /* (drawn once the cells under it have cleared: a frame or two) */
    if (open_t_) {
      if (!--open_t_) (void)kit_open(1);
      return FIGHT_GOING;
    }
    if (!kit_tick(pressed)) return FIGHT_GOING;
    scene_draw(SCENE_BENCH);
    icons_load();
    fight_ui = UI_INTRO;
    intro_k_ = 2;
    fight_t0 = sys_time;
    if (fight_gauntlet <= 1u) fs_open_bag();
    intro_draw();
    return FIGHT_GOING; /* (the bag screen may have changed your pins) */
  case UI_CHOOSE:
    arrow_(1);
    if (fight_kind == FK_BOSS && link_scene_shared()) {
      if (!link_scene_mine()) {
        partner_ = 1;
        fight_ui = UI_WAIT;
        fight_mark = 0xffu;
        line("THEY TAKE IT", T_CREAM);
        hints();
        return FIGHT_GOING;
      } /* the watcher's veto */
      if (ls_nudge != 0xffu) {
        char t[20], nm[14];
        uint8_t i = ls_nudge < nt_ ? ls_nudge : 0u;
        ls_nudge = 0xffu;
        crucible_get_name(cx_tool(0, tl_[i]), nm);
        nm[7] = 0;
        strcpy(t, "THEY POINT: ");
        strcat(t, nm);
        line(t, T_CREAM);
      }
    }
    if (fight_cursor != prev_cur_ || fight_mark != prev_mark_ || fight_look != prev_look_) {
      prev_cur_ = fight_cursor;
      prev_mark_ = fight_mark;
      prev_look_ = fight_look;
    }
    choose(pressed, dt);
    if (link_on && fight_kind == FK_VERSUS && link_result()) return finish(FIGHT_FLED);
    return FIGHT_GOING;
  case UI_WAIT:
    arrow_(0);
    if (partner_) { /* watching the partner answer (9.4): SELECT points at your thing in hand, B held a second takes the turn (once a phase) */
      if (pressed & J_SELECT) {
        link_scene_nudge(fight_cursor);
        sound_play(SFX_MOVE);
      }
      if (joypad() & J_B) {
        if (hold_b_ < 250u) hold_b_ = (uint8_t)(hold_b_ + dt);
        if (hold_b_ >= FLEE_FRAMES && link_scene_veto()) {
          partner_ = 0;
          hold_b_ = 0;
          fight_ui = UI_CHOOSE;
          sound_play(SFX_SWAP);
          line("YOU TAKE IT", T_BRASS);
          hints();
          preview();
          return FIGHT_GOING;
        }
      } else
        hold_b_ = 0;
    }
    wait_(dt);
    if ((pressed & J_SELECT) && !partner_ && !(fight_kind == FK_VERSUS && (link_rule(3) & 32u))) {
      fight_look ^= 1u;
      fight_cursor = 0;
      tools_now();
      name_row();
      hints();
    }
    if (pressed & (J_LEFT | J_RIGHT)) {
      if (nt_)
        fight_cursor = (uint8_t)((pressed & J_LEFT) ? (fight_cursor ? fight_cursor - 1u : nt_ - 1u)
                                                    : (fight_cursor + 1u >= nt_ ? 0u : fight_cursor + 1u));
      name_row();
    }
    if (link_on && fight_kind == FK_VERSUS && link_result()) return finish(FIGHT_FLED);
    return FIGHT_GOING;
  case UI_SHOW:
    arrow_(0);
    show_t_ = (uint16_t)(show_t_ + dt);
    if (show_t_ < SHOW_FRAMES && !(pressed & J_A)) return FIGHT_GOING;
    if (fight_kind == FK_BOSS && fo_nemesis && fo_b->met < 3u && cx_phase(1) >= 1u && cx.s[1].hp > 0) {
      fight_end = 3;
      end_(4);
      return FIGHT_GOING;
    } /* the nemesis flees at its first phase */
    r = cx_over();
    if (r) {
      round_over(r);
      return FIGHT_GOING;
    }
    turn_start();
    return FIGHT_GOING;
  case UI_ROUND:
    show_t_ = (uint16_t)(show_t_ + dt);
    if (show_t_ < 120u && !(pressed & J_A)) return FIGHT_GOING;
    if (fight_kind == FK_GAUNTLET) {
      fs_open_gauntlet_next();
      fui_fx |= FIGHT_FX_FACE;
      fight_ui = UI_INTRO;
      intro_k_ = 1;
      fight_t0 = sys_time;
      intro_draw();
      return FIGHT_GOING;
    }
    round_open();
    return FIGHT_GOING;
  case UI_CHOICE:
    if (fs_choice(pressed)) {
      fight_ui = UI_END;
      show_t_ = 0;
      end_draw();
    }
    return FIGHT_GOING;
  case UI_END:
    arrow_(0);
    show_t_ = (uint16_t)(show_t_ + dt);
    if (fight_kind == FK_VERSUS) { /* AGAIN: both ask, a new match; LEAVE: the session ends with this result */
      if (link_result()) return finish(FIGHT_FLED);
      if ((pressed & J_A) && show_t_ >= 40u) {
        link_rematch(1);
        text_(0, 17, " ", 20, T_CREAM);
        text_(1, 17, "WAITING FOR THEM", 16, T_BRASS);
        put_(18, 17, UI_B, T_CREAM);
      }
      if ((pressed & J_B) && show_t_ >= 40u) {
        link_rematch(0);
        return finish(FIGHT_FLED);
      }
      if (link_rematch_go()) {
        fight_round = 0;
        fight_wins[0] = fight_wins[1] = 0;
        fs_made_n = 0;
        chain_best_ = 0;
        dealt_ = 0;
        vs_sides();
        intro_k_ = 2;
        {
          uint8_t y;
          for (y = 1; y < 18u; y++)
            if (y < 3u || y == 8u || y == 9u || y >= 14u) row_clear(y);
        }
        round_open();
      }
      return FIGHT_GOING;
    }
    if (show_t_ < 150u && !((pressed & J_A) && show_t_ >= 40u))
      return FIGHT_GOING; /* (an A mashed through the last move does not skip it) */
    if (pl.pts || fui_gained) {
      fui_gained = 0;
      fight_ui = UI_LEVEL;
      open_t_ = 3;
      fui_fx |= FIGHT_FX_NOFACE;
      return FIGHT_GOING;
    }
    if (cru_story_state(&core, talk_saga()) != CRU_RUN_ON && story_on) return finish(FIGHT_OVER);
    return finish(fo_end == 1u ? FIGHT_WON : FIGHT_FLED);
  case UI_LEVEL:
    if (open_t_) {
      if (!--open_t_) (void)level_open();
      return FIGHT_GOING;
    }
    if (!level_tick(pressed)) return FIGHT_GOING;
    player_save();
    if (cru_story_state(&core, talk_saga()) != CRU_RUN_ON && story_on) return finish(FIGHT_OVER);
    return finish(fo_end == 1u ? FIGHT_WON : FIGHT_FLED);
  }
  return FIGHT_GOING;
}
/* the six cells: the merge row and the carousel */
void fight_view(crucible_cells *v) BANKED {
  uint8_t c, k = fight_look ? 1u : 0u, i;
  for (c = 0; c < CRU_CELLS; c++) {
    v->kind[c] = CRU_K_BLANK;
    v->id[c] = 0;
  }
  v->message = 0;
  v->sign = 0;
  if (fight_ui == UI_KIT || fight_ui == UI_LEVEL || (fight_ui == UI_INTRO && intro_k_ < 2u) || fight_ui == UI_END ||
      fight_ui == UI_CHOICE) {
    if (fight_ui == UI_END || fight_ui == UI_CHOICE)
      for (i = 0; i < fs_made_n && i < 3u; i++) {
        c = (uint8_t)(CRU_CL + made_cell(i));
        v->kind[c] = CRU_K_ITEM;
        v->id[c] = fs_made_ids[i];
      }
    if (fight_ui == UI_END || fight_ui == UI_CHOICE) v->message = 1u << CRU_CF;
    return;
  }
  if (fight_ui == UI_INTRO) { /* what it brings */
    if (fight_kind == FK_BOSS && fo_plan != CX_NONE) {
      v->kind[CRU_CA] = CRU_K_ITEM;
      v->id[CRU_CA] = cx_tool(1, CX_X(fo_plan));
      v->kind[CRU_CB] = fo_faction == FO_GHOST ? CRU_K_QUESTION : CRU_K_ITEM;
      v->id[CRU_CB] = cx_tool(1, CX_Y(fo_plan));
      v->kind[CRU_CR] = CRU_K_QUESTION;
    }
    if (nt_) {
      i = fight_cursor;
      v->kind[CRU_CF] = CRU_K_ITEM;
      v->id[CRU_CF] = cx_tool(1, tl_[i]);
      v->message |= 1u << CRU_CF;
      if (nt_ > 1u) {
        v->kind[CRU_CN] = CRU_K_ITEM;
        v->id[CRU_CN] = cx_tool(1, tl_[i + 1u >= nt_ ? 0u : i + 1u]);
      }
      if (nt_ > 2u) {
        v->kind[CRU_CL] = CRU_K_ITEM;
        v->id[CRU_CL] = cx_tool(1, tl_[i ? i - 1u : nt_ - 1u]);
      }
    }
    return;
  }
  v->kind[CRU_CA] = v->kind[CRU_CB] = v->kind[CRU_CR] = CRU_K_EMPTY;
  if (cx.pot != CX_NONE) {
    v->kind[CRU_CB] = CRU_K_ITEM;
    v->id[CRU_CB] = cx.pot;
  }
  if (fight_ui == UI_SHOW) {
    uint8_t ev = cx.ev;
    if (ev == CX_EV_POUR) {
      v->kind[CRU_CB] = CRU_K_ITEM;
      v->id[CRU_CB] = cx.ex;
    } else if (ev != CX_EV_PASS) {
      v->kind[CRU_CA] = CRU_K_ITEM;
      v->id[CRU_CA] = cx.ex;
      if (pot_was_ != CX_NONE) {
        v->kind[CRU_CB] = CRU_K_ITEM;
        v->id[CRU_CB] = pot_was_;
      } else if (ev == CX_EV_FORGE) {
        v->kind[CRU_CB] = CRU_K_EMPTY;
      }
      if (ev == CX_BUILD || ev == CX_HIJACK || ev == CX_EV_FORGE || ev == CX_SPLIT) {
        v->kind[CRU_CR] = CRU_K_ITEM;
        v->id[CRU_CR] = cx.er;
        v->message |= 1u << CRU_CR;
      } else if (ev == CX_BREAK || ev == CX_MISS)
        v->kind[CRU_CR] = CRU_K_TRIED;
      else if (ev == CX_START) {
        v->kind[CRU_CA] = CRU_K_EMPTY;
        v->kind[CRU_CB] = CRU_K_ITEM;
        v->id[CRU_CB] = cx.ex;
      }
    }
  } else if (fight_ui == UI_CHOOSE && !fight_look && nt_) {
    uint16_t x = hand_id();
    if (cx.pot == CX_NONE && fight_mark != 0xffu) {
      v->kind[CRU_CA] = CRU_K_ITEM;
      v->id[CRU_CA] = cx_tool(0, fight_mark);
      if (fight_mark != tl_[fight_cursor]) {
        v->kind[CRU_CB] = CRU_K_ITEM;
        v->id[CRU_CB] = x;
      }
    } else {
      v->kind[CRU_CA] = CRU_K_ITEM;
      v->id[CRU_CA] = x;
    }
    if (pv_kind_ == CX_START)
      v->kind[CRU_CR] = CRU_K_EMPTY;
    else if (pv_kind_ == CX_BREAK || pv_kind_ == CX_MISS || (pv_kind_ == CX_EV_FORGE && pv_r_ == CX_NONE))
      v->kind[CRU_CR] = CRU_K_TRIED;
    else if (pv_known_ && pv_r_ != CX_NONE) {
      v->kind[CRU_CR] = CRU_K_ITEM;
      v->id[CRU_CR] = pv_r_;
    } else
      v->kind[CRU_CR] = CRU_K_QUESTION;
  } else if (fight_ui == UI_WAIT) {
    v->kind[CRU_CR] = CRU_K_QUESTION;
    v->sign = 1;
    if (fight_kind == FK_BOSS && cx.pot == CX_NONE && fo_plan != CX_NONE && CX_X(fo_plan) < cx.s[1].n &&
        CX_Y(fo_plan) < cx.s[1].n) { /* its telegraphed forge */
      v->kind[CRU_CA] = CRU_K_ITEM;
      v->id[CRU_CA] = cx_tool(1, CX_X(fo_plan));
      v->kind[CRU_CB] = fo_faction == FO_GHOST && cx_phase(1) >= 1u ? CRU_K_QUESTION : CRU_K_ITEM;
      v->id[CRU_CB] = cx_tool(1, CX_Y(fo_plan));
      v->sign = 0;
    }
  }
  /* the carousel */
  if (nt_) {
    i = fight_cursor;
    v->kind[CRU_CF] = CRU_K_ITEM;
    v->id[CRU_CF] = cx_tool(k, tl_[i]);
    v->message |= 1u << CRU_CF;
    if (nt_ > 1u) {
      v->kind[CRU_CN] = CRU_K_ITEM;
      v->id[CRU_CN] = cx_tool(k, tl_[i + 1u >= nt_ ? 0u : i + 1u]);
    }
    if (nt_ > 2u) {
      v->kind[CRU_CL] = CRU_K_ITEM;
      v->id[CRU_CL] = cx_tool(k, tl_[i ? i - 1u : nt_ - 1u]);
    }
  }
}
uint8_t fight_fx(void) BANKED {
  uint8_t f = fui_fx;
  fui_fx = 0;
  return f;
}
uint8_t fight_state(void) BANKED { return fight_ui == UI_SHOW ? FIGHT_SHOW : FIGHT_CUE; }
uint16_t fight_lost(void) BANKED { return fe_lost; }
/* The faction that dislikes you most, if any does (0xff: none). */
uint8_t fight_rival(void) BANKED {
  crucible_story *s = talk_saga();
  uint8_t f, best = 0xffu;
  int8_t low = -8;
  for (f = 0; f < CRU_FACTIONS; f++)
    if (s->stand[f] <= low) {
      low = s->stand[f];
      best = f;
    }
  return best;
}
/* the "?" while they think: flat steps between two inks (no glow) */
void fight_shimmer(uint16_t *p) BANKED {
  p[0] = 0x7fffu;
  p[1] = (sys_time & 16u) ? (8u | (25u << 5) | (31u << 10)) : (28u | (6u << 5) | (24u << 10));
  p[2] = (8u | (25u << 5) | (31u << 10));
  p[3] = (9u | (3u << 5) | (18u << 10));
}
