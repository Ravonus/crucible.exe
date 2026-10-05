/* Lost pieces (src/cru_lost.c): a piece that stops being owned cools down for 2..10 mixes that make something before it
 * can form again. Its own binary (tools/core-tests.sh test): the cooldown's span and determinism, the first attempt that glitches,
 * the cued later attempts that fail without counting, the mixes that clear it, the table's place in the record (a round
 * trip in both layouts and in a story slot), old and damaged saves, a full table, a regain, a power cut at every write of
 * a drop, and every loss path of a story run. */
#include <string.h>
#include "harness.h"

static mem_store m, pre;
static crucible_store base, ss;
static crucible_story_link lk;
static crucible_story st;

static uint16_t STEAM, FIRE, WATER, EARTH, AIR;
static void ids(void) {
  STEAM = id_of("STEAM");
  FIRE = id_of("FIRE");
  WATER = id_of("WATER");
  EARTH = id_of("EARTH");
  AIR = id_of("AIR");
}

/* 1: no recipe makes id (such a piece is never tracked) */
static int cru_route_none(crucible_core *c, uint16_t id) {
  uint16_t a, b;
  for (a = 0; a < c->items; a++)
    for (b = a; b < c->items; b++)
      if (cru_recipe(c, a, b) == id) return 0;
  return 1;
}
static uint16_t crc16(const uint8_t *p, unsigned n) {
  uint16_t crc = 0xffffu;
  unsigned i, k;
  for (i = 0; i < n; i++) {
    crc ^= (uint16_t)(p[i] << 8);
    for (k = 0; k < 8; k++) crc = (uint16_t)(crc & 0x8000u ? (crc << 1) ^ 0x1021u : crc << 1);
  }
  return crc;
}
static uint8_t *newest_record(mem_store *s, const crucible_core *c) {
  return s->mem + ((c->sequence & 1u) ? 0x0700 : 0x0500);
}
static void reseal(uint8_t *r) {
  uint16_t crc = crc16(r, 510);
  r[510] = (uint8_t)crc;
  r[511] = (uint8_t)(crc >> 8);
}
/* a session that owns STEAM, then loses it */
static uint8_t made_then_lost(crucible_core *c, mem_store *s, uint8_t layout) {
  uint8_t e = boot_clean(c, s, layout);
  EQ(mix(c, "FIRE", "WATER"), CRU_NEW);
  CHECK(cru_owned(c, STEAM));
  EQ(cru_lost_state(c, STEAM), CRU_LOST_NONE);
  EQ(cru_drop(c, STEAM), 1);
  CHECK(!cru_owned(c, STEAM));
  return e;
}

static void span_and_determinism(void) {
  crucible_core c, d;
  unsigned seen[CRU_LOST_MAX + 1] = {0}, seed, id, serial, v;
  uint8_t e, n;
  for (seed = 0; seed < 0x10000u; seed += 251u)
    for (id = 0; id < 600u; id += 7u)
      for (serial = 0; serial < 256u; serial += 37u) {
        v = cru_lost_span((uint16_t)seed, (uint16_t)id, (uint8_t)serial);
        if (v < CRU_LOST_MIN || v > CRU_LOST_MAX) {
          CHECK(0);
          return;
        }
        seen[v]++;
        if (v != cru_lost_span((uint16_t)seed, (uint16_t)id, (uint8_t)serial)) {
          CHECK(0);
          return;
        }
      }
  for (v = CRU_LOST_MIN; v <= CRU_LOST_MAX; v++) CHECK(seen[v] > 0); /* every N in 2..10 is dealt */
  e = made_then_lost(&c, &m, CRU_LAYOUT_32K);
  n = cru_lost_left(&c, STEAM);
  CHECK(n >= CRU_LOST_MIN && n <= CRU_LOST_MAX);
  EQ(n, cru_lost_span(c.variant_seed, STEAM, 1)); /* the save's seed, the id, the first loss */
  EQ(cru_lost_state(&c, STEAM), CRU_LOST_FIRST);
  EQ(c.lost[0], CRU_LOST_VERSION);
  EQ(c.lost[1], 1);
  /* the same save and the same loss deal the same N */
  ms_reset(&pre);
  boot(&d, &pre, &world, CRU_LAYOUT_32K, e);
  EQ(mix(&d, "FIRE", "WATER"), CRU_NEW);
  cru_drop(&d, STEAM);
  EQ(cru_lost_left(&d, STEAM), n);
  /* a starter, or anything no recipe makes, is not tracked */
  EQ(cru_drop_any(&c, FIRE), 1);
  EQ(cru_lost_state(&c, FIRE), CRU_LOST_NONE);
  EQ(c.lost[1], 1);
}

static void first_attempt_glitches(void) {
  crucible_core c;
  uint16_t points, made, fails, recipes, pairs, toasts;
  uint8_t n;
  made_then_lost(&c, &m, CRU_LAYOUT_32K);
  n = cru_lost_left(&c, STEAM);
  points = c.points;
  made = c.made;
  fails = c.fails;
  recipes = cru_play_value(&c, CRU_PV_RECIPES);
  pairs = cru_play_value(&c, CRU_PV_PAIRS);
  toasts = c.toast_n;
  /* the merge opens (the animation may play) but as a miss that is a lost piece's: nothing will form */
  EQ(cru_mix_begin(&c, FIRE, WATER, 9), CRU_NOTHING);
  EQ(c.mix.lost, CRU_LOST_FIRST);
  EQ(c.mix.result, STEAM); /* what tried to form, for the host's glitch */
  EQ(cru_mix_finish(&c), 0);
  CHECK(!cru_owned(&c, STEAM));
  EQ(c.points, points);
  EQ(c.made, made);
  EQ(c.fails, fails);
  EQ(c.toast_n, toasts);
  EQ(cru_play_value(&c, CRU_PV_RECIPES), recipes);
  EQ(cru_play_value(&c, CRU_PV_PAIRS), pairs);
  EQ(cru_lost_left(&c, STEAM), n); /* it does not count */
  EQ(cru_lost_state(&c, STEAM), CRU_LOST_AGAIN); /* the next attempt is cued */
}

static void later_attempts_cued(void) {
  crucible_core c;
  crucible_cells v;
  uint16_t points, made;
  uint8_t n;
  made_then_lost(&c, &m, CRU_LAYOUT_32K);
  /* before the first attempt the bench shows nothing unusual */
  c.slot_a = FIRE;
  c.focus = WATER;
  cru_bench_cells(&c, &v);
  CHECK(v.kind[CRU_CR] != CRU_K_GLITCH);
  EQ(cru_lost_cue(&c, FIRE, WATER), 0);
  EQ(cru_bench_a(&c, 3), CRU_NOTHING);
  cru_mix_finish(&c);
  EQ(c.slot_a, FIRE);
  EQ(c.focus, WATER);
  EQ(c.message, CRU_MSG_NAME); /* lands like a miss, without NO RESULT */
  /* now the pair that would make it shows a glitched result cell, in either order; other pairs do not */
  cru_bench_cells(&c, &v);
  EQ(v.kind[CRU_CR], CRU_K_GLITCH);
  EQ(v.message, CRU_MSG_NAME);
  EQ(cru_lost_cue(&c, FIRE, WATER), 1);
  EQ(cru_lost_cue(&c, WATER, FIRE), 1);
  EQ(cru_lost_cue(&c, EARTH, FIRE), 0);
  EQ(cru_lost_cue(&c, FIRE, CRU_NONE), 0);
  c.focus = EARTH;
  cru_bench_cells(&c, &v);
  CHECK(v.kind[CRU_CR] != CRU_K_GLITCH);
  c.focus = WATER;
  /* mixing anyway: fails quietly, no reward, not counted */
  n = cru_lost_left(&c, STEAM);
  points = c.points;
  made = c.made;
  EQ(cru_bench_a(&c, 3), CRU_NOTHING);
  EQ(c.mix.lost, CRU_LOST_AGAIN);
  EQ(cru_mix_finish(&c), 0);
  CHECK(!cru_owned(&c, STEAM));
  EQ(c.points, points);
  EQ(c.made, made);
  EQ(cru_lost_left(&c, STEAM), n);
  EQ(cru_lost_state(&c, STEAM), CRU_LOST_AGAIN);
}

static void combos_clear_it(void) {
  crucible_core c;
  crucible_cells v;
  uint8_t n, k, o;
  made_then_lost(&c, &m, CRU_LAYOUT_32K);
  n = cru_lost_left(&c, STEAM);
  mixi(&c, FIRE, WATER); /* the first attempt, warned */
  /* a mix that makes nothing does not count */
  EQ(cru_mix_begin(&c, FIRE, FIRE, 1), CRU_NEW);
  cru_mix_finish(&c); /* SUN: counts */
  EQ(cru_lost_left(&c, STEAM), n - 1u);
  /* find a pair that makes nothing among the owned ones */
  {
    uint16_t a, b, done = 0;
    for (a = 0; a < c.items && !done; a++)
      for (b = a; b < c.items && !done; b++)
        if (cru_owned(&c, a) && cru_owned(&c, b) && cru_recipe(&c, a, b) == CRU_NONE) {
          EQ(mixi(&c, a, b), CRU_NOTHING);
          done = 1;
        }
    CHECK(done);
  }
  EQ(cru_lost_left(&c, STEAM), n - 1u);
  /* N other mixes that make something (KNOWN repeats count too): one short of N it still cools */
  for (k = 1; k + 1u < n; k++) {
    o = mix(&c, "FIRE", "FIRE");
    EQ(o, CRU_KNOWN);
  }
  EQ(cru_lost_left(&c, STEAM), 1);
  EQ(cru_mix_begin(&c, FIRE, WATER, 1), CRU_NOTHING);
  cru_mix_finish(&c);
  EQ(mix(&c, "EARTH", "FIRE"), CRU_NEW); /* the N-th */
  EQ(cru_lost_left(&c, STEAM), 0);
  EQ(cru_lost_state(&c, STEAM), CRU_LOST_NONE);
  c.slot_a = FIRE;
  c.focus = WATER;
  cru_bench_cells(&c, &v);
  CHECK(v.kind[CRU_CR] != CRU_K_GLITCH);
  /* normal again: it forms */
  EQ(mix(&c, "WATER", "FIRE"), CRU_NEW);
  CHECK(cru_owned(&c, STEAM));
  EQ(c.lost[CRU_LOST_HEAD + 2], 0); /* the table is empty */
}

static void persists(void) {
  crucible_core c, d;
  uint8_t e, n, layout;
  for (layout = CRU_LAYOUT_32K; layout <= CRU_LAYOUT_128K; layout++) {
    e = made_then_lost(&c, &m, layout);
    n = cru_lost_left(&c, STEAM);
    /* saved with the drop: a power-on finds it cooling, unwarned */
    EQ(boot(&d, &m, &world, layout, e), CRU_LOAD_V4);
    EQ(cru_lost_left(&d, STEAM), n);
    EQ(cru_lost_state(&d, STEAM), CRU_LOST_FIRST);
    CHECK(!memcmp(d.lost, c.lost, CRU_LOST_BYTES));
    /* the warning is saved too */
    mixi(&d, FIRE, WATER);
    EQ(boot(&c, &m, &world, layout, e), CRU_LOAD_V4);
    EQ(cru_lost_state(&c, STEAM), CRU_LOST_AGAIN);
    EQ(cru_lost_left(&c, STEAM), n);
    /* and each count down */
    mix(&c, "FIRE", "FIRE");
    EQ(boot(&d, &m, &world, layout, e), CRU_LOAD_V4);
    EQ(cru_lost_left(&d, STEAM), n - 1u);
    /* the record holds it at 246..263, under the CRC */
    {
      const uint8_t *r = newest_record(&m, &d);
      CHECK(!memcmp(r + CRU_LOST_REC_AT, d.lost, CRU_LOST_BYTES));
      EQ(r[CRU_LOST_REC_AT + 2] | (r[CRU_LOST_REC_AT + 3] << 8), STEAM);
    }
  }
}

static void old_and_damaged_saves(void) {
  crucible_core c, d;
  uint8_t e, *r;
  /* a save from before the table: zeros at 246..263 (as every older v4/v5 record wrote them) */
  e = made_then_lost(&c, &m, CRU_LAYOUT_32K);
  r = newest_record(&m, &c);
  memset(r + CRU_LOST_REC_AT, 0, CRU_LOST_BYTES);
  reseal(r);
  EQ(boot(&d, &m, &world, CRU_LAYOUT_32K, e), CRU_LOAD_V4);
  EQ(cru_lost_state(&d, STEAM), CRU_LOST_NONE);
  EQ(d.lost[0], CRU_LOST_VERSION);
  {
    unsigned i;
    for (i = 1; i < CRU_LOST_BYTES; i++) EQ(d.lost[i], 0);
  }
  EQ(mix(&d, "FIRE", "WATER"), CRU_NEW); /* nothing holds it back */
  /* an unknown version reads as empty */
  e = made_then_lost(&c, &m, CRU_LAYOUT_32K);
  r = newest_record(&m, &c);
  r[CRU_LOST_REC_AT] = 0x7e;
  reseal(r);
  boot(&d, &m, &world, CRU_LAYOUT_32K, e);
  EQ(cru_lost_state(&d, STEAM), CRU_LOST_NONE);
  /* damaged entries go, sound ones stay packed: [junk id] [left 99] [STEAM] [free] */
  e = made_then_lost(&c, &m, CRU_LAYOUT_32K);
  r = newest_record(&m, &c) + CRU_LOST_REC_AT;
  memset(r + 2, 0, 16);
  r[2] = 0xff;
  r[3] = 0x3f;
  r[4] = 3; /* id 16383: not in the catalogue */
  r[6] = (uint8_t)EARTH;
  r[8] = 99; /* past CRU_LOST_MAX */
  r[10] = (uint8_t)STEAM;
  r[11] = 0;
  r[12] = 4;
  r[13] = 0xfe; /* sound (stray flag bits dropped) */
  reseal(r - CRU_LOST_REC_AT);
  boot(&d, &m, &world, CRU_LAYOUT_32K, e);
  EQ(cru_lost_left(&d, STEAM), 4);
  EQ(cru_lost_state(&d, STEAM), CRU_LOST_FIRST);
  EQ(d.lost[2] | (d.lost[3] << 8), STEAM);
  EQ(d.lost[6], 0);
  /* an entry for a piece owned now goes */
  e = made_then_lost(&c, &m, CRU_LAYOUT_32K);
  c.lost[2] = (uint8_t)FIRE;
  c.lost[3] = 0;
  cru_save(&c);
  boot(&d, &m, &world, CRU_LAYOUT_32K, e);
  EQ(d.lost[CRU_LOST_HEAD + 2], 0);
  /* fresh saves and RESET GAME start empty */
  ms_reset(&m);
  boot(&c, &m, &world, CRU_LAYOUT_128K, 1);
  EQ(c.lost[0], CRU_LOST_VERSION);
  EQ(c.lost[CRU_LOST_HEAD + 2], 0);
  e = made_then_lost(&c, &m, CRU_LAYOUT_32K);
  cru_reset(&c, 5);
  EQ(cru_lost_state(&c, STEAM), CRU_LOST_NONE);
  boot(&d, &m, &world, CRU_LAYOUT_32K, e);
  EQ(cru_lost_state(&d, STEAM), CRU_LOST_NONE);
  EQ(d.lost[0], CRU_LOST_VERSION);
}

static void full_table_and_regain(void) {
  crucible_core c;
  static const char *const made[6][3] = {{"FIRE", "WATER", "STEAM"}, {"EARTH", "WATER", "MUD"},
                                         {"EARTH", "FIRE", "LAVA"},  {"EARTH", "AIR", "DUST"},
                                         {"WATER", "AIR", "RAIN"},   {"FIRE", "AIR", "ENERGY"}};
  uint8_t k;
  boot_clean(&c, &m, CRU_LAYOUT_32K);
  for (k = 0; k < 6u; k++) EQ(mix(&c, made[k][0], made[k][1]), CRU_NEW);
  for (k = 0; k < 5u; k++) EQ(cru_drop(&c, id_of(made[k][2])), 1);
  /* four slots, newest first: the oldest (STEAM) dropped out */
  EQ(cru_lost_state(&c, STEAM), CRU_LOST_NONE);
  for (k = 1; k < 5u; k++) CHECK(cru_lost_left(&c, id_of(made[k][2])) >= CRU_LOST_MIN);
  EQ(c.lost[2] | (c.lost[3] << 8), id_of("RAIN"));
  EQ(c.lost[1], 5);
  /* lost again while cooling: a fresh cooldown, as the newest */
  EQ(cru_grant(&c, id_of("MUD"), CRU_GRANT_SYNC), 1);
  EQ(cru_lost_state(&c, id_of("MUD")), CRU_LOST_NONE); /* back some other way: no cooldown */
  EQ(cru_lost_left(&c, id_of("LAVA")) >= CRU_LOST_MIN, 1);
  EQ(c.lost[CRU_LOST_HEAD + 3 * 4 + 2], 0); /* repacked: three left */
  EQ(cru_drop(&c, id_of("MUD")), 1);
  EQ(c.lost[2] | (c.lost[3] << 8), id_of("MUD"));
  EQ(cru_lost_left(&c, id_of("MUD")), cru_lost_span(c.variant_seed, id_of("MUD"), 6));
}

/* a power cut after every single write of a drop: the next power-on has it owned and not cooling, or lost and cooling */
static void torn_drop(void) {
  crucible_core c, d;
  static mem_store base_img;
  unsigned long k, writes;
  uint8_t e, n, layout, before = 0, after = 0;
  for (layout = CRU_LAYOUT_32K; layout <= CRU_LAYOUT_128K; layout++) {
    e = boot_clean(&c, &m, layout);
    mix(&c, "FIRE", "WATER");
    memcpy(base_img.mem, m.mem, IMAGE);
    m.writes = 0;
    cru_drop(&c, STEAM);
    writes = m.writes;
    n = cru_lost_left(&c, STEAM);
    for (k = 0; k <= writes; k++) {
      memcpy(m.mem, base_img.mem, IMAGE);
      boot(&c, &m, &world, layout, e);
      m.budget = (long)k;
      m.writes = 0;
      cru_drop(&c, STEAM);
      m.budget = -1;
      boot(&d, &m, &world, layout, e);
      if (cru_owned(&d, STEAM)) {
        EQ(cru_lost_state(&d, STEAM), CRU_LOST_NONE);
        before++;
      } else {
        EQ(cru_lost_left(&d, STEAM), n);
        EQ(cru_lost_state(&d, STEAM), CRU_LOST_FIRST);
        after++;
      }
    }
  }
  CHECK(before > 0 && after > 0);
}

/* every way a story run loses a piece starts a cooldown, and the slot keeps it */
static void story_losses(void) {
  crucible_core run;
  uint16_t ab[2], lost[4], gift, mud = id_of("MUD");
  uint8_t i, n, f, r;
  /* a split */
  ms_reset(&m);
  base = ms_store(&m);
  lk.base = &base;
  lk.slot = 0;
  cru_story_open(&run, &st, &world, &lk, &ss, 5, CRU_STORY_NORMAL);
  if (!cru_owned(&run, STEAM)) cru_story_gain(&run, &st, STEAM);
  EQ(cru_story_split(&run, &st, STEAM, ab), 1);
  CHECK(cru_lost_left(&run, STEAM) >= CRU_LOST_MIN);
  n = cru_lost_left(&run, STEAM);
  EQ(cru_mix_begin(&run, ab[0], ab[1], 1), CRU_NOTHING);
  EQ(run.mix.lost, CRU_LOST_FIRST);
  cru_mix_finish(&run);
  cru_story_save(&lk, &st);
  /* the slot keeps it (its record lives in the slot's bank) */
  memset(&st, 0, sizeof st);
  EQ(cru_story_open(&run, &st, &world, &lk, &ss, 99, CRU_STORY_NORMAL), 0);
  EQ(cru_lost_left(&run, STEAM), n);
  EQ(cru_lost_state(&run, STEAM), CRU_LOST_AGAIN);
  /* a loss by the scale */
  for (i = 0; i < 20u; i++) cru_story_gain(&run, &st, i);
  n = cru_story_loss(&run, &st, lost, 4);
  EQ(n, 1);
  CHECK(cru_lost_left(&run, lost[0]) >= CRU_LOST_MIN || cru_route_none(&run, lost[0]));
  /* a gift a faction keeps */
  gift = CRU_NONE;
  for (f = 0; f < CRU_FACTIONS && gift == CRU_NONE; f++)
    for (i = 4; i < 57u; i++)
      if (cru_owned(&run, i) && cru_affinity(&run, f, i) > 0 && !cru_is_starter(&run, i)) {
        r = cru_story_choose(&run, &st, f, CRU_SAY_OFFER, i);
        CHECK(r >= CRU_REACT_WARM);
        gift = i;
        break;
      }
  CHECK(gift != CRU_NONE);
  if (gift != CRU_NONE) {
    CHECK(!cru_owned(&run, gift));
    CHECK(cru_lost_left(&run, gift) >= CRU_LOST_MIN || cru_route_none(&run, gift));
  }
  /* a miss that destroys */
  {
    uint16_t a, b, gone = CRU_NONE;
    uint8_t tries;
    if (!cru_owned(&run, mud)) cru_story_gain(&run, &st, mud);
    for (tries = 0; tries < 40u && gone == CRU_NONE; tries++) {
      for (a = 0; a < run.items; a++)
        if (cru_owned(&run, a) && a != mud && cru_recipe(&run, a, mud) == CRU_NONE) break;
      if (a >= run.items) break;
      b = mud;
      if (!cru_owned(&run, b)) break;
      cru_story_fail(&run, &st, a, b, &gone);
      if (gone != CRU_NONE && gone != mud) {
        cru_story_gain(&run, &st, gone);
        gone = CRU_NONE;
      }
    }
    EQ(gone, mud);
    if (gone == mud) CHECK(cru_lost_left(&run, mud) >= CRU_LOST_MIN);
  }
}

int main(void) {
  ids();
  printf("lost pieces\n");
  RUN(span_and_determinism);
  RUN(first_attempt_glitches);
  RUN(later_attempts_cued);
  RUN(combos_clear_it);
  RUN(persists);
  RUN(old_and_damaged_saves);
  RUN(full_table_and_regain);
  RUN(torn_drop);
  RUN(story_losses);
  printf("%d checks, %d failed\n", t_checks, t_fails);
  return t_fails ? 1 : 0;
}
