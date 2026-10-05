/* Byte agreement with the reference play module (core/test/reference/crucible_play.c). Both run the same history on the real catalogue: the core through
 * cru_mix_begin/finish over the byte store, the reference through its own harness (h_fresh/h_mix). Every area
 * (OWNED, RBITS, LIVE, SPILL, USES, both dud generations, the migration scratch) must match byte for byte after
 * every mix; only the record slots differ (the core writes the full game record, the harness a minimal one).
 * Built twice by tools/core-tests.sh: the 32 KB layout, and with -DPLAY_SRAM_128K the 128 KB one.
 * The one byte the reference never writes is the placement mark (play block offset 66): 0 in the 32K placement, so
 * that layout matches everywhere, and 2 in the 128K one, which is how a cartridge whose header grew from 32 KB to
 * 128 KB knows its dud filter has not been written in banks 4-11 yet. The world's recipe_bit is its sorted row (the
 * reference's tried-bit indexing). */
#include <string.h>
#include "harness.h"
#include "play_harness.h"
#include "../generated/crucible_tables_world.h"

#ifdef PLAY_SRAM_128K
#define LAYOUT CRU_LAYOUT_128K
#define LABEL "128K"
#else
#define LAYOUT CRU_LAYOUT_32K
#define LABEL "32K"
#endif

static mem_store m;
static uint16_t ra[CRUCIBLE_WORLD_RECIPES], rb[CRUCIBLE_WORLD_RECIPES], rr[CRUCIBLE_WORLD_RECIPES],
    rfirst[CRUCIBLE_WORLD_ITEMS + 1], rlist[CRUCIBLE_WORLD_RECIPES];
static uint8_t depth[CRUCIBLE_WORLD_ITEMS];
static uint16_t starters[8];
static uint8_t nstarters;

static void reference_catalogue(void) {
  unsigned i;
  for (i = 0; i < CRUCIBLE_WORLD_RECIPES; i++) {
    ra[i] = crucible_world_recipe_a[i];
    rb[i] = crucible_world_recipe_b[i];
    rr[i] = crucible_world_recipe_r[i];
    rlist[i] = crucible_world_route_list[i];
  }
  for (i = 0; i < CRUCIBLE_WORLD_ITEMS; i++) depth[i] = crucible_world_depth[i];
  for (i = 0; i <= CRUCIBLE_WORLD_ITEMS; i++) rfirst[i] = crucible_world_route_first[i];
  nstarters = 0;
  for (i = 0; i < CRUCIBLE_WORLD_ITEMS; i++)
    if (crucible_world_starters[i >> 3] & (1u << (i & 7u))) starters[nstarters++] = (uint16_t)i;
  h_catalog(CRUCIBLE_WORLD_ITEMS, CRUCIBLE_WORLD_RECIPES, ra, rb, rr, depth, rfirst, rlist);
}

/* compare everything but the record slots 0x0500..0x08FF */
static long first_difference(void) {
  uint32_t i, end = LAYOUT ? 0x18000u : 0x8000u;
  for (i = 0; i < end; i++) {
    if (i >= 0x0500u && i < 0x0900u) continue;
    if (LAYOUT && i == 0x0f00u + 66u) {
      if (m.mem[i] != CRU_PLACE_128K) return (long)i;
      continue;
    }
    if (m.mem[i] != play_sram[i]) return (long)i;
  }
  return -1;
}

static uint32_t lcg = 12345u;
static unsigned rnd(unsigned n) {
  lcg = lcg * 1103515245u + 12345u;
  return (lcg >> 16) % n;
}

static void history(void) {
  crucible_core c;
  uint16_t owned[CRUCIBLE_WORLD_ITEMS], n, a, b, i, k, mismatches = 0;
  uint8_t rflags, v;
  unsigned mixes = 0;
  long at;
  ms_reset(&m);
  memset(play_sram, 0, sizeof play_sram);
  reference_catalogue();
  boot(&c, &m, &world_rows, LAYOUT, 7);
  h_fresh(starters, nstarters);
  at = first_difference();
  EQ(at, -1);
  /* explorers: random owned pairs (finds, routes, known, duds; the 32K filter rotates every 341 duds) */
  for (k = 0; k < 4000u && !mismatches; k++) {
    n = 0;
    for (i = 0; i < CRUCIBLE_WORLD_ITEMS; i++)
      if (cru_owned(&c, i)) owned[n++] = i;
    a = owned[rnd(n)];
    b = owned[rnd(n)];
    if (rnd(4) == 0 && c.last_new != CRU_NONE) a = c.last_new; /* build on the newest find, as players do */
    v = cru_mix_begin(&c, a, b, 3);
    cru_mix_finish(&c);
    mixes++;
    rflags = h_mix(a, b);
    if (rflags != c.mix.flags) {
      mismatches++;
      fprintf(stderr, "  mix %u (%u,%u): flags %u, reference %u\n", k, a, b, c.mix.flags, rflags);
    }
    at = first_difference();
    if (at >= 0) {
      mismatches++;
      fprintf(stderr, "  mix %u (%u,%u) outcome %u: first difference at 0x%05lx\n", k, a, b, v, at);
    }
  }
  /* a grinder: one favourite pair, past 255 uses (the spill table) */
  for (k = 0; k < 600u && !mismatches; k++) {
    cru_mix_begin(&c, 1, 2, 3);
    cru_mix_finish(&c);
    mixes++;
    h_mix(1, 2);
    if (first_difference() >= 0) mismatches++;
  }
  EQ(mismatches, 0);
  for (v = 0; v <= CRU_PV_PAIRS; v++) EQ(cru_play_value(&c, v), play_value(v));
  for (i = 0; i < CRUCIBLE_WORLD_ITEMS; i++) EQ(cru_play_uses(&c, i), play_uses(i));
  CHECK(cru_play_uses(&c, 1) > 600u);
  /* both boot paths agree too: the reference's play_load on its image, the core's cru_load on its own */
  EQ(h_boot(), 0);
  EQ(boot(&c, &m, &world_rows, LAYOUT, 7), CRU_LOAD_V4);
  EQ(first_difference(), -1);
  printf("    %s: %u mixes, %u owned, %u recipes, %u duds, generation %u, every area byte-identical after every mix\n",
         LABEL, mixes, cru_play_value(&c, CRU_PV_OWNED), cru_play_value(&c, CRU_PV_RECIPES),
         cru_play_value(&c, CRU_PV_DUDS), m.mem[0x0f00 + 17]);
}

/* the same v3 record migrated by both */
static void migration(void) {
  crucible_core c;
  uint8_t r[512];
  unsigned i, q;
  uint16_t crc = 0xffffu;
  static const uint16_t pairs[][2] = {{2, 1}, {0, 1}, {0, 2}, {2, 2}, {1, 1},  {3, 3}, {4, 7},
                                      {3, 0}, {3, 1}, {5, 6}, {8, 1}, {27, 1}, {9, 9}, {4, 4}};
  memset(r, 0, sizeof r);
  r[0] = 0xc1;
  r[1] = 3;
  r[2] = 5;
  r[14] = 57;
  r[16] = 0xff;
  r[17] = 0x13;
  r[19] = 0x08; /* ids 0..7, 8, 9, 12, 27 */
  for (i = 0; i < sizeof pairs / sizeof *pairs; i++) {
    unsigned a = pairs[i][0], b = pairs[i][1];
    if (a > b) {
      q = a;
      a = b;
      b = q;
    }
    q = b * (b + 1u) / 2u + a;
    r[112 + (q >> 3)] |= (uint8_t)(1u << (q & 7u));
  }
  for (i = 0; i < 510; i++) {
    unsigned k;
    crc ^= (uint16_t)(r[i] << 8);
    for (k = 0; k < 8; k++) crc = (uint16_t)(crc & 0x8000u ? (crc << 1) ^ 0x1021u : crc << 1);
  }
  r[510] = (uint8_t)crc;
  r[511] = (uint8_t)(crc >> 8);
  ms_reset(&m);
  memset(play_sram, 0, sizeof play_sram);
  memcpy(m.mem + 0x0100, r, 512);
  memcpy(play_sram + 0x0100, r, 512);
  reference_catalogue();
  EQ(boot(&c, &m, &world_rows, LAYOUT, 0), CRU_LOAD_V3);
  h_seq = 0;
  play_migrate_v3(0x0100, 57);
  h_store(0);
  play_finish_migration(0x0100, 57);
  h_store(0);
  EQ(first_difference(), -1);
  for (i = 0; i <= CRU_PV_PAIRS; i++) EQ(cru_play_value(&c, (uint8_t)i), play_value((uint8_t)i));
}

int main(void) {
  printf("agreement with crucible_play.c (%s)\n", LABEL);
  RUN(history);
  RUN(migration);
  printf("%d checks, %d failed\n", t_checks, t_fails);
  return t_fails ? 1 : 0;
}
