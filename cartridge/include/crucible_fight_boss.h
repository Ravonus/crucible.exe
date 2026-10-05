#ifndef CRUCIBLE_FIGHT_BOSS_H
#define CRUCIBLE_FIGHT_BOSS_H
/* Bosses (crucible_fight_boss.c; docs/fight-system.md 6): a faction's champion telegraphs what it combines
 * (two of its hand and the real recipe's "?"), and you answer from your hand. A boss is (faction, tier, seed): its
 * moves are the first min(tier, 5) of its faction's signature, its loop and favourite trick come from the seed. */
#include "crucible_fight_rules.h"
#define FB_STRIKE 0u
#define FB_LOOP 1u
#define FB_DOUBLE 2u
#define FB_FEINT 3u
#define FB_SHIELD 4u
#define FB_COMPILE 5u
#define FB_CHARGE 6u
#define FB_STEAL 7u
#define FB_OVERHEAT 8u
#define FB_HAUNT 9u
#define FB_POSSESS 10u
#define FB_MIRROR 11u
#define FB_ADAPT 12u
#define FB_PREDICT 13u
#define FB_GROW 14u
#define FB_JACK 15u
#define FB_ARMOR 16u
#define FB_ERODE 17u
#define FB_RECALL 18u
#define FB_KINDS 19u
#define FB_MISS 0xfeu /* a pick that never came: the pips ran out */
#define FB_PASS 0xfdu /* a pick that does nothing (a slip onto a card already used this round) */
/* outcome bits of a round */
#define FBO_HIT 1u /* you landed a blow */
#define FBO_HURT 2u /* it landed one */
#define FBO_TIE 4u
#define FBO_GUARD 8u
#define FBO_SHIELDED 16u /* your answer met its shield */
#define FBO_PHASE 32u /* it changed */
#define FBO_STOLE 64u
#define FBO_FREE 128u /* a charging round: your card hit freely */
typedef struct {
  uint8_t fac, tier, hp, max, pow, pips, chip, veil, home, li, loop[3], moves[5], nmoves;
  uint8_t charged, shield, shield_t, erode, phase, memory, t, seen[3], cool[FB_KINDS];
  /* the round in play */
  uint8_t kind, shown, real, pw, n, demo, veiled, possess_t, possess_slot, jack;
  uint16_t a, b, atk, atk_tr, atk_bt, possess_id; /* the two it combines, the attack (the recipe's result) */
  uint8_t dealt[3], fused, close, guards; /* how you beat it (the nemesis remembers) */
  uint8_t stolen[2], nstolen; /* kit slots it took for the fight */
  uint8_t target, plan_k, pending; /* the attack being planned: the stance it wants, the next pair */
  uint16_t prod[10]; /* what each pair of its hand makes (CRU_NONE: nothing), looked up once */
  uint8_t known; /* how many of prod[] are looked up */
} fb_state;
#define FB ((fb_state *)&fr.s[1]) /* a boss fight has one card side: the boss lives in the other's room */
void fb_begin(uint8_t fac, uint8_t tier, uint8_t memory, int8_t pip_adj) BANKED;
uint8_t fb_plan_step(const uint16_t *hand)
    BANKED; /* plans the attack (its hand's recipes are looked up once, one a call): 1 when planned */
void fb_round(void) BANKED; /* after the plan: this round's move, the stance it shows and the real one */
uint8_t fb_resolve(const uint8_t *picks,
                   const uint8_t *close) BANKED; /* picks: actions (FB_MISS none); returns FBO_* */
uint8_t fb_over(void) BANKED; /* 0 going, 1 it broke, 2 you fell */
uint8_t fb_beater(uint8_t s) BANKED;
uint8_t fb_best_stance(void) BANKED; /* the stance you dealt it the most with (FR_NONE: none) */
#endif
