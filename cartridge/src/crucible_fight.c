/* FIGHT: every fight runs on one engine (crucible_fight_rules.c: the triangle read from the traits, kits, clashes,
 * passives, arenas) and comes to the bench from play (crucible_flow.c), never from a menu. This file is the screen and
 * the flow of a fight: the duel (a faction's figure, the duel AI), the link versus (the same rules in lockstep), and
 * the boss (crucible_fight_boss.c is its engine). crucible.c draws the four cells this reports (fight_view) and the
 * opponent's face as sprites, and plays the effects this raises (fight_fx); the player's own face is 36 BG tiles.
 *
 * The duel screen (docs/fight-system.md 1.5, adapted to the cartridge's cells):
 *   row 0      the opponent's name, its HP bar, its HP
 *   row 1      the ticker
 *   rows 2-7   your face (BG, left) | its face (sprites) | its focus, passive patterns, lock mark and open hand (right)
 *   rows 9-12  a fuse's product (left cell) | your card VS its card (middle and right cells, when they flip)
 *   rows 13-16 your hand (left: glyph, power, name; the focused card turns in the cell) | your HP, focus, pips, patterns
 *   row 17     what the focused card is, and the buttons
 * Glyphs: HUM '=', WALL '#', DREAM '*' (the font's own; no new tiles). */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_data.h"
#include "crucible_ui.h"
#include "crucible_scene.h"
#include "crucible_state.h"
#include "crucible_lines.h"
#include "crucible_truths.h"
#include "crucible_lines.h"
#include "crucible_avatar.h"
#include "crucible_storyrun.h"
#include "crucible_fight.h"
#include "crucible_fight_rules.h"
#include "crucible_fight_boss.h"
#include "crucible_fight_int.h"
#include "crucible_player.h"
#include "crucible_link.h"
#include "crucible_link_scene.h"
#include "crucible_time.h"
#define T_CREAM 7u
#define T_BRASS 15u
#define PIP_FRAMES 150u /* frames per pip: four pips, ten seconds a turn */
#define SHOW_FRAMES 100u /* the outcome holds this long (A skips) */
#define INTRO_FRAMES 60u /* the tear in; SELECT during it opens the kit */
#define FLEE_FRAMES 60u /* B held this long flees a duel */
uint8_t room_family(void) BANKED;
uint16_t cri_shelf(crucible_core *c, uint16_t pos) BANKED;
uint8_t cri_owned(crucible_core *c, uint16_t id) BANKED;
uint8_t cri_depth(crucible_core *c, uint16_t id) BANKED;
uint8_t kit_open(uint8_t where) BANKED; /* crucible_fight_kit.c */
uint8_t kit_tick(uint8_t pressed) BANKED;
uint8_t level_open(void) BANKED;
uint8_t level_tick(uint8_t pressed) BANKED;

/* ---- shared with the boss screen (crucible_fight_bossui.c; crucible_fight_int.h) ---- */
uint8_t fui_fx, fui_faction, fui_flip, fui_partner, fui_intro_t, fui_end_line, fui_gained, fui_pips, fui_ready, fui_won,
    fui_lost, fui_tlen, fui_tat, fui_tt;
uint16_t fui_pip_t, fui_show_t, fui_xp;
char fui_ticker[96], fui_who[12];
crucible_boss *fui_b;
/* ---- what a harness reads (the .noi) ---- */
uint8_t fight_kind, fight_ui, fight_cursor, fight_me, fight_them, fight_dbg, fight_pips, fight_tutor, fight_glitch,
    fight_end, fight_swap;
uint8_t fight_fuse = 0xffu; /* the card marked for a fuse (0xff: none) */
uint16_t fight_seed, fight_t0;
uint8_t fight_room = 0xffu, fight_force_family = 0xffu, fight_force_part = 0xffu,
        fight_force_special = 0xffu; /* the room the arena came from; test pokes (0xff: none) */
uint8_t fight_gauntlet, flow_gauntlet;
/* a gauntlet's duel (1..3); the director asks for one */ /* fight_t0: sys_time when the fight opened (its intro counts from there) */
static const char *const FNAME[6] = {"PROGRAM", "DAEMON", "GHOST", "AI", "OPERATOR", "RELIC"};
static const char STANCE_CH[4] = {'=', '#', '*', ' '};
static const uint8_t BIT8[8] = {1, 2, 4, 8, 16, 32, 64, 128}; /* (field >> var) & 1 miscompiles on sdcc: masks */
static const uint16_t BIT16[16] = {1u,   2u,   4u,    8u,    16u,   32u,   64u,    128u,
                                   256u, 512u, 1024u, 2048u, 4096u, 8192u, 16384u, 32768u};
/* passives as two letters (their pattern shows as pips) */
static const char PCODE[FP_COUNT][3] = {"EC", "PR", "SC", "VI", "CA", "ST", "SA", "UN", "LU", "NO", "OV", "ME"};
static const uint8_t PNEED[FP_COUNT] = {2, 3, 5, 2, 3, 3, 3, 2, 3, 2, 2, 3};
/* the passive each faction teaches at FRIEND tier (section 4) */
static const uint8_t TEACH[6] = {FP_MEMORY, FP_OVERCLOCK, FP_NOCLIP, FP_STALEMATE, FP_SALVE, FP_CATALYST};
static const uint8_t LIKE_CAT[6] = {5u, 3u, 2u, 1u, 4u, 6u};

static uint8_t tear_t_, show_t_lo_, ai_lock_at_, ai_locked_;
uint8_t fight_hold; /* frames B has been held (harness) */
static uint8_t lucid_flag_, vs_got_, vs_sent_t_;
static uint16_t cosm_ = 0x2b1du;

static uint16_t lost_ = 0xffffu;

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
static uint8_t cosm(void) {
  cosm_ ^= (uint16_t)(cosm_ << 7);
  cosm_ ^= (uint16_t)(cosm_ >> 9);
  cosm_ ^= (uint16_t)(cosm_ << 8);
  return (uint8_t)cosm_;
} /* looks only: never fight state */
static void clear_rows(uint8_t y0, uint8_t y1) {
  for (; y0 <= y1; y0++) text_(0, y0, " ", 20, T_CREAM);
}

/* ---- the ticker (row 1): what it says, torn letters when the scene glitches ---- */
static void say_text(const char *s) {
  strncpy(ticker_, s, sizeof ticker_ - 1u);
  ticker_[sizeof ticker_ - 1u] = 0;
  ticker_len_ = (uint8_t)strlen(ticker_);
  ticker_at_ = 0;
  ticker_t_ = 0;
}
static void say(uint8_t intent, uint16_t item) {
  const char *slots[CRUCIBLE_TEXT_SLOTS];
  char item_n[14];
  uint8_t i, j;
  if (item != CRU_NONE)
    crucible_get_name(item, item_n);
  else
    strcpy(item_n, "SOMETHING");
  memset(slots, 0, sizeof slots);
  slots[0] = who_;
  slots[1] = item_n;
  ticker_len_ = crucible_text_line(
      crucible_line_pick((uint8_t)(faction_ + 1u), intent, (uint16_t)((uint16_t)cosm() << 8 | cosm())), ticker_,
      sizeof ticker_, slots);
  for (i = 0, j = 0; i < ticker_len_; i++) {
    char c = ticker_[i];
    if ((uint8_t)c >= ' ') ticker_[j++] = c;
  }
  ticker_[j] = 0;
  ticker_len_ = j;
  ticker_at_ = 0;
  ticker_t_ = 0;
}
static void ticker(uint8_t dt) {
  char row[19];
  uint8_t i, k;
  ticker_t_ = (uint8_t)(ticker_t_ + dt);
  if (ticker_t_ < 5u) return;
  ticker_t_ = 0;
  for (i = 0; i < 18u; i++) {
    k = (uint8_t)(ticker_at_ + i);
    row[i] = k < 18u ? ' ' : (uint8_t)(k - 18u) < ticker_len_ ? ticker_[k - 18u] : ' ';
  }
  row[18] = 0;
  if (tear_t_) {
    tear_t_ = tear_t_ > 5u ? (uint8_t)(tear_t_ - 5u) : 0u;
    for (i = 0; i < 18u; i += 5u) row[(uint8_t)(i + (cosm() & 3u))] = (char)('!' + (cosm() & 15u));
  }
  text_(1, 1, row, 18, T_CREAM);
  if (++ticker_at_ > (uint8_t)(ticker_len_ + 18u)) ticker_at_ = 0;
}
static void status(const char *s, uint8_t attr) {
  text_(0, 17, " ", 20, T_CREAM);
  text_(1, 17, s, (uint8_t)strlen(s), attr);
}

/* ---- the duel's panels ---- */
static void hp_bar(uint8_t x, uint8_t y, int8_t hp, uint8_t max) {
  uint8_t i, n = 0, t = 0, h8;
  if (hp < 0) hp = 0;
  h8 = (uint8_t)((uint8_t)hp << 3); /* cells lit: i with hp*8 > i*max, by repeated addition */
  for (i = 0; i < 8u; i++) {
    if (h8 > t) n++;
    t = (uint8_t)(t + max);
  }
  for (i = 0; i < 8u; i++) put_((uint8_t)(x + i), y, GLYPH(i < n ? '#' : '.'), i < n ? T_CREAM : T_BRASS);
}
static void pips_draw(uint8_t x, uint8_t y, uint8_t n, uint8_t max) {
  uint8_t i;
  for (i = 0; i < max; i++) put_((uint8_t)(x + i), y, GLYPH(i < n ? '!' : ' '), n <= 1u ? T_CREAM : T_BRASS);
}
/* focus as F n/max (four tiles) */
static void focus_draw(uint8_t x, uint8_t y, uint8_t f) {
  char s[5];
  s[0] = 'F';
  s[1] = (char)('0' + f);
  s[2] = '/';
  s[3] = (char)('0' + 3u + ((fr.rules & FR_R_NIGHT) ? 1u : 0u));
  s[4] = 0;
  text_(x, y, s, 4, f ? T_CREAM : T_BRASS);
}
/* a passive: two letters, then its public pattern as pips (unlocked: all lit) */
static void passive_draw(uint8_t x, uint8_t y, const fr_side *p, uint8_t k) {
  uint8_t id = p->pas[k], need, g, on;
  char s[5];
  if (id >= FP_COUNT) {
    text_(x, y, "    ", 4, T_BRASS);
    return;
  }
  on = (p->on & BIT8[k]) ? 1u : 0u;
  need = PNEED[id];
  g = on ? need : p->prog[k];
  if (g > need) g = need;
  s[0] = PCODE[id][0];
  s[1] = PCODE[id][1];
  s[2] = (char)((uint8_t)(g + g) >= need ? '#' : g ? ':' : '.'); /* half the pattern, then all of it */
  s[3] = (char)(g >= need ? '#' : '.');
  s[4] = 0;
  text_(x, y, s, 4, on ? T_CREAM : T_BRASS);
}
static uint8_t fog(void) { return (fr.rules & FR_R_FOG) || (fr.arena == FA_FLICKER && fr.turn && !(fr.turn % 3u)); }
static void card_glyphs(char *s, uint8_t mask, uint8_t pw) {
  uint8_t st, n = 0;
  for (st = 0; st < 3u; st++)
    if (mask & BIT8[st]) s[n++] = STANCE_CH[st];
  if (n < 2u) s[n++] = ' ';
  s[2] = (char)('0' + pw);
  s[3] = 0;
}
static void opp_block(void) {
  const fr_side *p = &fr.s[1];
  uint8_t i;
  char s[4];
  focus_draw(14, 2, p->focus);
  passive_draw(14, 3, p, 0);
  passive_draw(14, 4, p, 1);
  text_(14, 5, ai_locked_ && fight_ui == UI_CHOOSE ? "LOCK" : "    ", 4, T_CREAM);
  for (i = 0; i < 4u; i++) {
    if (i < p->nhand) {
      if (fog())
        strcpy(s, "?? ");
      else
        card_glyphs(s, p->kst[p->hand[i]], p->kpw[p->hand[i]]);
    } else
      strcpy(s, "   ");
    text_((uint8_t)(12u + (i & 1u) * 4u), (uint8_t)(6u + (i >> 1)), s, 3, T_CREAM);
  }
}
static void top_bar(void) {
  char n[4];
  const fr_side *p = &fr.s[1];
  text_(0, 0, " ", 20, T_CREAM);
  text_(1, 0, who_, 8, T_BRASS);
  hp_bar(9, 0, p->hp, p->max);
  num_(n, p->hp > 0 ? (uint8_t)p->hp : 0u, 2);
  text_(18, 0, n, 2, T_CREAM);
}
static void me_block(void) {
  const fr_side *p = &fr.s[0];
  char n[10];
  strcpy(n, "HP ");
  num_(n + 3, p->hp > 0 ? (uint8_t)p->hp : 0u, 2);
  n[5] = '/';
  num_(n + 6, p->max, 2);
  text_(12, 13, n, 8, T_CREAM);
  hp_bar(12, 14, p->hp, p->max);
  focus_draw(12, 15, p->focus);
  pips_draw(16, 15, pips_, 4);
  passive_draw(12, 16, p, 0);
  passive_draw(16, 16, p, 1);
}
/* the stance a hand card plays now: its first stance, or the other one of a dual once flipped (UP) */
static uint8_t live_stance(uint8_t i) {
  uint8_t m = fr.s[0].kst[fr.s[0].hand[i]], st;
  for (st = 0; st < 3u; st++)
    if (m & BIT8[st]) break;
  if (flip_ & BIT8[i])
    for (st = (uint8_t)(st + 1u); st < 3u; st++)
      if (m & BIT8[st]) break;
  return st < 3u ? st : 0u;
}
static void hand_draw(void) {
  const fr_side *p = &fr.s[0];
  uint8_t i, s, m, st, pw;
  char row[9], nm[14];
  for (i = 0; i < 4u; i++) {
    memset(row, ' ', 8);
    row[8] = 0;
    if (i < p->nhand) {
      s = p->hand[i];
      m = p->kst[s];
      st = live_stance(i);
      pw = p->kpw[s];
      if (m == 3u || m == 5u || m == 6u) pw = pw > 1u ? (uint8_t)(pw - 1u) : 1u;
      row[0] = (char)(i == fight_cursor ? '>' : (fuse_ == i ? '+' : ' '));
      row[1] = STANCE_CH[st];
      {
        uint8_t o = (uint8_t)(m & (uint8_t)~BIT8[st]);
        row[2] = (m == 3u || m == 5u || m == 6u) ? STANCE_CH[o == 1u ? 0u : o == 2u ? 1u : 2u] : ' ';
      }
      row[3] = (char)('0' + pw);
      crucible_get_name(p->kit[s], nm);
      memcpy(row + 4, nm, 4);
    }
    text_(0, (uint8_t)(13u + i), row, 8, i == fight_cursor ? T_CREAM : T_BRASS);
  }
}
/* what A does now (row 17): the focused card, or the fuse */
static uint8_t fuse_act(void) {
  uint8_t a = fuse_ < partner_ ? fuse_ : partner_, b = fuse_ < partner_ ? partner_ : fuse_, x, st;
  fr_cardv c;
  for (st = 0; st < 3u; st++) {
    x = FR_ACT(FR_FUSE, a, b, st);
    if (fr_legal(0, x)) {
      (void)fr_card(0, x, &c);
      return x;
    }
  }
  return 0xffu;
}
static uint16_t fuse_product(void) {
  uint8_t x = fuse_act();
  fr_cardv c;
  if (x == 0xffu) return CRU_NONE;
  (void)fr_card(0, x, &c);
  return c.id;
}
static void answer_line(void) {
  char n[14];
  uint16_t id;
  const fr_side *p = &fr.s[0];
  text_(0, 17, " ", 20, T_CREAM);
  if (fight_ui == UI_WAIT) {
    text_(1, 17, "LOCKED. WAITING", 15, T_BRASS);
    return;
  }
  if (fight_ui != UI_CHOOSE) return;
  if (fuse_ != 0xffu) {
    id = fuse_product();
    if (id == CRU_NONE) {
      text_(1, 17, partner_ == fuse_ ? "PICK ITS PAIR" : "NOTHING", 13, T_BRASS);
      return;
    }
    crucible_get_name(id, n);
    text_(0, 17, n, 12, T_CREAM);
    put_(13, 17, UI_A, T_CREAM);
    text_(14, 17, "FUSE", 4, T_CREAM);
    return;
  }
  if (fight_cursor >= p->nhand) {
    put_(13, 17, UI_B, T_CREAM);
    text_(14, 17, "GUARD", 5, T_CREAM);
    return;
  }
  crucible_get_name(p->kit[p->hand[fight_cursor]], n);
  text_(0, 17, n, 7, T_CREAM); /* the card, A PLAY, B GUARD */
  put_(7, 17, UI_A, T_CREAM);
  text_(8, 17, "PLAY", 4, T_CREAM);
  put_(13, 17, UI_B, T_CREAM);
  text_(14, 17, "GUARD", 5, T_BRASS);
}

/* ---- opening a duel ---- */
/* the opponent's kit: its faction's category, depth within reach of the chapter, two of each stance where it can,
 * from a seeded walk (no table); fitted to the budget like any kit */
static void opp_kit(uint8_t f, uint8_t cap, uint16_t *kit) {
  uint8_t cat = LIKE_CAT[f], have[3] = {0, 0, 0}, n = 0, m, st, tries;
  uint16_t first = core.cat_first[cat], end = core.cat_first[cat + 1u], id, r = fr.ai | 1u, range;
  for (st = 0; st < 6u; st++) kit[st] = CRU_NONE;
  if (end <= first) {
    first = 0;
    end = core.items;
  }
  range = (uint16_t)(end - first);
  for (tries = 0; tries < 64u && n < 6u;
       tries++) { /* seeded samples from its category: within reach, two of each stance */
    r ^= (uint16_t)(r << 7);
    r ^= (uint16_t)(r >> 9);
    r ^= (uint16_t)(r << 8);
    id = cri_shelf(&core, (uint16_t)(first + r % range));
    if (cri_depth(&core, id) > cap) continue;
    m = fr_stance(id);
    st = (m & 1u) ? 0u : (m & 2u) ? 1u : 2u;
    if (have[st] >= 2u && tries < 40u) continue;
    have[st]++;
    kit[n++] = id;
  }
  while (n < 6u) {
    kit[n] = n && kit[n - 1u] != CRU_NONE ? kit[n - 1u] : cri_shelf(&core, first);
    n++;
  }
  for (st = 0; st < 8u && fr_kit_cost(kit, 6) > 12u; st++) { /* the dearest becomes a copy of the cheapest */
    uint8_t top = 0, low = 0, k;
    for (k = 1; k < 6u; k++) {
      if (fr_cost(kit[k]) > fr_cost(kit[top])) top = k;
      if (fr_cost(kit[k]) < fr_cost(kit[low])) low = k;
    }
    if (top == low) break;
    kit[top] = kit[low];
  }
}
/* the first contact (5.7): HUM, HUM, WALL from the starters; it plays HUM on its first turn */
static uint8_t tutor_kit(uint16_t *kit) {
  uint16_t id, first = player_owned_next(0), n;
  uint8_t h = 0, w = 0, m;
  for (id = first, n = 0; id < core.items && n < core.items && (h < 2u || w < 1u); n++, id = player_owned_next(id)) {
    if (n && id == first) break;
    if (!cru_is_starter(&core, id)) continue;
    m = fr_stance(id);
    if (m == 1u && h < 2u)
      kit[h++] = id;
    else if (m == 2u && !w) {
      kit[2] = id;
      w = 1;
    }
  }
  if (h == 1u) kit[1] = kit[0];
  return (uint8_t)(h && w ? 3u : 0u);
}
static uint8_t equip(uint8_t k) {
  uint8_t id = (uint8_t)(k ? pl.equip >> 4 : pl.equip & 15u);
  return id < FP_COUNT && (pl.known & BIT16[id]) ? id : FP_NONE;
}
/* the arena (3.1): the living room's family % 8 (the home room has none), night lifts focus; derived, never stored */
static uint8_t arena_now(void) {
  uint8_t f = fight_force_family != 0xffu ? fight_force_family : room_family();
  crucible_time_ctx t;
  crucible_time_context(&t);
  if (fight_force_part != 0xffu) {
    t.part = fight_force_part;
    t.flags |= CT_F_KNOWN;
  }
  if ((t.flags & CT_F_KNOWN) && t.part == CT_NIGHT) fr.rules |= FR_R_NIGHT;
  fight_room = f;
  return f == 0xffu ? FA_EMPTY : (uint8_t)(f & 7u);
}
static void draw_duel(void) {
  clear_rows(0, 17);
  if (fight_kind != FK_BOSS) {
    top_bar();
    opp_block();
    text_(12, 10, "VS", 2, T_BRASS);
  } /* a boss draws its own top (crucible_fight_bossui.c) */
  me_block();
  hand_draw();
  avatar_bg_map(0, 2);
}
static void face_open(uint8_t f) {
  avatar_bg_genome(pl.genome, 0, 2); /* yours, as background tiles (drawn once, then from SRAM) */
  avatar_make((uint16_t)(fight_seed ^ 0xb055u), f); /* theirs, the sprites crucible.c streams */
  avatar_fx((uint8_t)(f == 1u ? 3u : f == 2u ? 2u : f == 3u ? 5u : 4u));
}
/* the alignment's quiet lean (keel_dialogue_choice.h kdc_nudge, kept local): axis 0 order, 1 heart, clamped to 60 */
static void kdc_nudge(uint8_t *k, uint8_t axis, int8_t d) {
  int16_t v = (int16_t)(int8_t)k[14u + axis] + d;
  k[14u + axis] = (uint8_t)(int8_t)(v > 60 ? 60 : v < -60 ? -60 : v);
}
static void learn(uint8_t id) { /* a passive learned by living it: one line, never a menu */
  if (pl.known & BIT16[id]) return;
  pl.known |= BIT16[id];
  say_text(id == FP_SCAR       ? "SOMETHING IN YOU HARDENS"
           : id == FP_UNDERTOW ? "YOU LEARN TO PULL BACK"
                               : "YOU LEARN TO HOLD IT");
}
static void duel_open(uint8_t f, uint8_t level) {
  crucible_story *s = talk_saga();
  uint16_t kit[6], okit[6];
  uint8_t pas[2], opas[2], hp, n, k, sk;
  crucible_time_ctx t;
  fight_kind = FK_DUEL;
  faction_ = f < 6u ? f : 0u;
  strcpy(who_, FNAME[faction_]);
  fight_t0 = sys_time;
  fight_gauntlet = flow_gauntlet ? 1u : 0u;
  flow_gauntlet = 0;
  scene_draw(SCENE_RECORDS);
  clear_rows(0, 17); /* the bench goes at once; the figure is made behind the tear */
  fight_seed = (uint16_t)((uint16_t)s->seed ^ ((uint16_t)faction_ * 0x3a1u) ^ ((uint16_t)pl.duels << 9) ^ 0x6b5du);
  cosm_ ^= (uint16_t)DIV_REG << 8;
  fr_begin(fight_seed);
  fight_tutor = !(pl.flags & PF_FIRST_DUEL);
  fr.arena = fight_tutor ? FA_EMPTY : arena_now();
  crucible_time_context(&t);
  if (fight_force_special != 0xffu) t.special = fight_force_special;
  fight_glitch = 0;
  if (!fight_tutor && (s->lucid < 64u || t.special == CT_SP_333)) {
    fight_glitch = (uint8_t)(1u + s->lucid % 3u);
    if (fight_glitch == 1u)
      fr.rules |= FR_R_INVERT;
    else if (fight_glitch == 2u)
      fr.rules |= FR_R_FOG;
  }
  for (k = 0; k < 6u; k++)
    if (cru_story_tier(s, k) >= CRU_TIER_FRIEND) pl.known |= BIT16[TEACH[k]]; /* friends teach */
  (void)player_kit(kit);
  pas[0] = equip(0);
  pas[1] = player_slots() >= 2u ? equip(1) : FP_NONE;
  hp = (uint8_t)(8u + player_attr(PA_GRIT));
  if (s->scale == CRU_STORY_GENTLE)
    hp = (uint8_t)(hp + 2u);
  else if (s->scale == CRU_STORY_HARSH)
    hp = (uint8_t)(hp - 2u);
  fr_side_init(0, kit, 6, pas, hp, player_attr(PA_FOCUS));
  opas[0] = opas[1] = FP_NONE;
  if (fight_tutor)
    n = tutor_kit(okit);
  else {
    opp_kit(faction_, (uint8_t)(3u + level), okit);
    n = 6;
    if (level >= 2u) opas[0] = TEACH[faction_];
    if (level >= 5u) opas[1] = (uint8_t)(fr.ai % 3u == 0u ? FP_ECHO : fr.ai % 3u == 1u ? FP_SCAR : FP_PRISM);
  }
  if (!n) {
    opp_kit(faction_, 3, okit);
    n = 6;
    fight_tutor = 0;
  }
  fr_side_init(1, okit, n, opas, 8, 0);
  sk = level <= 1u ? 2u : level <= 3u ? 3u : level <= 5u ? 4u : 6u;
  fr_ai_setup(faction_, fight_tutor ? 1u : sk);
  won_turns_ = lost_turns_ = 0;
  fuse_ = 0xffu;
  fight_cursor = 0;
  flip_ = 0;
  xp_ = 0;
  gained_ = 0;
  fight_end = 0;
  fight_ui = UI_INTRO;
  intro_t_ = 0;
  fx_ = FIGHT_FX_TEAR;
  avatar_bg_genome(pl.genome, 0, 2);
  draw_duel(); /* the screen, your face from SRAM, then theirs is made */
  avatar_make((uint16_t)(fight_seed ^ 0xb055u), faction_);
  avatar_fx((uint8_t)(faction_ == 1u ? 3u : faction_ == 2u ? 2u : faction_ == 3u ? 5u : 4u));
  if (fight_tutor)
    say_text("THE WALLS KEEP THE NOISE IN");
  else if (fight_glitch)
    say_text(fight_glitch == 1u   ? "THE ROOM TURNS OVER"
             : fight_glitch == 2u ? "YOU CANNOT SEE THEIR HANDS"
                                  : "YOUR THINGS WILL NOT HOLD STILL");
  else
    say(SAY_TAUNT, CRU_NONE);
}

/* ---- a turn ---- */
static uint8_t ai_act_;
/* the engine runs in the host's side order on both carts; the guest's screen keeps itself as side 0 in between */
static void eng_draw(void) {
  if (fight_swap) fr_swap_sides();
  fr_draw();
  if (fight_swap) fr_swap_sides();
}
static uint8_t eng_hash(void) {
  uint8_t h;
  if (fight_swap) fr_swap_sides();
  h = fr_hash();
  if (fight_swap) fr_swap_sides();
  return h;
}
static void turn_start(void) {
  uint8_t k;
  fr_side *p = &fr.s[0];
  eng_draw();
  if (fight_glitch == 3u && p->nhand && fr.turn > 1u) { /* DRIFT: one of your hand becomes its dream cousin */
    crucible_story *s = talk_saga();
    uint8_t i = (uint8_t)(fr.turn % p->nhand);
    uint16_t id = cru_dream_slot(&core, s, p->kit[p->hand[i]]);
    if (id < core.items) fr_set_card(0, p->hand[i], id);
  }
  if (fight_kind == FK_DUEL) {
    if (fight_tutor && fr.turn == 1u) {
      for (ai_act_ = FR_GUARD_ACT, k = 0; k < fr.s[1].nhand; k++)
        if (fr_legal(1, FR_ACT(FR_PLAY, k, 0, FR_HUM))) {
          ai_act_ = FR_ACT(FR_PLAY, k, 0, FR_HUM);
          break;
        }
    } else
      ai_act_ = 0xffu; /* it thinks a little each frame while you choose, once the hands' fusions are known */
  }
  ai_locked_ = 0;
  ai_lock_at_ = (uint8_t)(40u + (cosm() & 127u));
  vs_got_ = 0;
  ready_ = 0;
  fight_cursor = 0;
  flip_ = 0;
  fuse_ = 0xffu;
  pips_ = fight_kind == FK_VERSUS ? link_fight_pips() : 4u;
  pip_t_ = 0;
  hold_b_ = 0;
  fight_me = 0xffu;
  fight_them = 0xffu;
  fight_ui = UI_CHOOSE;
  if (fight_glitch == 1u && fr.turn == 3u) say_text("THE GLYPHS TURN UPSIDE DOWN");
  top_bar();
  opp_block();
  me_block();
  hand_draw();
  answer_line();
  fx_ |= FIGHT_FX_CHARGE;
}
/* the outcome of a resolved turn: the effects, the sound, the line */
static void outcome_show(void) {
  char s[20], n[3];
  int8_t mine = fr.dmg[0], theirs = fr.dmg[1];
  fr_cardv c;
  show_t_ = 0;
  fight_ui = UI_SHOW;
  if (fr.out == 1) {
    won_turns_++;
    fx_ |= FIGHT_FX_FLASH | FIGHT_FX_SHAKE;
    sound_play(SFX_NEW);
    strcpy(s, "YOU HIT ");
    num_(n, (uint8_t)theirs, theirs >= 10 ? 2u : 1u);
    strcat(s, n);
  } else if (fr.out == -1) {
    lost_turns_++;
    fx_ |= FIGHT_FX_TEAR;
    sound_play(SFX_DENY);
    strcpy(s, "IT HITS ");
    num_(n, (uint8_t)mine, mine >= 10 ? 2u : 1u);
    strcat(s, n);
  } else if (fr.out == 0) {
    fx_ |= FIGHT_FX_CHARGE;
    sound_play(SFX_SWAP);
    strcpy(s, "EVEN ");
    num_(n, (uint8_t)theirs, 1);
    strcat(s, n);
    strcat(s, ":");
    num_(n, (uint8_t)mine, 1);
    strcat(s, n);
  } else {
    sound_play(SFX_CLOSE);
    strcpy(s, theirs || mine ? "GUARDED, CHIPPED" : "GUARDED");
    fx_ |= theirs || mine ? FIGHT_FX_SHAKE : 0u;
  }
  if (fr.sever[1]) strcat(s, " SEVER");
  status(s, T_CREAM);
  if (FR_KIND(fight_me) == FR_FUSE && fr_card(0, fight_me, &c)) say(SAY_NOTICE, c.id);
  if (fr.s[0].lost_hp >= 5u) learn(FP_SCAR);
  if (fr.s[0].lostrow >= 2u) learn(FP_UNDERTOW);
  if (fr.s[0].wins >= 3u) learn(FP_LUCID);
  top_bar();
  opp_block();
  me_block();
  hand_draw();
}
static void lock(uint8_t act) {
  fight_me = act;
  sound_play(SFX_PICK);
  fx_ |= FIGHT_FX_CHARGE;
  if (fight_kind == FK_VERSUS) {
    fight_ui = UI_WAIT;
    vs_sent_t_ = 0;
    link_fturn(act, fr.turn, eng_hash(), pips_);
    answer_line();
    return;
  }
  if (!ready_) {
    fr_pairs_all();
    ready_ = 1;
    if (fight_kind == FK_DUEL && ai_act_ == 0xffu) fr_ai_begin(fog());
  }
  while (ai_act_ == 0xffu) ai_act_ = fr_ai_step(32); /* you were quicker than it: it finishes thinking now */
  fight_them = ai_act_;
  fr_resolve(fight_me, fight_them);
  outcome_show();
}
/* CHOOSE: LEFT/RIGHT the hand, UP flips a dual, DOWN marks a fuse (LEFT/RIGHT its pair, A fuses, DOWN or B cancels),
 * A plays, B guards (held a second in a duel: flee), SELECT says the rules in play */
static uint8_t choose(uint8_t pressed, uint8_t dt) {
  const fr_side *p = &fr.s[0];
  uint8_t n = p->nhand, act, st, held = joypad();
  if (fuse_ != 0xffu) {
    if (pressed & (J_LEFT | J_RIGHT)) {
      do {
        partner_ = (uint8_t)((pressed & J_LEFT) ? (partner_ ? partner_ - 1u : n - 1u)
                                                : (partner_ + 1u >= n ? 0u : partner_ + 1u));
      } while (partner_ == fuse_ && n > 1u);
      fight_cursor = partner_;
      sound_play(SFX_MOVE);
      hand_draw();
      answer_line();
    } else if (pressed & (J_DOWN | J_B)) {
      fuse_ = 0xffu;
      sound_play(SFX_UNDO);
      hand_draw();
      answer_line();
    } else if (pressed & J_A) {
      act = fuse_act();
      if (act != 0xffu) {
        fuse_ = 0xffu;
        lock(act);
        return FIGHT_GOING;
      }
      sound_play(SFX_DENY);
    }
  } else {
    if (pressed & J_LEFT) {
      fight_cursor = fight_cursor ? (uint8_t)(fight_cursor - 1u) : (uint8_t)(n ? n - 1u : 0u);
      sound_play(SFX_MOVE);
      hand_draw();
      answer_line();
    } else if (pressed & J_RIGHT) {
      fight_cursor = (uint8_t)(fight_cursor + 1u >= n ? 0u : fight_cursor + 1u);
      sound_play(SFX_MOVE);
      hand_draw();
      answer_line();
    } else if ((pressed & J_UP) && fight_cursor < n) {
      st = p->kst[p->hand[fight_cursor]];
      if (st == 3u || st == 5u || st == 6u) {
        flip_ ^= BIT8[fight_cursor];
        sound_play(SFX_SWAP);
        hand_draw();
      }
    } else if ((pressed & J_DOWN) && !ready_)
      sound_play(SFX_MOVE); /* still reading what the hand makes */
    else if ((pressed & J_DOWN) && n >= 2u &&
             p->focus >=
                 2u - ((p->pas[0] == FP_CATALYST && (p->on & 1u)) || (p->pas[1] == FP_CATALYST && (p->on & 2u)))) {
      fuse_ = fight_cursor;
      partner_ = (uint8_t)(fuse_ + 1u >= n ? 0u : fuse_ + 1u);
      fight_cursor = partner_;
      sound_play(SFX_MOVE);
      hand_draw();
      answer_line();
    } else if ((pressed & J_A) && fight_cursor < n) {
      act = FR_ACT(FR_PLAY, fight_cursor, 0, live_stance(fight_cursor));
      if (fr_legal(0, act)) {
        lock(act);
        return FIGHT_GOING;
      }
    } else if (pressed & J_SELECT)
      say_text(fr.arena == FA_HUM       ? "THE LIGHTS HUM LOUDER"
               : fr.arena == FA_WALL    ? "THE CARPET IS WET"
               : fr.arena == FA_DREAM   ? "THE AIR IS TOO STILL"
               : fr.arena == FA_FLICKER ? "THE ROOM BLINKS"
               : fr.arena == FA_NARROW  ? "THE WALLS ARE CLOSE"
               : fr.arena == FA_ECHO    ? "EVERY SOUND COMES TWICE"
               : fr.arena == FA_EXIT    ? "A GREEN SIGN GLOWS"
                                        : "NOTHING HERE");
    /* B: a tap guards, held a second a duel is fled. The press is the latched one (a busy loop can miss a short tap
     * in the held state); hold_b_ counts from it, 1 + frames held */
    if (pressed & J_B) hold_b_ = 1;
    if (hold_b_) {
      if (held & J_B) {
        hold_b_ = (uint8_t)(hold_b_ + dt);
        if (fight_kind == FK_DUEL && hold_b_ >= FLEE_FRAMES) {
          fight_end = 3;
          return FIGHT_FLED;
        }
      } else {
        hold_b_ = 0;
        lock(FR_GUARD_ACT);
        return FIGHT_GOING;
      }
    }
  }
  if (!ready_) {
    if (fr_pairs_step()) {
      ready_ = 1;
      if (fight_kind == FK_DUEL && ai_act_ == 0xffu) fr_ai_begin(fog());
      answer_line();
    }
  } /* a lookup a frame */
  else if (fight_kind == FK_DUEL && ai_act_ == 0xffu)
    ai_act_ = fr_ai_step(4);
  if (fight_kind == FK_DUEL && !ai_locked_ && ai_act_ != 0xffu) {
    if (ai_lock_at_ > dt)
      ai_lock_at_ = (uint8_t)(ai_lock_at_ - dt);
    else {
      ai_locked_ = 1;
      opp_block();
    }
  }
  pip_t_ = (uint16_t)(pip_t_ + dt);
  if (pip_t_ >= PIP_FRAMES) {
    pip_t_ = 0;
    fx_ |= FIGHT_FX_CHARGE;
    if (pips_) {
      pips_--;
      me_block();
      sound_play(SFX_MOVE);
    }
    if (!pips_) {
      fuse_ = 0xffu;
      lock(FR_GUARD_ACT);
    } /* the last pip ran out: an unlocked side guards */
  }
  return FIGHT_GOING;
}
/* ---- the end of a duel: what it gives and costs (7.1) ---- */
static void duel_end(uint8_t r) {
  crucible_story *s = talk_saga();
  uint16_t got = CRU_NONE;
  uint8_t k;
  char line[20];
  pl.flags |= PF_FIRST_DUEL;
  if (r == 1u) { /* won: XP 15; a quarter of the time one of its hand, if new; its faction -3, the rival +3 */
    xp_ = fight_gauntlet ? 15u * 3u + 30u : 15u;
    if (pl.duels < 255u) pl.duels++;
    cru_story_shift(s, faction_, -3);
    cru_story_shift(s, cru_faction_rival(faction_), 3);
    if (!(fr.ai & 3u))
      for (k = 0; k < fr.s[1].nhand; k++) {
        uint16_t id = fr.s[1].kit[fr.s[1].hand[k]];
        if (!cru_owned(&core, id) && cru_story_gain(&core, s, id)) {
          got = id;
          break;
        }
      }
    cru_story_act(s, CRU_ACT_WIN);
    strcpy(line, "IT FALLS APART");
  } else if (r == 3u) {
    xp_ = 0;
    cru_story_lucid(s, -8);
    cru_story_act(s, CRU_ACT_REFUSE);
    strcpy(line, "YOU RAN");
  } else {
    xp_ = r == 2u ? 5u : 5u;
    if (r == 2u) cru_story_lucid(s, -8);
    strcpy(line, r == 2u ? "IT WALKS AWAY" : "NEITHER FALLS");
  } /* a duel loss never destroys elements */
  cru_story_event(s, CRU_EV_FIGHT, fr.s[0].hp > 0 ? (uint16_t)fr.s[0].hp : 0u, (uint16_t)r);
  gained_ = player_xp(xp_);
  player_save();
  story_save();
  fight_ui = UI_END;
  show_t_ = 0;
  end_line_ = r;
  status(line, T_CREAM);
  {
    char x[12];
    strcpy(x, "+");
    num_(x + 1, xp_, xp_ >= 10u ? 2u : 1u);
    strcat(x, " XP");
    text_(12, 12, x, 6, T_BRASS);
  }
  if (got != CRU_NONE)
    say(SAY_PLEASED, got);
  else
    say(r == 1u ? SAY_HURT : SAY_FAREWELL, CRU_NONE);
  fx_ |= FIGHT_FX_PHASE;
}

/* ---- the gauntlet (5.4): three duels behind doors. HP carries over and the deck is not reshuffled; between duels
 * one of two doors: an arena for the next duel, or +2 HP ---- */
static uint8_t door_arena_, door_at_;
static void doors_draw(void) {
  static const char *const AN[7] = {"LIGHTS", "CARPET", "STILL", "BLINK", "NARROW", "ECHO", "EXIT"};
  uint8_t y;
  clear_rows(2, 16);
  for (y = 4; y < 12u; y++) {
    text_(3, y, "[    ]", 6, door_at_ ? T_BRASS : T_CREAM);
    text_(11, y, "[    ]", 6, door_at_ ? T_CREAM : T_BRASS);
  }
  text_(4, 7, AN[door_arena_], 4, door_at_ ? T_BRASS : T_CREAM);
  text_(13, 7, "+2", 2, door_at_ ? T_CREAM : T_BRASS);
  put_(door_at_ ? 13u : 5u, 13, GLYPH('^'), T_CREAM);
  status("TWO DOORS", T_CREAM);
}
static void doors_open(void) {
  door_arena_ = (uint8_t)(fr.ai % 7u);
  door_at_ = 0;
  fight_ui = UI_DOORS;
  scene_draw(SCENE_RECORDS);
  clear_rows(0, 17);
  say_text("ANOTHER WAITS BEHIND ONE");
  doors_draw();
  sound_play(SFX_OPEN);
}
static void gauntlet_next(void) { /* through a door: the next figure, the same you */
  uint16_t okit[6];
  uint8_t opas[2] = {FP_NONE, FP_NONE}, lvl = talk_saga()->chapter;
  if (door_at_) {
    fr.s[0].hp = (int8_t)(fr.s[0].hp + 2);
    if (fr.s[0].hp > (int8_t)fr.s[0].max) fr.s[0].max = (uint8_t)fr.s[0].hp;
  } else
    fr.arena = door_arena_;
  fight_gauntlet++;
  faction_ = (uint8_t)(faction_ >= 5u ? 0u : faction_ + 1u);
  strcpy(who_, FNAME[faction_]);
  opp_kit(faction_, (uint8_t)(3u + lvl), okit);
  if (lvl >= 2u) opas[0] = TEACH[faction_];
  if (fight_gauntlet >= 3u) opas[1] = FP_SCAR; /* the last door's figure has learned to be hurt */
  fr_side_init(1, okit, 6, opas, 8, 0);
  fr.turn = 0;
  fr_ai_setup(faction_, 4);
  fight_t0 = sys_time;
  fight_ui = UI_INTRO;
  avatar_bg_genome(pl.genome, 0, 2);
  draw_duel();
  avatar_make((uint16_t)(fight_seed ^ 0xb055u ^ fight_gauntlet), faction_);
  say(SAY_TAUNT, CRU_NONE);
  fx_ |= FIGHT_FX_TEAR;
}

/* ---- the link versus: the same rules in lockstep; only the action bytes travel (crucible_link.c P_FTURN) ---- */
static uint8_t vs_setup_; /* 0: the partner's setup has not arrived yet */
static void versus_setup(void) {
  uint16_t kit[6], okit[6];
  uint8_t me = link_role == LINK_HOST ? 0u : 1u, pas[2], opas[2], g[6], next = vs_setup_;
  if (next) { /* the next bout of a best-of: the same kits (as the engine holds them, host order), a new seed */
    if (fight_swap) fr_swap_sides();
    memcpy(me ? okit : kit, fr.s[0].kit, 12);
    memcpy(me ? kit : okit, fr.s[1].kit, 12);
    (me ? opas : pas)[0] = fr.s[0].pas[0];
    (me ? opas : pas)[1] = fr.s[0].pas[1];
    (me ? pas : opas)[0] = fr.s[1].pas[0];
    (me ? pas : opas)[1] = fr.s[1].pas[1];
  } else {
    link_kit_mine(kit, pas);
    link_kit_theirs(okit, opas);
  }
  fight_seed = link_fight_seed();
  fr_begin(fight_seed);
  fr.arena = link_fight_arena();
  fr.rules = link_fight_rules();
  fight_glitch = (fr.rules & FR_R_INVERT) ? 1u : (fr.rules & FR_R_FOG) && (link_rule(3) & 4u) ? 2u : 0u;
  /* the engine runs in the host's side order on both carts (draws and shuffles follow it); the guest swaps to see itself */
  if (!me) {
    fr_side_init(0, kit, 6, pas, link_fight_hp(0), link_fight_focus(0));
    fr_side_init(1, okit, 6, opas, link_fight_hp(1), link_fight_focus(1));
  } else {
    fr_side_init(0, okit, 6, opas, link_fight_hp(0), link_fight_focus(0));
    fr_side_init(1, kit, 6, pas, link_fight_hp(1), link_fight_focus(1));
  }
  fight_swap = me;
  if (me) fr_swap_sides();
  if (!next) {
    avatar_bg_genome(pl.genome, 0, 2);
    link_partner_genome(g);
    avatar_make_genome(g);
  } /* the partner's face, regenerated here from its six bytes */
  vs_setup_ = 1;
  draw_duel();
  if (next) {
    char l[8];
    crucible_text_line(EV_AGAIN_FIRST, l, sizeof l, 0);
    say_text(l);
  } else
    say_text("THE SIGNAL HOLDS");
  fight_t0 = sys_time;
}
static void versus_open(void) {
  fight_kind = FK_VERSUS;
  faction_ = 3;
  link_peer(who_);
  if (!who_[0]) strcpy(who_, "THEM");
  who_[7] = 0;
  won_turns_ = lost_turns_ = 0;
  fuse_ = 0xffu;
  fight_cursor = 0;
  flip_ = 0;
  xp_ = 0;
  gained_ = 0;
  fight_end = 0;
  vs_setup_ = 0;
  fight_swap = 0;
  fr_begin(1);
  fr.s[0].hp = fr.s[1].hp = 1;
  fight_ui = UI_INTRO;
  intro_t_ = 0;
  fx_ = FIGHT_FX_TEAR;
  scene_draw(SCENE_RECORDS);
  clear_rows(0, 17);
  say_text("THE SIGNAL TUNES IN");
  avatar_make((uint16_t)link_seed, 3);
}
static uint8_t versus_wait(uint8_t dt) {
  uint8_t act, turn, hash, pip;
  turn = fr.turn;
  if (fight_ui != UI_SHOW && link_fturn_in(&act, &turn, &hash, &pip)) {
    /* the partner's move for this turn (not while showing: the next turn's draw is not done here yet, its hash would differ) */ /* not while showing: the next turn's draw is not done here yet, its hash would differ */
    if (turn == fr.turn) {
      if (hash != eng_hash())
        link_fsync_ask(); /* a desync: the host's state wins */
      else {
        fight_them = act;
        vs_got_ = 1;
      }
    }
  }
  if (link_result()) return FIGHT_FLED;
  if (fight_ui == UI_WAIT && vs_got_) {
    if (!ready_) {
      fr_pairs_all();
      ready_ = 1;
    }
    if (!fr_legal(1, fight_them)) fight_them = FR_GUARD_ACT;
    if (fight_swap) {
      fr_swap_sides();
      fr_resolve(fight_them, fight_me);
      fr_swap_sides();
    } else
      fr_resolve(fight_me, fight_them);
    outcome_show();
    vs_got_ = 0;
  }
  (void)dt;
  return FIGHT_GOING;
}

/* ---- for the boss screen: drawing and setup it shares (numbers only cross the banks) ---- */
void fui_hand_draw(void) BANKED { hand_draw(); }
void fui_me_block(void) BANKED { me_block(); }
void fui_answer_line(void) BANKED { answer_line(); }
uint8_t fui_live_stance(uint8_t i) BANKED { return live_stance(i); }
uint8_t fui_fuse_act(void) BANKED { return fuse_act(); }
uint16_t fui_fuse_product(void) BANKED { return fuse_product(); }
void fui_face_open(uint8_t f) BANKED { face_open(f); }
void fui_draw_duel(void) BANKED { draw_duel(); }
uint8_t fui_arena(void) BANKED { return arena_now(); }
uint8_t fui_equip(uint8_t k) BANKED { return equip(k); }
void fui_say(uint8_t intent, uint16_t item) BANKED { say(intent, item); }
void fui_hp_bar(uint8_t x, uint8_t y, int8_t hp, uint8_t max) BANKED { hp_bar(x, y, hp, max); }
uint8_t fui_cosm(void) BANKED {
  cosm_ ^= (uint16_t)DIV_REG << 8;
  return cosm();
}
static void top_bar_any(void) {
  if (fight_kind == FK_BOSS)
    fbui_top();
  else
    top_bar();
}
/* ---- the API crucible.c calls ---- */
/* A fight with faction f's figure at level (the chapter): f | FIGHT_DUEL for a duel; otherwise its champion (the
 * nemesis when nemesis). A FIGHT session on the link is always the versus. */
void fight_open(uint8_t f, uint8_t level, uint8_t nemesis) BANKED {
  fight_dbg = 0;
  fx_ = 0;
  tear_t_ = 0;
  avatar_still = 1; /* a fight's face holds still: a bob split across a DMA put 12 sprites on a line (step 17) */
  if (link_scene_watching()) {
    fbui_open_shared();
    return;
  } /* pulled into the partner's boss (9.4) */
  if (link_on && link_started && link_mode == LINK_FIGHT) {
    versus_open();
    return;
  }
  if (f & FIGHT_DUEL) {
    duel_open((uint8_t)(f & 7u), level);
    return;
  }
  fbui_open((uint8_t)(f & 7u), level, nemesis);
}
static uint8_t finish(uint8_t r) { /* the fight is over: palette 4 back */
  if (r == FIGHT_WON) time_mark_act(); /* a win the minute before an angel minute calls someone in */
  avatar_bg_end();
  avatar_still = 0;
  if (fight_kind == FK_BOSS) link_scene_end();
  return r;
}
/* One frame. Returns FIGHT_GOING, or how it ended (FIGHT_WON / FIGHT_FLED / FIGHT_OVER: the run is lost). */
uint8_t fight_tick(uint8_t pressed, uint8_t dt) BANKED {
  uint8_t r;
  ticker(dt);
  if (fight_kind == FK_BOSS) switch (fight_ui) {
    case UI_INTRO:
      if (pressed & J_SELECT) {
        fight_ui = UI_KIT;
        (void)kit_open(1);
        return FIGHT_GOING;
      }
      if (link_scene_wait()) return FIGHT_GOING; /* the partner is being pulled in (4 s at most) */
      if ((uint16_t)(sys_time - fight_t0) >= INTRO_FRAMES) fbui_round();
      return FIGHT_GOING;
    case UI_PLAN:
      if (fb_plan_step(b_->hand)) fbui_ready();
      return FIGHT_GOING; /* a recipe lookup a frame */
    case UI_CHOOSE: fbui_choose(pressed, dt); return FIGHT_GOING;
    case UI_SHOW:
      show_t_ = (uint16_t)(show_t_ + dt);
      if (show_t_ < SHOW_FRAMES && !(pressed & J_A)) return FIGHT_GOING;
      if ((fbui_outc & FBO_PHASE) && fbui_nemesis && b_->met < 3u && FB->hp) {
        fbui_end(3);
        return FIGHT_GOING;
      } /* the nemesis flees at its first phase */
      r = fb_over();
      if (r) {
        fbui_end(r);
        return FIGHT_GOING;
      }
      if (fr.turn >= 40u) {
        fbui_end(2);
        return FIGHT_GOING;
      }
      fbui_round();
      return FIGHT_GOING;
    case UI_CHOICE: (void)fbui_choice(pressed); return FIGHT_GOING;
    }
  switch (fight_ui) {
  case UI_INTRO:
    if (fight_kind == FK_VERSUS && !vs_setup_) { /* the partner's kit and face are still crossing */
      r = link_fight_ready();
      if (r == 2u) {
        status("THE OTHER ROOM IS DIFFERENT", T_BRASS);
        link_end(LINK_LOST);
        return finish(FIGHT_FLED);
      }
      if (link_result()) return finish(FIGHT_FLED);
      if (r)
        versus_setup();
      else
        return FIGHT_GOING;
    }
    if ((pressed & J_SELECT) && fight_kind != FK_VERSUS) {
      fight_ui = UI_KIT;
      (void)kit_open(1);
      return FIGHT_GOING;
    }
    if ((uint16_t)(sys_time - fight_t0) >= INTRO_FRAMES) turn_start(); /* a second from the open (the work counts) */
    return FIGHT_GOING;
  case UI_KIT:
    if (!kit_tick(pressed)) return FIGHT_GOING;
    {
      uint16_t kit[6];
      (void)player_kit(kit);
      fr_side_init(0, kit, 6, fr.s[0].pas, fr.s[0].max, fr.s[0].focus);
    } /* the edited kit fights */
    scene_draw(SCENE_RECORDS);
    draw_duel();
    if (fight_kind == FK_BOSS) {
      fbui_top();
      put_(6, 10, GLYPH('+'), T_CREAM);
      put_(13, 10, GLYPH('='), T_CREAM);
      text_(12, 10, " ", 1, T_CREAM);
    }
    fight_ui = UI_INTRO;
    fight_t0 = (uint16_t)(sys_time - INTRO_FRAMES + 10u);
    return FIGHT_GOING;
  case UI_CHOOSE:
    r = choose(pressed, dt);
    if (r == FIGHT_FLED) {
      duel_end(3);
      return FIGHT_GOING;
    }
    if (fight_kind == FK_VERSUS) return versus_wait(dt);
    return FIGHT_GOING;
  case UI_WAIT: return versus_wait(dt);
  case UI_SHOW:
    if (fight_kind == FK_VERSUS) (void)versus_wait(dt);
    show_t_ = (uint16_t)(show_t_ + dt);
    if (show_t_ < SHOW_FRAMES && !(pressed & J_A)) return FIGHT_GOING;
    r = fr_over();
    if (!r) {
      turn_start();
      return FIGHT_GOING;
    }
    if (fight_kind == FK_VERSUS) {
      if (link_fight_over(r)) {
        versus_setup();
        fight_ui = UI_INTRO;
        return FIGHT_GOING;
      }
      fight_ui = UI_END;
      show_t_ = 0;
      return FIGHT_GOING;
    } /* best of 3 or 5: the next bout */
    if (fight_gauntlet && r == 1u && fight_gauntlet < 3u) {
      doors_open();
      return FIGHT_GOING;
    }
    duel_end(r);
    return FIGHT_GOING;
  case UI_DOORS:
    if (pressed & (J_LEFT | J_RIGHT)) {
      door_at_ ^= 1u;
      sound_play(SFX_MOVE);
      doors_draw();
    } else if (pressed & J_A) {
      sound_play(SFX_SWAP);
      gauntlet_next();
    }
    return FIGHT_GOING;
  case UI_END:
    show_t_ = (uint16_t)(show_t_ + dt);
    if (show_t_ < 150u && !(pressed & J_A)) return FIGHT_GOING;
    if (fight_kind == FK_VERSUS) return finish(FIGHT_FLED);
    if (gained_ || pl.pts) {
      fight_ui = UI_LEVEL;
      (void)level_open();
      return FIGHT_GOING;
    }
    if (cru_story_state(&core, talk_saga()) != CRU_RUN_ON) return finish(FIGHT_OVER);
    return finish(end_line_ == 1u ? FIGHT_WON : FIGHT_FLED);
  case UI_LEVEL:
    if (!level_tick(pressed)) return FIGHT_GOING;
    player_save();
    gained_ = 0;
    if (cru_story_state(&core, talk_saga()) != CRU_RUN_ON) return finish(FIGHT_OVER);
    return finish(end_line_ == 1u ? FIGHT_WON : FIGHT_FLED);
  }
  return FIGHT_GOING;
}
/* the four cells crucible.c draws: kind[c], id[c] (CRU_K_*), message = turning cells (bit c), sign = the ? shimmers */
void fight_view(crucible_cells *v) BANKED {
  uint8_t c;
  fr_cardv k;
  const fr_side *p = &fr.s[0];
  for (c = 0; c < CRU_CELLS; c++) {
    v->kind[c] = 0xffu;
    v->id[c] = 0;
  }
  v->message = 0;
  v->sign = 0;
  if (fight_kind == FK_BOSS) {
    fb_state *b = FB;
    for (c = 0; c < CRU_CELLS; c++)
      if (c != CRU_CL && c != CRU_CN) v->kind[c] = CRU_K_EMPTY;
    if (fight_ui == UI_KIT || fight_ui == UI_LEVEL || fight_ui == UI_INTRO || fight_ui == UI_PLAN) return;
    if (b->a != CRU_NONE && b->kind != FB_CHARGE) {
      v->kind[CRU_CA] = CRU_K_ITEM;
      v->id[CRU_CA] = b->a;
      v->kind[CRU_CB] = CRU_K_ITEM;
      v->id[CRU_CB] = b->b;
    }
    if ((fight_ui == UI_SHOW || fight_ui == UI_END || fight_ui == UI_CHOICE) && b->atk != CRU_NONE &&
        b->kind != FB_CHARGE) {
      v->kind[CRU_CR] = CRU_K_ITEM;
      v->id[CRU_CR] = b->atk;
      v->message |= 1u << CRU_CR;
    } else if (b->kind != FB_CHARGE) {
      v->kind[CRU_CR] = CRU_K_QUESTION;
      v->sign = (uint16_t)(b->kind == FB_FEINT || b->veiled ? 1u : (fr.turn & 1u));
    }
    if (fight_ui == UI_CHOOSE && fuse_ != 0xffu) {
      uint16_t id = fuse_product();
      if (id != CRU_NONE) {
        v->kind[CRU_CF] = CRU_K_ITEM;
        v->id[CRU_CF] = id;
        v->message |= 1u << CRU_CF;
      }
      return;
    }
    if (fight_ui == UI_CHOOSE && fight_cursor < p->nhand) {
      v->kind[CRU_CF] = CRU_K_ITEM;
      v->id[CRU_CF] = p->kit[p->hand[fight_cursor]];
      v->message |= 1u << CRU_CF;
    }
    return;
  }
  for (c = 0; c < CRU_CELLS; c++)
    if (c != CRU_CL && c != CRU_CN) v->kind[c] = CRU_K_BLANK;
  if (fight_ui == UI_KIT || fight_ui == UI_LEVEL) return;
  v->kind[CRU_CA] = v->kind[CRU_CB] = v->kind[CRU_CR] = CRU_K_EMPTY; /* the slots: a fuse, your card, theirs */
  if (fight_ui == UI_INTRO) return;
  if (fight_ui == UI_CHOOSE && fuse_ != 0xffu) {
    uint16_t id = fuse_product();
    v->kind[CRU_CA] = id == CRU_NONE ? CRU_K_TRIED : CRU_K_ITEM;
    v->id[CRU_CA] = id;
  }
  if (fight_ui == UI_SHOW || fight_ui == UI_END) {
    if (fight_me != 0xffu) {
      if (fr_last_card(0, &k)) {
        v->kind[CRU_CB] = CRU_K_ITEM;
        v->id[CRU_CB] = k.id;
      } else
        v->kind[CRU_CB] = CRU_K_EMPTY;
    }
    if (fight_them != 0xffu) {
      if (fr_last_card(1, &k)) {
        v->kind[CRU_CR] = CRU_K_ITEM;
        v->id[CRU_CR] = k.id;
      } else
        v->kind[CRU_CR] = CRU_K_EMPTY;
    }
  } else if (fight_ui == UI_WAIT) {
    v->kind[CRU_CR] = CRU_K_QUESTION;
    v->sign = 1;
  }
  if ((fight_ui == UI_CHOOSE || fight_ui == UI_WAIT) && fight_cursor < p->nhand) {
    v->kind[CRU_CF] = CRU_K_ITEM;
    v->id[CRU_CF] = p->kit[p->hand[fight_cursor]];
    v->message |= 1u << CRU_CF;
  }
}
uint16_t fight_attack_a(void) BANKED { return fight_kind == FK_BOSS ? FB->a : CRU_NONE; }
uint16_t fight_attack_b(void) BANKED { return fight_kind == FK_BOSS ? FB->b : CRU_NONE; }
uint16_t fight_result(void) BANKED { return fight_kind == FK_BOSS && fight_ui == UI_SHOW ? FB->atk : CRU_NONE; }
uint16_t fight_answer(void) BANKED {
  const fr_side *p = &fr.s[0];
  return fight_cursor < p->nhand ? p->kit[p->hand[fight_cursor]] : CRU_NONE;
}
uint8_t fight_fx(void) BANKED {
  uint8_t f = fx_;
  if (f & FIGHT_FX_TEAR) tear_t_ = 24u;
  fx_ = 0;
  return f;
}
uint8_t fight_state(void) BANKED { return fight_ui == UI_SHOW ? FIGHT_SHOW : FIGHT_CUE; }
uint16_t fight_lost(void) BANKED { return lost_; }
/* The faction that hates you most, if any hates you (0xff: none). */
uint8_t fight_rival(void) BANKED {
  crucible_story *s = talk_saga();
  uint8_t f, best = 0xffu;
  int8_t low = -15;
  for (f = 0; f < CRU_FACTIONS; f++)
    if (s->stand[f] <= low) {
      low = s->stand[f];
      best = f;
    }
  return best;
}
/* the "?" of an attack still charging: a palette that flickers toward the glitch colours */
void fight_shimmer(uint16_t *p) BANKED {
  p[0] = 0x7fffu;
  p[1] = (uint16_t)(((uint16_t)cosm() << 7) ^ (uint16_t)(cosm() * 37u)) & 0x7fffu;
  p[2] = (8u | (25u << 5) | (31u << 10));
  p[3] = (28u | (6u << 5) | (24u << 10));
}
