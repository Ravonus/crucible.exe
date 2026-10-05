/* Feats: twenty-four personal achievements with four tiers each, and the 8x7 title matrix (a title's grade is the
 * lower of its trait feat's and its domain feat's tiers). Every statistic is kept incrementally, so a check costs
 * the same at 57 ids as at 4096: no scan of the catalogue or the tried set ever runs per event.
 * Clocks (fastest pair, five finds, flurry and pairs per minute) use 16-bit frame stamps; the sliding windows are
 * queues of stamps that expire on every tick, so a stamp never lives long enough to wrap. */
#include "cru_internal.h"

CRI_BITS;
CRI_AREA_RD
CRI_SAT
/* the rule tables are this file's own constants (not catalogue tables): plain reads */
#define RULE(p, i) ((p)[i])

/* ---- the game's rule tables, kept beside the code that reads them ---- */
#define ALL 0xffffu /* clamps to the catalogue's total */
#define SEC 60u /* frames */

static const uint8_t feat_stat[CRU_FEATS] = {
    CRU_S_FOUND,    CRU_S_RECIPES,  CRU_S_GAP,      CRU_S_FLURRY,   CRU_S_STREAK,   CRU_S_CHAIN,
    CRU_S_PAIRS,    CRU_S_FAILS,    CRU_S_MIRROR,   CRU_S_GILDED,   CRU_S_CAT + 4u, CRU_S_CAT + 1u,
    CRU_S_CAT + 2u, CRU_S_CAT + 5u, CRU_S_CAT + 3u, CRU_S_CAT + 6u, CRU_S_BOOK,     CRU_S_MINUTES,
    CRU_S_COMPLETE, CRU_S_FIRST,    CRU_S_NOFAIL,   CRU_S_POINTS,   CRU_S_SWIFT,    CRU_S_LINKS};
/* 1: smaller is better (QUICK HAND in frames, SPEEDRUN in minutes, OPENING in seconds) */
static const uint8_t feat_lower[CRU_FEATS] = {0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0};
static const uint16_t feat_tier[CRU_FEATS * 4u] = {
    8,         16,        24,        ALL, /* COLLECTOR   DISCOVER THINGS */
    5,         12,        20,        ALL, /* RECIPES     DISTINCT RECIPES */
    60u * SEC, 30u * SEC, 15u * SEC, 8u * SEC, /* QUICK HAND  TWO FINDS, QUICKLY */
    2,         3,         5,         8, /* FLURRY      FINDS IN 60 SEC */
    3,         6,         12,        20, /* HOT STREAK  NEW PAIRS, NO MISS */
    2,         3,         4,         6, /* CHAIN       BUILD ON NEWEST */
    15,        40,        100,       250, /* EXPLORER    DIFFERENT PAIRS */
    10,        40,        120,       300, /* STUBBORN    FRESH PAIRS FAILED */
    1,         2,         3,         4, /* MIRROR      SAME + SAME */
    1,         2,         4,         8, /* GILDED      GILDED SPECIMENS */
    25,        50,        75,        100, /* NATURALIST  LIFE FOUND (%) */
    25,        50,        75,        100, /* GEOLOGIST   MATTER FOUND */
    25,        50,        75,        100, /* STORMCALL   WEATHER FOUND */
    25,        50,        75,        100, /* ARTISAN     CRAFTS FOUND */
    25,        50,        75,        100, /* CONDUCTOR   ENERGY FOUND */
    25,        50,        75,        100, /* VOYAGER     PLACES FOUND */
    10,        40,        120,       300, /* SCHOLAR     READ BOOK ENTRIES */
    10,        60,        240,       900, /* DEVOTED     TIME AT THE BENCH (minutes) */
    120,       60,        30,        15, /* SPEEDRUN    FIND ALL, FAST (minutes) */
    300,       180,       120,       90, /* OPENING     5 FINDS FROM START (seconds) */
    3,         6,         12,        20, /* CLEAN RUN   NEW FINDS, NO MISS */
    100,       300,       700,       1500, /* HOARDER     EARN POINTS */
    5,         10,        16,        24, /* SWIFT       NEW PAIRS A MINUTE */
    1,         3,         10,        25}; /* LINKED      LINK WITH A FRIEND */
static const uint8_t tier_points[4] = {5, 10, 20, 40};
/* title rows (traits) SWIFT DOGGED CURIOUS GILDED FIERCE SOCIAL WISE DEVOTED, columns (domains) ADEPT MASON
 * RAINMAKER SPARKSMITH DRUID ARTISAN VOYAGER (the category order) */
static const uint8_t trait_feat[CRU_TITLE_TRAITS] = {2, 7, 6, 9, 4, 23, 16, 17};
static const uint8_t domain_feat[CRU_TITLE_DOMAINS] = {0, 11, 12, 14, 10, 13, 15};

const crucible_rules cru_rules_world = {feat_stat,  feat_lower,  feat_tier, tier_points,
                                        trait_feat, domain_feat, 10u,       3u};

#define WINDOW 3600u /* 60 s in frames */
#define SWIFT_RING 24u
#define FLURRY_RING 8u

/* floor(100*f/n) for f <= n <= 32767: two decimal long-division steps, each adding the dividend ten times and
 * taking n off whenever the remainder reaches it (remainder + dividend < 2n stays in 16 bits). */
static uint8_t percent(uint16_t f, uint16_t n) {
  uint16_t r = 0, r2 = 0;
  uint8_t d1 = 0, d2 = 0, k;
  if (!n) return 0;
  for (k = 0; k < 10u; k++) {
    r = (uint16_t)(r + f);
    if (r >= n) {
      r = (uint16_t)(r - n);
      d1++;
    }
  }
  for (k = 0; k < 10u; k++) {
    r2 = (uint16_t)(r2 + r);
    if (r2 >= n) {
      r2 = (uint16_t)(r2 - n);
      d2++;
    }
  }
  return (uint8_t)((d1 << 3) + (d1 << 1) + d2);
}
static void push_toast(crucible_core *c, uint8_t code) {
  uint8_t n = c->toast_n, at;
  if (n >= CRU_TOASTS) return;
  at = c->toast_first;
  at = (uint8_t)((at + n) & (CRU_TOASTS - 1u));
  c->toast[at] = code;
  c->toast_n = (uint8_t)(n + 1u);
}
uint8_t cru_toast_peek(const crucible_core *c) CORE_BANKED {
  uint8_t n = c->toast_n, at = c->toast_first, v;
  if (!n) return CRU_TOAST_NONE;
  v = c->toast[at];
  return v;
}
void cru_toast_done(crucible_core *c) CORE_BANKED {
  uint8_t n = c->toast_n, at = c->toast_first;
  if (!n) return;
  c->toast_first = (uint8_t)((at + 1u) & (CRU_TOASTS - 1u));
  c->toast_n = (uint8_t)(n - 1u);
}

/* ---- sliding 60-second windows ---- */
static void expire(uint16_t now, const uint16_t *ring, uint8_t size, uint8_t *first, uint8_t *n) {
  uint8_t f = *first, k = *n;
  uint16_t t;
  while (k) {
    t = ring[f];
    if ((uint16_t)(now - t) <= WINDOW) break;
    f++;
    if (f >= size) f = 0;
    k--;
  }
  *first = f;
  *n = k;
}
static uint8_t window_push(uint16_t now, uint16_t *ring, uint8_t size, uint8_t *first, uint8_t *n) {
  uint8_t f, k, at;
  expire(now, ring, size, first, n);
  f = *first;
  k = *n;
  if (k >= size) {
    f++;
    if (f >= size) f = 0;
    k--;
  }
  at = (uint8_t)(f + k);
  if (at >= size) at = (uint8_t)(at - size);
  ring[at] = now;
  k++;
  *first = f;
  *n = k;
  return k;
}

/* ---- derived counts (once per load): found per filter, gilded, the shelf summary ---- */
void cri_sum_mark(crucible_core *c, uint16_t pos) CORE_LOCAL {
  uint8_t blk = (uint8_t)(pos >> 6), v = c->shelf_sum[(uint8_t)(blk >> 3)];
  v |= bit_mask[blk & 7u];
  c->shelf_sum[(uint8_t)(blk >> 3)] = v;
}
static void bump(crucible_core *c, uint8_t f) {
  uint16_t v = c->found[f];
  c->found[f] = (uint16_t)(v + 1u);
}
/* an owned id into found[], gilded and the summary (pos: its shelf position) */
static void tally(crucible_core *c, uint16_t id, uint16_t pos) {
  uint16_t m = cri_traits(c, id);
  uint8_t f, k = cri_category(c, id);
  bump(c, 0);
  if (k < CRU_CATEGORIES) bump(c, (uint8_t)(k + 1u));
  for (f = 8; f < CRU_FILTERS; f++, m >>= 1)
    if (m & 1u) bump(c, f);
  if (cri_variant(c, id) == 3u) c->gilded = (uint16_t)(c->gilded + 1u);
  cri_sum_mark(c, pos);
}
/* Walks the OWNED bytes (8 items a read; the journal is empty after a load) and touches the tables only for owned
 * items. The totals per filter and the mirror count come baked with the tables (cri_tables_init). */
void cri_derive(crucible_core *c) CORE_LOCAL {
  uint16_t j, n = c->items, bytes = (uint16_t)((n + 7u) >> 3), id, pos;
  uint8_t f, byte, b, wide = 0;
  if (c->place == CRU_PLACE_WIDE) wide = 1;
  for (f = 0; f < CRU_FILTERS; f++) c->found[f] = 0;
  for (f = 0; f < 32u; f++) c->shelf_sum[f] = 0;
  c->gilded = 0;
  for (j = 0; j < bytes; j++) {
    byte = cri_ar(c, (uint16_t)(c->a_owned + j));
    if (!byte) continue;
    for (b = 0; b < 8u; b++) {
      if (!(byte & bit_mask[b])) continue;
      if (wide) {
        pos = (uint16_t)((j << 3) + b);
        id = cri_shelf(c, pos);
      } /* v5: bits are shelf positions */
      else {
        id = (uint16_t)((j << 3) + b);
        pos = cri_shelf_pos(c, id);
      }
      tally(c, id, pos);
    }
  }
  /* pairs = n(n+1)/2 without a multiply, saturating */
  c->pair_cap = 0;
  for (j = 1; j <= n; j++) {
    if (c->pair_cap > (uint16_t)(0xffffu - j)) {
      c->pair_cap = 0xffffu;
      break;
    }
    c->pair_cap = (uint16_t)(c->pair_cap + j);
  }
}
/* a newly owned id */
void cri_count_new(crucible_core *c, uint16_t id) CORE_LOCAL { tally(c, id, cri_shelf_pos(c, id)); }

/* ---- values, thresholds, tiers ---- */
static uint16_t stat_value(crucible_core *c, uint8_t s) {
  uint16_t f, n;
  if (s >= CRU_S_CAT && s < CRU_S_CAT + CRU_CATEGORIES) {
    s = (uint8_t)(s - CRU_S_CAT + 1u);
    f = c->found[s];
    n = c->total[s];
    return percent(f, n);
  }
  switch (s) {
  case CRU_S_FOUND: return c->found[0];
  case CRU_S_RECIPES: return cri_value(c, CRU_PV_RECIPES);
  case CRU_S_GAP: return c->best_gap;
  case CRU_S_FLURRY: return c->best_flurry;
  case CRU_S_STREAK: return c->best_streak;
  case CRU_S_CHAIN: return c->best_chain;
  case CRU_S_PAIRS: return cri_value(c, CRU_PV_PAIRS);
  case CRU_S_FAILS: return c->fails;
  case CRU_S_MIRROR: return cri_value(c, CRU_PV_MIRRORS);
  case CRU_S_GILDED: return c->gilded;
  case CRU_S_BOOK: return c->book_views;
  case CRU_S_MINUTES: return c->minutes;
  case CRU_S_COMPLETE: return c->complete;
  case CRU_S_FIRST: return c->best_first;
  case CRU_S_NOFAIL: return c->best_nofail;
  case CRU_S_POINTS: return c->points;
  case CRU_S_SWIFT: return c->best_swift;
  default: return c->links;
  }
}
/* Thresholds above what this catalogue allows clamp to its total, so every tier stays reachable. */
static uint16_t threshold(const crucible_core *c, uint8_t f, uint8_t tier) {
  const crucible_rules *r = c->rules;
  const uint16_t *tiers = r->feat_tier;
  const uint8_t *stats = r->feat_stat;
  uint16_t t = RULE(tiers, ((uint16_t)f << 2) + tier), cap = 0;
  uint8_t s = RULE(stats, f);
  if (s == CRU_S_FOUND)
    cap = c->items;
  else if (s == CRU_S_RECIPES)
    cap = c->recipes;
  else if (s == CRU_S_MIRROR)
    cap = c->mirror_total;
  else if (s == CRU_S_PAIRS)
    cap = c->pair_cap;
  if (cap && t > cap) t = cap;
  return t;
}
/* A feat reaching its first tier can open a row or a column of the title matrix: queue each newly earned title. */
static uint8_t tier_of(const crucible_core *c, uint8_t f) {
  uint8_t v = c->feats[f];
  return v;
}
static void announce(crucible_core *c, uint8_t f) {
  const uint8_t *tf = c->rules->trait_feat, *df = c->rules->domain_feat;
  uint8_t i, k, x, y;
  for (i = 0; i < CRU_TITLE_TRAITS; i++) {
    x = RULE(tf, i);
    if (x != f) continue;
    for (k = 0; k < CRU_TITLE_DOMAINS; k++) {
      y = RULE(df, k);
      if (tier_of(c, y)) push_toast(c, (uint8_t)(CRU_TOAST_TITLE | (uint8_t)((i << 3) - i + k)));
    }
  }
  for (k = 0; k < CRU_TITLE_DOMAINS; k++) {
    y = RULE(df, k);
    if (y != f) continue;
    for (i = 0; i < CRU_TITLE_TRAITS; i++) {
      x = RULE(tf, i);
      if (tier_of(c, x)) push_toast(c, (uint8_t)(CRU_TOAST_TITLE | (uint8_t)((i << 3) - i + k)));
    }
  }
}
/* One pass in feat order (as the cartridge): tier points land at once, so HOARDER sees the earlier feats' points. */
uint8_t cri_check(crucible_core *c) CORE_LOCAL {
  const uint8_t *stats = c->rules->feat_stat, *lowers = c->rules->feat_lower, *pts = c->rules->tier_points;
  uint8_t f, tier, got = 0, lower, s, have, reached, p;
  uint16_t v, th;
  for (f = 0; f < CRU_FEATS; f++) {
    s = RULE(stats, f);
    v = stat_value(c, s);
    lower = RULE(lowers, f);
    tier = 0;
    while (tier < CRU_TIERS) {
      th = threshold(c, f, tier);
      reached = 0;
      if (lower) {
        if (v && v <= th) reached = 1;
      } else if (th && v >= th)
        reached = 1;
      if (!reached) break;
      tier++;
    }
    have = c->feats[f];
    while (have < tier) {
      p = RULE(pts, have);
      c->points = cri_sat_add(c->points, p);
      have++;
      c->feats[f] = have;
      push_toast(c, (uint8_t)((f << 2) | (uint8_t)(have - 1u)));
      if (have == 1u) announce(c, f);
      got = 1;
    }
  }
  return got;
}

void cri_feats_reset(crucible_core *c) CORE_LOCAL {
  c->clock = 0;
  c->since_find = 0;
  c->session_s = 0;
  c->frames = 0;
  c->session_finds = 0;
  c->found_this_session = 0;
  c->swift_first = c->swift_n = 0;
  c->flurry_first = c->flurry_n = 0;
  c->toast_first = c->toast_n = 0;
}

/* fresh: this pair had never been tried. Repeats never count toward pace, streaks, clean runs or fails. */
void cri_feats_mix(crucible_core *c, uint16_t a, uint16_t b, uint16_t result, uint8_t is_new,
                   uint8_t fresh) CORE_LOCAL {
  uint8_t n, chain;
  uint16_t last;
  if (fresh) {
    n = window_push(c->clock, c->swift_t, SWIFT_RING, &c->swift_first, &c->swift_n);
    if (n > c->best_swift) c->best_swift = n;
    if (result == CRU_NONE) {
      if (c->fails < 0xffffu) c->fails++;
      c->streak = 0;
      c->nofail = 0;
    } else {
      if (c->streak < 255u) c->streak++;
      if (c->streak > c->best_streak) c->best_streak = c->streak;
    }
  }
  if (is_new) {
    /* QUICK HAND: frames since the previous find of this power-on */
    if (c->found_this_session && (!c->best_gap || c->since_find < c->best_gap)) c->best_gap = c->since_find;
    /* OPENING: seconds from power-on to the fifth new find of this power-on */
    if (c->session_finds < 255u && ++c->session_finds == 5u && (!c->best_first || c->session_s < c->best_first))
      c->best_first = c->session_s;
    c->found_this_session = 1;
    c->since_find = 0;
    n = window_push(c->clock, c->flurry_t, FLURRY_RING, &c->flurry_first, &c->flurry_n);
    if (n > c->best_flurry) c->best_flurry = n;
    last = c->last_new;
    chain = c->chain;
    if (a == last || b == last) {
      if (chain < 255u) chain++;
    } else
      chain = 1;
    c->chain = chain;
    if (chain > c->best_chain) c->best_chain = chain;
    c->last_new = result;
    if (c->nofail < 255u) c->nofail++;
    if (c->nofail > c->best_nofail) c->best_nofail = c->nofail;
    if (!c->complete && c->found[0] >= c->items) {
      last = c->minutes;
      if (!last) last = 1;
      c->complete = last;
    }
  }
  cri_check(c);
}

void cru_tick(crucible_core *c, uint8_t dt) CORE_BANKED {
  uint16_t f;
  c->clock = (uint16_t)(c->clock + dt);
  f = c->since_find;
  if (f > (uint16_t)(0xffffu - dt))
    f = 0xffffu;
  else
    f = (uint16_t)(f + dt);
  c->since_find = f;
  expire(c->clock, c->swift_t, SWIFT_RING, &c->swift_first, &c->swift_n);
  expire(c->clock, c->flurry_t, FLURRY_RING, &c->flurry_first, &c->flurry_n);
  f = (uint16_t)(c->frames + dt);
  while (f >= 60u) {
    f = (uint16_t)(f - 60u);
    if (c->session_s < 0xffffu) c->session_s++;
    if (++c->seconds >= 60u) {
      c->seconds = 0;
      if (c->minutes < 0xffffu) c->minutes++;
      if (cri_check(c)) cri_save(c);
    }
  }
  c->frames = (uint8_t)f;
}
void cru_link(crucible_core *c) CORE_BANKED {
  if (c->links < 255u) c->links++;
  cri_check(c);
  cri_save(c);
}

/* ---- feat accessors (records pages) ---- */
uint8_t cru_feat_tier(const crucible_core *c, uint8_t f) CORE_BANKED {
  if (f >= CRU_FEATS) return 0;
  return tier_of(c, f);
}
uint8_t cri_points(crucible_core *c, uint8_t outcome) CORE_LOCAL {
  const crucible_rules *r = c->rules;
  uint8_t v = 0;
  if (outcome == CRU_NEW)
    v = r->points_new;
  else if (outcome == CRU_ROUTE)
    v = r->points_route;
  return v;
}
uint16_t cru_feat_value(crucible_core *c, uint8_t f) CORE_BANKED {
  const uint8_t *p = c->rules->feat_stat;
  uint8_t s = RULE(p, f);
  return stat_value(c, s);
}
uint16_t cru_feat_threshold(const crucible_core *c, uint8_t f, uint8_t tier) CORE_BANKED {
  return threshold(c, f, (uint8_t)(tier & 3u));
}
uint8_t cru_feat_lower(const crucible_core *c, uint8_t f) CORE_BANKED {
  const uint8_t *p = c->rules->feat_lower;
  uint8_t v = RULE(p, f);
  return v;
}
uint8_t cru_feat_unit(const crucible_core *c, uint8_t f) CORE_BANKED {
  const uint8_t *p = c->rules->feat_stat;
  uint8_t s = RULE(p, f);
  if (s == CRU_S_GAP) return CRU_UNIT_FRAMES;
  if (s == CRU_S_FIRST) return CRU_UNIT_SECONDS;
  if (s == CRU_S_COMPLETE || s == CRU_S_MINUTES) return CRU_UNIT_MINUTES;
  if (s >= CRU_S_CAT && s < CRU_S_CAT + CRU_CATEGORIES) return CRU_UNIT_PERCENT;
  return CRU_UNIT_COUNT;
}
uint8_t cru_feats_total(const crucible_core *c) CORE_BANKED {
  uint8_t i, n = 0;
  for (i = 0; i < CRU_FEATS; i++) n = (uint8_t)(n + tier_of(c, i));
  return n;
}

/* ---- titles: row = trait, column = domain; t = row*7 + column ---- */
static uint8_t split(uint8_t t, uint8_t *k) {
  uint8_t r = 0;
  while (t >= CRU_TITLE_DOMAINS) {
    t = (uint8_t)(t - CRU_TITLE_DOMAINS);
    r++;
  }
  *k = t;
  return r;
}
uint8_t cru_title_feat(const crucible_core *c, uint8_t t, uint8_t axis) CORE_BANKED {
  const uint8_t *tf = c->rules->trait_feat, *df = c->rules->domain_feat;
  uint8_t k, r = split(t, &k), v;
  if (axis)
    v = RULE(df, k);
  else
    v = RULE(tf, r);
  return v;
}
static uint8_t grade(const crucible_core *c, uint8_t t) {
  uint8_t k, r, a, b;
  if (t >= CRU_TITLES) return 0;
  r = split(t, &k);
  a = RULE(c->rules->trait_feat, r);
  b = RULE(c->rules->domain_feat, k);
  a = tier_of(c, a);
  b = tier_of(c, b);
  if (b < a) a = b;
  return a;
}
uint8_t cru_title_grade(const crucible_core *c, uint8_t t) CORE_BANKED { return grade(c, t); }
uint8_t cru_title_needs(const crucible_core *c, uint8_t t) CORE_BANKED {
  const uint8_t *tf = c->rules->trait_feat, *df = c->rules->domain_feat;
  uint8_t k, r = split(t, &k), a, b, need = 0;
  a = RULE(tf, r);
  b = RULE(df, k);
  if (!tier_of(c, a)) need |= 1u;
  if (!tier_of(c, b)) need |= 2u;
  return need;
}
uint8_t cru_titles_earned(const crucible_core *c) CORE_BANKED {
  uint8_t t, n = 0;
  for (t = 0; t < CRU_TITLES; t++)
    if (grade(c, t)) n++;
  return n;
}
uint8_t cru_title_wear(crucible_core *c, uint8_t t) CORE_BANKED {
  uint8_t r;
  if (t >= CRU_TITLES || !grade(c, t)) return 0;
  r = c->title;
  if (r == (uint8_t)(t + 1u)) {
    c->title = 0;
    r = 2;
  } else {
    c->title = (uint8_t)(t + 1u);
    r = 1;
  }
  cri_save(c);
  return r;
}
