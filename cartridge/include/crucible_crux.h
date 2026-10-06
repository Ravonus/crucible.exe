#ifndef CRUCIBLE_CRUX_H
#define CRUCIBLE_CRUX_H
/* THE CRUCIBLE: the fight engine (crucible_crux.c) and its AI (crucible_crux_ai.c). One pot between two sides; every
 * move is a real recipe. Rules: docs/fight-system.md; reference: docs/fight-system/sim/crux.py,
 * which this matches turn by turn (golden traces on the host build and in the ROM).
 * Pure logic over the global cx; deterministic from the two sides' action words, so the link plays it in lockstep.
 * Host-buildable (CRUX_HOST): the catalogue then comes from cx_host_* tables. */
#ifdef CRUX_HOST
#include <stdint.h>
#ifndef BANKED
#define BANKED
#endif
#else
#include <gb/gb.h>
#endif
#define CX_NONE 0xffffu
#define CX_BAG 10u /* a bag holds 10 at most (8 brought; splits and twists add) */
#define CX_HEAT 4u /* heat cap */
#define CX_TURNS 40u
#define CX_OPEN_HP 2u /* the side that opens a round: 2 HP more */
/* action words (10 bits; the link's P_FTURN): kind:2 << 8 | x:4 << 4 | y:4. Tool index: 0..9 a bag slot, 12..15 one
 * of the four (12 + element id) */
#define CX_ADD 0u
#define CX_FORGE 1u
#define CX_POUR 2u
#define CX_PASS 3u
#define CX_ACT(k, x, y) ((uint16_t)(((uint16_t)(k) << 8) | ((x) << 4) | (y)))
#define CX_KIND(w) ((uint8_t)((w) >> 8))
#define CX_X(w) ((uint8_t)(((w) >> 4) & 15u))
#define CX_Y(w) ((uint8_t)((w) & 15u))
/* outcomes of an ADD */
#define CX_START 0u
#define CX_BUILD 1u
#define CX_HIJACK 2u
#define CX_SPLIT 3u
#define CX_BREAK 4u
#define CX_MISS 5u
/* events (cx.ev) after cx_do: an outcome above, or */
#define CX_EV_FORGE 6u
#define CX_EV_POUR 7u
#define CX_EV_PASS 8u
/* categories */
#define CX_C_WEATHER 2u
#define CX_C_LIFE 4u
#define CX_C_PLACE 6u
/* twists (a side's tw: its faction's style; bosses only) */
#define CX_TW_CHARGE 1u /* DAEMON: its own pot heats +1 at its turn (+2 from phase 2) */
#define CX_TW_SKYALL 2u /* GHOST: every pot it makes sets the sky */
#define CX_TW_ROOT 4u /* OPERATOR: from phase 1 its pot you let stand seeds back into its bag; phase 2: its pots grow */
#define CX_TW_LOCK 8u /* RELIC: from phase 2 its pots are locked to you; its pours shield it (your next pour -1) */
#define CX_TW_COMPILE 16u /* PROGRAM: from phase 2 a pot you start teaches it a partner for it */
typedef struct {
  uint16_t bag[CX_BAG];
  uint8_t n, four, tw, made; /* four: bit e = element e still held */
  int8_t hp;
  uint8_t max;
} cx_side;
typedef struct {
  cx_side s[2];
  uint16_t pot, sky; /* the pot (CX_NONE: empty) and the sky (a trait mask; 0: none) */
  uint8_t owner, heat, turn, act, chain, passes, shield;
  /* the last action, for the screen: its event, the item played, the product (or the ingredient taken), damage */
  uint8_t ev, dmg;
  uint16_t ex, er;
} cx_state;
extern cx_state cx;
extern uint8_t fr_scratch[80]; /* scratch other screens borrow (the avatar editor, the link's setup) */
/* the catalogue (the cartridge's core, or the host's tables) */
uint16_t cx_recipe(uint16_t a, uint16_t b) BANKED;
uint8_t cx_stars(uint16_t id) BANKED; /* depth 0-1: 1, 2-3: 2, 4-5: 3, 6+: 4 */
uint8_t cx_cat(uint16_t id) BANKED;
uint16_t cx_traits(uint16_t id) BANKED;
uint16_t cx_split(uint16_t x, uint16_t s) BANKED; /* the other ingredient when x is one of s's (its first route) */
uint8_t cx_edges(uint16_t x, uint16_t s) BANKED; /* how many of s's traits x's traits beat (BEATS[]) */
uint16_t cx_partner(uint16_t id, uint8_t maxdepth) BANKED; /* the first row whose a is id with b this shallow: b */
/* the engine */
void cx_begin(uint8_t first) BANKED; /* after both sides are set: the opener and its 2 HP */
void cx_side_set(uint8_t k, const uint16_t *bag, uint8_t n, int8_t hp, uint8_t tw) BANKED;
uint16_t cx_tool(uint8_t k, uint8_t i) BANKED; /* the item at tool index i */
uint8_t cx_has(uint8_t k, uint8_t i) BANKED; /* is tool index i held? */
uint8_t cx_outcome(uint8_t k, uint16_t x,
                   uint16_t *r) BANKED; /* what ADD of x by side k does now (CX_START..CX_MISS) */
uint8_t cx_power(void) BANKED;
uint8_t cx_phase(uint8_t k) BANKED;
uint8_t cx_locked_to(uint8_t k) BANKED;
uint8_t cx_legal(uint8_t k, uint16_t w) BANKED;
void cx_begin_turn(void) BANKED; /* the side to move: its own growing pot heats */
void cx_do(uint16_t w) BANKED; /* the side to move plays w (legal); the turn passes */
uint8_t cx_over(void) BANKED; /* 0 going; 1 side 0 won, 2 side 1 won, 3 a draw */
uint8_t cx_hash(void) BANKED;
uint8_t cx_tools(uint8_t k) BANKED; /* how many tools side k holds */
void cx_swap(void) BANKED; /* the whole state seen from the other side (the link's guest) */
/* the AI: it thinks a candidate at a time (cx_ai_step), so the screen keeps moving */
void cx_ai_begin(uint8_t k, uint16_t seed, uint16_t know, uint16_t skill) BANKED;
uint8_t cx_ai_step(void) BANKED; /* 1 when it has decided: cx_ai_word */
extern uint16_t cx_ai_word, cx_ai_rng;
uint8_t cx_knows(uint16_t seed, uint16_t know, uint16_t a, uint16_t b) BANKED;
#endif
