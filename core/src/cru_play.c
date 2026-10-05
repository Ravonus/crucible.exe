/* Play memory, saves v4 and v5: a port of core/test/reference/crucible_play.c over the abstract byte store (in the
 * 32K and 128K placements the same SRAM image byte for byte; the tests check it against that module). Areas:
 *   exact   OWNED  1 bit per id (v4), or per shelf position (v5)   RBITS  1 bit per recipe (its stable bit)
 *           USES   uint8 per id + SPILL (32 items past 255, exact to 65790)
 *   approx  DUDS   pairs that made nothing: two rotating Bloom generations, 3 Pearson hashes of the key (min,max).
 *                  A false positive only shows "makes nothing" on a pair that truly makes nothing.
 *   record  LIVE   the 72-byte play block: counters, filter state and the journal, carried in every record.
 * Placements (crucible_core.h): 32K and 128K keep the areas at fixed bank-0 offsets (v4, up to 4096 ids and 8192
 * recipes); WIDE puts them in banks 12-15 in slots sized for 16383 ids and 32767 recipes (v5), addressed as 16-bit
 * offsets in the 0x10000 window so journal entries keep their size.
 * Torn-write safety: cri_play_mix() changes no area; it stages at most 10 absolute byte writes (reads see them)
 * and the dud key in LIVE. The caller writes the CRC record (which carries LIVE), then cri_play_commit() applies
 * the journal, clears a rotated generation if due and inserts the dud. cri_play_load() replays the newest record's
 * journal (idempotent), finishes an interrupted clear and checks the areas against the record's byte sum. */
#include "cru_internal.h"

CRI_BITS;
CRI_STORE
CRI_AREA
#define SR(a) cri_rd(c, (uint16_t)(a))
#define SW(a, v) cri_wr(c, (uint16_t)(a), (uint8_t)(v))
#define LV(o) SR(P_LIVE + (o))
#define SLV(o, v) SW(P_LIVE + (o), (v))

static const uint8_t pop4[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};
/* bits kept in the last OWNED byte of a catalogue of items&7 ids there */
static const uint8_t low_mask[8] = {0xff, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f};
static const uint8_t cri_pearson[256] = {
    208, 69,  158, 108, 188, 53,  47,  199, 225, 216, 62,  253, 220, 250, 156, 213, 246, 157, 193, 6,   130, 242,
    222, 39,  173, 44,  167, 90,  233, 239, 231, 84,  101, 113, 66,  33,  154, 29,  32,  120, 191, 76,  183, 15,
    132, 207, 112, 3,   232, 147, 145, 181, 248, 150, 51,  57,  151, 77,  153, 187, 26,  162, 89,  103, 28,  36,
    133, 25,  238, 105, 190, 99,  201, 137, 205, 224, 97,  182, 40,  17,  251, 228, 22,  218, 211, 129, 107, 41,
    119, 23,  85,  111, 93,  134, 38,  92,  223, 19,  122, 254, 252, 241, 88,  115, 203, 143, 186, 245, 116, 174,
    234, 106, 217, 229, 56,  139, 35,  227, 86,  171, 54,  176, 50,  9,   255, 68,  79,  48,  43,  152, 243, 141,
    74,  124, 161, 206, 123, 110, 125, 200, 94,  210, 60,  117, 235, 21,  82,  240, 63,  4,   249, 160, 215, 177,
    37,  24,  80,  196, 71,  100, 168, 144, 204, 226, 91,  72,  179, 61,  30,  59,  46,  192, 55,  14,  212, 5,
    169, 65,  180, 221, 102, 73,  236, 136, 2,   131, 184, 138, 197, 109, 127, 244, 194, 214, 209, 98,  185, 16,
    13,  0,   163, 52,  247, 114, 34,  20,  31,  81,  11,  155, 165, 195, 170, 189, 219, 49,  87,  42,  67,  172,
    126, 75,  83,  12,  128, 140, 135, 7,   149, 237, 64,  95,  146, 45,  159, 18,  118, 8,   164, 175, 230, 96,
    178, 104, 78,  121, 10,  148, 1,   142, 166, 27,  202, 198, 70,  58};

static uint16_t lv16(crucible_core *c, uint8_t o) {
  uint8_t lo = LV(o), hi = LV(o + 1u);
  return (uint16_t)(lo | ((uint16_t)hi << 8));
}
static void slv16(crucible_core *c, uint8_t o, uint16_t v) {
  SLV(o, (uint8_t)v);
  SLV(o + 1u, (uint8_t)(v >> 8));
}
static void inc16(crucible_core *c, uint8_t o) { slv16(c, o, (uint16_t)(lv16(c, o) + 1u)); }
/* the 32K placement keeps both dud generations in bank 0; the others in banks 4-11 */
static uint8_t big_filter(const crucible_core *c) {
  uint8_t p = c->place;
  if (p == CRU_PLACE_32K) return 0;
  return 1;
}
static uint16_t dud_cap(const crucible_core *c) {
  if (big_filter(c)) return 21845u;
  return 341u;
} /* 12 bits per key */
static void fill(crucible_core *c, uint16_t at, uint16_t n, uint8_t v) {
  while (n--) SW(at++, v);
}
static void afill(crucible_core *c, uint16_t rel, uint16_t n, uint8_t v) {
  while (n--) cri_aw(c, rel++, v);
}
static uint16_t asum(crucible_core *c, uint16_t rel, uint16_t n) {
  uint16_t s = 0;
  uint8_t v;
  while (n--) {
    v = cri_ar(c, rel++);
    s = (uint16_t)(s + v);
  }
  return s;
}

/* where the placement's areas sit */
void cri_set_place(crucible_core *c, uint8_t place) CORE_LOCAL {
  c->place = place;
  if (place == CRU_PLACE_WIDE) {
    c->area_hi = 1;
    c->a_owned = W_OWNED;
    c->a_rbits = W_RBITS;
    c->a_uses = W_USES;
    c->a_spill = W_SPILL;
  } else {
    c->area_hi = 0;
    c->a_owned = P_OWNED;
    c->a_rbits = P_RBITS;
    c->a_uses = P_USES;
    c->a_spill = P_SPILL;
  }
}
/* OWNED's bit for an id: the id itself in v4, its shelf position in v5 */
static uint16_t obit(crucible_core *c, uint16_t id) {
  if (c->place == CRU_PLACE_WIDE) return cri_shelf_pos(c, id);
  return id;
}

/* ---- write-ahead journal over the exact areas ---- */
/* area bytes as the journal will leave them: staged value, else the area itself */
static uint8_t rd(crucible_core *c, uint16_t addr) {
  uint8_t n = LV(L_JN), i, lo = (uint8_t)addr, hi = (uint8_t)(addr >> 8), x, y;
  uint16_t j = P_LIVE + L_JOURNAL;
  if (n > P_JOURNAL) n = 0;
  for (i = 0; i < n; i++, j += 3u) {
    x = SR(j);
    y = SR(j + 1u);
    if (x == lo && y == hi) {
      x = SR(j + 2u);
      return x;
    }
  }
  x = cri_ar(c, addr);
  return x;
}
static void stage(crucible_core *c, uint16_t addr, uint8_t v) {
  uint8_t n = LV(L_JN), i, old, lo = (uint8_t)addr, hi = (uint8_t)(addr >> 8), x, y;
  uint16_t j = P_LIVE + L_JOURNAL;
  for (i = 0; i < n; i++, j += 3u) {
    x = SR(j);
    y = SR(j + 1u);
    if (x == lo && y == hi) {
      old = SR(j + 2u);
      SW(j + 2u, v);
      slv16(c, L_SUM, (uint16_t)(lv16(c, L_SUM) + v - old));
      return;
    }
  }
  if (n >= P_JOURNAL) return; /* unreachable: a mix stages at most 10 bytes */
  old = cri_ar(c, addr);
  SW(j, lo);
  SW(j + 1u, hi);
  SW(j + 2u, v);
  SLV(L_JN, n + 1u);
  slv16(c, L_SUM, (uint16_t)(lv16(c, L_SUM) + v - old));
}
static uint8_t exact_addr(const crucible_core *c, uint16_t a) {
  if (c->place == CRU_PLACE_WIDE) {
    if (a < W_OWNED || a >= W_END) return 0;
    return 1;
  }
  if (a < P_OWNED || a >= P_USES + CRU_COMPACT_ITEMS) return 0;
  if (a >= P_LIVE && a < P_SPILL) return 0;
  return 1;
}
static void apply_bytes(crucible_core *c) {
  uint8_t n = LV(L_JN), i, lo, hi, v;
  uint16_t j = P_LIVE + L_JOURNAL, a;
  if (n > P_JOURNAL) n = 0;
  for (i = 0; i < n; i++, j += 3u) {
    lo = SR(j);
    hi = SR(j + 1u);
    a = (uint16_t)(lo | ((uint16_t)hi << 8));
    if (exact_addr(c, a)) {
      v = SR(j + 2u);
      cri_aw(c, a, v);
    }
  }
  SLV(L_JN, 0);
}
static uint8_t test_bit(crucible_core *c, uint16_t base, uint16_t i) {
  uint8_t v = rd(c, (uint16_t)(base + (i >> 3)));
  uint8_t m = bit_mask[i & 7u];
  if (v & m) return 1;
  return 0;
}
static void stage_bit(crucible_core *c, uint16_t base, uint16_t i) {
  uint16_t a = (uint16_t)(base + (i >> 3));
  uint8_t v = rd(c, a);
  uint8_t m = bit_mask[i & 7u];
  v |= m;
  stage(c, a, v);
}
/* plain bits (no journal), for the migration and fresh-game paths that write directly: in bank 0 (old records,
 * scratch, the v4 areas a widening copies from) and in the placement's areas */
static uint8_t bank0_bit(crucible_core *c, uint16_t at, uint16_t i) {
  uint8_t v = SR(at + (i >> 3));
  uint8_t m = bit_mask[i & 7u];
  if (v & m) return 1;
  return 0;
}
static void set_area_bit(crucible_core *c, uint16_t rel, uint16_t i) {
  uint16_t a = (uint16_t)(rel + (i >> 3));
  uint8_t v = cri_ar(c, a);
  uint8_t m = bit_mask[i & 7u];
  v |= m;
  cri_aw(c, a, v);
}

/* ---- dud filter: two Bloom generations, 3 hashes of the key (min,max) ----
 * hash k: Pearson chains started at 3k, 3k+1, 3k+2 over the key bytes; bit index = the chain bits masked to the
 * generation size. Ids below 4096 make the 24-bit key (min<<12)|max of v4 and the same three-byte chain; above, a
 * fourth byte carries the ids' top bits and the chain takes one more step (so a v4 128K filter stays valid in v5). */
typedef struct {
  uint8_t lo, mid, hi, top;
} pkey_t;
static void set_key(pkey_t *k, uint16_t a, uint16_t b) {
  uint16_t t;
  uint8_t x;
  if (a > b) {
    t = a;
    a = b;
    b = t;
  }
  k->lo = (uint8_t)b;
  k->mid = (uint8_t)(((b >> 8) & 15u) | ((a & 15u) << 4));
  k->hi = (uint8_t)(a >> 4);
  x = (uint8_t)(b >> 12);
  x = (uint8_t)(x << 2);
  x |= (uint8_t)(a >> 12);
  k->top = x;
}
static uint8_t chain(const pkey_t *k, uint8_t s) {
  uint8_t lo = k->lo, mid = k->mid, hi = k->hi, top = k->top, h;
  h = cri_pearson[(uint8_t)(lo ^ s)];
  h = cri_pearson[(uint8_t)(h ^ mid)];
  h = cri_pearson[(uint8_t)(h ^ hi)];
  if (top) h = cri_pearson[(uint8_t)(h ^ top)];
  return h;
}
/* read (and with set, set) hash bit kk of key k in generation g */
static uint8_t probe(crucible_core *c, const pkey_t *k, uint8_t g, uint8_t kk, uint8_t set) {
  uint8_t s = (uint8_t)((kk << 1) + kk), c1 = chain(k, (uint8_t)(s + 1u)), c2 = chain(k, (uint8_t)(s + 2u)), c0;
  uint8_t m = bit_mask[c2 & 7u], raw, big = big_filter(c);
  uint16_t off = (uint16_t)(((uint16_t)c1 << 5) | (c2 >> 3));
  uint32_t at;
  if (big) { /* 32 KB per generation: banks 4-7 (0x8000) and 8-11 (0x10000) */
    c0 = chain(k, s);
    c0 &= 3u;
    off |= (uint16_t)((uint16_t)c0 << 13);
    at = (uint32_t)off;
    if (g)
      at |= 0x10000ul;
    else
      at |= 0x8000ul;
  } else { /* 512 B per generation at 0x0000 and 0x0200 */
    off &= 511u;
    if (g) off += 0x0200u;
    at = (uint32_t)off;
  }
  raw = c->store.read(c->store.ctx, at);
  if (raw & m) return 1;
  if (set) {
    raw |= m;
    c->store.write(c->store.ctx, at, raw);
  }
  return 0;
}
static uint8_t in_gen(crucible_core *c, const pkey_t *k, uint8_t g) {
  if (!probe(c, k, g, 0, 0)) return 0;
  if (!probe(c, k, g, 1, 0)) return 0;
  if (!probe(c, k, g, 2, 0)) return 0;
  return 1;
}
static void put_gen(crucible_core *c, const pkey_t *k, uint8_t g) {
  probe(c, k, g, 0, 1);
  probe(c, k, g, 1, 1);
  probe(c, k, g, 2, 1);
}
static void clear_gen(crucible_core *c, uint8_t g) {
  uint32_t base;
  uint16_t i = 0;
  if (c->lean) return; /* a story run keeps no dud filter */
  if (big_filter(c)) {
    base = 0x8000ul;
    if (g) base = 0x10000ul;
    do c->store.write(c->store.ctx, base | i, 0);
    while (++i < 0x8000u);
  } else if (g)
    fill(c, 0x0200u, 512u, 0);
  else
    fill(c, 0, 512u, 0);
}
static uint8_t filter_ready(crucible_core *c) {
  uint8_t v = LV(L_FLAGS);
  if (v & 1u) return 1;
  return 0;
}
/* forget every dud: the next commit (or load) clears both generations */
void cri_filter_reset(crucible_core *c) CORE_LOCAL {
  uint8_t v = LV(L_FLAGS);
  v &= (uint8_t)~1u;
  SLV(L_FLAGS, v);
  SLV(L_CLEARING, 0);
  SLV(L_JDUD, 0);
}
/* commit side: initialise the filter once, finish a pending rotation clear, insert the journalled dud */
static void filter_apply(crucible_core *c) {
  pkey_t k;
  uint8_t v = LV(L_FLAGS);
  uint16_t a, b;
  if (!(v & 1u)) {
    clear_gen(c, 0);
    clear_gen(c, 1);
    v |= 1u;
    SLV(L_FLAGS, v);
    SLV(L_CLEARING, 0);
  }
  v = LV(L_CLEARING);
  if (v) {
    v = LV(L_GEN);
    clear_gen(c, v);
    SLV(L_CLEARING, 0);
  }
  v = LV(L_JDUD);
  if (v) {
    a = lv16(c, L_JA);
    b = lv16(c, L_JB);
    set_key(&k, a, b);
    v = LV(L_GEN);
    put_gen(c, &k, v);
    SLV(L_JDUD, 0);
  }
}

/* ---- uses: uint8 per id, then a 32-entry spill table {id, extra} past 255 ---- */
static uint8_t spill_find(crucible_core *c, uint16_t id) {
  uint8_t i, lo = (uint8_t)id, hi = (uint8_t)(id >> 8), x, y;
  uint16_t j = c->a_spill;
  for (i = 0; i < P_SPILL_N; i++, j += 4u) {
    x = rd(c, j);
    y = rd(c, (uint16_t)(j + 1u));
    if (x == lo && y == hi) return i;
  }
  return CRU_NONE8;
}
static uint16_t spill_extra(crucible_core *c, uint8_t i) {
  uint16_t j = (uint16_t)(c->a_spill + ((uint16_t)i << 2));
  uint8_t lo = rd(c, (uint16_t)(j + 2u)), hi = rd(c, (uint16_t)(j + 3u));
  return (uint16_t)(lo | ((uint16_t)hi << 8));
}
static uint16_t uses_get(crucible_core *c, uint16_t id) {
  uint8_t v = rd(c, (uint16_t)(c->a_uses + id)), i;
  if (v < 255u) return v;
  i = spill_find(c, id);
  if (i == CRU_NONE8) return 255u;
  return (uint16_t)(255u + spill_extra(c, i));
}
static void use(crucible_core *c, uint16_t id) {
  uint8_t v = rd(c, (uint16_t)(c->a_uses + id)), i;
  uint16_t e, u, j;
  if (v < 255u) {
    u = (uint16_t)(v + 1u);
    stage(c, (uint16_t)(c->a_uses + id), (uint8_t)u);
  } else {
    i = spill_find(c, id);
    if (i != CRU_NONE8) {
      e = spill_extra(c, i);
      if (e == 65535u - 255u) return;
      e++;
      j = (uint16_t)(c->a_spill + ((uint16_t)i << 2));
      stage(c, (uint16_t)(j + 2u), (uint8_t)e);
      if (!(uint8_t)e) stage(c, (uint16_t)(j + 3u), (uint8_t)(e >> 8));
      u = (uint16_t)(255u + e);
    } else {
      i = spill_find(c, CRU_NONE);
      if (i == CRU_NONE8) return; /* table full: this item stays at 255 */
      j = (uint16_t)(c->a_spill + ((uint16_t)i << 2));
      stage(c, j, (uint8_t)id);
      stage(c, (uint16_t)(j + 1u), (uint8_t)(id >> 8));
      stage(c, (uint16_t)(j + 2u), 1);
      stage(c, (uint16_t)(j + 3u), 0);
      u = 256u;
    }
  }
  if (u == 10u) inc16(c, L_USED10);
  e = lv16(c, L_TOP);
  if (u > e) slv16(c, L_TOP, u);
}

/* every recipe that makes r has been tried (and there is at least one) */
static uint8_t routes_done(crucible_core *c, uint16_t r) {
  uint16_t i = cri_route_first(c, r), e = cri_route_first(c, (uint16_t)(r + 1u)), row, bi;
  if (i == e) return 0;
  for (; i < e; i++) {
    row = cri_route(c, i);
    bi = cri_rbit(c, row);
    if (!test_bit(c, c->a_rbits, bi)) return 0;
  }
  return 1;
}

/* ---- per mix: stage everything; the caller writes the record, then cri_play_commit() ---- */
uint8_t cri_play_mix(crucible_core *c, uint16_t a, uint16_t b, uint16_t ri, uint16_t r) CORE_LOCAL {
  uint8_t flags = 0, d, deep, gen;
  uint16_t bi;
  pkey_t k;
  d = LV(L_JN);
  d |= LV(L_JDUD);
  d |= LV(L_CLEARING);
  if (d) {
    apply_bytes(c);
    filter_apply(c);
  }
  if (!cri_owned(c, a) || !cri_owned(c, b)) return 0;
  if (ri != CRU_NONE) {
    if (!cri_owned(c, r)) {
      stage_bit(c, c->a_owned, obit(c, r));
      inc16(c, L_OWNED);
      flags |= CRU_PLAY_NEW_ITEM;
      d = cri_depth(c, r);
      deep = LV(L_DEEP);
      if (d > deep) SLV(L_DEEP, d);
    }
    bi = cri_rbit(c, ri);
    if (!test_bit(c, c->a_rbits, bi)) {
      stage_bit(c, c->a_rbits, bi);
      inc16(c, L_RECIPES);
      flags |= CRU_PLAY_NEW_PAIR | CRU_PLAY_NEW_RECIPE;
      if (a == b) inc16(c, L_MIRRORS);
      if (routes_done(c, r)) {
        inc16(c, L_ROUTES);
        flags |= CRU_PLAY_NEW_ROUTES;
      }
    }
  } else if (filter_ready(c)) {
    set_key(&k, a, b);
    gen = LV(L_GEN);
    if (!in_gen(c, &k, gen)) {
      if (in_gen(c, &k, (uint8_t)(gen ^ 1u)))
        flags |= CRU_PLAY_SEEN_DUD;
      else {
        inc16(c, L_DUDS);
        flags |= CRU_PLAY_NEW_PAIR;
      }
      /* (re)insert into the active generation, so recently re-tried duds survive a rotation */
      if (lv16(c, L_GCOUNT) >= dud_cap(c)) {
        SLV(L_GEN, gen ^ 1u);
        slv16(c, L_GCOUNT, 0);
        SLV(L_CLEARING, 1);
      }
      inc16(c, L_GCOUNT);
      SLV(L_JDUD, 1);
      slv16(c, L_JA, a);
      slv16(c, L_JB, b);
    } else
      flags |= CRU_PLAY_SEEN_DUD;
  }
  use(c, a);
  if (a != b) use(c, b);
  return flags;
}
void cri_play_commit(crucible_core *c) CORE_LOCAL {
  apply_bytes(c);
  filter_apply(c);
}
uint8_t cri_owned(crucible_core *c, uint16_t id) CORE_LOCAL { return test_bit(c, c->a_owned, obit(c, id)); }
/* a grant (cru_grant.c): the owned bit staged in the journal like a mix's, with its counters */
void cri_play_grant(crucible_core *c, uint16_t id) CORE_LOCAL {
  uint8_t d, deep;
  stage_bit(c, c->a_owned, obit(c, id));
  inc16(c, L_OWNED);
  d = cri_depth(c, id);
  deep = LV(L_DEEP);
  if (d > deep) SLV(L_DEEP, d);
}
/* a drop (cru_drop): the owned bit cleared through the journal, the owned counter lowered */
void cri_play_drop(crucible_core *c, uint16_t id) CORE_LOCAL {
  uint16_t a = (uint16_t)(c->a_owned + (obit(c, id) >> 3));
  stage(c, a, (uint8_t)(rd(c, a) & (uint8_t)~bit_mask[obit(c, id) & 7u]));
  slv16(c, L_OWNED, (uint16_t)(lv16(c, L_OWNED) - 1u));
}
uint8_t cri_tried(crucible_core *c, uint16_t a, uint16_t b, uint16_t ri) CORE_LOCAL {
  pkey_t k;
  uint16_t bi;
  uint8_t flags;
  if (ri != CRU_NONE) {
    bi = cri_rbit(c, ri);
    return test_bit(c, c->a_rbits, bi);
  }
  flags = LV(L_FLAGS);
  if (!(flags & 1u)) return 0;
  set_key(&k, a, b);
  if (in_gen(c, &k, 0)) return 1;
  if (in_gen(c, &k, 1)) return 1;
  return 0;
}
uint16_t cri_value(crucible_core *c, uint8_t s) CORE_LOCAL {
  switch (s) {
  case CRU_PV_OWNED: return lv16(c, L_OWNED);
  case CRU_PV_RECIPES: return lv16(c, L_RECIPES);
  case CRU_PV_ROUTES: return lv16(c, L_ROUTES);
  case CRU_PV_USED10: return lv16(c, L_USED10);
  case CRU_PV_TOP: return lv16(c, L_TOP);
  case CRU_PV_DUDS: return lv16(c, L_DUDS);
  case CRU_PV_MIRRORS: return lv16(c, L_MIRRORS);
  case CRU_PV_PAIRS: return (uint16_t)(lv16(c, L_RECIPES) + lv16(c, L_DUDS));
  default: return LV(L_DEEP);
  }
}

/* ---- boot, fresh game, migration ---- */
/* v4: the fixed bank-0 spans (as crucible_play.c); v5: what the catalogue uses of each slot (the rest is zero) */
static uint16_t area_sum(crucible_core *c) {
  uint16_t s;
  if (c->place != CRU_PLACE_WIDE)
    return (uint16_t)(asum(c, P_OWNED, P_LIVE - P_OWNED) + asum(c, P_SPILL, P_USES + CRU_COMPACT_ITEMS - P_SPILL));
  s = asum(c, W_OWNED, (uint16_t)((c->items + 7u) >> 3));
  s = (uint16_t)(s + asum(c, W_RBITS, (uint16_t)((c->recipes + 7u) >> 3)));
  if (c->lean) return s; /* no use counts kept */
  s = (uint16_t)(s + asum(c, W_SPILL, P_SPILL_N << 2));
  s = (uint16_t)(s + asum(c, W_USES, c->items));
  return s;
}
static uint16_t popcount(crucible_core *c, uint16_t rel, uint16_t bytes) {
  uint16_t n = 0;
  uint8_t v;
  while (bytes--) {
    v = cri_ar(c, rel++);
    n = (uint16_t)(n + pop4[v & 15u] + pop4[v >> 4]);
  }
  return n;
}
/* every exact counter from the areas (the approximate dud count cannot be recounted and is kept) */
static void recount(crucible_core *c) {
  uint16_t i, u, a, b, bi, top;
  uint8_t d, deep;
  slv16(c, L_OWNED, popcount(c, c->a_owned, (uint16_t)((c->items + 7u) >> 3)));
  slv16(c, L_RECIPES, popcount(c, c->a_rbits, (uint16_t)((c->recipes + 7u) >> 3)));
  slv16(c, L_MIRRORS, 0);
  slv16(c, L_ROUTES, 0);
  slv16(c, L_USED10, 0);
  slv16(c, L_TOP, 0);
  SLV(L_DEEP, 0);
  for (i = 0; i < c->recipes; i++) {
    a = cri_recipe_a(c, i);
    b = cri_recipe_b(c, i);
    if (a != b) continue;
    bi = cri_rbit(c, i);
    if (test_bit(c, c->a_rbits, bi)) inc16(c, L_MIRRORS);
  }
  for (i = 0; i < c->items; i++) {
    if (routes_done(c, i)) inc16(c, L_ROUTES);
    u = uses_get(c, i);
    if (u >= 10u) inc16(c, L_USED10);
    top = lv16(c, L_TOP);
    if (u > top) slv16(c, L_TOP, u);
    if (cri_owned(c, i)) {
      d = cri_depth(c, i);
      deep = LV(L_DEEP);
      if (d > deep) SLV(L_DEEP, d);
    }
  }
  slv16(c, L_SUM, area_sum(c));
}
void cri_play_recount(crucible_core *c) CORE_LOCAL { recount(c); }
static void clear_exact(crucible_core *c) {
  uint8_t place = c->place;
  fill(c, P_LIVE, P_BLOCK, 0);
  if (place == CRU_PLACE_WIDE) { /* whole slots, so a catalogue that grows finds zeros */
    if (!c->lean) { /* a story slot is wiped to zero before a run starts */
      afill(c, W_OWNED, W_RBITS - W_OWNED, 0);
      afill(c, W_RBITS, W_USES - W_RBITS, 0);
      afill(c, W_USES, W_SPILL - W_USES, 0);
      afill(c, W_SPILL, W_END - W_SPILL, 0xffu);
    }
  } else {
    afill(c, P_OWNED, P_LIVE - P_OWNED, 0);
    afill(c, P_SPILL, P_USES - P_SPILL, 0xffu);
    afill(c, P_USES, CRU_COMPACT_ITEMS, 0);
  }
  SLV(L_VERSION, 1);
  if (place != CRU_PLACE_32K) SLV(L_PLACE, place);
}
/* A new play memory owning the starters (plus extra4: ids 0..31 carried from a v1/v2 record). The dud filter is
 * left uninitialised: the caller writes a record first, then commits (which clears the filter area, and in the
 * 32K layout that area holds the old v1-v3 records). */
void cri_play_init(crucible_core *c, const uint8_t *extra4) CORE_LOCAL {
  uint16_t i, items = c->items, n = (uint16_t)((items + 7u) >> 3), owned = 0;
  uint8_t v, x, last = low_mask[(uint8_t)(items & 7u)], b;
  clear_exact(c);
  for (i = 0; i < n; i++) {
    v = cri_starter_byte(c, i);
    if (extra4 && i < 4u) {
      x = extra4[i];
      v |= x;
    }
    if (i + 1u == n) v &= last;
    if (!v) continue;
    if (c->place != CRU_PLACE_WIDE) {
      cri_aw(c, (uint16_t)(P_OWNED + i), v);
      continue;
    }
    for (b = 0; b < 8u; b++) /* v5: each owned id at its shelf position */
      if (v & bit_mask[b]) set_area_bit(c, W_OWNED, cri_shelf_pos(c, (uint16_t)((i << 3) + b)));
  }
  /* a fresh memory owns only starters (and v1/v2's ids): no recipe, use or route yet, nothing deeper than the
   * starters, so the counters need no catalogue walk; only v1/v2 owners can be deeper */
  owned = popcount(c, c->a_owned, n);
  slv16(c, L_OWNED, owned);
  if (extra4)
    recount(c);
  else
    slv16(c, L_SUM, area_sum(c));
}
/* 0 consistent, 1 counters recounted from the areas */
/* the record's play block into LIVE, its journal replayed (idempotent), an interrupted clear finished; 0 consistent,
 * 1 counters recounted from the areas */
uint8_t cri_play_load(crucible_core *c, uint16_t rec) CORE_LOCAL {
  uint8_t i, v;
  uint16_t s, want;
  for (i = 0; i < P_BLOCK; i++) {
    v = SR(rec + P_REC_AT + i);
    SLV(i, v);
  }
  apply_bytes(c);
  filter_apply(c);
  v = LV(L_VERSION);
  s = area_sum(c);
  want = lv16(c, L_SUM);
  if (v != 1u || (!c->lean && s != want)) {
    recount(c);
    return 1;
  } /* a story run trusts its CRC'd record */
  return 0;
}
/* v3 kept owned ids < 256 as a bitmap at +16 and tried pairs of ids < 64 as bit tri(b)+a at +112. The exact parts
 * come over first (use counts start at the exact lower bound "one mix per distinct pair"); the dud filter overlaps
 * the v3 records in the 32K layout, so v3's duds wait for cri_finish_migration(), after the first v4 record. */
void cri_migrate_v3(crucible_core *c, uint16_t rec, uint8_t items) CORE_LOCAL {
  uint8_t a, b, lim = items;
  uint16_t i, tt = 0, ri, bi;
  if (lim > 64u) lim = 64u;
  clear_exact(c);
  for (i = 0; i < items && i < c->items; i++)
    if (bank0_bit(c, (uint16_t)(rec + 16u), i)) set_area_bit(c, c->a_owned, obit(c, i));
  for (b = 0; b < lim; b++) {
    for (a = 0; a <= b; a++) {
      i = (uint16_t)(tt + a);
      if (!bank0_bit(c, (uint16_t)(rec + 112u), i)) continue;
      if (b < c->items) {
        ri = cri_find(c, a, b);
        if (ri != CRU_NONE) {
          bi = cri_rbit(c, ri);
          set_area_bit(c, c->a_rbits, bi);
        }
      }
      use(c, a);
      if (a != b) use(c, b);
      apply_bytes(c);
    }
    tt = (uint16_t)(tt + b + 1u);
  }
  SLV(L_FLAGS, 0);
  recount(c);
}
void cri_finish_migration(crucible_core *c, uint16_t rec, uint8_t items) CORE_LOCAL {
  uint8_t a, b, lim = items, v, gen;
  uint16_t i, tt = 0;
  pkey_t k;
  if (lim > 64u) lim = 64u;
  /* copy the v3 tried bitmap aside before the filter clear overwrites the v3 records */
  for (i = 0; i < 264u; i++) {
    v = SR(rec + 112u + i);
    SW(P_SCRATCH + i, v);
  }
  v = LV(L_FLAGS);
  v &= (uint8_t)~1u;
  SLV(L_FLAGS, v);
  filter_apply(c);
  for (b = 0; b < lim; b++) {
    for (a = 0; a <= b; a++) {
      i = (uint16_t)(tt + a);
      if (!bank0_bit(c, P_SCRATCH, i)) continue;
      if (cri_find(c, a, b) != CRU_NONE) continue;
      set_key(&k, a, b);
      gen = LV(L_GEN);
      if (in_gen(c, &k, gen)) continue;
      if (lv16(c, L_GCOUNT) >= dud_cap(c)) {
        gen ^= 1u;
        SLV(L_GEN, gen);
        slv16(c, L_GCOUNT, 0);
        clear_gen(c, gen);
      }
      put_gen(c, &k, gen);
      inc16(c, L_DUDS);
      inc16(c, L_GCOUNT);
    }
    tt = (uint16_t)(tt + b + 1u);
  }
}

/* v4 -> v5: a v4 record's play memory moves from bank 0 into the WIDE slots, OWNED re-indexed by shelf position.
 * Nothing in bank 0 is changed (the journal replay only rewrites what it already holds), so a power cut anywhere
 * before the caller's first v5 record repeats the move at the next load. A v4 128K filter (banks 4-11, hashed the
 * same way for ids below 4096) is kept; a 32K one (or an unknown placement) is forgotten. The caller writes the v5
 * record, commits, and writes it again. */
void cri_widen(crucible_core *c, uint16_t rec, uint8_t rec_place, uint16_t old_items, uint16_t old_recipes) CORE_LOCAL {
  uint16_t i, n;
  uint8_t v, keep = 0;
  if (rec_place == CRU_PLACE_128K) keep = 1;
  if (old_items > c->items) old_items = c->items;
  if (old_items > CRU_COMPACT_ITEMS) old_items = CRU_COMPACT_ITEMS;
  if (old_recipes > CRU_COMPACT_RECIPES) old_recipes = CRU_COMPACT_RECIPES;
  if (keep)
    cri_set_place(c, CRU_PLACE_128K);
  else
    cri_set_place(c, CRU_PLACE_32K);
  for (i = 0; i < P_BLOCK; i++) {
    v = SR(rec + P_REC_AT + i);
    SLV(i, v);
  }
  apply_bytes(c);
  if (keep) filter_apply(c); /* a pending clear or dud lands in the filter that stays */
  cri_set_place(c, CRU_PLACE_WIDE);
  afill(c, W_OWNED, W_RBITS - W_OWNED, 0);
  afill(c, W_RBITS, W_USES - W_RBITS, 0);
  afill(c, W_USES, W_SPILL - W_USES, 0);
  for (i = 0; i < old_items; i++)
    if (bank0_bit(c, P_OWNED, i)) set_area_bit(c, W_OWNED, cri_shelf_pos(c, i));
  n = (uint16_t)((old_recipes + 7u) >> 3);
  for (i = 0; i < n; i++) {
    v = SR(P_RBITS + i);
    cri_aw(c, (uint16_t)(W_RBITS + i), v);
  }
  for (i = 0; i < old_items; i++) {
    v = SR(P_USES + i);
    cri_aw(c, (uint16_t)(W_USES + i), v);
  }
  for (i = 0; i < (P_SPILL_N << 2); i++) {
    v = SR(P_SPILL + i);
    cri_aw(c, (uint16_t)(W_SPILL + i), v);
  }
  SLV(L_JN, 0);
  SLV(L_PLACE, CRU_PLACE_WIDE);
  if (!keep) cri_filter_reset(c);
  recount(c);
}

uint16_t cru_play_value(crucible_core *c, uint8_t which) CORE_BANKED { return cri_value(c, which); }
uint16_t cru_play_uses(crucible_core *c, uint16_t id) CORE_BANKED {
  if (id >= c->items) return 0;
  return uses_get(c, id);
}
uint8_t cru_owned(crucible_core *c, uint16_t id) CORE_BANKED {
  if (id >= c->items) return 0;
  return cri_owned(c, id);
}

/* ---- tints ---- */
/* v mod 3 without a divide: 256, 16 and 4 are all 1 mod 3, so folding digits keeps the remainder. */
uint8_t cri_mod3(uint16_t v) CORE_LOCAL {
  v = (uint16_t)((v >> 8) + (v & 255u));
  v = (uint16_t)((v >> 4) + (v & 15u));
  while (v > 3u) v = (uint16_t)((v >> 2) + (v & 3u));
  if (v == 3u) v = 0;
  return (uint8_t)v;
}
/* Two bits per id below 256 (rolled at discovery, kept in the record). Later ids derive theirs from the save's seed
 * with the same distribution (three tints, gilded one time in sixteen), so they cost no bytes. */
uint8_t cri_variant(const crucible_core *c, uint16_t id) CORE_LOCAL {
  uint8_t v, k0, k1, h1, h2;
  uint16_t seed;
  if (id < 256u) {
    k0 = (uint8_t)(id >> 2);
    v = c->variants[k0];
    switch (id & 3u) {
    case 1: v >>= 2; break;
    case 2: v >>= 4; break;
    case 3: v >>= 6; break;
    default: break;
    }
    return (uint8_t)(v & 3u);
  }
  seed = c->variant_seed;
  k0 = (uint8_t)((uint8_t)id ^ (uint8_t)seed);
  k1 = (uint8_t)((uint8_t)(id >> 8) ^ (uint8_t)(seed >> 8));
  h1 = cri_pearson[k0];
  h1 = cri_pearson[(uint8_t)(h1 ^ k1)];
  h2 = cri_pearson[(uint8_t)(k0 ^ 0x5au)];
  h2 = cri_pearson[(uint8_t)(h2 ^ k1)];
  if (!(h1 & 15u)) return 3u;
  return cri_mod3((uint16_t)((h1 >> 4) | ((uint16_t)h2 << 4)));
}
void cri_set_variant(crucible_core *c, uint16_t id, uint8_t v) CORE_LOCAL {
  uint8_t *p, mask = 3u, old;
  if (id >= 256u) return;
  p = &c->variants[(uint8_t)(id >> 2)];
  v &= 3u;
  switch (id & 3u) {
  case 1:
    v <<= 2;
    mask = 0x0cu;
    break;
  case 2:
    v <<= 4;
    mask = 0x30u;
    break;
  case 3:
    v <<= 6;
    mask = 0xc0u;
    break;
  default: break;
  }
  old = *p;
  old &= (uint8_t)~mask;
  old |= v;
  *p = old;
}
/* The seeded tint roll: xorshift the save's rng, add the timer, gilded 1 in 16. */
uint8_t cri_roll(crucible_core *c, uint8_t entropy) CORE_LOCAL {
  uint16_t r = c->rng;
  r ^= (uint16_t)(r << 7);
  r ^= (uint16_t)(r >> 9);
  r ^= (uint16_t)(r << 8);
  r = (uint16_t)(r + entropy + 1u);
  c->rng = r;
  if (!(r & 15u)) return 3u;
  return cri_mod3((uint16_t)(r >> 4));
}
uint8_t cru_variant(const crucible_core *c, uint16_t id) CORE_BANKED { return cri_variant(c, id); }
