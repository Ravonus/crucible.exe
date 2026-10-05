/* Encounters (src/cru_encounter.c): the encounter director's rules, as the cartridge's crucible_flow.c and
 * docs/game-flow.md give them. Its own binary (build.sh test): free play gets visitors only; nothing while
 * linked; the breath after an encounter; champions wait from 6 and force from 12; rivals from 8 (certain after a lost
 * piece); the nemesis from 10; chapter gatekeepers (a voice in chapter 1, a fight from chapter 2); owed visits; hints
 * that are approached, announced ones that come by themselves, ignored ones that fade and the run that notices; a
 * machine without fights. */
#include <string.h>
#include "harness.h"

static crucible_enc_view calm(void) {
  crucible_enc_view v;
  memset(&v, 0, sizeof v);
  v.hostile = v.rival = 0xffu;
  return v;
}

/* mixes until something is queued (waiting or forced), at most n */
static int until_queued(crucible_encounter *e, const crucible_enc_view *v, uint8_t signal, int n) {
  int i;
  for (i = 1; i <= n; i++) {
    cru_enc_after_mix(e, v, signal, (uint16_t)(i * 77));
    if (e->hint || e->force) return i;
  }
  return 0;
}

static void free_play_visitors_only(void) {
  crucible_encounter e;
  crucible_enc_view v = calm();
  int i, visits = 0, fights = 0;
  v.hostile = 0;
  v.rival = 0;
  v.met = 1; /* everything hostile: free play still never fights */
  cru_enc_start(&e, CRU_ENC_FREE, 0x1234u);
  for (i = 0; i < 400; i++) {
    uint8_t s = cru_enc_signal(&e, &v, 1u, (uint16_t)i);
    CHECK(s == CRU_ENC_SIG_NONE || s == CRU_ENC_SIG_VISIT);
    cru_enc_after_mix(&e, &v, s, (uint16_t)i);
    if (e.hint == CRU_ENC_FIGHT || e.force == CRU_ENC_FIGHT) fights++;
    if (e.hint == CRU_ENC_TALK || e.force == CRU_ENC_TALK) {
      visits++;
      CHECK(e.arg < CRU_FACTIONS);
      EQ(cru_enc_tick(&e, 1, 1u), CRU_ENC_TALK);
    }
  }
  EQ(fights, 0);
  CHECK(visits >= 20 && visits <= 100); /* a visitor every 6 to 10 makes or so */
  /* a forced fight can never begin outside a run */
  e.force = CRU_ENC_FIGHT;
  EQ(cru_enc_tick(&e, 200, 1u), 0);
  EQ(e.force, 0);
}

static void nothing_while_linked(void) {
  crucible_encounter e;
  crucible_enc_view v = calm();
  int i;
  cru_enc_start(&e, CRU_ENC_LINKED, 7u);
  for (i = 0; i < 100; i++) {
    EQ(cru_enc_signal(&e, &v, 1u, (uint16_t)i), 0);
    cru_enc_after_mix(&e, &v, CRU_ENC_SIG_VISIT, 1u);
  }
  EQ(e.hint, 0);
  EQ(e.force, 0);
  EQ(cru_enc_wait(&e), 0);
  EQ(cru_enc_after_mix(&e, &v, CRU_ENC_SIG_OVER, 1u), CRU_ENC_SIG_OVER);
}

static void visits_and_breath(void) {
  crucible_encounter e;
  crucible_enc_view v = calm();
  int forced = 0, waited = 0, i;
  cru_enc_start(&e, CRU_ENC_STORY, 99u);
  cru_enc_after_mix(&e, &v, CRU_ENC_SIG_VISIT, 1u);
  CHECK(e.hint == CRU_ENC_TALK || e.force == CRU_ENC_TALK);
  EQ(cru_enc_tick(&e, 1, 1u), CRU_ENC_TALK);
  EQ(e.since, 0);
  EQ(e.count, 1);
  /* a breath of two makes: the visit right after is owed, and comes once the breath is over */
  cru_enc_after_mix(&e, &v, CRU_ENC_SIG_VISIT, 2u);
  EQ(e.hint | e.force, 0);
  EQ(e.owe, 1);
  cru_enc_after_mix(&e, &v, CRU_ENC_SIG_NONE, 3u);
  CHECK(e.hint == CRU_ENC_TALK || e.force == CRU_ENC_TALK);
  EQ(e.owe, 0);
  /* forced about 1 in 5 */
  for (i = 0; i < 500; i++) {
    cru_enc_start(&e, CRU_ENC_STORY, (uint16_t)(i * 131u + 1u));
    cru_enc_after_mix(&e, &v, CRU_ENC_SIG_VISIT, (uint16_t)i);
    if (e.force == CRU_ENC_TALK)
      forced++;
    else if (e.hint == CRU_ENC_TALK)
      waited++;
  }
  EQ(forced + waited, 500);
  CHECK(forced > 60 && forced < 150);
}

static void champions_wait_then_force(void) {
  crucible_encounter e;
  crucible_enc_view v = calm();
  int i;
  v.hostile = 2;
  v.rival = 2;
  v.coldest = 2;
  v.met = 3;
  cru_enc_start(&e, CRU_ENC_STORY, 5u);
  e.fgap = 0; /* just fought */
  for (i = 0; i < 5; i++) {
    cru_enc_after_mix(&e, &v, CRU_ENC_SIG_BOSS, (uint16_t)i);
    CHECK(e.hint != CRU_ENC_FIGHT && e.force != CRU_ENC_FIGHT);
    if (e.hint || e.force) cru_enc_tick(&e, 1, 1u);
  }
  /* 6 makes since the last fight: it waits below (if no voice came instead) */
  e.hint = e.force = 0;
  e.fgap = 5;
  e.since = 5;
  cru_enc_after_mix(&e, &v, CRU_ENC_SIG_BOSS, 1u);
  EQ(e.hint, CRU_ENC_FIGHT);
  EQ(e.arg, 2);
  /* still waiting 12 makes on, the faction still hostile: it loses patience */
  for (i = 0; i < 5; i++) cru_enc_after_mix(&e, &v, CRU_ENC_SIG_BOSS, 9u);
  EQ(e.hint, CRU_ENC_FIGHT);
  EQ(e.force, 0);
  cru_enc_after_mix(&e, &v, CRU_ENC_SIG_BOSS, 9u);
  EQ(e.force, CRU_ENC_FIGHT);
  /* a champion due at 12+ comes forced straight away; the nemesis, not yet met three times, is the one */
  cru_enc_start(&e, CRU_ENC_STORY, 5u);
  v.met = 0;
  e.fgap = 11;
  e.since = 9;
  cru_enc_after_mix(&e, &v, CRU_ENC_SIG_BOSS, 1u);
  EQ(e.force, CRU_ENC_FIGHT);
  EQ(e.arg, 0x82);
}

static void rivals_and_nemesis(void) {
  crucible_encounter e;
  crucible_enc_view v = calm();
  int n;
  v.rival = 4;
  v.coldest = 4;
  v.met = 3;
  cru_enc_start(&e, CRU_ENC_STORY, 11u);
  e.fgap = 0;
  /* a rival lurks from 8 makes, and always right after a lost piece would not form */
  v.lost = 1;
  n = until_queued(&e, &v, CRU_ENC_SIG_NONE, 40);
  EQ(n, 8);
  EQ(e.hint, CRU_ENC_FIGHT);
  EQ(e.arg, 4);
  /* the nemesis that fled returns from 10 makes, forced */
  cru_enc_start(&e, CRU_ENC_STORY, 13u);
  e.fgap = 0;
  v = calm();
  v.met = 1;
  v.nemesis = 3;
  n = until_queued(&e, &v, CRU_ENC_SIG_NONE, 60);
  CHECK(n >= 10);
  EQ(e.force, CRU_ENC_FIGHT);
  EQ(e.arg, 0x83);
}

static void chapter_gatekeepers(void) {
  crucible_encounter e;
  crucible_enc_view v = calm();
  int n;
  cru_enc_start(&e, CRU_ENC_STORY, 21u);
  cru_enc_after_mix(&e, &v, CRU_ENC_SIG_NONE, 1u); /* chapter 0 seen */
  v.chapter = 1;
  n = until_queued(&e, &v, CRU_ENC_SIG_NONE, 10);
  EQ(n, 3);
  EQ(e.force, CRU_ENC_TALK); /* chapter 1: a voice steps in */
  cru_enc_tick(&e, 1, 1u);
  v.chapter = 2;
  v.coldest = 5;
  n = until_queued(&e, &v, CRU_ENC_SIG_NONE, 10);
  EQ(n, 3);
  EQ(e.force, CRU_ENC_FIGHT);
  EQ(e.arg & 0x7f, 5); /* chapter 2: the gatekeeper (no rival: the coldest) */
  EQ(cru_enc_tick(&e, 60, 0), 0);
  EQ(cru_enc_tick(&e, 60, 0), CRU_ENC_FIGHT); /* it comes by itself after two seconds */
  /* a machine without a fight scene: the gatekeeper is a voice, and no fight ever queues */
  cru_enc_start(&e, CRU_ENC_STORY | CRU_ENC_NO_FIGHTS, 21u);
  cru_enc_after_mix(&e, &v, CRU_ENC_SIG_NONE, 1u);
  v.chapter = 3;
  v.rival = 1;
  v.hostile = 1;
  v.met = 1;
  n = until_queued(&e, &v, CRU_ENC_SIG_NONE, 10);
  EQ(n, 3);
  EQ(e.force, CRU_ENC_TALK);
  for (n = 0; n < 200; n++) {
    cru_enc_tick(&e, 200, 1u);
    cru_enc_after_mix(&e, &v, CRU_ENC_SIG_BOSS, (uint16_t)n);
    CHECK(e.hint != CRU_ENC_FIGHT && e.force != CRU_ENC_FIGHT);
  }
}

static void hints_fade_and_the_run_notices(void) {
  crucible_encounter e;
  crucible_enc_view v = calm();
  crucible_story s;
  uint8_t fl = 0, r = 0;
  unsigned t;
  memset(&s, 0, sizeof s);
  s.stand[3] = -20;
  cru_enc_start(&e, CRU_ENC_STORY, 3u);
  e.hint = CRU_ENC_FIGHT;
  e.arg = 3;
  EQ(cru_enc_shown(&e, &fl), CRU_ENC_FIGHT);
  EQ(fl, 0);
  for (t = 0; t < CRU_ENC_LIFE - 8u; t += 8u) EQ(cru_enc_tick(&e, 8, 0), 0);
  EQ(cru_enc_shown(&e, &fl), CRU_ENC_FIGHT);
  EQ(fl, 1); /* its last seconds flicker */
  for (t = 0; t < 16u && !r; t++) r = cru_enc_tick(&e, 8, 0);
  EQ(r, CRU_ENC_FIGHT_FADED);
  EQ(e.hint, 0);
  EQ(cru_enc_shown(&e, &fl), 0);
  cru_enc_ignored(&s, r, e.arg);
  EQ(s.stand[3], -23); /* the grudge grows */
  /* approached: it begins, and the counts reset */
  cru_enc_start(&e, CRU_ENC_STORY, 3u);
  e.hint = CRU_ENC_TALK;
  e.tgap = 9;
  EQ(cru_enc_tick(&e, 1, 1u), CRU_ENC_TALK);
  EQ(e.tgap, 0);
  EQ(e.count, 1);
  /* the clock: someone waits above, once */
  EQ(cru_enc_wait(&e), 1);
  EQ(e.hint, CRU_ENC_TALK);
  EQ(cru_enc_wait(&e), 0);
  (void)v;
}

int main(void) {
  printf("cru_encounter\n");
  RUN(free_play_visitors_only);
  RUN(nothing_while_linked);
  RUN(visits_and_breath);
  RUN(champions_wait_then_force);
  RUN(rivals_and_nemesis);
  RUN(chapter_gatekeepers);
  RUN(hints_fade_and_the_run_notices);
  printf("%d checks, %d failed\n", t_checks, t_fails);
  return t_fails != 0;
}
