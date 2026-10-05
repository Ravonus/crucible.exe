/* CRUCIBLE play memory, save v4 (see crucible_play.h for the layout and the write-ahead protocol).
 * SDCC notes: no static work RAM; no 32-bit math; no multiply or divide; masks and Pearson bytes come from
 * ROM tables and every shift is by a constant (SM83 has no barrel shifter). */
#ifdef __SDCC
#if !defined(PLAY_NO_BANK)
#pragma bank 255
#endif
#endif
#include <string.h>
#include "crucible_play.h"
#ifdef PLAY_TEST
#include "play_test_catalog.h"
#else
#include "crucible_data.h"
#define PLAY_ITEMS CRUCIBLE_ITEMS
#define PLAY_RECIPES CRUCIBLE_RECIPES
#endif

#ifdef PLAY_HOST
uint8_t play_sram[16u * 8192u];
static uint8_t host_bank;
#define RAMBANK(b) (host_bank = (uint8_t)(b))
#define SR(a) play_sram[(uint16_t)(a)]
#define SRB(a) play_sram[(uint32_t)host_bank * 8192u + (uint16_t)(a)]
#define RAM_ON() (host_bank = 0)
#define RAM_OFF()
#else
#define RAMBANK(b) SWITCH_RAM(b)
#define SR(a) (((volatile uint8_t *)0xa000u)[(uint16_t)(a)])
#define SRB(a) SR(a)
#define RAM_ON()                                                                                                       \
  do {                                                                                                                 \
    ENABLE_RAM;                                                                                                        \
    SWITCH_RAM(0);                                                                                                     \
  } while (0)
#define RAM_OFF() DISABLE_RAM
#endif
#define LV(o) SR(PLAY_LIVE + (o))
#define NONE8 255u
#define SCRATCH_BANK 2u
#define SCRATCH_AT 0x1e00u /* v3's own tried working copy lived here (bank-2 scratch) */

static const uint8_t bit[8] = {1, 2, 4, 8, 16, 32, 64, 128};
static const uint8_t pop4[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};
static const uint8_t pearson[256] = {
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

/* Bulk loops. SDCC compiles a C byte loop over SRAM to 128-380 clocks per byte; GBDK's memset and the
 * hand-written sum below (sdcccall(1): at in DE, n in BC, result in BC) run at about 44 clocks per byte. */
#ifdef PLAY_HOST
static void fill(uint16_t at, uint16_t n, uint8_t v) { memset(&play_sram[at], v, n); }
static uint16_t sum_bytes(uint16_t at, uint16_t n) {
  uint16_t s = 0;
  while (n--) s += play_sram[at++];
  return s;
}
#else
static void fill(uint16_t at, uint16_t n, uint8_t v) { memset((uint8_t *)(0xa000u + at), v, n); }
uint16_t play_sum_sram(uint16_t at, uint16_t n) __naked {
  // clang-format off
 __asm
	ld	a, d
	add	a, #0xa0
	ld	h, a
	ld	l, e
	ld	d, b
	ld	e, c
	ld	bc, #0x0000
	ld	a, d
	or	a, e
	ret	z
	ld	a, e
	or	a, a
	jr	z, 1$
	inc	d
1$:
	ld	a, (hl+)
	add	a, c
	ld	c, a
	jr	nc, 2$
	inc	b
2$:
	dec	e
	jr	nz, 1$
	dec	d
	jr	nz, 1$
	ret
 __endasm;
  // clang-format on
}
#define sum_bytes(at, n) play_sum_sram(at, n)
#endif
static uint16_t lv16(uint8_t o) { return LV(o) | ((uint16_t)LV(o + 1u) << 8); }
static void slv16(uint8_t o, uint16_t v) {
  LV(o) = (uint8_t)v;
  LV(o + 1u) = (uint8_t)(v >> 8);
}
static void inc16(uint8_t o) { slv16(o, lv16(o) + 1u); }

/* ---- write-ahead journal over the exact areas (bank 0) ---- */
static uint8_t rd(uint16_t addr) {
  uint8_t n = LV(L_JN), i, lo = (uint8_t)addr, hi = (uint8_t)(addr >> 8);
  uint16_t j = PLAY_LIVE + L_JOURNAL;
  for (i = 0; i < n; i++, j += 3u)
    if (SR(j) == lo && SR(j + 1u) == hi) return SR(j + 2u);
  return SR(addr);
}
static void stage(uint16_t addr, uint8_t v) {
  uint8_t n = LV(L_JN), i, old, lo = (uint8_t)addr, hi = (uint8_t)(addr >> 8);
  uint16_t j = PLAY_LIVE + L_JOURNAL;
  for (i = 0; i < n; i++, j += 3u)
    if (SR(j) == lo && SR(j + 1u) == hi) {
      old = SR(j + 2u);
      SR(j + 2u) = v;
      slv16(L_SUM, lv16(L_SUM) + v - old);
      return;
    }
  if (n >= PLAY_JOURNAL) return; /* unreachable: a mix stages at most 10 bytes */
  old = SR(addr);
  SR(j) = lo;
  SR(j + 1u) = hi;
  SR(j + 2u) = v;
  LV(L_JN) = n + 1u;
  slv16(L_SUM, lv16(L_SUM) + v - old);
}
static uint8_t exact_addr(uint16_t a) {
  return a >= PLAY_OWNED && a < PLAY_USES + PLAY_MAX_IDS && (a < PLAY_LIVE || a >= PLAY_SPILL);
}
static void apply_bytes(void) {
  uint8_t n = LV(L_JN), i;
  uint16_t j = PLAY_LIVE + L_JOURNAL, a;
  if (n > PLAY_JOURNAL) n = 0;
  for (i = 0; i < n; i++, j += 3u) {
    a = SR(j) | ((uint16_t)SR(j + 1u) << 8);
    if (exact_addr(a)) SR(a) = SR(j + 2u);
  }
  LV(L_JN) = 0;
}
static uint8_t test_bit(uint16_t base, uint16_t i) { return (rd(base + (i >> 3)) & bit[i & 7u]) != 0; }
static void stage_bit(uint16_t base, uint16_t i) {
  uint16_t a = base + (i >> 3);
  stage(a, rd(a) | bit[i & 7u]);
}

/* ---- dud filter: two Bloom generations, 3 hashes of the key (min<<12)|max as three bytes ----
 * hash bits k: Pearson chains with starts 3k, 3k+1, 3k+2 (h24); bit index = h24 & (2^LOG-1) */
typedef struct {
  uint8_t lo, mid, hi;
} pkey_t;
static void set_key(pkey_t *k, uint16_t a, uint16_t b) {
  uint16_t t;
  if (a > b) {
    t = a;
    a = b;
    b = t;
  }
  k->lo = (uint8_t)b;
  k->mid = (uint8_t)((b >> 8) | ((a & 15u) << 4));
  k->hi = (uint8_t)(a >> 4);
}
static uint8_t chain(const pkey_t *k, uint8_t s) { return pearson[pearson[pearson[k->lo ^ s] ^ k->mid] ^ k->hi]; }
/* read (and with set, set) bit kk of key k in generation g */
static uint8_t probe(const pkey_t *k, uint8_t g, uint8_t kk, uint8_t set) {
  uint8_t s = (uint8_t)((kk << 1) + kk), c1 = chain(k, s + 1u), c2 = chain(k, s + 2u), m = bit[c2 & 7u], v, bank;
  uint16_t off = ((uint16_t)c1 << 5) | (c2 >> 3), addr;
#ifdef PLAY_SRAM_128K
  off |= (uint16_t)(chain(k, s) & 3u) << 13;
  bank = (uint8_t)(4u + (g << 2) + (off >> 13));
  addr = off & 0x1fffu;
#else
  off &= 511u;
  bank = 0;
  addr = (g ? 0x0200u : 0u) + off;
#endif
  RAMBANK(bank);
  v = SRB(addr) & m;
  if (set) SRB(addr) |= m;
  RAMBANK(0);
  return v != 0;
}
static uint8_t in_gen(const pkey_t *k, uint8_t g) {
  return probe(k, g, 0, 0) && probe(k, g, 1, 0) && probe(k, g, 2, 0);
}
static void put_gen(const pkey_t *k, uint8_t g) {
  probe(k, g, 0, 1);
  probe(k, g, 1, 1);
  probe(k, g, 2, 1);
}
static void clear_gen(uint8_t g) {
#ifdef PLAY_SRAM_128K
  uint8_t b;
  for (b = 0; b < 4u; b++) {
    RAMBANK(4u + (g << 2) + b);
#ifdef PLAY_HOST
    {
      uint32_t i;
      for (i = 0; i < 8192u; i++) play_sram[(uint32_t)host_bank * 8192u + i] = 0;
    }
#else
    fill(0, 8192u, 0);
#endif
  }
  RAMBANK(0);
#else
  fill(g ? 0x0200u : 0u, 512u, 0);
#endif
}
/* commit side: initialise the filter once, finish a pending rotation clear, insert the journalled dud */
static void filter_apply(void) {
  pkey_t k;
  if (!(LV(L_FLAGS) & 1u)) {
    clear_gen(0);
    clear_gen(1);
    LV(L_FLAGS) |= 1u;
    LV(L_CLEARING) = 0;
  }
  if (LV(L_CLEARING)) {
    clear_gen(LV(L_GEN));
    LV(L_CLEARING) = 0;
  }
  if (LV(L_JDUD)) {
    set_key(&k, lv16(L_JA), lv16(L_JB));
    put_gen(&k, LV(L_GEN));
    LV(L_JDUD) = 0;
  }
}

/* ---- uses: uint8 per id, then a 32-entry spill table {id, extra} past 255 ---- */
static uint8_t spill_find(uint16_t id) {
  uint8_t i;
  uint16_t j = PLAY_SPILL;
  for (i = 0; i < PLAY_SPILL_N; i++, j += 4u)
    if (rd(j) == (uint8_t)id && rd(j + 1u) == (uint8_t)(id >> 8)) return i;
  return NONE8;
}
static uint16_t spill_extra(uint8_t i) {
  uint16_t j = PLAY_SPILL + ((uint16_t)i << 2);
  return rd(j + 2u) | ((uint16_t)rd(j + 3u) << 8);
}
static uint16_t uses_get(uint16_t id) {
  uint8_t v = rd(PLAY_USES + id), i;
  if (v < 255u) return v;
  i = spill_find(id);
  return i == NONE8 ? 255u : 255u + spill_extra(i);
}
static void use(uint16_t id) {
  uint8_t v = rd(PLAY_USES + id), i;
  uint16_t e, u, j;
  if (v < 255u) {
    u = v + 1u;
    stage(PLAY_USES + id, (uint8_t)u);
  } else {
    i = spill_find(id);
    if (i != NONE8) {
      e = spill_extra(i);
      if (e == 65535u - 255u) return;
      e++;
      j = PLAY_SPILL + ((uint16_t)i << 2);
      stage(j + 2u, (uint8_t)e);
      if (!(uint8_t)e) stage(j + 3u, (uint8_t)(e >> 8));
      u = 255u + e;
    } else {
      i = spill_find(PLAY_NONE16);
      if (i == NONE8) return; /* table full: this item stays at 255 */
      j = PLAY_SPILL + ((uint16_t)i << 2);
      stage(j, (uint8_t)id);
      stage(j + 1u, (uint8_t)(id >> 8));
      stage(j + 2u, 1);
      stage(j + 3u, 0);
      u = 256u;
    }
  }
  if (u == 10u) inc16(L_USED10);
  if (u > lv16(L_TOP)) slv16(L_TOP, u);
}

static uint8_t routes_done(uint16_t r) {
  uint8_t k;
  uint16_t ri;
  for (k = 0;; k++) {
    ri = crucible_route(r, k);
    if (ri == PLAY_NONE16) return k != 0;
    if (!test_bit(PLAY_RBITS, ri)) return 0;
  }
}

/* ---- per mix: stage everything; the caller writes the record, then play_commit() ---- */
uint8_t play_mix(uint16_t a, uint16_t b, uint16_t ri, uint16_t r) PLAY_BANKED {
  uint8_t flags = 0, d;
  pkey_t k;
  RAM_ON();
  if (LV(L_JN) || LV(L_JDUD) || LV(L_CLEARING)) {
    apply_bytes();
    filter_apply();
  }
  if (!test_bit(PLAY_OWNED, a) || !test_bit(PLAY_OWNED, b)) {
    RAM_OFF();
    return 0;
  }
  if (ri != PLAY_NONE16) {
    if (!test_bit(PLAY_OWNED, r)) {
      stage_bit(PLAY_OWNED, r);
      inc16(L_OWNED);
      flags |= PLAY_NEW_ITEM;
      d = crucible_depth(r);
      if (d > LV(L_DEEP)) LV(L_DEEP) = d;
    }
    if (!test_bit(PLAY_RBITS, ri)) {
      stage_bit(PLAY_RBITS, ri);
      inc16(L_RECIPES);
      flags |= PLAY_NEW_PAIR | PLAY_NEW_RECIPE;
      if (a == b) inc16(L_MIRRORS);
      if (routes_done(r)) {
        inc16(L_ROUTES);
        flags |= PLAY_NEW_ROUTES;
      }
    }
  } else if (LV(L_FLAGS) & 1u) {
    set_key(&k, a, b);
    if (!in_gen(&k, LV(L_GEN))) {
      if (in_gen(&k, LV(L_GEN) ^ 1u))
        flags |= PLAY_SEEN_DUD;
      else {
        inc16(L_DUDS);
        flags |= PLAY_NEW_PAIR;
      }
      /* (re)insert into the active generation, so recently re-tried duds survive a rotation */
      if (lv16(L_GCOUNT) >= PLAY_DUD_CAP) {
        LV(L_GEN) ^= 1u;
        slv16(L_GCOUNT, 0);
        LV(L_CLEARING) = 1;
      }
      inc16(L_GCOUNT);
      LV(L_JDUD) = 1;
      slv16(L_JA, a);
      slv16(L_JB, b);
    } else
      flags |= PLAY_SEEN_DUD;
  }
  use(a);
  if (a != b) use(b);
  RAM_OFF();
  return flags;
}
void play_commit(void) PLAY_BANKED {
  RAM_ON();
  apply_bytes();
  filter_apply();
  RAM_OFF();
}
uint8_t play_block_byte(uint8_t i) PLAY_BANKED {
  uint8_t v;
  RAM_ON();
  v = LV(i);
  RAM_OFF();
  return v;
}
uint8_t play_owned(uint16_t id) PLAY_BANKED {
  uint8_t v;
  RAM_ON();
  v = test_bit(PLAY_OWNED, id);
  RAM_OFF();
  return v;
}
uint8_t play_tried(uint16_t a, uint16_t b, uint16_t ri) PLAY_BANKED {
  uint8_t v = 0;
  pkey_t k;
  RAM_ON();
  if (ri != PLAY_NONE16)
    v = test_bit(PLAY_RBITS, ri);
  else if (LV(L_FLAGS) & 1u) {
    set_key(&k, a, b);
    v = in_gen(&k, 0) || in_gen(&k, 1);
  }
  RAM_OFF();
  return v;
}
uint16_t play_uses(uint16_t id) PLAY_BANKED {
  uint16_t v;
  RAM_ON();
  v = uses_get(id);
  RAM_OFF();
  return v;
}
uint16_t play_value(uint8_t s) PLAY_BANKED {
  uint16_t v;
  RAM_ON();
  switch (s) {
  case PLAY_V_OWNED: v = lv16(L_OWNED); break;
  case PLAY_V_RECIPES: v = lv16(L_RECIPES); break;
  case PLAY_V_ROUTES: v = lv16(L_ROUTES); break;
  case PLAY_V_USED10: v = lv16(L_USED10); break;
  case PLAY_V_TOP: v = lv16(L_TOP); break;
  case PLAY_V_DUDS: v = lv16(L_DUDS); break;
  case PLAY_V_MIRRORS: v = lv16(L_MIRRORS); break;
  case PLAY_V_PAIRS: v = lv16(L_RECIPES) + lv16(L_DUDS); break;
  default: v = LV(L_DEEP);
  }
  RAM_OFF();
  return v;
}

/* ---- boot, fresh game, migration ---- */
static uint16_t area_sum(void) {
  return sum_bytes(PLAY_OWNED, PLAY_LIVE - PLAY_OWNED) + sum_bytes(PLAY_SPILL, PLAY_USES + PLAY_MAX_IDS - PLAY_SPILL);
}
static uint16_t popcount(uint16_t base, uint16_t bytes) {
  uint16_t n = 0;
  uint8_t v;
  volatile uint8_t *p = &SR(base);
  while (bytes--) {
    v = *p++;
    n += pop4[v & 15u] + pop4[v >> 4];
  }
  return n;
}
/* every exact counter from the areas (the approximate dud count cannot be recounted and is kept) */
static void recount(void) {
  uint16_t i, ab[2], u;
  uint8_t d;
  slv16(L_OWNED, popcount(PLAY_OWNED, (PLAY_ITEMS + 7u) >> 3));
  slv16(L_RECIPES, popcount(PLAY_RBITS, (PLAY_RECIPES + 7u) >> 3));
  slv16(L_MIRRORS, 0);
  slv16(L_ROUTES, 0);
  slv16(L_USED10, 0);
  slv16(L_TOP, 0);
  LV(L_DEEP) = 0;
  for (i = 0; i < PLAY_RECIPES; i++)
    if (test_bit(PLAY_RBITS, i)) {
      crucible_recipe_at(i, ab);
      if (ab[0] == ab[1]) inc16(L_MIRRORS);
    }
  for (i = 0; i < PLAY_ITEMS; i++) {
    if (routes_done(i)) inc16(L_ROUTES);
    u = uses_get(i);
    if (u >= 10u) inc16(L_USED10);
    if (u > lv16(L_TOP)) slv16(L_TOP, u);
    if (test_bit(PLAY_OWNED, i)) {
      d = crucible_depth(i);
      if (d > LV(L_DEEP)) LV(L_DEEP) = d;
    }
  }
  slv16(L_SUM, area_sum());
}
static void clear_exact(void) {
  fill(PLAY_OWNED, PLAY_LIVE - PLAY_OWNED, 0);
  fill(PLAY_LIVE, PLAY_BLOCK, 0);
  fill(PLAY_SPILL, PLAY_USES - PLAY_SPILL, 0xffu);
  fill(PLAY_USES, PLAY_MAX_IDS, 0);
  LV(L_VERSION) = 1;
}
void play_fresh(const uint16_t *starters, uint8_t count) PLAY_BANKED {
  uint8_t i;
  RAM_ON();
  clear_exact();
  for (i = 0; i < count; i++) SR(PLAY_OWNED + (starters[i] >> 3)) |= bit[starters[i] & 7u];
  filter_apply();
  recount();
  RAM_OFF();
}
/* returns 0 consistent, 1 counters recounted from the areas */
uint8_t play_load(uint16_t rec) PLAY_BANKED {
  uint8_t i, ret = 0;
  RAM_ON();
  for (i = 0; i < PLAY_BLOCK; i++) LV(i) = SR(rec + PLAY_REC_AT + i);
  apply_bytes();
  filter_apply();
  if (LV(L_VERSION) != 1u || area_sum() != lv16(L_SUM)) {
    recount();
    ret = 1;
  }
  RAM_OFF();
  return ret;
}
/* v3 kept owned ids < 256 as a bitmap at +16 and tried pairs of ids < 64 as bit tri(b)+a at +112. The exact
 * parts are carried over first (use counts start at the exact lower bound "one mix per distinct pair"); the dud
 * filter overlaps the v3 records in the 32 KB layout, so its duds wait for play_finish_migration(), which
 * the game calls after the first v4 record is safely written. */
uint16_t play_migrate_v3(uint16_t rec, uint8_t items) PLAY_BANKED {
  uint8_t a, b, lim = items < 64u ? items : 64u;
  uint16_t i, n = 0, t = 0, ri;
  RAM_ON();
  clear_exact();
  for (i = 0; i < items; i++)
    if (SR(rec + 16u + (i >> 3)) & bit[i & 7u]) SR(PLAY_OWNED + (i >> 3)) |= bit[i & 7u];
  for (b = 0; b < lim; b++) {
    for (a = 0; a <= b; a++) {
      i = t + a;
      if (!(SR(rec + 112u + (i >> 3)) & bit[i & 7u])) continue;
      n++;
      ri = crucible_recipe_find(a, b);
      if (ri != PLAY_NONE16) SR(PLAY_RBITS + (ri >> 3)) |= bit[ri & 7u];
      use(a);
      if (a != b) use(b);
      apply_bytes();
    }
    t += b + 1u;
  }
  LV(L_FLAGS) = 0;
  recount();
  RAM_OFF();
  return n;
}
void play_finish_migration(uint16_t rec, uint8_t items) PLAY_BANKED {
  uint8_t a, b, lim = items < 64u ? items : 64u, v;
  uint16_t i, t = 0;
  pkey_t k;
  RAM_ON();
  /* copy the v3 tried bitmap to bank-2 scratch before the filter clear overwrites the v3 records */
  for (i = 0; i < 264u; i++) {
    v = SR(rec + 112u + i);
    RAMBANK(SCRATCH_BANK);
    SRB(SCRATCH_AT + i) = v;
    RAMBANK(0);
  }
  LV(L_FLAGS) &= (uint8_t)~1u;
  filter_apply();
  for (b = 0; b < lim; b++) {
    for (a = 0; a <= b; a++) {
      i = t + a;
      RAMBANK(SCRATCH_BANK);
      v = SRB(SCRATCH_AT + (i >> 3)) & bit[i & 7u];
      RAMBANK(0);
      if (v && crucible_recipe_find(a, b) == PLAY_NONE16) {
        set_key(&k, a, b);
        if (!in_gen(&k, LV(L_GEN))) {
          if (lv16(L_GCOUNT) >= PLAY_DUD_CAP) {
            LV(L_GEN) ^= 1u;
            slv16(L_GCOUNT, 0);
            clear_gen(LV(L_GEN));
          }
          put_gen(&k, LV(L_GEN));
          inc16(L_DUDS);
          inc16(L_GCOUNT);
        }
      }
    }
    t += b + 1u;
  }
  RAM_OFF();
}
#ifdef PLAY_TEST
/* benchmark hook: the cost of clearing one dud generation (what a rotation pays once per PLAY_DUD_CAP duds) */
void play_test_clear(uint8_t g) {
  RAM_ON();
  clear_gen(g);
  RAM_OFF();
}
#endif
