/* Who comes to the crucible and what a fight gives and costs (docs/fight-system.md; tuned with
 * docs/fight-system/sim/cruxsim.py on the live slice).
 *   your bag   what you pinned (the bag screen), then what you own of two stars, then one, each by how often you used
 *              it on the bench; at most two of a category; three stars only pinned (REACH of them)
 *   a figure   (duel) a faction's favourites: its category, liked traits, within its tier's depth, walked from a seeded
 *              start; each followed by the shallow ingredients of its first recipe, so it has things to forge
 *   a champion (boss) the same, plus its faction's twist, and it telegraphs its next forge
 *   tiers      chapter 0-1 T1, 2-3 T2 ... the finale T5; every cycle after the first ending T6 */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_data.h"
#include "crucible_ui.h"
#include "crucible_state.h"
#include "crucible_lines.h"
#include "crucible_avatar.h"
#include "crucible_storyrun.h"
#include "crucible_fight.h"
#include "crucible_fight_int.h"
#include "crucible_player.h"
#include "crucible_link.h"
#include "crucible_link_scene.h"
#define T_CREAM 7u
#define T_BRASS 15u
uint8_t cri_depth(crucible_core *c, uint16_t id) BANKED;
uint16_t cri_shelf(crucible_core *c, uint16_t pos) BANKED;
uint8_t cri_owned(crucible_core *c, uint16_t id) BANKED;
uint16_t cri_route_first(crucible_core *c, uint16_t id) BANKED;
uint16_t cri_route(crucible_core *c, uint16_t i) BANKED;
uint16_t cri_recipe_a(crucible_core *c, uint16_t row) BANKED;
uint16_t cri_recipe_b(crucible_core *c, uint16_t row) BANKED;
uint8_t fo_faction, fo_tier, fo_nemesis, fo_end, fo_took, fui_gained;
uint16_t fo_seed, fo_know, fo_skill, fo_plan = CX_NONE, fo_xp;
char fo_name[10];
crucible_boss *fo_b;
uint16_t fs_made_ids[FS_MADE];
uint8_t fs_made_n;
static crucible_boss local_;
static const char *const FNAME[6] = {"PROGRAM", "DAEMON", "GHOST", "AI", "OPERATOR", "RELIC"};
static const uint8_t LIKE_CAT[6] = {5u, 3u, 2u, 1u, 4u, 6u};
static const uint8_t TWIST[6] = {CX_TW_COMPILE, CX_TW_CHARGE, CX_TW_SKYALL, 0, CX_TW_ROOT, CX_TW_LOCK};
/* tier: its HP, bag size, depth allowance, skill (of 256: how often it plays the baiter), knowledge (of 256). T4 brings
 * three-star things (depth 4) but plays and knows less: first tries for a typical player run 88/84/70/63/41/32% (cruxsim.py) */
static const uint8_t T_HP[7] = {0, 7, 10, 11, 9, 10, 11}, T_BAG[7] = {0, 6, 6, 7, 6, 8, 8},
                     T_DEPTH[7] = {0, 3, 3, 3, 4, 4, 4};
static const uint8_t T_SKILL[7] = {0, 0, 60, 140, 60, 130, 180}, T_KNOW[7] = {0, 115, 128, 170, 135, 165, 190};
static const uint8_t BIT8[8] = {1, 2, 4, 8, 16, 32, 64, 128};
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
static uint16_t xs16(uint16_t x) {
  x ^= (uint16_t)(x << 7);
  x ^= (uint16_t)(x >> 9);
  x ^= (uint16_t)(x << 8);
  return x;
}

/* ---- your bag ---- */
#define CAND 40u
uint8_t fs_bag(uint16_t *bag) BANKED {
  uint16_t *cid = (uint16_t *)fr_scratch; /* the best CAND candidates by score (80 bytes) */
  uint8_t sc[CAND], nc = 0, n = 0, i, j, k, s, threes = 0, reach = player_attr(PA_REACH), cats[7], c, best;
  uint16_t id, uses;
  memset(cats, 0, sizeof cats);
  if (fight_kind == FK_VERSUS) reach = 1;
  for (i = 0; i < 6u; i++) { /* pinned */
    id = pl.kit[i];
    if (id < 4u || id >= core.items || !cri_owned(&core, id)) continue;
    for (j = 0; j < n; j++)
      if (bag[j] == id) break;
    if (j < n) continue;
    s = cx_stars(id);
    if (s > 3u || (s == 3u && threes >= reach)) continue;
    bag[n++] = id;
    if (s == 3u) threes++;
    c = cx_cat(id);
    if (c < 7u) cats[c]++;
  }
  {
    uint16_t first = player_owned_next(0), walked;
    id = first;
    for (walked = 0; walked < core.found[0];
         walked++, id = player_owned_next(
                       id)) { /* candidates: what you own of two stars or one, by stars then bench uses (63 at most) */
      if (walked && id == first) break;
      if (id < 4u || cri_depth(&core, id) > 3u) continue;
      uses = cru_play_uses(&core, id);
      s = (uint8_t)((cx_stars(id) << 6) | (uses > 63u ? 63u : (uint8_t)uses));
      if (nc < CAND) {
        k = nc++;
      } else {
        for (k = 0, j = 1; j < CAND; j++)
          if (sc[j] < sc[k] || (sc[j] == sc[k] && cid[j] > cid[k])) k = j;
        if (sc[k] >= s) continue;
      }
      sc[k] = s;
      cid[k] = id;
    }
  }
  while (n < 8u) { /* the best candidate still fitting, the lower id on a tie */
    best = 0xffu;
    for (j = 0; j < nc; j++) {
      if (cid[j] == CX_NONE) continue;
      c = cx_cat(cid[j]);
      if (c < 7u && cats[c] >= 2u) continue;
      if (best == 0xffu || sc[j] > sc[best] || (sc[j] == sc[best] && cid[j] < cid[best])) best = j;
    }
    if (best == 0xffu) break;
    for (i = 0; i < n; i++)
      if (bag[i] == cid[best]) break;
    if (i == n) {
      bag[n++] = cid[best];
      c = cx_cat(cid[best]);
      if (c < 7u) cats[c]++;
    }
    cid[best] = CX_NONE;
  }
  for (j = 0; j < nc && n < 8u; j++)
    if (cid[j] != CX_NONE) {
      for (i = 0; i < n; i++)
        if (bag[i] == cid[j]) break;
      if (i == n) bag[n++] = cid[j];
    }
  return n;
}
/* ---- a faction's figure: its favourites, walked from a seeded start ---- */
static uint8_t fav_(uint8_t f, uint16_t id, uint8_t dmax) {
  uint8_t d = cri_depth(&core, id);
  return d >= 1u && d <= dmax && cru_affinity(&core, f, id) >= 2;
}
/* built a few shelf positions a frame (fs_prep_step) during the entrance: a category can be 2,000 things long. From a
 * seeded place in its category's shelf, each favourite (its liked traits, depth 1..the tier's) adds the shallow
 * ingredients of its first recipe (else itself) until the bag is full; the AI then brings partners for two of yours */
static uint16_t pp_pos, pp_first, pp_end, pp_left, pp_bag[CX_BAG];
static uint8_t pp_f, pp_dmax, pp_want, pp_n, pp_tw, pp_hp;
uint8_t fs_ready;
static uint16_t ybag_[CX_BAG];
static uint8_t yn_, duel_;
static void prep_begin(uint8_t f, uint8_t tier, uint16_t seed, uint8_t hp, uint8_t tw) {
  uint8_t cat = LIKE_CAT[f];
  pp_f = f;
  pp_dmax = T_DEPTH[tier];
  pp_want = T_BAG[tier];
  pp_n = 0;
  pp_hp = hp;
  pp_tw = tw;
  fs_ready = 0;
  pp_first = core.cat_first[cat];
  pp_end = core.cat_first[cat + 1u];
  if (pp_end <= pp_first) {
    pp_first = 0;
    pp_end = core.items;
  }
  pp_left = (uint16_t)(pp_end - pp_first);
  pp_pos = (uint16_t)(pp_first + xs16(seed) % pp_left);
  cx_side_set(1, pp_bag, 0, (int8_t)hp, tw);
}
uint8_t fs_prep_step(void) BANKED {
  uint8_t k;
  uint16_t id, e, re, row, a, b;
  if (fs_ready) return 1;
  for (k = 0; k < 40u && pp_left && pp_n < pp_want; k++, pp_left--) {
    id = cri_shelf(&core, pp_pos);
    if (++pp_pos >= pp_end) pp_pos = pp_first;
    if (!fav_(pp_f, id, pp_dmax)) continue;
    re = cri_route_first(&core, (uint16_t)(id + 1u));
    for (e = cri_route_first(&core, id); e < re; e++) {
      row = cri_route(&core, e);
      a = cri_recipe_a(&core, row);
      b = cri_recipe_b(&core, row);
      if (cri_depth(&core, a) <= 3u && cri_depth(&core, b) <= 3u) {
        if (a >= 4u && pp_n < pp_want) pp_bag[pp_n++] = a;
        if (b >= 4u && pp_n < pp_want) pp_bag[pp_n++] = b;
        break;
      }
    }
    if (e == re && pp_n < pp_want) pp_bag[pp_n++] = id;
  }
  if (pp_left && pp_n < pp_want) return 0;
  if (pp_f == FO_AI && fight_kind == FK_BOSS)
    for (k = 0; k < 2u && k < yn_ && pp_n < CX_BAG; k++) { /* WATCH: it brings a partner for two of yours */
      uint16_t lo = 0, hi = core.recipes, mid;
      while (lo < hi) {
        mid = (uint16_t)((lo + hi) >> 1);
        if (cri_recipe_a(&core, mid) < ybag_[k])
          lo = (uint16_t)(mid + 1u);
        else
          hi = mid;
      }
      for (; lo < core.recipes && cri_recipe_a(&core, lo) == ybag_[k]; lo++) {
        b = cri_recipe_b(&core, lo);
        if (b >= 4u && cri_depth(&core, b) <= 4u) {
          pp_bag[pp_n++] = b;
          break;
        }
      }
    }
  if (!pp_n) pp_bag[pp_n++] = cri_shelf(&core, pp_first);
  memcpy(cx.s[1].bag, pp_bag, (uint16_t)pp_n * 2u);
  cx.s[1].n = pp_n;
  fs_ready = 1;
  if (fight_kind == FK_BOSS) fo_plan = fs_plan();
  return 1;
}
static uint8_t tier_of(const crucible_story *s) {
  uint8_t t;
  if (s->cycle) return 6;
  t = (uint8_t)(1u + (s->chapter >> 1));
  return t > 5u ? 5u : t;
}
static uint8_t sw_hp_, sw_ohp_, sw_first_;
void fs_open_story(uint8_t f, uint8_t level, uint8_t nemesis, uint8_t duel) BANKED {
  crucible_story *s = talk_saga();
  uint8_t hp, ohp, first, tw;
  if (f == 0xffu) { /* the round begins: your bag as it is now (the bag screen may have changed it) */
    if (fight_gauntlet > 1u) return;
    yn_ = fs_bag(ybag_);
    memcpy(cx.s[0].bag, ybag_, (uint16_t)yn_ * 2u);
    cx.s[0].n = yn_;
    return;
  }
  (void)level;
  fo_faction = f < 6u ? f : 0u;
  fo_nemesis = nemesis;
  duel_ = duel;
  strcpy(fo_name, FNAME[fo_faction]);
  fo_tier = tier_of(s);
  fight_tier = fo_tier;
  fight_seed = (uint16_t)((uint16_t)s->seed ^ ((uint16_t)fo_faction * 0x3a1u) ^ ((uint16_t)pl.duels << 9) ^
                          ((uint16_t)pl.bosses << 5) ^ 0x6b5du);
  fo_seed = (uint16_t)(fight_seed * 5u + 2u);
  fo_know = T_KNOW[fo_tier];
  fo_skill = T_SKILL[fo_tier];
  cx_ai_rng = (uint16_t)(fight_seed | 1u);
  fo_b = nemesis && !duel ? &s->nemesis : &local_;
  if (!duel) { /* cru_boss_begin's bookkeeping, without its favourites' hand (THE CRUCIBLE builds its own bag, a slice a frame) */
    if (!nemesis) memset(&local_, 0, sizeof local_);
    fo_b->next = fo_b->next_a = fo_b->next_b = fo_b->stolen = CRU_NONE;
    fo_b->faction = fo_faction;
    fo_b->level = s->chapter;
    fo_b->phase = 0;
    fo_b->cue = 0;
    fo_b->met = (uint8_t)(fo_b->met + 1u);
    fo_b->max = (uint8_t)(4u + (s->chapter << 1));
    if (fo_b->max > 24u) fo_b->max = 24u;
    fo_b->hp = fo_b->max;
    cru_story_event(s, CRU_EV_FIGHT, fo_faction, s->chapter);
  }
  yn_ = 0; /* your bag: fs_open_bag, once the entrance is on screen */
  if (fight_kind == FK_GAUNTLET) {
    fo_tier = fo_tier > 2u ? (uint8_t)(fo_tier - 2u) : 1u;
    fo_know = T_KNOW[fo_tier];
    fo_skill = T_SKILL[fo_tier];
  } /* three figures in a row: two tiers down */
  hp = (uint8_t)(10u + player_attr(PA_GRIT));
  if (s->scale == CRU_STORY_GENTLE)
    hp = (uint8_t)(hp + 2u);
  else if (s->scale == CRU_STORY_HARSH)
    hp = (uint8_t)(hp - 2u);
  tw = duel ? 0u : TWIST[fo_faction];
  ohp = T_HP[fo_tier];
  if (duel) ohp = (uint8_t)(ohp - 1u);
  if (!duel && (fo_faction == FO_RELIC || fo_faction == FO_OPERATOR)) ohp--;
  if (!duel && nemesis && fo_b->met >= 2u) ohp = (uint8_t)(ohp + 2u); /* the nemesis comes back stronger */
  if (!(pl.flags & PF_FIRST_DUEL)) {
    fo_skill = 0;
    fo_know = 90;
    ohp = 6;
  } /* the first contact: a figure still learning */
  cx_side_set(0, ybag_, yn_, (int8_t)hp, 0);
  prep_begin(fo_faction, fo_tier, fight_seed, ohp, tw);
  first = (uint8_t)(fight_seed & 1u);
  if (!(pl.flags & PF_FIRST_DUEL)) first = 0;
  cx_begin(first);
  fo_end = 0;
  fo_xp = 0;
  fo_plan = CX_NONE;
  fo_took = 0;
  if (!(pl.flags & PF_FIRST_DUEL)) {
    strcpy(fui_line, "ADD ONTO THEIR POT");
    fui_line_attr = T_BRASS;
  } else
    fui_line[0] = 0; /* (its taunt: fs_say after the entrance is drawn) */
  sw_hp_ = hp;
  sw_ohp_ = ohp;
  sw_first_ = first;
}
/* your bag (it walks your shelf: a moment), then the co-op words a partner joins with (9.4) */
void fs_open_bag(void) BANKED {
  yn_ = fs_bag(ybag_);
  memcpy(cx.s[0].bag, ybag_, (uint16_t)yn_ * 2u);
  cx.s[0].n = yn_;
  if (!duel_) {
    uint16_t w[LS_WORDS];
    uint8_t i;
    memset(w, 0, sizeof w);
    w[0] = fight_seed;
    w[1] = (uint16_t)(fo_faction | (fo_tier << 4) | ((uint16_t)fo_nemesis << 8));
    for (i = 0; i < 8u && i < yn_; i++) w[2u + i] = ybag_[i];
    w[12] = (uint16_t)((uint16_t)sw_hp_ << 8 | yn_);
    w[13] = (uint16_t)(sw_first_ | (fo_skill == 0u && fo_know == 90u ? 2u : 0u) | ((uint16_t)sw_ohp_ << 8));
    link_scene_owner(w);
  }
}
/* the partner's boss in a co-op session (9.4): the same champion from the owner's words; both carts play the same
 * engine, the owner's bag and HP; who answers each of your turns comes from the seed (link_scene_round) */
void fs_open_shared(void) BANKED {
  uint16_t w1 = ls_word(1), w12 = ls_word(12);
  uint8_t i;
  fo_faction = (uint8_t)(w1 & 15u);
  if (fo_faction > 5u) fo_faction = 0;
  fo_tier = (uint8_t)((w1 >> 4) & 15u);
  if (!fo_tier || fo_tier > 6u) fo_tier = 1;
  fo_nemesis = 0;
  duel_ = 0;
  strcpy(fo_name, FNAME[fo_faction]);
  fight_tier = fo_tier;
  fight_seed = ls_word(0);
  fo_seed = (uint16_t)(fight_seed * 5u + 2u);
  fo_know = T_KNOW[fo_tier];
  fo_skill = T_SKILL[fo_tier];
  cx_ai_rng = (uint16_t)(fight_seed | 1u);
  fo_b = &local_;
  memset(&local_, 0, sizeof local_);
  yn_ = (uint8_t)(w12 & 15u);
  if (yn_ > 8u) yn_ = 8u;
  for (i = 0; i < yn_; i++) ybag_[i] = ls_word((uint8_t)(2u + i));
  if (ls_word(13) & 2u) {
    fo_skill = 0;
    fo_know = 90;
  } /* the owner's first contact */
  cx_side_set(0, ybag_, yn_, (int8_t)(w12 >> 8), 0);
  prep_begin(fo_faction, fo_tier, fight_seed, (uint8_t)(ls_word(13) >> 8), TWIST[fo_faction]);
  cx_begin((uint8_t)(ls_word(13) & 1u));
  fo_end = 0;
  fo_xp = 0;
  fo_plan = CX_NONE;
  fight_gauntlet =
      2; /* (2: the bag is the owner's, never rebuilt; its bag is packed during the entrance, its face after it is drawn) */
  strcpy(fui_line, "PULLED INTO IT");
  fui_line_attr = T_BRASS;
  link_scene_joined((uint8_t)(w12 >> 8));
}
void fs_face(void) BANKED { /* its face, made after the entrance is drawn (it takes a moment) */
  avatar_make((uint16_t)(fight_seed ^ 0xb055u), fo_faction);
  avatar_fx((uint8_t)(fo_faction == 1u ? 3u : fo_faction == 2u ? 2u : fo_faction == 3u ? 5u : 4u));
}
void fs_open_gauntlet_next(
    void) BANKED { /* the next door's figure: a fresh bag and the four, your HP carries with 3 more */
  int8_t hp = cx.s[0].hp;
  uint8_t mx = cx.s[0].max;
  fight_gauntlet++;
  fo_faction = (uint8_t)(fo_faction >= 5u ? 0u : fo_faction + 1u);
  strcpy(fo_name, FNAME[fo_faction]);
  fight_seed = xs16((uint16_t)(fight_seed + fight_gauntlet));
  fo_seed = (uint16_t)(fight_seed * 5u + 2u);
  cx_side_set(0, ybag_, yn_, (int8_t)(hp + 3), 0);
  prep_begin(fo_faction, fo_tier, fight_seed, (uint8_t)(T_HP[fo_tier] - 1u), 0);
  cx_begin(0);
  cx.s[0].hp = (int8_t)(hp + 3);
  cx.s[0].max = (uint8_t)(cx.s[0].hp > (int8_t)mx ? (uint8_t)cx.s[0].hp : mx); /* (no opener bonus on top) */
  avatar_make((uint16_t)(fight_seed ^ 0xb055u), fo_faction);
  fs_say(SAY_TAUNT, CX_NONE);
}
/* the boss's telegraph: the forge of its own it knows that makes the most stars (the first on a tie) */
uint16_t fs_plan(void) BANKED {
  uint8_t i, j, n = cx.s[1].n, bs = 0, st;
  uint16_t best = CX_NONE, r, a, b;
  for (i = 0; i < n; i++)
    for (j = (uint8_t)(i + 1u); j < n; j++) {
      a = cx.s[1].bag[i];
      b = cx.s[1].bag[j];
      r = cx_recipe(a, b);
      if (r == CX_NONE || !cx_knows(fo_seed, fo_know, a, b)) continue;
      st = cx_stars(r);
      if (st > bs) {
        bs = st;
        best = CX_ACT(CX_FORGE, i, j);
      }
    }
  return best;
}
/* a line of theirs into fui_line: one that fits the box's 19 columns (a few tries), else cut at a word */
void fs_say(uint8_t intent, uint16_t item) BANKED {
  const char *slots[CRUCIBLE_TEXT_SLOTS];
  char nm[14], t[64];
  uint8_t i, j, n, tries, cut;
  if (item != CX_NONE)
    crucible_get_name(item, nm);
  else
    strcpy(nm, "SOMETHING");
  memset(slots, 0, sizeof slots);
  slots[0] = fo_name;
  slots[1] = nm;
  for (tries = 0; tries < 2u; tries++) { /* (a line takes a few frames to unpack: two tries, then cut at a word) */
    n = crucible_text_line(
        crucible_line_pick((uint8_t)(fo_faction + 1u), intent,
                           (uint16_t)(sys_time ^ ((uint16_t)DIV_REG << 8) ^ ((uint16_t)tries * 0x3d1u))),
        t, sizeof t, slots);
    for (i = 0, j = 0; i < n; i++)
      if ((uint8_t)t[i] >= ' ' && t[i] != '$' && t[i] != '&' && t[i] != '@' && t[i] != '#' && t[i] != '%' &&
          t[i] != '*' && t[i] != '[' && t[i] != '^' && t[i] != '_' && t[i] != '\\')
        t[j++] = t[i];
    t[j] = 0;
    if (j <= 19u) break;
  }
  if (j > 19u) {
    for (cut = 19; cut && t[cut] != ' '; cut--) {}
    if (!cut) cut = 19;
    while (cut > 1u && (t[cut - 1u] == ',' || t[cut - 1u] == ';' || t[cut - 1u] == ':' || t[cut - 1u] == '-')) cut--;
    t[cut] = 0;
  } /* (a cut line never ends on a comma) */
  strcpy(fui_line, t);
  fui_line_attr = T_CREAM;
}
void fs_note_made(uint16_t id) BANKED {
  uint8_t i;
  if (id >= core.items || cri_owned(&core, id)) return;
  for (i = 0; i < fs_made_n; i++)
    if (fs_made_ids[i] == id) return;
  if (fs_made_n < FS_MADE) fs_made_ids[fs_made_n++] = id;
}
static void kdc_nudge(uint8_t *k, uint8_t axis, int8_t d) {
  int16_t v = (int16_t)(int8_t)k[14u + axis] + d;
  k[14u + axis] = (uint8_t)(int8_t)(v > 60 ? 60 : v < -60 ? -60 : v);
}
/* the end: r 1 won, 2 lost, 3 you ran, 4 it fled (the nemesis), 5 neither fell. The gains and costs; the end screen's
 * words go to fe_* (crucible_fight.c draws it) */
char fe_big[14], fe_sub[22], fe_stat[24];
uint8_t fe_win;
uint16_t fe_lost = CX_NONE;
static void big_(const char *who, const char *verb) {
  strcpy(fe_big, who);
  strcat(fe_big, verb);
}
static void stat_name(const char *k, uint16_t id) {
  char nm[14];
  crucible_get_name(id, nm);
  strcpy(fe_stat, k);
  strcat(fe_stat, nm);
}
void fs_end(uint8_t r) BANKED {
  crucible_story *s = talk_saga();
  uint8_t i;
  uint16_t lost[3];
  fo_end = r == 1u ? 1u : 2u;
  fe_win = r == 1u;
  fe_lost = CX_NONE;
  fe_stat[0] = 0;
  fe_sub[0] = 0;
  for (i = 0; i < fs_made_n; i++) { /* what you made in the fight is yours: a discovery */
    uint16_t id = fs_made_ids[i];
    if (story_on && fight_kind != FK_VERSUS)
      (void)cru_story_gain(&core, s, id);
    else
      (void)cru_grant(&core, id, 0);
  }
  if (r == 1u)
    strcpy(fe_big, "YOU WIN");
  else if (r == 2u) {
    if (!strcmp(fo_name, "THEM"))
      strcpy(fe_big, "THEY WIN");
    else
      big_(fo_name, " WINS");
  } else if (r == 3u)
    strcpy(fe_big, "YOU RAN");
  else if (r == 4u)
    strcpy(fe_big, "IT FLED");
  else
    strcpy(fe_big, "NEITHER FALLS");
  if (fight_kind == FK_VERSUS) {
    fight_ui = UI_END;
    return;
  } /* (the score and the stat: crucible_fight.c) */
  if (fight_kind == FK_DUEL || fight_kind == FK_GAUNTLET) {
    pl.flags |= PF_FIRST_DUEL;
    if (r == 1u) {
      fo_xp = fight_kind == FK_GAUNTLET ? 75u : 15u;
      if (pl.duels < 255u) pl.duels++;
      cru_story_shift(s, fo_faction, -3);
      cru_story_shift(s, cru_faction_rival(fo_faction), 3);
      cru_story_act(s, CRU_ACT_WIN);
      strcpy(fe_sub, fight_kind == FK_GAUNTLET ? "THREE DOORS, PASSED" : "IT FALLS APART");
    } else if (r == 3u) {
      fo_xp = 0;
      cru_story_lucid(s, -4);
      cru_story_act(s, CRU_ACT_REFUSE);
      strcpy(fe_sub, "IT WATCHES YOU GO");
    } else {
      fo_xp = 5;
      if (r == 2u) cru_story_lucid(s, -4);
      strcpy(fe_sub, r == 2u ? "IT WALKS AWAY" : "THE ROOM GOES QUIET");
    } /* a duel never takes your things */
    cru_story_event(s, CRU_EV_FIGHT, cx.s[0].hp > 0 ? (uint16_t)cx.s[0].hp : 0u, (uint16_t)r);
  } else {
    if (ls_role == LS_WATCHER) { /* the partner's boss: this save takes only its XP */
      fo_xp = r == 1u ? (uint16_t)(30u * fo_tier) : 10u;
      strcpy(fe_sub, r == 1u ? "NOT YOURS. STILL." : "IT WALKS THROUGH YOU");
      fui_gained = player_xp(fo_xp);
      player_save();
      link_scene_end();
      fight_ui = UI_END;
      return;
    }
    if (r == 1u) {
      fo_xp = (uint16_t)(30u * fo_tier);
      if (fo_nemesis && fo_b->met >= 3u) fo_xp = (uint16_t)(60u * fo_tier);
      cru_story_shift(s, fo_faction, -10);
      cru_story_shift(s, cru_faction_rival(fo_faction), 10);
      if (fo_faction == FO_AI &&
          !fo_took) { /* the AI beaten three times without it taking a pot of yours: its eye is yours (8.4) */
        uint8_t n = (uint8_t)(pl.secret[0] >> 6);
        if (n < 3u) n++;
        pl.secret[0] = (uint8_t)((pl.secret[0] & 0x3fu) | (uint8_t)(n << 6));
        if (n >= 3u) {
          player_unlock(5);
          pl.flags |= PF_AI_EYE;
        }
      }
      if (fo_b->stolen != CRU_NONE) {
        (void)cru_story_gain(&core, s, fo_b->stolen);
        fo_b->stolen = CRU_NONE;
      }
      cru_story_lucid(s, 24);
      cru_story_act(s, CRU_ACT_WIN);
      if (pl.bosses < 255u) pl.bosses++;
      fo_b->hp = 0;
      fs_say(SAY_HURT, CX_NONE);
      strcpy(fe_sub, fui_line);
      sound_play(SFX_FLASH);
      fui_fx |= FIGHT_FX_PHASE;
      fight_ui = UI_CHOICE;
      cru_story_event(s, CRU_EV_FIGHT, fo_b->hp, r);
      return;
    }
    if (r == 4u) {
      fo_xp = 5;
      strcpy(fe_sub, "IT WILL BE BACK");
      sound_play(SFX_CLOSE);
    } else if (r != 2u) {
      fo_xp = 10;
      strcpy(fe_sub, "THE ROOM GOES QUIET");
    } /* neither falls: it takes nothing */
    else {
      fo_xp = 10;
      lost[0] = CX_NONE;
      if (cru_story_loss(&core, s, lost, 3) && lost[0] != CX_NONE) {
        fe_lost = lost[0];
        stat_name("LOST: ", lost[0]);
      }
      cru_story_lucid(s, -6);
      cru_story_act(s, CRU_ACT_HIT);
      strcpy(fe_sub, "IT WALKS THROUGH YOU");
    }
    cru_story_event(s, CRU_EV_FIGHT, fo_b->hp, r);
  }
  fui_gained = player_xp(fo_xp);
  player_save();
  story_save();
  fui_fx |= FIGHT_FX_TEAR;
  fight_ui = UI_END;
}
/* the end choice (7.2), no labels: A takes one of its bag (the heart darkens), B lets it fade (it hates you less),
 * SELECT unmakes the biggest thing it brought into its halves (order loosens) */
uint8_t fs_choice(uint8_t pressed) BANKED {
  crucible_story *s = talk_saga();
  uint8_t *k = s->flags + 16u, i, best = 0;
  uint16_t took = CX_NONE;
  if (!(pressed & (J_A | J_B | J_SELECT))) return 0;
  if (s->lucid < 64u && (DIV_REG & 3u) == 0u)
    pressed = (pressed & J_A) ? J_B : (pressed & J_B) ? J_A : pressed; /* the dream misreads */
  if (pressed & J_A) {
    for (i = 0; i < cx.s[1].n; i++)
      if (!cru_owned(&core, cx.s[1].bag[i]) && cru_story_gain(&core, s, cx.s[1].bag[i])) {
        took = cx.s[1].bag[i];
        break;
      }
    kdc_nudge(k, 1, -3);
    cru_story_shift(s, fo_faction, -6);
    cru_story_act(s, CRU_ACT_TAKE);
    pl.secret[0] = (uint8_t)((pl.secret[0] & ~0x38u) |
                             ((((pl.secret[0] >> 3) & 7u) < 7u ? ((pl.secret[0] >> 3) & 7u) + 1u : 7u) << 3));
    if (((pl.secret[0] >> 3) & 7u) >= 3u) player_unlock(39); /* horns: you took three */
    strcpy(fe_sub, "YOU KEEP A PIECE");
    if (took != CX_NONE) stat_name("TOOK: ", took);
  } else if (pressed & J_B) {
    kdc_nudge(k, 1, 3);
    cru_story_shift(s, fo_faction, 4);
    cru_story_lucid(s, 8);
    cru_story_act(s, CRU_ACT_SPARE);
    pl.secret[0] = (uint8_t)((pl.secret[0] & ~7u) | ((pl.secret[0] & 7u) < 7u ? (pl.secret[0] & 7u) + 1u : 7u));
    if ((pl.secret[0] & 7u) >= 3u) player_unlock(38); /* halo: you let three go */
    strcpy(fe_sub, "IT FADES AWAY");
  } else {
    for (i = 1; i < cx.s[1].n; i++)
      if (cx_stars(cx.s[1].bag[i]) > cx_stars(cx.s[1].bag[best])) best = i;
    if (cx.s[1].n) {
      uint16_t ab[2];
      if (cru_story_split(&core, s, cx.s[1].bag[best], ab)) {
        (void)cru_story_gain(&core, s, ab[0]);
        (void)cru_story_gain(&core, s, ab[1]);
        stat_name("UNMADE: ", cx.s[1].bag[best]);
      }
    }
    kdc_nudge(k, 0, -3);
    cru_story_lucid(s, -8);
    strcpy(fe_sub, "IT COMES APART");
  }
  sound_play(SFX_NEW);
  fui_gained = player_xp(fo_xp);
  player_save();
  story_save();
  fui_fx |= FIGHT_FX_TEAR;
  return 1;
}
