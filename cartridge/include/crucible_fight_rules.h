#ifndef CRUCIBLE_FIGHT_RULES_H
#define CRUCIBLE_FIGHT_RULES_H
/* The fight engine (crucible_fight_rules.c): the triangle, the kit, draws, clashes, passives, arenas and the duel AI
 * (docs/fight-system.md 1-5). Pure logic over a fr_state; deterministic from its seed and the two sides'
 * action bytes, so the link plays it in lockstep. Host-buildable (FIGHT_HOST) and checked turn by turn against the
 * simulator's reference (tools/fight-sim/fightref.py). */
#ifdef FIGHT_HOST
#include <stdint.h>
#ifndef BANKED
#define BANKED
#endif
#else
#include <gb/gb.h>
#endif
#define FR_HUM 0u
#define FR_WALL 1u
#define FR_DREAM 2u
#define FR_NONE 3u /* no stance (a guard, or nothing yet) */
/* action bytes (the P_FTURN layout): kind:2 | hand slot a:2 | hand slot b:2 | stance:2 */
#define FR_PLAY 0u
#define FR_FUSE 1u
#define FR_GUARD 2u
#define FR_ACT(k, a, b, st) ((uint8_t)(((k) << 6) | ((a) << 4) | ((b) << 2) | (st)))
#define FR_KIND(x) ((uint8_t)((x) >> 6))
#define FR_A(x) ((uint8_t)(((x) >> 4) & 3u))
#define FR_B(x) ((uint8_t)(((x) >> 2) & 3u))
#define FR_ST(x) ((uint8_t)((x) & 3u))
#define FR_GUARD_ACT 0x80u
#define FR_KIT 6u
#define FR_HAND 4u
#define FR_PRODS 2u
#define FR_PAIRS 6u
/* passives (section 4), the simulator's order */
#define FP_ECHO 0u
#define FP_PRISM 1u
#define FP_SCAR 2u
#define FP_VIGIL 3u
#define FP_CATALYST 4u
#define FP_STALEMATE 5u
#define FP_SALVE 6u
#define FP_UNDERTOW 7u
#define FP_LUCID 8u
#define FP_NOCLIP 9u
#define FP_OVERCLOCK 10u
#define FP_MEMORY 11u
#define FP_COUNT 12u
#define FP_NONE 0xffu
/* arenas (section 3.1): the room family % 8 */
#define FA_HUM 0u /* HUMMING LIGHTS: HUM wins +1 */
#define FA_WALL 1u /* DAMP CARPET: WALL wins +1 */
#define FA_DREAM 2u /* STILL AIR: DREAM wins +1 */
#define FA_FLICKER 3u /* every 3rd turn both hands show as ? */
#define FA_NARROW 4u /* guards take full damage */
#define FA_ECHO 5u /* stack bonus +1 more */
#define FA_EXIT 6u /* ties go to the lower HP */
#define FA_EMPTY 7u
/* rule toggles (fr_state.rules) */
#define FR_R_NOSTACK 1u
#define FR_R_SUDDEN 2u /* from turn 15 every hit +1 */
#define FR_R_NIGHT 4u /* focus max +1 */
#define FR_R_NOPAS 8u
#define FR_R_FOG 16u /* hands hidden */
#define FR_R_INVERT 32u /* GLITCH: the triangle reverses from turn 4 */
#define FR_TURNS 30u
typedef struct {
  uint16_t id, tr, bt;
  uint8_t st, pw, slot;
} fr_cardv; /* a card in play: element, traits, the traits
                                                                           * those beat, stance, power, kit slot (0xff fused) */
typedef struct {
  uint16_t kit[FR_KIT], ktr[FR_KIT], kbt[FR_KIT]; /* elements, their traits, the traits those beat (BEATS[]) */
  uint8_t kst[FR_KIT], kpw[FR_KIT],
      kgrp[FR_KIT]; /* stance mask (two bits: a dual), base power, slots of the same element */
  uint8_t nkit, deck[FR_KIT], ndeck, disc[FR_KIT], ndisc, hand[FR_HAND], nhand, dmask; /* kit slots in the simulator's
                                                                           * list order; dmask: the slots in the discard */
  int8_t hp;
  uint8_t max, focus, last, lostrow, wins, lost_hp;
  uint8_t pas[2], prog[2], on; /* equipped passives, public pattern progress, bit i: pas[i] unlocked */
  uint16_t act; /* the unlocked passives, a bit per passive id */
  uint8_t win[3], nwin; /* PRISM: the last three stances (FR_NONE for a guard) */
  uint8_t played, nprod;
  uint16_t prod[FR_PRODS]; /* MEMORY: kit slots played, fused products played */
  uint16_t pair[FR_PAIRS], ptr[FR_PAIRS], pbt[FR_PAIRS];
  uint8_t pst[FR_PAIRS], ppw[FR_PAIRS]; /* this hand's fusions */
  uint8_t after[6], hist[4], nhist, turns,
      repeats; /* its habits, read by the other side's AI: after[prev*3+st] as nibbles */
} fr_side;
typedef struct {
  fr_side s[2];
  uint16_t rng, ai; /* the fight rng (shuffles) and the AI's */
  uint8_t turn, arena, rules, turns; /* turn: the one being played (1..) */
  uint8_t persona, skill, loop[3]; /* the AI (side 1): faction personality, 1 in skill plays its second best */
  int8_t out; /* the last clash for side 0: 1 won, 0 tie, -1 lost, 9 a guard was in it */
  int8_t dmg[2]; /* damage each side took in the last turn */
  uint8_t sever[2]; /* side k's pattern was severed in the last turn */
  fr_cardv shown[2];
  uint8_t has[2]; /* the card each side played in the last turn (has 0: a guard) */
  uint8_t solo; /* a boss fight: only side 0 draws (the boss lives in side 1's room) */
} fr_state;
extern fr_state fr; /* the one fight (global: harnesses read it from the .noi) */
extern uint8_t fr_scratch[80]; /* the AI's lists; the link's resync buffer (never both at once) */
uint8_t fr_stance(uint16_t id) BANKED; /* stance mask of an element */
uint8_t fr_power(uint16_t id) BANKED; /* 1 + (depth >= 5) + (depth >= 7) */
uint8_t fr_edges(uint16_t ta, uint16_t tb) BANKED; /* how many of b's traits a's traits beat (BEATS[]), 0..12 */
uint8_t fr_cost(uint16_t id) BANKED; /* kit cost: power, +1 for a dual */
uint8_t fr_kit_cost(const uint16_t *kit, uint8_t n) BANKED; /* with the stack discount */
void fr_begin(uint16_t seed) BANKED; /* clears fr; seeds both rngs */
void fr_side_init(uint8_t k, const uint16_t *kit, uint8_t n, const uint8_t *pas, uint8_t hp, uint8_t focus) BANKED;
void fr_draw(void) BANKED; /* the turn starts: turn++, side 0 then side 1 draws to a full hand */
uint8_t fr_pairs_step(void) BANKED; /* one of the hands' fusion lookups; 1 when all are known (before actions) */
void fr_pairs_all(void) BANKED;
uint8_t fr_hash(void) BANKED; /* the state hash before a turn (after the draw) */
uint8_t fr_hand_size(uint8_t k) BANKED;
uint8_t fr_legal(uint8_t k, uint8_t act) BANKED;
uint8_t fr_card(uint8_t k, uint8_t act, fr_cardv *c) BANKED; /* 0 for a guard */
uint8_t fr_actions(uint8_t k, uint8_t *out) BANKED; /* every legal action in the simulator's order */
void fr_resolve(uint8_t a, uint8_t b) BANKED; /* both actions are in: the turn */
uint8_t fr_over(void) BANKED; /* 0 going, else 1 side 0 won, 2 side 1 won, 3 a draw */
uint8_t fr_ai_choose(uint8_t uniform) BANKED; /* side 1's action (the duel AI), all at once */
void fr_ai_begin(uint8_t uniform) BANKED; /* ...or a little each frame: begin, then step until it answers */
uint8_t fr_ai_step(uint8_t budget) BANKED; /* evaluates up to budget pairs; 0xff while thinking, else the action */
void fr_ai_setup(uint8_t persona, uint8_t skill) BANKED;
uint8_t fr_last_card(uint8_t k, fr_cardv *c) BANKED; /* the card side k played in the last turn; 0: it guarded */
void fr_swap_sides(void) BANKED; /* the whole state seen from the other side (the link's guest) */
void fr_set_card(uint8_t k, uint8_t slot, uint16_t id) BANKED;
void fr_side_fix(uint8_t k) BANKED;
/* derived masks again after a side was set from outside */ /* a kit slot becomes another element (DRIFT) */
#endif
