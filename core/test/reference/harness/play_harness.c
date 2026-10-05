/* See play_harness.h. Portable C: compiled by the host cc (PLAY_HOST) and by SDCC for the SM83 ROM. */
#include "play_harness.h"
#ifdef PLAY_HOST
#define HS(a) play_sram[(uint16_t)(a)]
#else
#include <gb/gb.h>
#define HS(a) (((volatile uint8_t *)0xa000u)[(uint16_t)(a)])
#endif
uint16_t play_cat_items, play_cat_recipes, h_seq;
const uint16_t *cat_ra, *cat_rb, *cat_rr, *cat_rfirst, *cat_rlist;
const uint8_t *cat_depth;
void h_catalog(uint16_t n, uint16_t recipes, const uint16_t *ra, const uint16_t *rb, const uint16_t *rr,
               const uint8_t *depth, const uint16_t *rfirst, const uint16_t *rlist) {
  play_cat_items = n;
  play_cat_recipes = recipes;
  cat_ra = ra;
  cat_rb = rb;
  cat_rr = rr;
  cat_depth = depth;
  cat_rfirst = rfirst;
  cat_rlist = rlist;
}
uint8_t crucible_depth(uint16_t id) { return cat_depth[id]; }
uint16_t crucible_route(uint16_t id, uint8_t k) {
  uint16_t i = cat_rfirst[id] + k;
  return i < cat_rfirst[id + 1u] ? cat_rlist[i] : PLAY_NONE16;
}
uint16_t crucible_recipe_at(uint16_t ri, uint16_t *ab) {
  ab[0] = cat_ra[ri];
  ab[1] = cat_rb[ri];
  return cat_rr[ri];
}
/* binary search over rows sorted by (min,max) */
uint16_t crucible_recipe_find(uint16_t a, uint16_t b) {
  uint16_t lo = 0, hi = play_cat_recipes, mid, t;
  if (a > b) {
    t = a;
    a = b;
    b = t;
  }
  while (lo < hi) {
    mid = (lo + hi) >> 1;
    if (cat_ra[mid] < a || (cat_ra[mid] == a && cat_rb[mid] < b))
      lo = mid + 1u;
    else
      hi = mid;
  }
  return lo < play_cat_recipes && cat_ra[lo] == a && cat_rb[lo] == b ? lo : PLAY_NONE16;
}
uint16_t h_result(uint16_t ri) { return ri == PLAY_NONE16 ? PLAY_NONE16 : cat_rr[ri]; }

static uint16_t crc16(uint16_t at, uint16_t n) {
  uint16_t c = 0xffffu;
  uint8_t i;
  while (n--) {
    c ^= (uint16_t)HS(at++) << 8;
    for (i = 0; i < 8u; i++) c = (c & 0x8000u) ? (uint16_t)((c << 1) ^ 0x1021u) : (uint16_t)(c << 1);
  }
  return c;
}
static void ram(uint8_t on) {
#ifndef PLAY_HOST
  if (on) {
    ENABLE_RAM;
    SWITCH_RAM(0);
  } else
    DISABLE_RAM;
#else
  (void)on;
#endif
}
void h_store(uint16_t torn) {
  uint8_t rec[2];
  uint16_t at, i, c;
  h_seq++;
  at = (h_seq & 1u) ? PLAY_V4_B : PLAY_V4_A;
  /* build in place: header, play block, zeros, CRC (a torn store stops after `torn` bytes) */
  for (i = 0; i < 512u; i++) {
    uint8_t v = 0;
    if (i == 0)
      v = 0xc1u;
    else if (i == 1)
      v = 4u;
    else if (i == 2)
      v = (uint8_t)h_seq;
    else if (i == 3)
      v = (uint8_t)(h_seq >> 8);
    else if (i >= PLAY_REC_AT && i < PLAY_REC_AT + PLAY_BLOCK)
      v = play_block_byte((uint8_t)(i - PLAY_REC_AT));
    if (i == 510u) {
      ram(1);
      c = crc16(at, 510u);
      rec[0] = (uint8_t)c;
      rec[1] = (uint8_t)(c >> 8);
      v = rec[0];
    } else if (i == 511u)
      v = rec[1];
    if (torn && i == torn) return;
    ram(1);
    HS(at + i) = v;
    ram(0);
  }
}
static uint8_t valid(uint16_t at) {
  uint16_t c;
  if (HS(at) != 0xc1u || HS(at + 1u) != 4u) return 0;
  c = crc16(at, 510u);
  return HS(at + 510u) == (uint8_t)c && HS(at + 511u) == (uint8_t)(c >> 8);
}
uint8_t h_boot(void) {
  uint8_t best = 0xffu, r;
  uint16_t s, seq = 0, at;
  ram(1);
  for (r = 0; r < 2u; r++) {
    at = r ? PLAY_V4_B : PLAY_V4_A;
    if (valid(at)) {
      s = HS(at + 2u) | ((uint16_t)HS(at + 3u) << 8);
      if (best == 0xffu || (int16_t)(s - seq) > 0) {
        best = r;
        seq = s;
      }
    }
  }
  ram(0);
  if (best == 0xffu) return 0xffu;
  h_seq = seq;
  return play_load(best ? PLAY_V4_B : PLAY_V4_A);
}
void h_fresh(const uint16_t *starters, uint8_t count) {
  h_seq = 0;
  play_fresh(starters, count);
  h_store(0);
}
uint8_t h_mix(uint16_t a, uint16_t b) {
  uint16_t ri = crucible_recipe_find(a, b);
  uint8_t f = play_mix(a, b, ri, h_result(ri));
  h_store(0);
  play_commit();
  return f;
}
void h_mix_torn(uint16_t a, uint16_t b, uint8_t kind, uint16_t k) {
  uint16_t ri = crucible_recipe_find(a, b), j, addr;
  uint8_t n, i;
  play_mix(a, b, ri, h_result(ri));
  if (kind == 0) return;
  if (kind == 1) {
    h_store(k);
    return;
  }
  h_store(0);
  if (kind == 2) return;
  /* kind 3: power fails while the journal is being applied */
  ram(1);
  n = HS(PLAY_LIVE + L_JN);
  for (i = 0, j = PLAY_LIVE + L_JOURNAL; i < n && i < k; i++, j += 3u) {
    addr = HS(j) | ((uint16_t)HS(j + 1u) << 8);
    HS(addr) = HS(j + 2u);
  }
  ram(0);
}
void h_migrate_v3(uint8_t items, const uint8_t *owned32, const uint8_t *tried264) {
  uint16_t i;
  ram(1);
  for (i = 0; i < 512u; i++) HS(0x0100u + i) = 0;
  HS(0x0100u) = 0xc1u;
  HS(0x0101u) = 3u;
  for (i = 0; i < 32u; i++) HS(0x0110u + i) = owned32[i];
  for (i = 0; i < 264u; i++) HS(0x0170u + i) = tried264[i];
  ram(0);
  h_seq = 0;
  play_migrate_v3(0x0100u, items);
  h_store(0);
  play_finish_migration(0x0100u, items);
  h_store(0);
}
