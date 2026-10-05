/* The saga layer (src/cru_saga.c): factions notice what you make and say, the dream bends lines, bosses fight with
 * real recipes. */
#include <string.h>
#include "harness.h"

static mem_store m;
static crucible_store base, ss;
static crucible_story_link lk;
static crucible_core run;
static crucible_story st;

static void fresh(uint8_t e, uint8_t scale) {
  ms_reset(&m);
  base = ms_store(&m);
  cru_story_open(&run, &st, &world, (lk.base = &base, &lk), &ss, e, scale);
}
static uint16_t owned_n(void) {
  uint16_t i, n = 0;
  for (i = 0; i < run.items; i++) n = (uint16_t)(n + cru_owned(&run, i));
  return n;
}
static uint16_t best_for(uint8_t f) {
  uint16_t i, best = CRU_NONE;
  int8_t a = -99;
  for (i = 0; i < run.items; i++)
    if (cru_affinity(&run, f, i) > a) {
      a = cru_affinity(&run, f, i);
      best = i;
    }
  return best;
}

static void factions_notice(void) {
  uint16_t ghostly;
  uint8_t k, changed = 0;
  fresh(3, CRU_STORY_NORMAL);
  ghostly = best_for(CRU_FAC_GHOST);
  CHECK(cru_affinity(&run, CRU_FAC_GHOST, ghostly) >= 3);
  EQ(cru_story_tier(&st, CRU_FAC_GHOST), CRU_TIER_NEUTRAL);
  for (k = 0; k < 40u; k++) changed |= cru_story_notice(&run, &st, ghostly);
  CHECK(changed & (1u << CRU_FAC_GHOST));
  CHECK(cru_story_tier(&st, CRU_FAC_GHOST) >= CRU_TIER_FRIEND);
  CHECK(st.stand[CRU_FAC_GHOST] <= 100);
  EQ(cru_story_recall(&st, 0), ghostly);
}
static void choices_move_rivals(void) {
  int8_t r;
  fresh(4, CRU_STORY_NORMAL);
  r = st.stand[CRU_FAC_AI];
  EQ(cru_story_choose(&run, &st, CRU_FAC_OPERATOR, CRU_SAY_AGREE, CRU_NONE), CRU_REACT_WARM);
  CHECK(st.stand[CRU_FAC_OPERATOR] > 0);
  CHECK(st.stand[CRU_FAC_AI] < r);
  EQ(cru_story_choose(&run, &st, CRU_FAC_PROGRAM, CRU_SAY_REFUSE, CRU_NONE), CRU_REACT_COLD);
  CHECK(st.stand[CRU_FAC_PROGRAM] < 0);
  CHECK(st.stand[CRU_FAC_DAEMON] > 0);
}
static void gifts(void) {
  uint16_t liked;
  uint8_t r;
  fresh(5, CRU_STORY_NORMAL);
  liked = best_for(CRU_FAC_RELIC);
  cru_story_gain(&run, &st, liked);
  r = cru_story_choose(&run, &st, CRU_FAC_RELIC, CRU_SAY_OFFER, liked);
  EQ(r, CRU_REACT_LOVE);
  CHECK(!cru_owned(&run, liked) || cru_is_starter(&run, liked));
  CHECK(st.stand[CRU_FAC_RELIC] >= 14);
}
static void endings(void) {
  memset(st.stand, 0, sizeof st.stand);
  EQ(cru_story_ending(&st), CRU_END_LOOP);
  st.stand[CRU_FAC_GHOST] = 60;
  EQ(cru_story_ending(&st), 1u + CRU_FAC_GHOST);
  st.stand[CRU_FAC_RELIC] = 55;
  EQ(cru_story_ending(&st), CRU_END_SCHISM);
  memset(st.stand, 20, sizeof st.stand);
  EQ(cru_story_ending(&st), CRU_END_WAKE);
}
static void dream(void) {
  uint16_t id = id_of("STEAM"), r;
  int k, drift = 0;
  fresh(6, CRU_STORY_NORMAL);
  st.lucid = 255;
  for (k = 0; k < 200; k++) EQ(cru_dream_slot(&run, &st, id), id);
  for (k = 0; k < 200; k++) EQ(cru_dream_glitch(&st, 0), 0);
  st.lucid = 0;
  for (k = 0; k < 400; k++) {
    r = cru_dream_slot(&run, &st, id);
    CHECK(r < run.items);
    drift += r != id;
  }
  CHECK(drift > 100);
  for (k = 0, drift = 0; k < 400; k++) drift += cru_dream_glitch(&st, 1) == 3;
  CHECK(drift > 5);
  cru_story_lucid(&st, -50);
  EQ(st.lucid, 0);
  st.lucid = 250;
  cru_story_lucid(&st, 50);
  EQ(st.lucid, 255);
}
static uint16_t best_answer(crucible_boss *b) {
  uint16_t i, best = CRU_NONE;
  uint8_t s = 0, v;
  for (i = 0; i < run.items; i++)
    if (cru_owned(&run, i)) {
      v = cru_counter(&run, b->next, i);
      if (v > s) {
        s = v;
        best = i;
      }
    }
  return best;
}
static void boss_fight(void) {
  crucible_boss *b = &st.nemesis;
  uint16_t i, lost;
  uint8_t out, rounds, fled = 0, down = 0;
  fresh(7, CRU_STORY_GENTLE);
  for (i = 0; i < run.items; i++) cru_story_gain(&run, &st, i); /* a full shelf: every counter is in reach */
  cru_boss_begin(&run, &st, b, CRU_FAC_DAEMON, 2);
  EQ(b->hp, 8);
  EQ(b->met, 1);
  for (i = 0; i < 4u; i++) CHECK(b->hand[i] < run.items);
  EQ(cru_counter(&run, id_of("FIRE"), id_of("WATER")) >= 1, 1);
  for (rounds = 0; rounds < 40u && !down; rounds++) {
    CHECK(cru_boss_plan(&run, &st, b) < run.items);
    CHECK(b->cue >= 1);
    out = cru_boss_answer(&run, &st, b, best_answer(b), &lost);
    if (out == CRU_BOSS_FLED) {
      uint8_t met = b->met;
      fled = 1;
      cru_boss_begin(&run, &st, b, b->faction, (uint8_t)(b->level + 1u));
      EQ(b->met, met + 1);
    }
    if (out == CRU_BOSS_DOWN) down = 1;
  }
  CHECK(fled);
  CHECK(down);
  CHECK(st.stand[CRU_FAC_DAEMON] < 0);
  CHECK(st.stand[CRU_FAC_PROGRAM] > 0);
  /* no answer: the attack lands (Gentle: clock, not elements) */
  cru_boss_begin(&run, &st, b, CRU_FAC_GHOST, 0);
  cru_boss_plan(&run, &st, b);
  EQ(cru_boss_answer(&run, &st, b, CRU_NONE, &lost), CRU_BOSS_HIT);
  EQ(lost, CRU_NONE);
}
static void boss_steals(void) {
  crucible_boss b;
  uint16_t lost;
  memset(&b, 0, sizeof b);
  fresh(8, CRU_STORY_NORMAL);
  {
    uint16_t i;
    for (i = 0; i < 30u; i++) cru_story_gain(&run, &st, i);
  }
  cru_boss_begin(&run, &st, &b, CRU_FAC_AI, 1);
  cru_boss_plan(&run, &st, &b);
  EQ(cru_boss_answer(&run, &st, &b, CRU_NONE, &lost), CRU_BOSS_HIT);
  CHECK(lost != CRU_NONE);
  EQ(b.stolen, lost);
  CHECK(!cru_owned(&run, lost));
}
static void saga_saves(void) {
  fresh(9, CRU_STORY_NORMAL);
  st.stand[CRU_FAC_AI] = -42;
  st.lucid = 77;
  cru_story_remember(&st, 5);
  st.nemesis.met = 2;
  cru_story_save(&lk, &st);
  memset(&st, 0, sizeof st);
  EQ(cru_story_open(&run, &st, &world, (lk.base = &base, &lk), &ss, 1, 0), 0);
  EQ(st.stand[CRU_FAC_AI], -42);
  EQ(st.lucid, 77);
  EQ(cru_story_recall(&st, 0), 5);
  EQ(st.nemesis.met, 2);
}
static void run_course(void) {
  uint8_t k, st8;
  fresh(10, CRU_STORY_NORMAL);
  EQ(cru_story_state(&run, &st), CRU_RUN_ON);
  /* the dream thins faster each chapter */
  {
    uint8_t l0 = st.lucid, l1, l2;
    cru_story_advance(&run, &st);
    l1 = st.lucid;
    cru_story_advance(&run, &st);
    l2 = st.lucid;
    CHECK(l0 - l1 < l1 - l2);
  }
  /* grudges grow */
  st.stand[CRU_FAC_AI] = -20;
  cru_story_advance(&run, &st);
  CHECK(st.stand[CRU_FAC_AI] < -20);
  /* lose early: four hostile factions */
  memset(st.stand, -60, 4);
  EQ(cru_story_state(&run, &st), CRU_RUN_LOST_DELETED);
  memset(st.stand, 0, sizeof st.stand);
  st.lucid = 0;
  EQ(cru_story_state(&run, &st), CRU_RUN_LOST_DREAM);
  /* reach the end with a helper keeping the dream alive */
  fresh(11, CRU_STORY_GENTLE);
  for (k = 0, st8 = CRU_RUN_ON; k < 20u && st8 == CRU_RUN_ON; k++) {
    st.lucid = 255;
    st8 = cru_story_advance(&run, &st);
  }
  EQ(st8, CRU_RUN_END);
  CHECK(cru_story_grade(&run, &st) <= 3);
  /* keep going: harder */
  {
    uint8_t p = cru_story_pressure(&st);
    cru_story_continue(&st);
    EQ(st.chapter, 0);
    EQ(st.cycle, 1);
    CHECK(st.lucid < 220);
    EQ(cru_story_pressure(&st), 2);
    CHECK(p >= 8);
  }
}
static void truths(void) {
  uint8_t k, i, due = 0, ghost = 2u, v;
  uint16_t airy;
  fresh(12, CRU_STORY_NORMAL);
  EQ(cru_story_clue_due(&st), 0xff); /* chapter 0: you do not know yet */
  /* play leans the matrix: cold, airy, magic weather and letting go read as a GHOST */
  airy = best_for(CRU_FAC_GHOST);
  for (i = 0; i < 12u; i++) {
    cru_story_lean(&run, &st, airy);
    cru_story_act(&st, CRU_ACT_LOSS);
  }
  EQ(cru_story_truth(&st, CRU_TRUTH_WHAT), ghost);
  CHECK(cru_story_runner_up(&st, CRU_TRUTH_WHAT) != ghost);
  EQ(cru_story_knows(&st, CRU_TRUTH_WHAT), CRU_KNOW_NOTHING);
  cru_story_clue(&st, CRU_TRUTH_WHAT);
  EQ(cru_story_clue(&st, CRU_TRUTH_WHAT), 2);
  EQ(cru_story_knows(&st, CRU_TRUTH_WHAT), CRU_KNOW_SUSPECT);
  cru_story_clue(&st, CRU_TRUTH_WHAT);
  cru_story_clue(&st, CRU_TRUTH_WHAT);
  EQ(cru_story_knows(&st, CRU_TRUTH_WHAT), CRU_KNOW_ANSWER);
  /* play turns: agreeing and making machines leans to PROGRAM; the ghost clues stop answering */
  for (i = 0; i < 40u; i++) {
    cru_story_lean(&run, &st, best_for(CRU_FAC_PROGRAM));
    cru_story_act(&st, CRU_ACT_AGREE);
  }
  v = cru_story_truth(&st, CRU_TRUTH_WHAT);
  CHECK(v != ghost);
  EQ(cru_story_knows(&st, CRU_TRUTH_WHAT), CRU_KNOW_NOTHING);
  /* it evolves: chapters fade old leanings, so a long-ago lean loses to a fresh one */
  {
    int16_t before = st.lean[CRU_TRUTH_WHAT][v];
    cru_story_advance(&run, &st);
    CHECK(st.lean[CRU_TRUTH_WHAT][v] < before);
  }
  st.chapter = 6;
  st.lucid = 40;
  for (i = 0; i < 200u; i++) {
    k = cru_story_clue_due(&st);
    if (k != 0xffu) due++;
  }
  CHECK(due > 40);
}
static void misses(void) {
  uint16_t lost, i, n, first, chained;
  uint8_t o0, o3;
  fresh(13, CRU_STORY_NORMAL);
  o0 = cru_story_fail_odds(&st);
  CHECK(o0 >= 128u);
  st.fails = 3;
  o3 = cru_story_fail_odds(&st);
  CHECK(o3 > o0);
  st.fails = 0;
  /* the chain: over many runs, the 4th miss in a row destroys more often than the 1st */
  for (first = 0, chained = 0, i = 0; i < 60u; i++) {
    fresh((uint8_t)(20u + i), CRU_STORY_NORMAL);
    if (cru_story_fail(&run, &st, id_of("FIRE"), id_of("WATER"), &lost)) first++;
    st.fails = 3;
    n = owned_n();
    if (cru_story_fail(&run, &st, id_of("EARTH"), id_of("AIR"), &lost)) {
      chained++;
      CHECK(owned_n() == n - 1u);
      CHECK(!cru_owned(&run, lost));
    }
  }
  CHECK(first >= 15u);
  CHECK(chained > first);
  /* a real make breaks the chain */
  st.fails = 5;
  cru_story_notice(&run, &st, id_of("STEAM"));
  EQ(st.fails, 0);
  /* run out: fewer than two elements is the end */
  fresh(14, CRU_STORY_HARSH);
  for (i = 0; i < run.items; i++)
    if (cru_owned(&run, i)) cru_drop_any(&run, i);
  cru_story_gain(&run, &st, id_of("FIRE"));
  EQ(cru_story_state(&run, &st), CRU_RUN_LOST_HOLLOW);
}
static void repaid(void) {
  uint16_t liked, n;
  fresh(15, CRU_STORY_NORMAL);
  liked = best_for(CRU_FAC_GHOST);
  cru_story_gain(&run, &st, liked);
  n = owned_n();
  EQ(cru_story_choose(&run, &st, CRU_FAC_GHOST, CRU_SAY_OFFER, liked), CRU_REACT_LOVE);
  CHECK(st.gift != CRU_NONE);
  CHECK(cru_owned(&run, st.gift));
  EQ(owned_n(), n); /* one given, one repaid */
}
int main(void) {
  printf("saga\n");
  RUN(factions_notice);
  RUN(choices_move_rivals);
  RUN(gifts);
  RUN(endings);
  RUN(dream);
  RUN(boss_fight);
  RUN(boss_steals);
  RUN(saga_saves);
  RUN(run_course);
  RUN(truths);
  RUN(misses);
  RUN(repaid);
  printf("%d checks, %d failed\n", t_checks, t_fails);
  return t_fails ? 1 : 0;
}
