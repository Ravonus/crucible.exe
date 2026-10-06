/* THE CRUCIBLE's engine (crucible_crux.h): one pot between two sides, every move a real recipe. Matches
 * docs/fight-system/sim/crux.py turn by turn (golden traces: host build, then the ROM).
 * SDCC: no variable shifts in bit tests (mask tables), no ternaries around loads through pointers. */
#ifndef CRUX_HOST
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_state.h"
#else
#include <string.h>
#endif
#include "crucible_crux.h"

cx_state cx;
uint8_t fr_scratch[80];
static const uint8_t BIT8[8] = {1, 2, 4, 8, 16, 32, 64, 128};
static const uint16_t BIT16[12] = {1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u, 256u, 512u, 1024u, 2048u};
/* BEATS[k]: the traits that beat trait k (cru_saga.c): water and cold put out heat, stone and weight stop air ... */
static const uint16_t BEATS[12] = {6u, 65u, 258u, 1040u, 260u, 20u, 1040u, 2049u, 3u, 2052u, 2560u, 576u};

/* ---- the catalogue ---- */
#ifndef CRUX_HOST
uint8_t cri_category(crucible_core *c, uint16_t id) BANKED;
uint16_t cri_traits(crucible_core *c, uint16_t id) BANKED;
uint8_t cri_depth(crucible_core *c, uint16_t id) BANKED;
uint16_t cri_recipe_a(crucible_core *c, uint16_t row) BANKED;
uint16_t cri_recipe_b(crucible_core *c, uint16_t row) BANKED;
uint16_t cri_route_first(crucible_core *c, uint16_t id) BANKED;
uint16_t cri_route(crucible_core *c, uint16_t i) BANKED;
uint16_t cri_result(crucible_core *c, uint16_t row) BANKED;
#define DEPTH(id) cri_depth(&core, id)
#define CAT_(id) cri_category(&core, id)
#define TRAITS(id) cri_traits(&core, id)
#define ROW_A(r) cri_recipe_a(&core, r)
#define ROW_B(r) cri_recipe_b(&core, r)
#define ROW_R(r) cri_result(&core, r)
#define ROUTE_FIRST(id) cri_route_first(&core, id)
#define ROUTE(i) cri_route(&core, i)
#define NROWS core.recipes
uint16_t cx_recipe(uint16_t a, uint16_t b) BANKED { return cru_recipe(&core, a, b); }
#else
extern const uint16_t *cx_host_traits, *cx_host_ra, *cx_host_rb, *cx_host_rr, *cx_host_rf, *cx_host_rl;
extern const uint8_t *cx_host_depth, *cx_host_cat;
extern uint16_t cx_host_rows;
#define DEPTH(id) cx_host_depth[id]
#define CAT_(id) cx_host_cat[id]
#define TRAITS(id) cx_host_traits[id]
#define ROW_A(r) cx_host_ra[r]
#define ROW_B(r) cx_host_rb[r]
#define ROW_R(r) cx_host_rr[r]
#define ROUTE_FIRST(id) cx_host_rf[id]
#define ROUTE(i) cx_host_rl[i]
#define NROWS cx_host_rows
static uint16_t find_row(uint16_t a, uint16_t b) {
  uint16_t lo = 0, hi = NROWS, mid, ra;
  if (a > b) {
    uint16_t t = a;
    a = b;
    b = t;
  }
  while (lo < hi) {
    mid = (uint16_t)((lo + hi) >> 1);
    ra = ROW_A(mid);
    if (ra < a || (ra == a && ROW_B(mid) < b))
      lo = (uint16_t)(mid + 1u);
    else
      hi = mid;
  }
  return lo < NROWS && ROW_A(lo) == a && ROW_B(lo) == b ? lo : CX_NONE;
}
uint16_t cx_recipe(uint16_t a, uint16_t b) {
  uint16_t r = find_row(a, b);
  return r == CX_NONE ? CX_NONE : ROW_R(r);
}
#endif
uint8_t cx_stars(uint16_t id) BANKED {
  uint8_t d = DEPTH(id);
  return d <= 1u ? 1u : d <= 3u ? 2u : d <= 5u ? 3u : 4u;
}
uint8_t cx_cat(uint16_t id) BANKED { return CAT_(id); }
uint16_t cx_traits(uint16_t id) BANKED { return TRAITS(id); }
uint16_t cx_split(uint16_t x, uint16_t s) BANKED {
  uint16_t i, e, row, a, b;
  if (!DEPTH(s)) return CX_NONE;
  e = ROUTE_FIRST((uint16_t)(s + 1u));
  for (i = ROUTE_FIRST(s); i < e; i++) {
    row = ROUTE(i);
    a = ROW_A(row);
    b = ROW_B(row);
    if (a == x) return b;
    if (b == x) return a;
  }
  return CX_NONE;
}
uint8_t cx_edges(uint16_t x, uint16_t s) BANKED {
  uint16_t ts = TRAITS(s), tx = TRAITS(x);
  uint8_t k, n = 0;
  for (k = 0; k < 12u; k++)
    if ((ts & BIT16[k]) && (tx & BEATS[k])) n++;
  return n;
}
/* the first recipe row whose a is id (rows are sorted by a, then b) with b of at most maxdepth: b */
uint16_t cx_partner(uint16_t id, uint8_t maxdepth) BANKED {
  uint16_t lo = 0, hi = NROWS, mid, b;
  while (lo < hi) {
    mid = (uint16_t)((lo + hi) >> 1);
    if (ROW_A(mid) < id)
      lo = (uint16_t)(mid + 1u);
    else
      hi = mid;
  }
  for (; lo < NROWS && ROW_A(lo) == id; lo++) {
    b = ROW_B(lo);
    if (DEPTH(b) <= maxdepth) return b;
  }
  return CX_NONE;
}
static uint8_t can_break(uint16_t x, uint16_t s) { return cx_edges(x, s) >= 1u && cx_stars(x) >= cx_stars(s); }

/* ---- the engine ---- */
void cx_side_set(uint8_t k, const uint16_t *bag, uint8_t n, int8_t hp, uint8_t tw) BANKED {
  cx_side *p = &cx.s[k];
  if (n > CX_BAG) n = CX_BAG;
  memcpy(p->bag, bag, (uint16_t)n * 2u);
  p->n = n;
  p->four = 15u;
  p->hp = hp;
  p->max = (uint8_t)hp;
  p->tw = tw;
  p->made = 0;
}
void cx_begin(uint8_t first) BANKED {
  cx.pot = CX_NONE;
  cx.sky = 0;
  cx.owner = 0;
  cx.heat = 0;
  cx.turn = 0;
  cx.act = first;
  cx.chain = 0;
  cx.passes = 0;
  cx.shield = 0;
  cx.ev = CX_EV_PASS;
  cx.dmg = 0;
  cx.ex = cx.er = CX_NONE;
  cx.s[first].hp = (int8_t)(cx.s[first].hp + CX_OPEN_HP);
  cx.s[first].max = (uint8_t)(cx.s[first].max + CX_OPEN_HP);
}
uint16_t cx_tool(uint8_t k, uint8_t i) BANKED { return i < 12u ? cx.s[k].bag[i] : (uint16_t)(i - 12u); }
uint8_t cx_has(uint8_t k, uint8_t i) BANKED {
  if (i < 12u) return i < cx.s[k].n;
  return i < 16u && (cx.s[k].four & BIT8[i - 12u]) ? 1u : 0u;
}
uint8_t cx_tools(uint8_t k) BANKED {
  uint8_t e, n = cx.s[k].n, f = cx.s[k].four;
  for (e = 0; e < 4u; e++)
    if (f & BIT8[e]) n++;
  return n;
}
uint8_t cx_phase(uint8_t k) BANKED {
  int16_t h3 = (int16_t)cx.s[k].hp * 3;
  int16_t m = (int16_t)cx.s[k].max;
  if (h3 > m + m) return 0;
  if (h3 > m) return 1;
  return 2;
}
uint8_t cx_locked_to(uint8_t k) BANKED {
  uint8_t o = cx.owner;
  if (cx.pot == CX_NONE) return 0;
  if (CAT_(cx.pot) == CX_C_PLACE) return 1;
  return o != k && (cx.s[o].tw & CX_TW_LOCK) && cx_phase(o) >= 2u ? 1u : 0u;
}
uint8_t cx_outcome(uint8_t k, uint16_t x, uint16_t *r) BANKED {
  uint16_t p = cx.pot, y;
  *r = CX_NONE;
  if (p == CX_NONE) {
    *r = x;
    return CX_START;
  }
  if (cx_locked_to(k)) return cx.owner != k && can_break(x, p) ? CX_BREAK : CX_MISS;
  y = cx_recipe(x, p);
  if (y != CX_NONE) {
    *r = y;
    return cx.owner == k ? CX_BUILD : CX_HIJACK;
  }
  if (cx.owner != k) {
    y = cx_split(x, p);
    if (y != CX_NONE) {
      *r = y;
      return CX_SPLIT;
    }
    if (can_break(x, p)) return CX_BREAK;
  }
  return CX_MISS;
}
uint8_t cx_power(void) BANKED {
  uint16_t p = cx.pot;
  uint8_t v = cx_stars(p);
  if (CAT_(p) != CX_C_PLACE) v = (uint8_t)(v + cx.heat);
  if (cx.sky && (TRAITS(p) & cx.sky)) v++;
  return v;
}
uint8_t cx_legal(uint8_t k, uint16_t w) BANKED {
  uint8_t kind = CX_KIND(w), x = CX_X(w), y = CX_Y(w);
  if (k != cx.act) return 0;
  if (cx.pot == CX_NONE) {
    if (!cx_tools(k)) return kind == CX_PASS;
    if (kind == CX_ADD) return cx_has(k, x);
    if (kind != CX_FORGE || x >= y || !cx_has(k, x) || !cx_has(k, y) || x >= 12u) return 0;
    return cx_recipe(cx_tool(k, x), cx_tool(k, y)) != CX_NONE;
  }
  if (cx.owner == k) {
    if (kind == CX_POUR) return 1;
    return kind == CX_ADD && !cx_locked_to(k) && cx_has(k, x);
  }
  if (kind == CX_PASS) return 1;
  return kind == CX_ADD && cx_has(k, x);
}
static uint16_t take(uint8_t k, uint8_t i) {
  cx_side *p = &cx.s[k];
  uint16_t x;
  uint8_t j;
  if (i >= 12u) {
    p->four &= (uint8_t)~BIT8[i - 12u];
    return (uint16_t)(i - 12u);
  }
  x = p->bag[i];
  for (j = i; (uint8_t)(j + 1u) < p->n; j++) p->bag[j] = p->bag[j + 1u];
  p->n--;
  return x;
}
static void stand(uint8_t k, uint16_t x, uint8_t heat) {
  cx.pot = x;
  cx.owner = k;
  cx.heat = heat < CX_HEAT ? heat : CX_HEAT;
  if (CAT_(x) == CX_C_WEATHER || (cx.s[k].tw & CX_TW_SKYALL)) cx.sky = TRAITS(x);
}
static void compile_(uint8_t k) { /* PROGRAM from phase 2: a pot the other side starts teaches it a partner */
  uint8_t o = (uint8_t)(1u - k);
  cx_side *op = &cx.s[o];
  uint16_t b;
  if (!(op->tw & CX_TW_COMPILE) || cx_phase(o) < 2u || op->n >= CX_BAG) return;
  b = cx_partner(cx.pot, 4);
  if (b != CX_NONE) op->bag[op->n++] = b;
}
void cx_begin_turn(void) BANKED {
  uint8_t k = cx.act, g = 0, tw;
  if (cx.pot == CX_NONE || cx.owner != k || CAT_(cx.pot) == CX_C_PLACE) return;
  if (CAT_(cx.pot) == CX_C_LIFE) g = 1;
  tw = cx.s[k].tw;
  if (tw & CX_TW_CHARGE) g = (uint8_t)(g + (cx_phase(k) >= 2u ? 2u : 1u));
  if ((tw & CX_TW_ROOT) && cx_phase(k) >= 2u) g++;
  if (g) {
    g = (uint8_t)(cx.heat + g);
    cx.heat = g < CX_HEAT ? g : CX_HEAT;
  }
}
void cx_do(uint16_t w) BANKED {
  uint8_t k = cx.act, kind = CX_KIND(w), i, j, o;
  cx_side *me = &cx.s[k], *op = &cx.s[1u - k];
  uint16_t x, y, r;
  cx.dmg = 0;
  cx.ex = cx.er = CX_NONE;
  if (kind == CX_PASS) {
    cx.ev = CX_EV_PASS;
    if (cx.pot != CX_NONE && cx.owner != k && (op->tw & CX_TW_ROOT) && cx_phase((uint8_t)(1u - k)) >= 1u &&
        op->n < CX_BAG)
      op->bag[op->n++] = cx.pot;
  } else if (kind == CX_POUR) {
    uint8_t d = cx_power();
    if (cx.shield && cx.shield != (uint8_t)(k + 1u)) d = d > 1u ? (uint8_t)(d - 1u) : 0u;
    if (cx.shield != (uint8_t)(k + 1u)) cx.shield = 0;
    op->hp = (int8_t)(op->hp - (int8_t)d);
    cx.dmg = d;
    cx.ev = CX_EV_POUR;
    cx.ex = cx.pot;
    if ((me->tw & CX_TW_LOCK) && cx_phase(k) >= 2u) cx.shield = (uint8_t)(k + 1u);
    cx.pot = CX_NONE;
    cx.heat = 0;
    cx.chain = 0;
  } else if (kind == CX_FORGE) {
    i = CX_X(w);
    j = CX_Y(w);
    if (i > j) {
      o = i;
      i = j;
      j = o;
    }
    x = cx_tool(k, i);
    y = cx_tool(k, j);
    r = cx_recipe(x, y);
    (void)take(k, j);
    (void)take(k, i);
    me->made++;
    cx.chain = 0;
    stand(k, r, 1);
    cx.ev = CX_EV_FORGE;
    cx.ex = x;
    cx.er = r;
    compile_(k);
  } else {
    i = CX_X(w);
    x = cx_tool(k, i);
    o = cx_outcome(k, x, &r);
    (void)take(k, i);
    cx.ev = o;
    cx.ex = x;
    cx.er = r;
    if (o == CX_START) {
      cx.chain = 0;
      stand(k, x, 0);
      compile_(k);
    } else if (o == CX_BUILD || o == CX_HIJACK) {
      me->made++;
      if (o == CX_HIJACK) cx.chain++;
      stand(k, r, (uint8_t)(cx.heat + 1u));
    } else if (o == CX_SPLIT) {
      if (me->n < CX_BAG) me->bag[me->n++] = r;
      cx.pot = CX_NONE;
      cx.heat = 0;
      cx.chain = 0;
    } else if (o == CX_BREAK) {
      cx.pot = CX_NONE;
      cx.heat = 0;
      cx.chain = 0;
    } else if (cx.owner == k && cx.pot != CX_NONE && cx.heat)
      cx.heat--;
  }
  cx.passes = kind == CX_PASS ? (uint8_t)(cx.passes + 1u) : 0u;
  cx.turn++;
  cx.act = (uint8_t)(1u - k);
}
uint8_t cx_over(void) BANKED {
  int8_t a = cx.s[0].hp, b = cx.s[1].hp;
  if (a > 0 && b > 0 && cx.turn < CX_TURNS && cx.passes < 6u && (cx_tools(0) || cx_tools(1) || cx.pot != CX_NONE))
    return 0;
  if (a <= 0 && b <= 0) return 3;
  if (b <= 0) return 1;
  if (a <= 0) return 2;
  return a > b ? 1u : b > a ? 2u : 3u;
}
uint8_t cx_hash(void) BANKED {
  uint8_t h, k, i;
  cx_side *p;
  uint16_t x;
  h = (uint8_t)((uint8_t)cx.s[0].hp + (uint8_t)cx.s[1].hp * 3u + cx.heat * 5u + cx.turn * 7u + (uint8_t)cx.pot +
                (uint8_t)(cx.pot >> 8) * 11u + cx.owner * 13u + (uint8_t)cx.sky);
  for (k = 0; k < 2u; k++) {
    p = &cx.s[k];
    h = (uint8_t)(h + p->n * 17u + p->four * 19u);
    for (i = 0; i < p->n; i++) {
      x = p->bag[i];
      h = (uint8_t)(h * 3u + (uint8_t)x + (uint8_t)(x >> 8));
    }
  }
  return h;
}
void cx_swap(void) BANKED {
  cx_side t;
  memcpy(&t, &cx.s[0], sizeof t);
  memcpy(&cx.s[0], &cx.s[1], sizeof t);
  memcpy(&cx.s[1], &t, sizeof t);
  cx.owner ^= 1u;
  cx.act ^= 1u;
  if (cx.shield) cx.shield = (uint8_t)(3u - cx.shield);
}
