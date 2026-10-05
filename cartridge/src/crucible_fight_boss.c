/* The boss engine (docs/fight-system.md 6; the model is tools/fight-sim/bosssim.py). A boss shows two of its
 * hand and a "?", the real recipe's result; you answer from your hand (crucible_fight_rules.c's side 0) before its pips
 * run out. Difficulty comes from information, not HP: veiled strikes show only the ingredients (the "?" leans to the
 * stance both halves share, else to the deeper half: know the recipe or learn the lean), tricks with readable tells
 * that each boss rehearses at half power the first time and then waits three rounds to repeat, phases at a half and a
 * quarter (the newest trick weighs double, then +1 power and one pip fewer), and the room's slow drain.
 *
 * Tiers (6.3): HP 22/30/30/30/28/30, power 2/3/3/3/4/4, pips 4/4/3/3/2/2, veiled 20..100%, a drain every 3 rounds
 * (every 2 from T5), 1..5 tricks. RELIC has 6 HP less from T4 (its ARMOR doubles it), AI 2 less. */
#ifdef FIGHT_HOST
#include <stdint.h>
#include <string.h>
#define BANKED
#define CRU_NONE 0xffffu
uint16_t fr_host_traits(uint16_t id);
uint8_t fr_host_depth(uint16_t id);
uint16_t fr_host_recipe(uint16_t a, uint16_t b);
#define FB_TRAITS(id) fr_host_traits(id)
#define FB_DEPTH(id) fr_host_depth(id)
#define FB_RECIPE(a, b) fr_host_recipe((a), (b))
#else
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_state.h"
uint16_t cri_traits(crucible_core *c, uint16_t id) BANKED;
uint8_t cri_depth(crucible_core *c, uint16_t id) BANKED;
#define FB_TRAITS(id) cri_traits(&core, (id))
#define FB_DEPTH(id) cri_depth(&core, (id))
#define FB_RECIPE(a, b) cru_recipe(&core, (a), (b))
#endif
#include "crucible_fight_rules.h"
#include "crucible_fight_boss.h"
/* hp, power, pips, veil % / 5, drain rounds */
static const uint8_t TIER[6][5] = {{22, 2, 4, 4, 3},  {30, 3, 4, 7, 3},  {30, 3, 3, 11, 3},
                                   {30, 3, 3, 14, 3}, {28, 4, 2, 17, 2}, {30, 4, 2, 20, 2}};
/* each faction's signature, in the order the tiers add them (6.4) */
static const uint8_t SIG[6][5] = {
    {FB_LOOP, FB_DOUBLE, FB_FEINT, FB_SHIELD, FB_COMPILE}, /* PROGRAM: rigid, loops, compiles you */
    {FB_CHARGE, FB_DOUBLE, FB_STEAL, FB_FEINT, FB_OVERHEAT}, /* DAEMON: hot, impatient, all-in */
    {FB_STEAL, FB_FEINT, FB_HAUNT, FB_CHARGE, FB_POSSESS}, /* GHOST: takes, lies, hides */
    {FB_MIRROR, FB_ADAPT, FB_SHIELD, FB_FEINT, FB_PREDICT}, /* AI: watches and adapts */
    {FB_GROW, FB_STEAL, FB_DOUBLE, FB_ADAPT, FB_JACK}, /* OPERATOR: grows, unplugs */
    {FB_ARMOR, FB_SHIELD, FB_CHARGE, FB_HAUNT, FB_ERODE}}; /* RELIC: slow, armoured, wears you down */
static const uint8_t HOME[6] = {FR_WALL, FR_HUM, FR_DREAM, FR_WALL, FR_DREAM, FR_WALL}; /* CAT_FAM of its category */
static const uint8_t BIT8B[8] = {1, 2, 4, 8, 16, 32, 64, 128};
static const uint16_t BEATSB[12] = {0x006u, 0x041u, 0x102u, 0x410u, 0x104u, 0x014u,
                                    0x410u, 0x801u, 0x003u, 0x804u, 0xA00u, 0x240u};
static const uint16_t BIT16B[12] = {1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u, 256u, 512u, 1024u, 2048u};
static uint16_t bmask(uint16_t t) {
  uint8_t k;
  uint16_t m = 0;
  for (k = 0; k < 12u; k++)
    if (t & BEATSB[k]) m |= BIT16B[k];
  return m;
}

uint8_t fb_beater(uint8_t s) BANKED { return (uint8_t)(s >= 2u ? 0u : s + 1u); }
static uint8_t rnd(void) {
  uint16_t x = fr.ai;
  x ^= (uint16_t)(x << 7);
  x ^= (uint16_t)(x >> 9);
  x ^= (uint16_t)(x << 8);
  fr.ai = x;
  return (uint8_t)x;
}
static uint8_t below(uint8_t n) { return (uint8_t)(((uint16_t)rnd() * n) >> 8); }
static uint8_t has(uint8_t kind) {
  fb_state *b = FB;
  uint8_t i;
  for (i = 0; i < b->nmoves; i++)
    if (b->moves[i] == kind) return 1;
  return 0;
}
static uint8_t seen(uint8_t kind) { return (FB->seen[kind >> 3] & BIT8B[kind & 7u]) ? 1u : 0u; }

void fb_begin(uint8_t fac, uint8_t tier, uint8_t memory, int8_t pip_adj) BANKED {
  fb_state *b = FB;
  uint8_t i, j, t;
  memset(b, 0, sizeof *b);
  if (fac > 5u) fac = 0;
  if (tier < 1u) tier = 1;
  if (tier > 6u) tier = 6;
  b->fac = fac;
  b->tier = tier;
  b->hp = TIER[tier - 1u][0];
  if (fac == 5u && tier >= 4u) b->hp = (uint8_t)(b->hp - 6u); /* RELIC: armour already doubles its HP */
  if (fac == 3u && tier >= 4u) b->hp = (uint8_t)(b->hp - 2u);
  b->max = b->hp;
  b->pow = TIER[tier - 1u][1];
  b->pips = (uint8_t)((int8_t)TIER[tier - 1u][2] + pip_adj);
  if (b->pips < 1u || b->pips > 6u) b->pips = 1;
  b->veil = (uint8_t)(TIER[tier - 1u][3] * 5u);
  b->chip = TIER[tier - 1u][4];
  b->nmoves = tier < 5u ? tier : 5u;
  for (i = 0; i < b->nmoves; i++) b->moves[i] = SIG[fac][i];
  b->home = HOME[fac];
  b->loop[0] = b->home;
  b->loop[1] = fb_beater(b->home);
  b->loop[2] = fb_beater(b->loop[1]);
  for (i = 2; i; i--) {
    j = below((uint8_t)(i + 1u));
    t = b->loop[i];
    b->loop[i] = b->loop[j];
    b->loop[j] = t;
  }
  for (i = 0; i < FB_KINDS; i++) b->cool[i] = 0xf0u;
  b->shield = b->memory = FR_NONE;
  if (memory < 3u) b->memory = memory;
  b->a = b->b = b->atk = CRU_NONE;
  b->stolen[0] = b->stolen[1] = 0xffu;
  fr.solo = 1;
}
/* The attack: it wants a stance (its home twice as often as either other: bosssim's lean) and combines the pair of its
 * hand whose result has it, from a seeded start; else any pair that makes something; else it throws one of its own raw.
 * The ten pairs' results are looked up once a fight, one a call; so the "?" is always the real recipe's result and its
 * stance is that result's (the veil can be read by knowing the recipe or the lean). */
static const uint8_t PA[10] = {0, 0, 0, 0, 1, 1, 1, 2, 2, 3}, PB[10] = {0, 1, 2, 3, 1, 2, 3, 2, 3, 3};
uint8_t fb_plan_step(const uint16_t *hand) BANKED {
  fb_state *b = FB;
  uint8_t k, x, start, pick = 0xffu, any = 0xffu;
  if (b->known < 10u) {
    b->prod[b->known] = FB_RECIPE(hand[PA[b->known]], hand[PB[b->known]]);
    b->known++;
    return 0;
  }
  b->target = below(4);
  b->target = b->target < 2u ? b->home : b->target == 2u ? fb_beater(b->home) : fb_beater(fb_beater(b->home));
  start = below(10);
  for (k = 0; k < 10u; k++) {
    x = (uint8_t)(start + k);
    if (x >= 10u) x = (uint8_t)(x - 10u);
    if (b->prod[x] == CRU_NONE) continue;
    if (any == 0xffu) any = x;
    if (fr_stance(b->prod[x]) & BIT8B[b->target]) {
      pick = x;
      break;
    }
  }
  if (pick != 0xffu) {
    b->a = hand[PA[pick]];
    b->b = hand[PB[pick]];
    b->atk = b->prod[pick];
  } else { /* nothing it makes has that stance: one of its own thrown raw, of that stance if it holds one */
    for (k = 0; k < 4u; k++)
      if (fr_stance(hand[k]) & BIT8B[b->target]) break;
    if (k < 4u) {
      b->a = b->b = b->atk = hand[k];
    } else if (any != 0xffu) {
      b->a = hand[PA[any]];
      b->b = hand[PB[any]];
      b->atk = b->prod[any];
    } else {
      b->a = b->b = b->atk = hand[rnd() & 3u];
    }
  }
  b->atk_tr = FB_TRAITS(b->atk);
  b->atk_bt = bmask(b->atk_tr);
  return 1;
}
static uint8_t most_of(void) { /* the stance the player played most in its last four */
  const fr_side *p = &fr.s[0];
  uint8_t c[3] = {0, 0, 0}, i, best = 0;
  for (i = 0; i < p->nhist; i++)
    if (p->hist[i] < 3u) c[p->hist[i]]++;
  for (i = 1; i < 3u; i++)
    if (c[i] > c[best]) best = i; /* first of equals, as Counter.most_common */
  if (p->nhist) {
    for (i = 0; i < p->nhist; i++)
      if (c[p->hist[i]] == c[best]) {
        best = p->hist[i];
        break;
      }
  }
  return best;
}
/* stance from the attack's own traits: its family, the boss's home when the attack is a dual that has it */
/* the attack's stance: the one it wanted (bosssim: its home twice as often as either other), which its result has
 * whenever any of its combinations or its own hand allowed */
static uint8_t atk_stance(void) { return FB->target; }
void fb_round(void) BANKED {
  fb_state *b = FB;
  const fr_side *p = &fr.s[0];
  uint8_t ms[6], nm = b->nmoves, i, kind, st, real, pw, n = 1;
  b->t++;
  for (i = 0; i < nm; i++) ms[i] = b->moves[i];
  if (b->phase >= 1u && b->tier >= 3u) ms[nm++] = b->moves[b->nmoves - 1u]; /* a phase leans on its newest trick */
  kind = below(100) < (uint8_t)(45u - 5u * b->tier) ? FB_STRIKE : ms[below(nm)];
  if (kind != FB_STRIKE && (uint8_t)(b->t - b->cool[kind]) <= 2u) kind = FB_STRIKE; /* every trick waits three rounds */
  if (kind != FB_STRIKE) b->cool[kind] = b->t;
  st = atk_stance();
  if (has(FB_LOOP) || has(FB_COMPILE)) {
    st = b->loop[b->li];
    b->li = (uint8_t)(b->li >= 2u ? 0u : b->li + 1u);
  }
  if (b->memory != FR_NONE && b->t <= 4u) {
    st = fb_beater(b->memory);
    kind = FB_RECALL;
  } /* the nemesis opens on what beats your best */
  if (b->memory != FR_NONE && b->t <= 5u) {
    b->shield = b->memory;
    b->shield_t = 1;
  }
  real = st;
  pw = (uint8_t)(b->pow + (b->phase >= 2u ? 1u : 0u));
  if (kind == FB_FEINT) real = fb_beater(fb_beater(st)); /* shown st; the real one beats your natural answer */
  if (kind == FB_CHARGE && !b->charged) {
    b->charged = 1;
    b->kind = FB_CHARGE;
    b->shown = b->real = FR_NONE;
    b->pw = 0;
    b->n = 0;
    b->demo = 0;
    b->veiled = 0;
    return;
  }
  if (b->charged) {
    pw = (uint8_t)(pw + pw);
    b->charged = 0;
  }
  if (kind == FB_DOUBLE || kind == FB_OVERHEAT) n = 2;
  if (kind == FB_OVERHEAT) pw++;
  if (kind == FB_MIRROR && p->last < 3u) st = real = p->last;
  if (kind == FB_COMPILE && p->nhist) {
    st = real = fb_beater(p->hist[p->nhist - 1u]);
  }
  if ((kind == FB_ADAPT || kind == FB_PREDICT) && p->nhist) st = real = fb_beater(most_of());
  if (kind == FB_SHIELD && b->shield == FR_NONE) {
    b->shield = p->last < 3u ? p->last : FR_HUM;
    b->shield_t = 2;
  }
  if (kind == FB_ERODE) b->erode = 1;
  if (kind == FB_POSSESS && p->nhand) {
    b->possess_slot = p->hand[below(p->nhand)];
    b->possess_t = 2;
  }
  if (kind == FB_JACK) b->jack = 1;
  b->demo = 0;
  if (kind != FB_STRIKE && kind != FB_RECALL && !seen(kind)) {
    b->seen[kind >> 3] |= BIT8B[kind & 7u];
    b->demo = 1;
    pw = (uint8_t)(pw >> 1);
    if (!pw) pw = 1;
  } /* rehearsed at half power */
  b->veiled = kind == FB_HAUNT || (kind == FB_STRIKE && below(100) < b->veil);
  b->kind = kind;
  b->shown = st;
  b->real = real;
  b->pw = pw;
  b->n = n;
}
static void hurt(int8_t d) {
  fr_side *p = &fr.s[0];
  int16_t v = (int16_t)p->hp - d;
  p->hp = (int8_t)(v < -100 ? -100 : v);
  if (d > 0) p->lost_hp = (uint8_t)(p->lost_hp + (uint8_t)d);
}
static void boss_hurt(uint8_t d) {
  fb_state *b = FB;
  b->hp = d >= b->hp ? 0u : (uint8_t)(b->hp - d);
}
static uint8_t edge(uint16_t bt, uint16_t tr) { return (bt & tr) ? 1u : 0u; }
static void discard_hand(uint8_t i) { /* the card the round used goes to the discard (or is taken: STEAL) */
  fr_side *p = &fr.s[0];
  uint8_t s = p->hand[i];
  for (; (uint8_t)(i + 1u) < p->nhand; i++) p->hand[i] = p->hand[i + 1u];
  p->nhand--;
  p->disc[p->ndisc++] = s;
  p->dmask |= BIT8B[s];
}
/* STEAL (bosssim's rule): what it won is played out as usual, but its copies still in your deck are gone for the fight */
static void steal_copies(uint8_t s) {
  fr_side *p = &fr.s[0];
  uint8_t i, j = 0;
  for (i = 0; i < p->ndeck; i++)
    if (p->kit[p->deck[i]] != p->kit[s]) p->deck[j++] = p->deck[i];
  p->ndeck = j;
}
static void drop_hand(uint8_t i) { /* JACK: a card unplugged, out of the fight */
  fr_side *p = &fr.s[0];
  for (; (uint8_t)(i + 1u) < p->nhand; i++) p->hand[i] = p->hand[i + 1u];
  p->nhand--;
}
static void note(fr_side *p, uint8_t st) { /* the player's habits, for MIRROR, ADAPT, PREDICT and COMPILE */
  if (p->nhist < 4u)
    p->hist[p->nhist++] = st;
  else {
    p->hist[0] = p->hist[1];
    p->hist[1] = p->hist[2];
    p->hist[2] = p->hist[3];
    p->hist[3] = st;
  }
  p->last = st;
}
uint8_t fb_resolve(const uint8_t *picks, const uint8_t *close) BANKED {
  fb_state *b = FB;
  fr_side *p = &fr.s[0];
  fr_cardv c;
  uint8_t k, out = 0, used[2], nused = 0, d, was = b->phase, st, steal[2], nsteal = 0;
  if (b->kind == FB_CHARGE) { /* a free round: what you play hits it (ARMOR lets one through) */
    if (picks[0] != FB_MISS && fr_card(0, picks[0], &c)) {
      d = c.pw;
      if (has(FB_ARMOR) && d > 1u) d = 1;
      boss_hurt(d);
      out |= FBO_FREE | FBO_HIT;
      if (c.st < 3u) b->dealt[c.st] = (uint8_t)(b->dealt[c.st] + d);
      if (FR_KIND(picks[0]) == FR_FUSE) {
        discard_hand(FR_B(picks[0]));
        discard_hand(FR_A(picks[0]));
        if (p->focus >= 2u) p->focus = (uint8_t)(p->focus - 2u);
        b->fused++;
      } else
        discard_hand(FR_A(picks[0]));
    }
    return out; /* a charging round is all it does (bosssim: no drain, no growth, no phase this round) */
  } else
    for (k = 0; k < b->n; k++) {
      uint8_t a = picks[k];
      if (a == FB_MISS) {
        hurt((int8_t)(b->pw + 1u));
        out |= FBO_HURT;
        continue;
      } /* too late: it lands */
      if (a == FB_PASS) continue;
      if (FR_KIND(a) == FR_GUARD) {
        hurt((int8_t)((b->pw + 1u) >> 1));
        if (p->focus < 3u) p->focus++;
        b->guards++;
        out |= FBO_GUARD;
        continue;
      }
      if (!fr_card(0, a, &c)) continue;
      if (FR_KIND(a) == FR_PLAY) c.pw = p->kpw[c.slot]; /* against a boss a card hits with its plain power (bosssim) */
      st = c.st;
      if (b->possess_t && c.slot == b->possess_slot)
        st = fb_beater(fb_beater(st)); /* POSSESS: that card is not yours this round */
      if (b->shield != FR_NONE && st == b->shield)
        out |= FBO_SHIELDED;
      else if (st == fb_beater(b->real)) {
        d = (uint8_t)(c.pw + edge(c.bt, b->atk_tr) + (close[k] ? 1u : 0u));
        if (has(FB_ARMOR) && !close[k] && FR_KIND(a) != FR_FUSE && d > 3u)
          d = 3; /* ARMOR: 3 at most, unless a last-pip or fused blow */
        boss_hurt(d);
        b->dealt[st] = (uint8_t)(b->dealt[st] + d);
        out |= FBO_HIT;
        if (close[k]) b->close++;
        if (FR_KIND(a) == FR_FUSE) b->fused++;
      } else if (st == b->real) {
        boss_hurt(edge(c.bt, b->atk_tr));
        hurt((int8_t)edge(b->atk_bt, c.tr));
        out |= FBO_TIE;
      } else {
        hurt((int8_t)(b->pw + edge(b->atk_bt, c.tr)));
        out |= FBO_HURT;
        if (b->kind == FB_STEAL && FR_KIND(a) == FR_PLAY) {
          steal_copies(c.slot);
          if (b->nstolen < 2u) b->stolen[b->nstolen++] = c.slot;
          out |= FBO_STOLE;
        }
      }
      note(p, st);
      if (FR_KIND(a) == FR_FUSE) {
        used[nused++] = FR_B(a);
        used[nused++] = FR_A(a);
        if (p->focus >= 2u) p->focus = (uint8_t)(p->focus - 2u);
        break;
      }
      used[nused++] = FR_A(a);
    }
  /* the cards the round used leave the hand for the discard, highest index first */
  while (nused) {
    uint8_t hi = 0, x;
    for (x = 1; x < nused; x++)
      if (used[x] > used[hi]) hi = x;
    k = used[hi];
    used[hi] = used[--nused];
    discard_hand(k);
  }
  (void)steal;
  (void)nsteal;
  if (b->chip && b->t % b->chip == 0u) hurt(1); /* the room hums: stalling loses */
  if (b->kind == FB_GROW && b->hp) {
    if (b->hp < b->max) b->hp++;
  }
  if (b->jack && p->nhand) {
    drop_hand((uint8_t)(p->nhand - 1u));
    b->jack = 0;
  } /* JACK unplugs a card for the fight */
  if (b->shield_t) b->shield_t--;
  if (!b->shield_t) b->shield = FR_NONE;
  if (b->possess_t) b->possess_t--;
  if (b->hp <= (b->max >> 2))
    b->phase = 2;
  else if (b->hp <= (b->max >> 1))
    b->phase = 1;
  if (b->phase != was) out |= FBO_PHASE;
  return out;
}
uint8_t fb_over(void) BANKED {
  if (!FB->hp) return 1;
  if (fr.s[0].hp <= 0) return 2;
  return 0;
}
uint8_t fb_best_stance(void) BANKED {
  fb_state *b = FB;
  uint8_t i, best = FR_NONE, m = 0;
  for (i = 0; i < 3u; i++)
    if (b->dealt[i] > m) {
      m = b->dealt[i];
      best = i;
    }
  return best;
}
