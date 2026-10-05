/* The catalogue: the only file that reads catalogue tables, always through CRU_T8/CRU_T16 (cru_internal.h), plus the
 * walks that need them in loops: the filter rule, the shelf step and the scrolling book.
 *
 * The shelf step at any size (the bench's most frequent input). In the WIDE placement OWNED is a bitmap in shelf order,
 * and shelf_sum (32 bytes of the caller's state) marks every 64-position block that holds an owned item, so a step
 * skips empty blocks without reading SRAM, reads one OWNED byte per 8 positions inside a live block, and reads a table
 * only for owned candidates: a category filter is a contiguous shelf range (cat_first), a trait filter ANDs the OWNED
 * byte with that trait's shelf-order bitmap (TAB_TRAIT_SHELF). The 32K and 128K placements (v4, at most 4096 ids)
 * keep OWNED by id and step position by position. */
#include "cru_internal.h"

CRI_BITS;
CRI_AREA_RD
/* filter 8..19: one trait bit each */
static const uint16_t trait_bit[CRU_TRAITS] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048};
/* bits at or above / at or below a bit index */
static const uint8_t from_mask[8] = {0xff, 0xfe, 0xfc, 0xf8, 0xf0, 0xe0, 0xc0, 0x80};
static const uint8_t upto_mask[8] = {0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff};
static const uint8_t pop4[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};

/* The crucible_tables struct is read here once (cru_init): pass it from RAM or bank 0 on a banked cartridge. */
void cri_tables_init(crucible_core *c, const crucible_tables *t) CORE_LOCAL {
  uint8_t i;
  c->t = t;
  c->rules = t->rules;
  c->items = t->items;
  c->recipes = t->recipes;
  c->mirror_total = t->mirrors;
  for (i = 0; i <= CRU_CATEGORIES; i++) c->cat_first[i] = t->cat_first[i];
  for (i = 0; i < CRU_FILTERS; i++) c->total[i] = t->totals[i];
}

uint8_t cri_category(crucible_core *c, uint16_t id) CORE_LOCAL {
  uint8_t v = CRU_T8(c, TAB_CATEGORY, id);
  return v;
}
uint16_t cri_traits(crucible_core *c, uint16_t id) CORE_LOCAL {
  uint16_t v = CRU_T16(c, TAB_TRAITS, id);
  return v;
}
uint16_t cri_shelf(crucible_core *c, uint16_t at) CORE_LOCAL {
  uint16_t v = CRU_T16(c, TAB_SHELF, at);
  return v;
}
uint16_t cri_shelf_pos(crucible_core *c, uint16_t id) CORE_LOCAL {
  uint16_t v = CRU_T16(c, TAB_SHELF_POS, id);
  return v;
}
uint8_t cri_starter_byte(crucible_core *c, uint16_t i) CORE_LOCAL {
  uint8_t v = CRU_T8(c, TAB_STARTERS, i);
  return v;
}
uint8_t cri_starter(crucible_core *c, uint16_t id) CORE_LOCAL {
  uint8_t v = CRU_T8(c, TAB_STARTERS, (uint16_t)(id >> 3));
  uint8_t m = bit_mask[id & 7u];
  if (v & m) return 1;
  return 0;
}
uint8_t cri_depth(crucible_core *c, uint16_t id) CORE_LOCAL {
  uint8_t v = CRU_T8(c, TAB_DEPTH, id);
  return v;
}
uint16_t cri_recipe_a(crucible_core *c, uint16_t row) CORE_LOCAL {
  uint16_t v = CRU_T16(c, TAB_RECIPE_A, row);
  return v;
}
uint16_t cri_recipe_b(crucible_core *c, uint16_t row) CORE_LOCAL {
  uint16_t v = CRU_T16(c, TAB_RECIPE_B, row);
  return v;
}
uint16_t cri_result(crucible_core *c, uint16_t row) CORE_LOCAL {
  uint16_t v;
  if (row == CRU_NONE) return CRU_NONE;
  v = CRU_T16(c, TAB_RECIPE_R, row);
  return v;
}
uint16_t cri_rbit(crucible_core *c, uint16_t row) CORE_LOCAL {
  uint16_t v = CRU_T16(c, TAB_RECIPE_BIT, row);
  return v;
}
uint8_t cri_swapped(crucible_core *c, uint16_t row) CORE_LOCAL {
  uint8_t v = CRU_T8(c, TAB_RECIPE_SWAP, (uint16_t)(row >> 3));
  uint8_t m = bit_mask[row & 7u];
  if (v & m) return 1;
  return 0;
}
uint16_t cri_route_first(crucible_core *c, uint16_t id) CORE_LOCAL {
  uint16_t v = CRU_T16(c, TAB_ROUTE_FIRST, id);
  return v;
}
uint16_t cri_route(crucible_core *c, uint16_t i) CORE_LOCAL {
  uint16_t v = CRU_T16(c, TAB_ROUTE_LIST, i);
  return v;
}

/* Binary search over rows sorted by (min,max): at most 15 probes for 32767 recipes. */
uint16_t cri_find(crucible_core *c, uint16_t a, uint16_t b) CORE_LOCAL {
  uint16_t rows = c->recipes, lo = 0, hi = rows, mid, x, y;
  if (a > b) {
    x = a;
    a = b;
    b = x;
  }
  while (lo < hi) {
    mid = (uint16_t)((lo + hi) >> 1);
    x = CRU_T16(c, TAB_RECIPE_A, mid);
    y = CRU_T16(c, TAB_RECIPE_B, mid);
    if (x < a)
      lo = (uint16_t)(mid + 1u);
    else if (x == a && y < b)
      lo = (uint16_t)(mid + 1u);
    else
      hi = mid;
  }
  if (lo >= rows) return CRU_NONE;
  x = CRU_T16(c, TAB_RECIPE_A, lo);
  y = CRU_T16(c, TAB_RECIPE_B, lo);
  if (x == a && y == b) return lo;
  return CRU_NONE;
}

/* Filters: 0 all, 1..7 a category, 8..19 a trait. */
uint8_t cri_match(crucible_core *c, uint8_t f, uint16_t id) CORE_LOCAL {
  uint8_t k;
  uint16_t v, m;
  if (!f) return 1;
  if (f < 8u) {
    k = CRU_T8(c, TAB_CATEGORY, id);
    if (k == (uint8_t)(f - 1u)) return 1;
    return 0;
  }
  if (f >= CRU_FILTERS) return 0;
  v = CRU_T16(c, TAB_TRAITS, id);
  m = trait_bit[(uint8_t)(f - 8u)];
  if (v & m) return 1;
  return 0;
}

/* ---- shelf-order helpers ---- */
/* trait k's first entry in a per-trait table of `stride` entries each (k*stride by repeated addition) */
static uint16_t trait_base(uint8_t k, uint16_t stride) {
  uint16_t base = 0;
  while (k--) base = (uint16_t)(base + stride);
  return base;
}
/* the filter as a shelf range [*lo, *hi) and an optional trait (0xFF none) */
static uint8_t filter_range(crucible_core *c, uint8_t f, uint16_t *lo, uint16_t *hi) {
  uint8_t k;
  *lo = 0;
  *hi = c->items;
  if (f >= 1u && f <= CRU_CATEGORIES) {
    k = (uint8_t)(f - 1u);
    *lo = c->cat_first[k];
    *hi = c->cat_first[(uint8_t)(k + 1u)];
    return 0xffu;
  }
  if (f >= 8u && f < CRU_FILTERS) return (uint8_t)(f - 8u);
  return 0xffu;
}
static uint8_t sum_bit(const crucible_core *c, uint16_t blk) {
  uint8_t v = c->shelf_sum[(uint8_t)(blk >> 3)];
  uint8_t m = bit_mask[blk & 7u];
  if (v & m) return 1;
  return 0;
}
static uint8_t low_bit(uint8_t v) {
  uint8_t b = 0;
  while (!(v & bit_mask[b])) b++;
  return b;
}
static uint8_t high_bit(uint8_t v) {
  uint8_t b = 7;
  while (!(v & bit_mask[b])) b--;
  return b;
}
/* first owned (and, with a trait, matching) shelf position in [from, to), or CRU_NONE */
static uint16_t scan_up(crucible_core *c, uint16_t from, uint16_t to, uint8_t tk, uint16_t tbase) {
  uint16_t p = from, j, blk, cand;
  uint8_t v, m;
  while (p < to) {
    blk = (uint16_t)(p >> 6);
    if (!sum_bit(c, blk)) {
      p = (uint16_t)((blk + 1u) << 6);
      continue;
    }
    j = (uint16_t)(p >> 3);
    v = cri_ar(c, (uint16_t)(c->a_owned + j));
    m = from_mask[p & 7u];
    v &= m;
    if (v && tk != 0xffu) {
      m = CRU_T8(c, TAB_TRAIT_SHELF, (uint16_t)(tbase + j));
      v &= m;
    }
    if (v) {
      cand = (uint16_t)((j << 3) + low_bit(v));
      if (cand >= to) return CRU_NONE;
      return cand;
    }
    p = (uint16_t)((j + 1u) << 3);
  }
  return CRU_NONE;
}
/* last owned (and matching) shelf position in [to, from], or CRU_NONE */
static uint16_t scan_down(crucible_core *c, uint16_t from, uint16_t to, uint8_t tk, uint16_t tbase) {
  uint16_t p = from, j, blk, cand;
  uint8_t v, m;
  for (;;) {
    if (p < to) return CRU_NONE;
    blk = (uint16_t)(p >> 6);
    if (!sum_bit(c, blk)) {
      if (!blk) return CRU_NONE;
      p = (uint16_t)((blk << 6) - 1u);
      continue;
    }
    j = (uint16_t)(p >> 3);
    v = cri_ar(c, (uint16_t)(c->a_owned + j));
    m = upto_mask[p & 7u];
    v &= m;
    if (v && tk != 0xffu) {
      m = CRU_T8(c, TAB_TRAIT_SHELF, (uint16_t)(tbase + j));
      v &= m;
    }
    if (v) {
      cand = (uint16_t)((j << 3) + high_bit(v));
      if (cand < to) return CRU_NONE;
      return cand;
    }
    if (!j) return CRU_NONE;
    p = (uint16_t)((j << 3) - 1u);
  }
}

/* Next owned item on the shelf that passes the active filter, wrapping; after a full turn it is the item itself. */
uint16_t cri_step(crucible_core *c, uint16_t id, int8_t d) CORE_LOCAL {
  uint16_t n = c->items, at = CRU_T16(c, TAB_SHELF_POS, id), i, x = id, lo, hi, tbase = 0, r;
  uint8_t f = c->filter, tk;
  if (c->place != CRU_PLACE_WIDE) { /* v4: OWNED by id, position by position */
    for (i = 0; i < n; i++) {
      if (d > 0) {
        at++;
        if (at >= n) at = 0;
      } else if (at)
        at--;
      else
        at = (uint16_t)(n - 1u);
      x = CRU_T16(c, TAB_SHELF, at);
      if (cri_match(c, f, x) && cri_owned(c, x)) return x;
    }
    return x;
  }
  tk = filter_range(c, f, &lo, &hi);
  if (lo >= hi) return id;
  if (tk != 0xffu) tbase = trait_base(tk, (uint16_t)((n + 7u) >> 3));
  if (d > 0) {
    if (at >= lo && at < hi) {
      r = scan_up(c, (uint16_t)(at + 1u), hi, tk, tbase);
      if (r == CRU_NONE) r = scan_up(c, lo, (uint16_t)(at + 1u), tk, tbase);
    } else
      r = scan_up(c, lo, hi, tk, tbase);
  } else {
    if (at >= lo && at < hi) {
      r = CRU_NONE;
      if (at > lo) r = scan_down(c, (uint16_t)(at - 1u), lo, tk, tbase);
      if (r == CRU_NONE) r = scan_down(c, (uint16_t)(hi - 1u), at, tk, tbase);
    } else
      r = scan_down(c, (uint16_t)(hi - 1u), lo, tk, tbase);
  }
  if (r == CRU_NONE) return id;
  x = CRU_T16(c, TAB_SHELF, r);
  return x;
}

/* ---- the scrolling book: every item (owned or not) in shelf order, narrowed by the filter ---- */
uint16_t cru_book_count(crucible_core *c) CORE_BANKED {
  uint8_t f = c->filter;
  uint16_t v;
  if (!f || f >= CRU_FILTERS) return c->items;
  v = c->total[f];
  return v;
}
/* the shelf position of the start-th position that has trait k (start < its total) */
static uint16_t trait_seek(crucible_core *c, uint8_t k, uint16_t start) {
  uint16_t blocks = (uint16_t)((c->items + 63u) >> 6), rbase = trait_base(k, blocks),
           sbase = trait_base(k, (uint16_t)((c->items + 7u) >> 3));
  uint16_t lo = 0, hi = blocks, mid, rank, j, left;
  uint8_t v, b, cnt;
  /* the last block whose rank is <= start */
  while ((uint16_t)(hi - lo) > 1u) {
    mid = (uint16_t)((lo + hi) >> 1);
    rank = CRU_T16(c, TAB_TRAIT_RANK, (uint16_t)(rbase + mid));
    if (rank <= start)
      lo = mid;
    else
      hi = mid;
  }
  rank = CRU_T16(c, TAB_TRAIT_RANK, (uint16_t)(rbase + lo));
  left = (uint16_t)(start - rank);
  for (j = (uint16_t)(lo << 3);; j++) {
    v = CRU_T8(c, TAB_TRAIT_SHELF, (uint16_t)(sbase + j));
    cnt = (uint8_t)(pop4[v & 15u] + pop4[v >> 4]);
    if (left < cnt) {
      for (b = 0;; b++) {
        if (!(v & bit_mask[b])) continue;
        if (!left) return (uint16_t)((j << 3) + b);
        left--;
      }
    }
    left = (uint16_t)(left - cnt);
  }
}
uint16_t cru_book_rows_from(crucible_core *c, uint16_t start, uint16_t *out, uint16_t max) CORE_BANKED {
  uint16_t total = cru_book_count(c), lo, hi, p, j, sbase, n = 0;
  uint8_t tk, v, m, f = c->filter;
  if (start >= total) return 0;
  tk = filter_range(c, f, &lo, &hi);
  if (tk == 0xffu) { /* ALL or a category: a contiguous shelf range */
    for (p = (uint16_t)(lo + start); p < hi && n < max; p++, n++) out[n] = CRU_T16(c, TAB_SHELF, p);
    return n;
  }
  sbase = trait_base(tk, (uint16_t)((c->items + 7u) >> 3));
  p = trait_seek(c, tk, start);
  while (n < max && p < c->items) {
    j = (uint16_t)(p >> 3);
    v = CRU_T8(c, TAB_TRAIT_SHELF, (uint16_t)(sbase + j));
    m = from_mask[p & 7u];
    v &= m;
    if (!v) {
      p = (uint16_t)((j + 1u) << 3);
      continue;
    }
    p = (uint16_t)((j << 3) + low_bit(v));
    out[n++] = CRU_T16(c, TAB_SHELF, p);
    p++;
  }
  return n;
}
uint16_t cru_book_rows(crucible_core *c, uint16_t *out, uint16_t max) CORE_BANKED {
  cru_book_rows_from(c, 0, out, max);
  return cru_book_count(c);
}
uint16_t cru_book_index(crucible_core *c, uint16_t id) CORE_BANKED {
  uint16_t pos, lo, hi, blocks, rank, j, k;
  uint8_t tk, v, f = c->filter;
  if (id >= c->items || !cri_match(c, f, id)) return CRU_NONE;
  pos = CRU_T16(c, TAB_SHELF_POS, id);
  tk = filter_range(c, f, &lo, &hi);
  if (tk == 0xffu) return (uint16_t)(pos - lo);
  blocks = (uint16_t)((c->items + 63u) >> 6);
  rank = CRU_T16(c, TAB_TRAIT_RANK, (uint16_t)(trait_base(tk, blocks) + (pos >> 6)));
  k = trait_base(tk, (uint16_t)((c->items + 7u) >> 3));
  for (j = (uint16_t)((pos >> 6) << 3); j < (uint16_t)(pos >> 3); j++) {
    v = CRU_T8(c, TAB_TRAIT_SHELF, (uint16_t)(k + j));
    rank = (uint16_t)(rank + pop4[v & 15u] + pop4[v >> 4]);
  }
  v = CRU_T8(c, TAB_TRAIT_SHELF, (uint16_t)(k + j));
  if (pos & 7u) {
    v &= upto_mask[(uint8_t)((pos & 7u) - 1u)];
    rank = (uint16_t)(rank + pop4[v & 15u] + pop4[v >> 4]);
  }
  return rank;
}

/* ---- public catalogue access ---- */
uint16_t cru_recipe_row(crucible_core *c, uint16_t a, uint16_t b) CORE_BANKED { return cri_find(c, a, b); }
uint16_t cru_recipe(crucible_core *c, uint16_t a, uint16_t b) CORE_BANKED { return cri_result(c, cri_find(c, a, b)); }
uint8_t cru_is_starter(crucible_core *c, uint16_t id) CORE_BANKED {
  if (id >= c->items) return 0;
  return cri_starter(c, id);
}
uint8_t cru_tried(crucible_core *c, uint16_t a, uint16_t b) CORE_BANKED {
  return cri_tried(c, a, b, cri_find(c, a, b));
}
uint8_t cru_filter_match(crucible_core *c, uint8_t f, uint16_t id) CORE_BANKED { return cri_match(c, f, id); }
uint16_t cru_shelf_step(crucible_core *c, uint16_t id, int8_t d) CORE_BANKED { return cri_step(c, id, d); }
uint16_t cru_sram_banks(const crucible_core *c) CORE_BANKED {
  if (c->place == CRU_PLACE_32K) return CRU_BANKS_32K;
  return CRU_BANKS_128K;
}
