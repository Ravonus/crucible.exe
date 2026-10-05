/* Story runs (src/cru_story.c): a run opens beside a Classic save without touching it, starts with the four classics
 * and two seeded extras, splits, loses by its scale (locks on Harsh), gains, and resumes from its own record. */
#include <string.h>
#include "harness.h"

static mem_store m;
static crucible_store base, ss;
static crucible_story_link lk;
static crucible_core classic, run;
static crucible_story st;

static uint16_t owned_count(crucible_core *c) {
  uint16_t i, n = 0;
  for (i = 0; i < c->items; i++) n = (uint16_t)(n + cru_owned(c, i));
  return n;
}
static int outside_bank15_same(const uint8_t *before) { return memcmp(before, m.mem, 0x1E000u) == 0; }

static void classic_untouched(void) {
  static uint8_t before[IMAGE];
  ms_reset(&m);
  base = ms_store(&m);
  boot(&classic, &m, &world, CRU_LAYOUT_128K, 7);
  mix(&classic, "WATER", "FIRE");
  memcpy(before, m.mem, IMAGE);
  EQ(cru_story_open(&run, &st, &world, (lk.base = &base, &lk), &ss, 3, CRU_STORY_NORMAL), 1);
  mixi(&run, id_of("EARTH"), id_of("WATER"));
  CHECK(outside_bank15_same(before));
  /* Classic still loads as it was */
  boot(&classic, &m, &world, CRU_LAYOUT_128K, 7);
  CHECK(cru_owned(&classic, id_of("STEAM")));
}
static void starts_with_six(void) {
  ms_reset(&m);
  base = ms_store(&m);
  EQ(cru_story_open(&run, &st, &world, (lk.base = &base, &lk), &ss, 11, CRU_STORY_NORMAL), 1);
  EQ(owned_count(&run), 6);
  EQ(st.scale, CRU_STORY_NORMAL);
}
static void split_gives_parts(void) {
  uint16_t ab[2], steam = id_of("STEAM");
  ms_reset(&m);
  base = ms_store(&m);
  cru_story_open(&run, &st, &world, (lk.base = &base, &lk), &ss, 5, CRU_STORY_NORMAL);
  if (!cru_owned(&run, steam)) cru_story_gain(&run, &st, steam);
  CHECK(cru_owned(&run, steam));
  EQ(cru_story_split(&run, &st, steam, ab), 1);
  CHECK(!cru_owned(&run, steam));
  CHECK(cru_owned(&run, ab[0]) && cru_owned(&run, ab[1]));
  EQ(cru_story_split(&run, &st, id_of("FIRE"), ab), 0); /* nothing makes a starter */
}
static void losses_by_scale(void) {
  uint16_t lost[4];
  uint8_t s, n, i;
  for (s = CRU_STORY_GENTLE; s <= CRU_STORY_HARSH; s++) {
    ms_reset(&m);
    base = ms_store(&m);
    cru_story_open(&run, &st, &world, (lk.base = &base, &lk), &ss, 9, s);
    for (i = 0; i < run.items; i++)
      if (cru_recipe(&run, 0, 0) || 1) {
        if (i < 20u) cru_story_gain(&run, &st, i);
      }
    n = cru_story_loss(&run, &st, lost, 4);
    if (s == CRU_STORY_GENTLE) EQ(n, 0);
    if (s == CRU_STORY_NORMAL) EQ(n, 1);
    if (s == CRU_STORY_HARSH) {
      CHECK(n >= 1 && n <= 3);
      EQ(cru_story_gain(&run, &st, lost[n - 1]), 0); /* locked */
    }
    for (i = 0; i < n; i++) {
      CHECK(!cru_owned(&run, lost[i]));
      CHECK(!cru_is_starter(&run, lost[i]));
    }
  }
}
static void resumes(void) {
  uint32_t seed;
  uint16_t n;
  ms_reset(&m);
  base = ms_store(&m);
  cru_story_open(&run, &st, &world, (lk.base = &base, &lk), &ss, 21, CRU_STORY_HARSH);
  cru_story_set_flag(&st, 42);
  cru_story_save(&lk, &st);
  seed = st.seed;
  n = owned_count(&run);
  memset(&st, 0, sizeof st);
  EQ(cru_story_open(&run, &st, &world, (lk.base = &base, &lk), &ss, 99, CRU_STORY_GENTLE), 0);
  EQ(st.seed, seed);
  EQ(st.scale, CRU_STORY_HARSH);
  EQ(cru_story_flag(&st, 42), 1);
  EQ(owned_count(&run), n);
}
static void runs_branch(void) {
  uint32_t a, b;
  ms_reset(&m);
  base = ms_store(&m);
  cru_story_open(&run, &st, &world, (lk.base = &base, &lk), &ss, 1, 1);
  cru_story_event(&st, CRU_EV_MIX, 3, 4);
  a = st.seed;
  ms_reset(&m);
  base = ms_store(&m);
  cru_story_open(&run, &st, &world, (lk.base = &base, &lk), &ss, 1, 1);
  cru_story_event(&st, CRU_EV_MIX, 4, 3);
  b = st.seed;
  CHECK(a != b);
}
static void three_slots(void) {
  crucible_story peek;
  uint8_t k;
  uint32_t seeds[3];
  ms_reset(&m);
  base = ms_store(&m);
  for (k = 0; k < CRU_STORY_SLOTS; k++) {
    lk.slot = k;
    EQ(cru_story_open(&run, &st, &world, (lk.base = &base, &lk), &ss, (uint8_t)(40u + k), k), 1);
    st.chapter = (uint8_t)(k + 2u);
    cru_story_save(&lk, &st);
    seeds[k] = st.seed;
  }
  for (k = 0; k < CRU_STORY_SLOTS; k++) {
    EQ(cru_story_peek(&base, k, &peek), 1);
    EQ(peek.chapter, k + 2u);
    EQ(peek.scale, k);
    CHECK(peek.seed == seeds[k]);
  }
  lk.slot = 1;
  EQ(cru_story_open(&run, &st, &world, (lk.base = &base, &lk), &ss, 1, 0), 0);
  EQ(st.chapter, 3); /* each resumes its own */
  cru_story_wipe(&base, 1);
  EQ(cru_story_peek(&base, 1, &peek), 0);
  EQ(cru_story_peek(&base, 2, &peek), 1);
  lk.slot = 0;
}
int main(void) {
  printf("story runs\n");
  RUN(classic_untouched);
  RUN(starts_with_six);
  RUN(split_gives_parts);
  RUN(losses_by_scale);
  RUN(resumes);
  RUN(runs_branch);
  RUN(three_slots);
  printf("%d checks, %d failed\n", t_checks, t_fails);
  return t_fails ? 1 : 0;
}
