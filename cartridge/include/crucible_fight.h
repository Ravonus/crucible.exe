#ifndef CRUCIBLE_FIGHT_H
#define CRUCIBLE_FIGHT_H
/* Boss fights (crucible_fight.c). */
#define FIGHT_CUE 0u /* an attack is coming: answer before the pips run out */
#define FIGHT_SHOW 1u /* the outcome holds on screen */
#define FIGHT_GOING 0u
#define FIGHT_WON 1u
#define FIGHT_FLED 2u
#define FIGHT_OVER 3u /* the run is lost */
#define FIGHT_FX_FLASH 1u /* the attack landed: white flash */
#define FIGHT_FX_SHAKE 2u /* the boss is hit: its sprite jolts */
#define FIGHT_FX_TEAR 4u /* the scene tears (glitch) */
#define FIGHT_FX_CHARGE 8u /* a pip: the boss charges (palette pulse) */
#define FIGHT_FX_PHASE 16u /* a new phase */
#define FIGHT_DUEL 8u /* fight_open's faction | FIGHT_DUEL: a duel with one of its figures (else its champion) */
#define FK_BOSS 0u
#define FK_DUEL 1u
#define FK_VERSUS 2u
#define FK_GAUNTLET 3u
extern uint8_t fight_kind, fight_ui, fight_cursor, fight_me, fight_them, fight_dbg, fight_swap;
extern uint8_t flow_gauntlet; /* the director asks the next duel to be a gauntlet of three (chapters 4 and 8) */
void fight_open(uint8_t faction, uint8_t level, uint8_t nemesis) BANKED;
void fight_view(crucible_cells *v) BANKED; /* the cells to draw: kind/id per cell, message = turning, sign = shimmer */
uint8_t fight_tick(uint8_t pressed, uint8_t dt) BANKED;
uint16_t fight_attack_a(void) BANKED;
uint16_t fight_attack_b(void) BANKED;
uint16_t fight_result(void) BANKED;
uint16_t fight_answer(void) BANKED;
uint8_t fight_fx(void) BANKED;
uint8_t fight_state(void) BANKED;
uint16_t fight_lost(void) BANKED;
uint8_t fight_rival(void) BANKED;
void fight_shimmer(uint16_t *p) BANKED;
#endif
