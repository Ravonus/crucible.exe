/* The fight engine: one triangle read from the traits the catalogue already has, kits, draws, clashes, passives,
 * arenas and the duel AI (docs/fight-system.md 1-5). Everything is integer and seeded: two Game Boys given the
 * same seed and the same action bytes end every turn in the same state (the link's lockstep), and the host build
 * (FIGHT_HOST) replays the simulator's golden vectors (tools/fight-sim/fightref.py) turn by turn.
 *
 * The stance: the trait family (HUM: hot airy shiny glows, WALL: wet stone made big, DREAM: cold alive green magic) with
 * the most of an element's traits; two tied make a dual, all three (or none) fall to its category. WALL beats HUM, DREAM
 * beats WALL, HUM beats DREAM. The winner deals power (1..3 from depth) plus 1 when any of its traits beats one of the
 * loser's under the core's BEATS table; passives, stacks and the arena add the rest. */
#ifdef FIGHT_HOST
#include <stdint.h>
#include <string.h>
#define BANKED
#define CRU_NONE 0xffffu
uint16_t fr_host_traits(uint16_t id);
uint8_t fr_host_depth(uint16_t id);
uint8_t fr_host_category(uint16_t id);
uint16_t fr_host_recipe(uint16_t a, uint16_t b);
#define FR_TRAITS(id) fr_host_traits(id)
#define FR_DEPTH(id) fr_host_depth(id)
#define FR_CAT(id) fr_host_category(id)
#define FR_RECIPE(a, b) fr_host_recipe((a), (b))
#else
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_state.h"
uint16_t cri_traits(crucible_core *c, uint16_t id) BANKED;
uint8_t cri_depth(crucible_core *c, uint16_t id) BANKED;
uint8_t cri_category(crucible_core *c, uint16_t id) BANKED;
#define FR_TRAITS(id) cri_traits(&core, (id))
#define FR_DEPTH(id) cri_depth(&core, (id))
#define FR_CAT(id) cri_category(&core, (id))
#define FR_RECIPE(a, b) cru_recipe(&core, (a), (b))
#endif
#include "crucible_fight_rules.h"

fr_state fr;
uint8_t fr_scratch[80];
/* ---- the triangle ---- */
static const uint16_t FAM[3] = {0x0069u, 0x0614u, 0x0982u}; /* HUM 0,3,5,6  WALL 2,4,9,10  DREAM 1,7,8,11 */
static const uint8_t CAT_FAM[7] = {0, 1, 2, 0, 2, 1, 1}; /* ELEMENT MATTER WEATHER ENERGY LIFE CRAFT PLACE */
static const uint8_t NIB[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};
/* what beats each trait (the core's BEATS in cru_saga.c): water and cold put out heat, stone and weight stop air... */
static const uint16_t BEATS[12] = {0x006u, 0x041u, 0x102u, 0x410u, 0x104u, 0x014u,
                                   0x410u, 0x801u, 0x003u, 0x804u, 0xA00u, 0x240u};
/* passive templates (section 4): pattern kind | need << 4, effect kind | magnitude << 4; the kinds are the passive's own */
static const uint8_t PAS[FP_COUNT][2] = {{0x20u, 0x20u}, {0x31u, 0x11u}, {0x52u, 0x12u}, {0x23u, 0x03u},
                                         {0x34u, 0x14u}, {0x35u, 0x15u}, {0x36u, 0x16u}, {0x27u, 0x27u},
                                         {0x38u, 0x28u}, {0x29u, 0x19u}, {0x2Au, 0x1Au}, {0x3Bu, 0x2Bu}};
#define NEED(p) (PAS[p][0] >> 4)
#define MAG(p) (PAS[p][1] >> 4)

/* bit masks by index: sdcc miscompiles (field >> var) & 1 (it shifts the index, never loading the field) */
static const uint8_t BIT8[8] = {1, 2, 4, 8, 16, 32, 64, 128};
static const uint16_t BIT16[16] = {1u,   2u,   4u,    8u,    16u,   32u,   64u,    128u,
                                   256u, 512u, 1024u, 2048u, 4096u, 8192u, 16384u, 32768u};
#define ON(p, id) (((p)->act & BIT16[id]) ? 1u : 0u)
static void act_set(fr_side *p) {
  p->act = 0;
  if ((p->on & 1u) && p->pas[0] < FP_COUNT) p->act |= BIT16[p->pas[0]];
  if ((p->on & 2u) && p->pas[1] < FP_COUNT) p->act |= BIT16[p->pas[1]];
}
/* the traits t beats: bit k when one of t's traits beats trait k (so edges are a popcount) */
static uint16_t beat_mask(uint16_t t) {
  uint8_t k;
  uint16_t m = 0;
  for (k = 0; k < 12u; k++)
    if (t & BEATS[k]) m |= BIT16[k];
  return m;
}
static uint8_t pop12(uint16_t m) { return (uint8_t)(NIB[m & 15u] + NIB[(m >> 4) & 15u] + NIB[(m >> 8) & 15u]); }
static uint8_t stance_of(uint16_t t, uint8_t cat) {
  uint8_t v0 = pop12(t & FAM[0]), v1 = pop12(t & FAM[1]), v2 = pop12(t & FAM[2]), m = v0, s = 0;
  if (v1 > m) m = v1;
  if (v2 > m) m = v2;
  if (v0 == m) s |= 1u;
  if (v1 == m) s |= 2u;
  if (v2 == m) s |= 4u;
  if (s == 7u) s = BIT8[CAT_FAM[cat < 7u ? cat : 0u]];
  return s;
}
uint8_t fr_stance(uint16_t id) BANKED { return stance_of(FR_TRAITS(id), FR_CAT(id)); }
static uint8_t power_of(uint8_t d) { return (uint8_t)(1u + (d >= 5u) + (d >= 7u)); }
uint8_t fr_power(uint16_t id) BANKED { return power_of(FR_DEPTH(id)); }
static uint8_t dual(uint8_t m) { return m == 3u || m == 5u || m == 6u; }
uint8_t fr_edges(uint16_t ta, uint16_t tb) BANKED { return pop12((uint16_t)(beat_mask(ta) & tb)); }
#define EDGES(a, b) pop12((uint16_t)((a)->bt & (b)->tr))
uint8_t fr_cost(uint16_t id) BANKED { return (uint8_t)(fr_power(id) + (dual(fr_stance(id)) ? 1u : 0u)); }
uint8_t fr_kit_cost(const uint16_t *kit, uint8_t n) BANKED {
  uint8_t i, j, c, sum = 0, seen;
  for (i = 0; i < n; i++) {
    c = fr_cost(kit[i]);
    seen = 0;
    for (j = 0; j < i; j++)
      if (kit[j] == kit[i]) seen = 1;
    if (seen && c > 1u) c--;
    sum = (uint8_t)(sum + c);
  }
  return sum;
}

/* ---- the rngs: 16-bit xorshift (7, 9, 8); below(n) an 8x8 multiply-shift ---- */
static uint16_t xs(uint16_t x) {
  x ^= (uint16_t)(x << 7);
  x ^= (uint16_t)(x >> 9);
  x ^= (uint16_t)(x << 8);
  return x;
}
static uint8_t below(uint16_t *r, uint8_t n) {
  *r = xs(*r);
  return (uint8_t)(((uint16_t)(uint8_t)*r * n) >> 8);
}
static void shuffle(uint8_t *a, uint8_t n) {
  uint8_t i, j, t;
  if (n < 2u) return;
  for (i = (uint8_t)(n - 1u); i; i--) {
    j = below(&fr.rng, (uint8_t)(i + 1u));
    t = a[i];
    a[i] = a[j];
    a[j] = t;
  }
}
void fr_begin(uint16_t seed) BANKED {
  memset(&fr, 0, sizeof fr);
  fr.rng = seed ? seed : 0x1D2Bu;
  {
    uint16_t a = seed;
    a ^= 0xA15Eu;
    if (!a) a = 0x1D2Bu;
    fr.ai = a;
  } /* (sdcc stored the low byte twice for the one-liner) */
  fr.arena = FA_EMPTY;
  fr.turns = FR_TURNS;
  fr.out = 9;
  fr.skill = 4;
  fr.loop[0] = 0;
  fr.loop[1] = 1;
  fr.loop[2] = 2;
}
void fr_ai_setup(uint8_t persona, uint8_t skill) BANKED {
  uint8_t i, j, t;
  fr.persona = persona;
  fr.skill = skill ? skill : 1u;
  fr.loop[0] = 0;
  fr.loop[1] = 1;
  fr.loop[2] = 2;
  for (i = 2; i; i--) {
    j = below(&fr.ai, (uint8_t)(i + 1u));
    t = fr.loop[i];
    fr.loop[i] = fr.loop[j];
    fr.loop[j] = t;
  }
}

/* ---- sides ---- */
static uint8_t has_(const fr_side *p, uint8_t id) { return p->pas[0] == id || p->pas[1] == id; }
static void groups(fr_side *p, uint8_t n) { /* kgrp[i]: the slots holding the same element as slot i */
  uint8_t i, j;
  for (i = 0; i < n; i++) {
    p->kgrp[i] = 0;
    for (j = 0; j < n; j++)
      if (p->kit[j] == p->kit[i]) p->kgrp[i] |= BIT8[j];
  }
}
void fr_side_init(uint8_t k, const uint16_t *kit, uint8_t n, const uint8_t *pas, uint8_t hp, uint8_t focus) BANKED {
  fr_side *p = &fr.s[k];
  uint8_t i;
  memset(p, 0, sizeof *p);
  if (n > FR_KIT) n = FR_KIT;
  for (i = 0; i < n; i++) {
    uint16_t t = FR_TRAITS(kit[i]);
    p->kit[i] = kit[i];
    p->ktr[i] = t;
    p->kbt[i] = beat_mask(t);
    p->kst[i] = stance_of(t, FR_CAT(kit[i]));
    p->kpw[i] = power_of(FR_DEPTH(kit[i]));
    p->deck[i] = i;
  }
  for (; i < FR_KIT; i++) p->kit[i] = CRU_NONE;
  groups(p, n);
  p->nkit = n;
  p->ndeck = n;
  shuffle(p->deck, n);
  p->hp = (int8_t)hp;
  p->max = hp;
  p->focus = focus;
  p->last = FR_NONE;
  p->pas[0] = pas ? pas[0] : FP_NONE;
  p->pas[1] = pas ? pas[1] : FP_NONE;
  for (i = 0; i < FR_PAIRS; i++) p->pair[i] = CRU_NONE;
}
uint8_t fr_hand_size(uint8_t k) BANKED {
  const fr_side *p = &fr.s[k];
  return (uint8_t)(3u + (ON(p, FP_PRISM) || p->hp <= 4));
}
static uint8_t fmax(void) { return (uint8_t)(3u + ((fr.rules & FR_R_NIGHT) ? 1u : 0u)); }
static uint8_t fuse_cost(const fr_side *p) { return (uint8_t)(2u - ON(p, FP_CATALYST)); }
static const uint8_t PI[4][4] = {{0xff, 0, 1, 2}, {0xff, 0xff, 3, 4}, {0xff, 0xff, 0xff, 5}, {0xff, 0xff, 0xff, 0xff}};
/* this hand's fusions: one recipe lookup per pair, only when a fuse is affordable. A lookup is a binary search through
 * the bank-switched catalogue tables (tens of thousands of clocks), so the draw only marks the pairs and
 * fr_pairs_step looks one up per call (a frame each while the player chooses); nothing depends on when. */
#define PENDING 0xfffeu
static void pairs(fr_side *p) {
  uint8_t i, j;
  for (i = 0; i < FR_PAIRS; i++) p->pair[i] = CRU_NONE;
  if (p->focus < fuse_cost(p)) return;
  for (i = 0; i < p->nhand; i++)
    for (j = (uint8_t)(i + 1u); j < p->nhand; j++) p->pair[PI[i][j]] = PENDING;
}
static uint8_t pair_one(fr_side *p) {
  uint8_t i, j, x;
  uint16_t r;
  for (i = 0; i < p->nhand; i++)
    for (j = (uint8_t)(i + 1u); j < p->nhand; j++) {
      x = PI[i][j];
      if (p->pair[x] != PENDING) continue;
      r = FR_RECIPE(p->kit[p->hand[i]], p->kit[p->hand[j]]);
      p->pair[x] = r;
      if (r != CRU_NONE) {
        p->ptr[x] = FR_TRAITS(r);
        p->pbt[x] = beat_mask(p->ptr[x]);
        p->pst[x] = stance_of(p->ptr[x], FR_CAT(r));
        p->ppw[x] = power_of(FR_DEPTH(r));
      }
      return 1;
    }
  return 0;
}
uint8_t fr_pairs_step(void) BANKED {
  if (pair_one(&fr.s[0]) || (!fr.solo && pair_one(&fr.s[1]))) return 0;
  return 1;
} /* 1: all looked up */
void fr_pairs_all(void) BANKED {
  while (!fr_pairs_step()) {}
}
static void draw_side(fr_side *p, uint8_t k) {
  uint8_t want = fr_hand_size(k), i;
  while (p->nhand < want) {
    if (!p->ndeck) {
      if (!p->ndisc) break;
      for (i = 0; i < p->ndisc; i++) p->deck[i] = p->disc[i];
      p->ndeck = p->ndisc;
      p->ndisc = 0;
      p->dmask = 0;
      shuffle(p->deck, p->ndeck);
    }
    p->hand[p->nhand++] = p->deck[--p->ndeck];
  }
}
void fr_draw(void) BANKED {
  fr.turn++;
  draw_side(&fr.s[0], 0);
  pairs(&fr.s[0]);
  if (fr.solo) return;
  draw_side(&fr.s[1], 1);
  pairs(&fr.s[1]);
}
uint8_t fr_hash(void) BANKED {
  const fr_side *a = &fr.s[0], *b = &fr.s[1];
  uint8_t s;
  s = (uint8_t)((uint8_t)a->hp + (uint8_t)b->hp * 3u + a->focus * 5u + b->focus * 7u + (uint8_t)fr.rng + fr.turn * 11u);
  s = (uint8_t)(s + a->ndeck * 13u + b->ndeck * 17u + a->ndisc + b->ndisc * 19u);
  return s;
}

/* ---- cards ---- */
static const uint8_t DUALM[8] = {0, 0, 0, 1, 0, 1, 1, 0};
uint8_t fr_card(uint8_t k, uint8_t act, fr_cardv *c) BANKED {
  const fr_side *p = &fr.s[k];
  uint8_t kind = FR_KIND(act), s, m, x;
  if (kind == FR_GUARD) return 0;
  c->st = FR_ST(act);
  if (kind == FR_PLAY) {
    s = p->hand[FR_A(act)];
    c->id = p->kit[s];
    c->tr = p->ktr[s];
    c->bt = p->kbt[s];
    c->slot = s;
    c->pw = p->kpw[s];
    if (DUALM[p->kst[s]] && c->pw > 1u) c->pw--;
    if (!(fr.rules & FR_R_NOSTACK)) {
      x = (uint8_t)(p->dmask & p->kgrp[s]);
      x = (uint8_t)(NIB[x & 15u] + NIB[x >> 4]); /* copies in the discard, at most 2 */
      if (x) {
        m = (uint8_t)(1u + (fr.arena == FA_ECHO) + ON(p, FP_MEMORY));
        c->pw = (uint8_t)(c->pw + m);
        if (x >= 2u) c->pw = (uint8_t)(c->pw + m);
      }
    }
    return 1;
  }
  x = PI[FR_A(act)][FR_B(act)];
  c->id = p->pair[x];
  c->tr = p->ptr[x];
  c->bt = p->pbt[x];
  c->slot = 0xffu;
  c->pw = (uint8_t)(p->ppw[x] + 1u + ON(p, FP_CATALYST));
  if (c->pw > 4u) c->pw = 4u;
  return 1;
}
uint8_t fr_legal(uint8_t k, uint8_t act) BANKED {
  const fr_side *p = &fr.s[k];
  uint8_t kind = FR_KIND(act), a = FR_A(act), b = FR_B(act), st = FR_ST(act), x;
  if (kind == FR_GUARD) return act == FR_GUARD_ACT;
  if (st > 2u) return 0;
  if (kind == FR_PLAY) return a < p->nhand && !b && (p->kst[p->hand[a]] & BIT8[st]) != 0u;
  if (kind != FR_FUSE || a >= b || b >= p->nhand || p->focus < fuse_cost(p)) return 0;
  x = PI[a][b];
  if (p->pair[x] == CRU_NONE) return 0;
  return (p->pst[x] & BIT8[st]) ? 1u : 0u;
}
uint8_t fr_actions(uint8_t k, uint8_t *out) BANKED {
  const fr_side *p = &fr.s[k];
  uint8_t n = 0, i, j, st, m, x;
  out[n++] = FR_GUARD_ACT;
  for (i = 0; i < p->nhand; i++) {
    m = p->kst[p->hand[i]];
    for (st = 0; st < 3u; st++)
      if (m & BIT8[st]) out[n++] = FR_ACT(FR_PLAY, i, 0, st);
  }
  if (p->focus >= fuse_cost(p))
    for (i = 0; i < p->nhand; i++)
      for (j = (uint8_t)(i + 1u); j < p->nhand; j++) {
        x = PI[i][j];
        if (p->pair[x] == CRU_NONE) continue;
        m = p->pst[x];
        for (st = 0; st < 3u; st++)
          if (m & BIT8[st]) out[n++] = FR_ACT(FR_FUSE, i, j, st);
      }
  return n;
}

/* ---- the clash: damage to b, damage to a, and a's outcome (1 won, 0 tie, -1 lost, 9 a guard was in it) ---- */
static uint8_t beats(uint8_t x, uint8_t y) {
  if ((fr.rules & FR_R_INVERT) && fr.turn >= 4u) {
    uint8_t t = x;
    x = y;
    y = t;
  } /* GLITCH INVERT: the triangle turns over */
  return (uint8_t)(x == (uint8_t)(y == 2u ? 0u : y + 1u));
}
static uint8_t late(void) { return (fr.rules & FR_R_SUDDEN) && fr.turn >= 15u; }
static int8_t hit(const fr_side *p, const fr_cardv *c, const fr_cardv *o) {
  int8_t d = (int8_t)(c->pw + ((c->bt & o->tr) ? 1u : 0u));
  if (p->act) {
    if (ON(p, FP_ECHO) && p->last == c->st) d = (int8_t)(d + MAG(FP_ECHO));
    if (ON(p, FP_SCAR) && (int16_t)p->hp * 2 <= (int16_t)p->max) d++;
    if (ON(p, FP_OVERCLOCK)) d++;
    if (ON(p, FP_NOCLIP) && c->st == FR_DREAM) d++;
  }
  if (fr.arena == c->st) d++;
  if (late()) d++;
  if (ON(p, FP_UNDERTOW) && p->lostrow) d = (int8_t)(d + d);
  return d;
}
/* the clash of two sides' cards (ha/hb 0: that side guards): damage to b, damage to a, a's outcome (1 won, 0 tie, -1
 * lost, 9 a guard was in it) */
static void clash_c(const fr_side *pa, uint8_t ha, const fr_cardv *ca, const fr_side *pb, uint8_t hb,
                    const fr_cardv *cb, int8_t *to_b, int8_t *to_a, int8_t *out) {
  int8_t d, e;
  *out = 9;
  *to_a = *to_b = 0;
  if (!ha && !hb) return;
  if (!ha) {
    clash_c(pb, hb, cb, pa, ha, ca, to_a, to_b, out);
    *out = 9;
    return;
  }
  if (!hb) { /* a card into a guard */
    d = (int8_t)(ca->pw + ON(pa, FP_OVERCLOCK));
    if (ca->st == FR_DREAM && ON(pa, FP_NOCLIP)) {
      *to_b = d;
      return;
    }
    if (ON(pb, FP_VIGIL)) return;
    if (late()) d++;
    *to_b = fr.arena == FA_NARROW ? d : (int8_t)(d >> 1);
    return;
  }
  if (ca->st ==
      cb->st) { /* a tie: each deals its own edge; equal edges go to the heavier card (or the EXIT SIGN's lower HP) */
    d = (ca->bt & cb->tr) ? 1 : 0;
    e = (cb->bt & ca->tr) ? 1 : 0;
    if (d == e) {
      if (fr.arena == FA_EXIT && pa->hp != pb->hp) {
        d = pa->hp < pb->hp ? 1 : 0;
        e = (int8_t)(1 - d);
      } else {
        d = ca->pw > cb->pw ? 1 : 0;
        e = cb->pw > ca->pw ? 1 : 0;
      }
    }
    if (pa->act) {
      if (ON(pa, FP_STALEMATE)) d++;
      if (ON(pa, FP_ECHO) && pa->last == ca->st) d++;
    }
    if (pb->act) {
      if (ON(pb, FP_STALEMATE)) e++;
      if (ON(pb, FP_ECHO) && pb->last == cb->st) e++;
    }
    *to_b = d;
    *to_a = e;
    *out = 0;
    return;
  }
  if (beats(ca->st, cb->st)) {
    d = hit(pa, ca, cb);
    if (ON(pb, FP_LUCID) && d > 2) d = 2;
    *to_b = d;
    *out = 1;
    return;
  }
  d = hit(pb, cb, ca);
  if (ON(pa, FP_LUCID) && d > 2) d = 2;
  *to_a = d;
  *out = -1;
}
static void clash(uint8_t ka, uint8_t a, uint8_t kb, uint8_t b, int8_t *to_b, int8_t *to_a, int8_t *out) {
  fr_cardv ca, cb;
  uint8_t ha = fr_card(ka, a, &ca), hb = fr_card(kb, b, &cb);
  clash_c(&fr.s[ka], ha, &ca, &fr.s[kb], hb, &cb, to_b, to_a, out);
}

/* ---- after the clash ---- */
static void discard(fr_side *p, uint8_t i) {
  uint8_t s = p->hand[i];
  for (; (uint8_t)(i + 1u) < p->nhand; i++) p->hand[i] = p->hand[i + 1u];
  p->nhand--;
  p->disc[p->ndisc++] = s;
  p->dmask |= BIT8[s];
}
static void apply(fr_side *p, uint8_t act) {
  uint8_t kind = FR_KIND(act), i, w;
  if (kind == FR_PLAY) {
    discard(p, FR_A(act));
    return;
  }
  if (kind == FR_FUSE) {
    discard(p, FR_B(act));
    discard(p, FR_A(act));
    p->focus = (uint8_t)(p->focus - fuse_cost(p));
    return;
  }
  if (p->focus < fmax()) p->focus++;
  if (!p->nhand) return;
  for (w = 0, i = 1; i < p->nhand; i++)
    if (p->kpw[p->hand[i]] < p->kpw[p->hand[w]]) w = i; /* a guard cycles the weakest */
  discard(p, w);
}
static uint8_t kit_slot(const fr_side *p, const fr_cardv *c) {
  uint8_t i;
  if (c->slot != 0xffu) return c->slot;
  for (i = 0; i < p->nkit; i++)
    if (p->kit[i] == c->id) return i;
  return 0xffu;
}
static uint8_t played_has(const fr_side *p, const fr_cardv *c) {
  uint8_t i, s = kit_slot(p, c);
  if (s != 0xffu) return (p->played & p->kgrp[s]) ? 1u : 0u;
  for (i = 0; i < p->nprod; i++)
    if (p->prod[i] == c->id) return 1;
  return 0;
}
static void played_add(fr_side *p, const fr_cardv *c) {
  uint8_t s = kit_slot(p, c);
  if (s != 0xffu) {
    p->played |= p->kgrp[s];
    return;
  }
  if (played_has(p, c) || p->nprod >= FR_PRODS) return;
  p->prod[p->nprod++] = c->id;
}
static void advance(fr_side *p, uint8_t act, int8_t o, const fr_cardv *c, uint8_t sev) {
  uint8_t k, id, g, st = c ? c->st : FR_NONE, d, i;
  for (k = 0; k < 2u; k++) {
    id = p->pas[k];
    if (id >= FP_COUNT || (p->on & BIT8[k])) continue;
    g = p->prog[k];
    switch (id) {
    case FP_ECHO: g = st == FR_NONE ? 0u : st == p->last ? (uint8_t)(g + 1u) : 1u; break;
    case FP_PRISM:
      if (p->nwin < 3u)
        p->win[p->nwin++] = st;
      else {
        p->win[0] = p->win[1];
        p->win[1] = p->win[2];
        p->win[2] = st;
      }
      for (d = 0, i = 0; i < p->nwin; i++)
        if (p->win[i] != FR_NONE) d |= BIT8[p->win[i]];
      g = NIB[d];
      break;
    case FP_SCAR: g = p->lost_hp; break;
    case FP_VIGIL: g = FR_KIND(act) == FR_GUARD ? (uint8_t)(g + 1u) : 0u; break;
    case FP_CATALYST: g = p->focus; break;
    case FP_STALEMATE:
      if (o == 0) g++;
      break;
    case FP_SALVE: g = c && c->pw == 1u ? (uint8_t)(g + 1u) : 0u; break;
    case FP_UNDERTOW: g = p->lostrow; break;
    case FP_LUCID:
      if (o == 1) g++;
      break;
    case FP_NOCLIP:
      if (o == 1 && st == FR_DREAM) g++;
      break;
    case FP_OVERCLOCK:
      if (c && c->pw >= 3u) g++;
      break;
    case FP_MEMORY:
      if (c && played_has(p, c)) g++;
      break;
    }
    if (sev) g = 0;
    p->prog[k] = g;
    if (g >= NEED(id)) {
      p->on |= BIT8[k];
      act_set(p);
    }
  }
  if (c) played_add(p, c);
}
static void habit(fr_side *p, const fr_cardv *c) {
  uint8_t prev, i;
  if (!c) return;
  prev = p->last;
  i = (uint8_t)(prev * 3u + c->st);
  if (((p->after[i >> 1] >> ((i & 1u) << 2)) & 15u) < 15u)
    p->after[i >> 1] = (uint8_t)(p->after[i >> 1] + (1u << ((i & 1u) << 2)));
  if (p->turns < 255u) p->turns++;
  if (c->st == p->last && p->repeats < 255u) p->repeats++;
  if (p->nhist < 4u)
    p->hist[p->nhist++] = c->st;
  else {
    p->hist[0] = p->hist[1];
    p->hist[1] = p->hist[2];
    p->hist[2] = p->hist[3];
    p->hist[3] = c->st;
  }
}
static void hp_sub(fr_side *p, int8_t d) {
  int16_t v = (int16_t)p->hp - d;
  p->hp = (int8_t)(v < -100 ? -100 : v);
}
void fr_resolve(uint8_t a, uint8_t b) BANKED {
  fr_side *A = &fr.s[0], *B = &fr.s[1];
  fr_cardv ca, cb;
  uint8_t ha, hb, sa = 0, sb = 0;
  int8_t dA, dB, out, ob;
  ha = fr_card(0, a, &ca);
  hb = fr_card(1, b, &cb);
  clash_c(A, ha, &ca, B, hb, &cb, &dB, &dA, &out);
  hp_sub(B, dB);
  hp_sub(A, dA);
  A->lost_hp = (uint8_t)(A->lost_hp + dA > 255 ? 255 : A->lost_hp + dA);
  B->lost_hp = (uint8_t)(B->lost_hp + dB > 255 ? 255 : B->lost_hp + dB);
  if (ON(A, FP_OVERCLOCK) && out == -1) hp_sub(A, 1);
  if (ON(B, FP_OVERCLOCK) && out == 1) hp_sub(B, 1);
  if (ON(A, FP_SALVE) && out == 1 && A->hp > 0 && A->hp < (int8_t)A->max) A->hp++;
  if (ON(B, FP_SALVE) && out == -1 && B->hp > 0 && B->hp < (int8_t)B->max) B->hp++;
  ob = out == 9 ? 9 : (int8_t)-out;
  if (out == -1) {
    if (A->lostrow < 255u) A->lostrow++;
    if (A->focus < fmax()) A->focus++;
  } else if (out == 1) {
    A->lostrow = 0;
    if (A->wins < 255u) A->wins++;
  }
  if (ob == -1) {
    if (B->lostrow < 255u) B->lostrow++;
    if (B->focus < fmax()) B->focus++;
  } else if (ob == 1) {
    B->lostrow = 0;
    if (B->wins < 255u) B->wins++;
  }
  if (out == 1 && EDGES(&ca, &cb) >= 2u) sa = 1; /* SEVER: a clean counter breaks the loser's unfinished pattern */
  if (out == -1 && EDGES(&cb, &ca) >= 2u) sb = 1;
  apply(A, a);
  apply(B, b);
  advance(A, a, out, ha ? &ca : 0, sb);
  advance(B, b, ob, hb ? &cb : 0, sa);
  habit(A, ha ? &ca : 0);
  habit(B, hb ? &cb : 0);
  A->last = ha ? ca.st : FR_NONE;
  B->last = hb ? cb.st : FR_NONE;
  fr.out = out;
  fr.dmg[0] = dA;
  fr.dmg[1] = dB;
  fr.sever[0] = sb;
  fr.sever[1] = sa;
  fr.has[0] = ha;
  fr.has[1] = hb;
  fr.shown[0] = ca;
  fr.shown[1] = cb;
}
uint8_t fr_last_card(uint8_t k, fr_cardv *c) BANKED {
  *c = fr.shown[k & 1u];
  return fr.has[k & 1u];
}
static void swap_bytes(uint8_t *a, uint8_t *b, uint8_t n) {
  uint8_t t;
  while (n--) {
    t = *a;
    *a++ = *b;
    *b++ = t;
  }
}
void fr_swap_sides(void) BANKED {
  swap_bytes((uint8_t *)&fr.s[0], (uint8_t *)&fr.s[1], (uint8_t)sizeof(fr_side));
  swap_bytes((uint8_t *)&fr.dmg[0], (uint8_t *)&fr.dmg[1], 1);
  swap_bytes(&fr.sever[0], &fr.sever[1], 1);
  swap_bytes(&fr.has[0], &fr.has[1], 1);
  swap_bytes((uint8_t *)&fr.shown[0], (uint8_t *)&fr.shown[1], (uint8_t)sizeof(fr_cardv));
  if (fr.out != 9) fr.out = (int8_t)-fr.out;
}
void fr_set_card(uint8_t k, uint8_t slot, uint16_t id) BANKED {
  fr_side *p = &fr.s[k];
  uint16_t t = FR_TRAITS(id);
  if (slot >= p->nkit) return;
  p->kit[slot] = id;
  p->ktr[slot] = t;
  p->kbt[slot] = beat_mask(t);
  p->kst[slot] = stance_of(t, FR_CAT(id));
  p->kpw[slot] = power_of(FR_DEPTH(id));
  groups(p, p->nkit);
  pairs(p);
}
uint8_t fr_over(void) BANKED {
  int8_t a = fr.s[0].hp, b = fr.s[1].hp;
  if (a > 0 && b > 0 && fr.turn < fr.turns) return 0;
  if ((a <= 0 && b <= 0) || a == b) return 3;
  if (b <= 0 || (a > b && a > 0)) return 1;
  return 2;
}

/* ---- the duel AI (side 1): the best expected trade against a model of the player's options; integer only ----
 * The model weighs each of the player's actions by what the open hand allows, the player's stance-after-stance habit,
 * repeats and the patterns the player is chasing. Personalities (section 5): PROGRAM loops (exploitable), DAEMON
 * favours big cards, GHOST plays the stance you just beat, AI counts your last 4 plays, OPERATOR guards when hurt,
 * RELIC fuses whenever it can. One time in `skill` it takes its second best, so it is never a fixed function. */
static uint8_t weight(const fr_side *op, uint8_t b, uint8_t uniform) {
  fr_cardv c;
  uint8_t w, k, id, i;
  if (!fr_card(0, b, &c)) {
    if (uniform) return 2;
    w = 2;
    if (!op->focus && op->nhand) w += 3;
    if (has_(op, FP_VIGIL) && !ON(op, FP_VIGIL)) w += 3;
    return w;
  }
  if (uniform) return 4;
  i = (uint8_t)(op->last * 3u + c.st);
  w = (uint8_t)(4u + (c.pw > 4u ? 4u : c.pw) + 2u * ((op->after[i >> 1] >> ((i & 1u) << 2)) & 15u));
  if (c.st == op->last && op->turns && (uint16_t)op->repeats * 2u >= op->turns) w += 3;
  for (k = 0; k < 2u; k++) {
    id = op->pas[k];
    if (id >= FP_COUNT || (op->on & BIT8[k])) continue;
    if (id == FP_ECHO) {
      if (c.st == op->last) w += 3;
    } else if (id == FP_PRISM) {
      if (!((op->nwin && op->win[op->nwin - 1u] == c.st) || (op->nwin >= 2u && op->win[op->nwin - 2u] == c.st))) w += 2;
    } else if (id == FP_NOCLIP) {
      if (c.st == FR_DREAM) w += 2;
    } else if (id == FP_OVERCLOCK) {
      if (c.pw >= 3u) w += 2;
    } else if (id == FP_SALVE) {
      if (c.pw == 1u) w += 2;
    } else if (id == FP_MEMORY) {
      if (played_has(op, &c)) w += 2;
    }
  }
  if (fr.persona == 3u)
    for (i = 0; i < op->nhist; i++)
      if (op->hist[i] == c.st) w += 3;
  return w > 31u ? 31u : w;
}
static int8_t bias(const fr_side *me, const fr_side *op, uint8_t a) {
  fr_cardv c;
  uint8_t h = fr_card(1, a, &c), t;
  switch (fr.persona) {
  case 0:
    t = fr.turn;
    while (t >= 3u) t = (uint8_t)(t - 3u);
    return h && c.st == fr.loop[t] ? 6 : 0;
  case 1: return h ? (int8_t)(2 * (c.pw - 1)) : 0;
  case 2: return h && op->last != FR_NONE && c.st == (uint8_t)(op->last == 0u ? 2u : op->last - 1u) ? 5 : 0;
  case 4:
    if (h) return 0;
    return (int16_t)me->hp * 2 <= (int16_t)me->max ? 8 : -2;
  case 5: return FR_KIND(a) == FR_FUSE ? 6 : 0;
  }
  return 0;
}
#define acts_ fr_scratch
#define opps_ (fr_scratch + 24)
#define ws_ (fr_scratch + 48)
/* the decision in progress: action i against the player's action j, the sum so far, the best two */
static uint8_t ai_n_, ai_no_, ai_i_, ai_j_, ai_best_, ai_second_, ai_done_;
static int16_t ai_sc_, ai_bs_, ai_ss_;
static uint16_t ai_w_;
static fr_cardv ai_ca_;
static uint8_t ai_ha_;
void fr_ai_begin(uint8_t uniform) BANKED {
  const fr_side *op = &fr.s[0];
  uint8_t j;
  ai_n_ = fr_actions(1, acts_);
  ai_no_ = fr_actions(0, opps_);
  ai_w_ = 0;
  for (j = 0; j < ai_no_; j++) {
    ws_[j] = weight(op, opps_[j], uniform);
    ai_w_ = (uint16_t)(ai_w_ + ws_[j]);
  }
  ai_i_ = 0;
  ai_j_ = 0;
  ai_sc_ = 0;
  ai_best_ = 0xffu;
  ai_second_ = 0xffu;
  ai_done_ = 0;
  ai_ha_ = fr_card(1, acts_[0], &ai_ca_);
}
uint8_t fr_ai_step(uint8_t budget) BANKED {
  const fr_side *me = &fr.s[1], *op = &fr.s[0];
  fr_cardv cb;
  uint8_t hb;
  int8_t tb, ta, o;
  if (ai_done_) return ai_done_ == 2u ? acts_[ai_second_] : acts_[ai_best_];
  while (budget--) {
    if (ai_j_ < ai_no_) {
      hb = fr_card(0, opps_[ai_j_], &cb);
      clash_c(me, ai_ha_, &ai_ca_, op, hb, &cb, &tb, &ta, &o);
      ai_sc_ = (int16_t)(ai_sc_ + (int16_t)ws_[ai_j_] * (int16_t)(tb - ta));
      ai_j_++;
      continue;
    }
    ai_sc_ = (int16_t)(ai_sc_ + (int16_t)bias(me, op, acts_[ai_i_]) * (int16_t)(ai_w_ >> 3));
    /* the best and the second best, online: the first of equals wins either place */
    if (ai_best_ == 0xffu || ai_sc_ > ai_bs_) {
      ai_second_ = ai_best_;
      ai_ss_ = ai_bs_;
      ai_best_ = ai_i_;
      ai_bs_ = ai_sc_;
    } else if (ai_second_ == 0xffu || ai_sc_ > ai_ss_) {
      ai_second_ = ai_i_;
      ai_ss_ = ai_sc_;
    }
    ai_i_++;
    ai_j_ = 0;
    ai_sc_ = 0;
    if (ai_i_ >= ai_n_) {
      ai_done_ = 1;
      if (ai_second_ != 0xffu && below(&fr.ai, fr.skill) == 0u) ai_done_ = 2;
      return ai_done_ == 2u ? acts_[ai_second_] : acts_[ai_best_];
    }
    ai_ha_ = fr_card(1, acts_[ai_i_], &ai_ca_);
  }
  return 0xffu;
}
uint8_t fr_ai_choose(uint8_t uniform) BANKED {
  uint8_t r;
  fr_ai_begin(uniform);
  do r = fr_ai_step(64);
  while (r == 0xffu);
  return r;
}
/* after a side's lists, unlocks or kit were set from outside (a resync, a test): its derived masks again */
void fr_side_fix(uint8_t k) BANKED {
  fr_side *p = &fr.s[k];
  uint8_t i;
  p->dmask = 0;
  for (i = 0; i < p->ndisc; i++) p->dmask |= BIT8[p->disc[i]];
  act_set(p);
  groups(p, p->nkit);
  pairs(p);
  fr_pairs_all();
}
#ifdef FIGHT_HOST
void fr_host_clash(uint8_t a, uint8_t b, int8_t *to_b, int8_t *to_a, int8_t *out) {
  clash(0, a, 1, b, to_b, to_a, out);
}
void fr_host_pairs(uint8_t k) { pairs(&fr.s[k]); }
#endif
