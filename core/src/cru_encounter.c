/* Encounters (crucible_core.h, "encounters"): talks and fights come to the bench during play, never from a menu.
 *
 * These are the encounter director's rules as the cartridge's crucible_flow.c had them before the fight system
 * (docs/game-flow.md), with the presentation left to each host: after every mix the run's signal decides who
 * comes and how. The cartridge keeps its own director (and does not carry this file): the fight system added rules that
 * need its player record and fight tables (no fight before a DREAM element, the first duel, duelists walking in, the
 * chapter-4 and finale gauntlets, a co-op session keeping its encounters, chapter XP); see game-flow.md, Encounters.
 *   - forced: announced for two seconds ("SOMEONE STEPS IN" / "!! IT COMES !!"), then it comes by itself (approaching
 *     its direction goes at once);
 *   - waiting: a hint at the bench's edge (a talker above, UP; a fighter below, DOWN) until approached, or ignored for
 *     about twenty seconds, when it fades and a story run notices.
 * Pacing, in mixes: a breath of 2 after any encounter; fights at least 6 apart (a hostile faction's champion waits from
 * 6, forces itself in from 12, and loses patience when it already waits); a rival lurks from 8 (certain right after a
 * lost piece would not form); the nemesis that fled returns from 10 (1 in 2); visitors come with the run's visits, forced
 * 1 in 5, and a visit that cannot come yet is owed; when a champion is not due a voice may come instead (5 apart,
 * 1 in 3); two mixes after a new chapter its gatekeeper comes (chapter 1 a voice, chapter 2 on a fight).
 * Free play gets visitors only; nothing comes while linked. No save: the director lives in RAM for the power-on. */
#include "cru_internal.h"

#define FIGHT_WAIT 6u
#define FIGHT_FORCE 12u
#define BREATH 2u

static uint8_t roll(crucible_encounter *e) {
  uint16_t s = e->seed;
  s ^= (uint16_t)(s << 7);
  s ^= (uint16_t)(s >> 9);
  s ^= (uint16_t)(s << 8);
  e->seed = s;
  return (uint8_t)s;
}
static void stir(crucible_encounter *e, uint16_t entropy) {
  e->seed ^= entropy;
  if (!e->seed) e->seed = 0x51f7u;
}
/* A host built without a fight scene may define CRU_ENC_FIGHTS 0: the same rules, with the fight paths compiled out
 * (as CRU_ENC_NO_FIGHTS does at run time). */
#ifndef CRU_ENC_FIGHTS
#define CRU_ENC_FIGHTS 1
#endif
/* 0 free play or linked, 1 a story run, 2 a story run with fights */
static uint8_t run_(const crucible_encounter *e) {
  if ((e->mode & 3u) != CRU_ENC_STORY) return 0;
  return (uint8_t)(!CRU_ENC_FIGHTS || (e->mode & CRU_ENC_NO_FIGHTS) ? 1u : 2u);
}
/* queue kind (forced or waiting); a talk's arg is a visitor's faction, a fight's the champion's (| 0x80 the nemesis) */
static void queue(crucible_encounter *e, uint8_t kind, uint8_t forced, uint8_t arg) {
  if (kind == CRU_ENC_TALK) do
      arg = (uint8_t)(roll(e) & 7u);
    while (arg >= CRU_FACTIONS);
  e->arg = arg;
  e->t = 0;
  e->hint = forced ? CRU_ENC_NONE : kind;
  e->force = forced ? kind : CRU_ENC_NONE;
}
/* a champion of faction f; while the nemesis has not been met three times, it is the one who comes */
static uint8_t champion(const crucible_enc_view *v, uint8_t f) {
  return (uint8_t)((f < CRU_FACTIONS ? f : 0u) | (v->met < 3u ? 0x80u : 0u));
}

void cru_enc_start(crucible_encounter *e, uint8_t mode, uint16_t seed) CORE_BANKED {
  uint8_t i, *p = (uint8_t *)e;
  for (i = 0; i < (uint8_t)sizeof *e; i++) p[i] = 0;
  e->mode = mode;
  e->since = 4u;
  e->fgap = FIGHT_WAIT;
  e->tgap = 4u;
  e->beat = 0xffu;
  e->chapter = 0xffu;
  stir(e, seed);
}

void cru_enc_view(const crucible_story *s, uint8_t lost, crucible_enc_view *v) CORE_BANKED {
  uint8_t f;
  int8_t low = -15, cold = s->stand[0], st;
  v->chapter = s->chapter;
  v->hostile = v->rival = 0xffu;
  v->coldest = 0;
  v->lost = lost;
  v->met = s->nemesis.met;
  v->nemesis = s->nemesis.faction;
  for (f = 0; f < CRU_FACTIONS; f++) {
    st = s->stand[f];
    if (st <= -50 && v->hostile == 0xffu) v->hostile = f;
    if (st <= low) {
      low = st;
      v->rival = f;
    }
    if (st < cold) {
      cold = st;
      v->coldest = f;
    }
  }
}

uint8_t cru_enc_signal(crucible_encounter *e, const crucible_enc_view *v, uint8_t made, uint16_t entropy) CORE_BANKED {
  uint8_t run = run_(e);
  stir(e, entropy);
  if (!made || v->lost || (e->mode & 3u) == CRU_ENC_LINKED) return CRU_ENC_SIG_NONE;
  /* a faction that hates you sends its champion */
  if (run && v->hostile != 0xffu && !(roll(e) & 3u)) return CRU_ENC_SIG_BOSS;
  /* now and then someone steps in while you work: free play from 6 makes, 1 in 4; a run from 3, more often the deeper */
  if (++e->visit >= (run ? 3u : 6u) && (uint8_t)(roll(e) & 3u) <= (run ? (uint8_t)(v->chapter >> 2) : 0u)) {
    e->visit = 0;
    return CRU_ENC_SIG_VISIT;
  }
  return CRU_ENC_SIG_NONE;
}

uint8_t cru_enc_after_mix(crucible_encounter *e, const crucible_enc_view *v, uint8_t r, uint16_t entropy) CORE_BANKED {
  uint8_t run = run_(e), fights = (uint8_t)(run == 2u), *gap;
  stir(e, entropy);
  if (r == CRU_ENC_SIG_LOSS || r == CRU_ENC_SIG_OVER) return r; /* the machine's own lines: at once */
  if ((e->mode & 3u) == CRU_ENC_LINKED) return 0;
  for (gap = &e->since; gap <= &e->tgap; gap++)
    if (*gap != 255u) (*gap)++; /* since, fgap, tgap */
  if (run) {
    if (e->chapter != 0xffu && v->chapter > e->chapter) e->beat = 2u;
    e->chapter = v->chapter;
  } /* its gatekeeper comes two makes later */
  if (r == CRU_ENC_SIG_VISIT)
    e->owe = 1u;
  else if (e->owe && r == CRU_ENC_SIG_NONE)
    r = CRU_ENC_SIG_VISIT; /* owed, not lost */
  if (e->force) return 0;
  if (e->hint) { /* someone already waits; a hostile champion loses patience */
    if (r == CRU_ENC_SIG_BOSS && e->hint == CRU_ENC_FIGHT && e->fgap >= FIGHT_FORCE)
      queue(e, CRU_ENC_FIGHT, 1u, e->arg);
    return 0;
  }
  if (run && e->beat != 0xffu) {
    if (e->beat)
      e->beat--;
    else { /* the chapter beat: from chapter 2 a gatekeeper fights (a rival, else the coldest faction); before, a voice */
      e->beat = 0xffu;
      if (v->chapter >= 2u && fights)
        queue(e, CRU_ENC_FIGHT, 1u, champion(v, v->rival != 0xffu ? v->rival : v->coldest));
      else
        queue(e, CRU_ENC_TALK, 1u, 0);
      return 0;
    }
  }
  if (e->since < BREATH) return 0;
  if (fights) {
    if (r == CRU_ENC_SIG_BOSS && e->fgap >= FIGHT_WAIT) {
      queue(e, CRU_ENC_FIGHT, (uint8_t)(e->fgap >= FIGHT_FORCE), champion(v, v->rival));
      return 0;
    }
    /* the nemesis that fled comes back with what it took */
    if (v->met && v->met < 3u && e->fgap >= 10u && !(roll(e) & 1u)) {
      queue(e, CRU_ENC_FIGHT, 1u, (uint8_t)(v->nemesis | 0x80u));
      return 0;
    }
  }
  if (r == CRU_ENC_SIG_VISIT || (r == CRU_ENC_SIG_BOSS && e->tgap >= 5u && roll(e) < 85u)) {
    e->owe = 0;
    queue(e, CRU_ENC_TALK, (uint8_t)(roll(e) < 52u), 0);
    return 0;
  }
  /* a rival lurks: certain right after a lost piece would not form (it has it) */
  if (fights && e->fgap >= 8u && v->rival != 0xffu && (v->lost || !(roll(e) & 3u)))
    queue(e, CRU_ENC_FIGHT, 0, champion(v, v->rival));
  return 0;
}

uint8_t cru_enc_wait(crucible_encounter *e) CORE_BANKED {
  if (e->hint || e->force || (e->mode & 3u) == CRU_ENC_LINKED) return 0;
  queue(e, CRU_ENC_TALK, 0, 0);
  return 1u;
}

uint8_t cru_enc_tick(crucible_encounter *e, uint8_t dt, uint8_t approach) CORE_BANKED {
  uint8_t k = e->force ? e->force : e->hint;
  if (!k) return 0;
  if (e->force == CRU_ENC_FIGHT && run_(e) != 2u) {
    e->force = CRU_ENC_NONE;
    return 0;
  } /* never a fight outside a run */
  e->t = (uint16_t)(e->t + dt);
  if (approach || (e->force && e->t >= CRU_ENC_TELEGRAPH)) { /* it begins */
    e->hint = e->force = CRU_ENC_NONE;
    e->t = 0;
    e->since = 0;
    e->count++;
    if (k == CRU_ENC_FIGHT)
      e->fgap = 0;
    else {
      e->tgap = 0;
      e->owe = 0;
    }
    return k;
  }
  if (!e->force && e->t >= CRU_ENC_LIFE) {
    e->hint = CRU_ENC_NONE;
    e->t = 0;
    return (uint8_t)(k + 2u);
  } /* ignored: it fades */
  return 0;
}

void cru_enc_ignored(crucible_story *s, uint8_t faded, uint8_t arg) CORE_BANKED {
  uint8_t f = (uint8_t)(arg & 0x7fu);
  if (faded == CRU_ENC_FIGHT_FADED && f < CRU_FACTIONS)
    s->stand[f] = s->stand[f] > -97 ? (int8_t)(s->stand[f] - 3) : (int8_t)-100; /* hiding: the grudge grows */
  else if (faded == CRU_ENC_TALK_FADED)
    cru_story_act(s, CRU_ACT_IDLE); /* a voice ignored: standing still */
}

uint8_t cru_enc_shown(const crucible_encounter *e, uint8_t *flicker) CORE_BANKED {
  if (e->force) {
    *flicker = 1u;
    return e->force;
  }
  *flicker = (uint8_t)(e->hint && e->t >= CRU_ENC_LIFE - CRU_ENC_FADE);
  return e->hint;
}
