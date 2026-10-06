#ifndef CRUCIBLE_FIGHT_H
#define CRUCIBLE_FIGHT_H
/* Fights (crucible_fight.c): THE CRUCIBLE on the bench's own cells, for story duels, bosses and the link versus. */
#define FIGHT_CUE 0u
#define FIGHT_SHOW 1u
#define FIGHT_GOING 0u
#define FIGHT_WON 1u
#define FIGHT_FLED 2u
#define FIGHT_OVER 3u /* the run is lost */
#define FIGHT_FX_FLASH 1u /* you were hit: a white wash */
#define FIGHT_FX_SHAKE 2u /* a hit landed on them: the face jolts (when it shows) */
#define FIGHT_FX_TEAR 4u /* the merge row tears (a combination resolves) */
#define FIGHT_FX_CHARGE 8u /* a beat: the face's palette pulses */
#define FIGHT_FX_PHASE 16u /* a new phase */
#define FIGHT_FX_FACE 32u /* the opponent's face shows (the intro, the end) */
#define FIGHT_FX_NOFACE 64u /* ... and goes (play: the merge row is the fight) */
#define FIGHT_DUEL 8u /* fight_open's faction | FIGHT_DUEL: a duel with one of its figures (else its champion) */
#define FK_BOSS 0u
#define FK_DUEL 1u
#define FK_VERSUS 2u
#define FK_GAUNTLET 3u
/* screens (fight_ui) */
#define UI_INTRO 0u
#define UI_CHOOSE 1u
#define UI_WAIT 2u
#define UI_SHOW 3u
#define UI_END 4u
#define UI_KIT 5u
#define UI_LEVEL 6u
#define UI_ROUND 7u
#define UI_CHOICE 9u
extern uint8_t fight_kind, fight_ui, fight_cursor, fight_dbg, fight_swap, fight_round, fight_tier, fight_gauntlet,
    fight_look;
extern uint8_t fight_wins[2];
extern uint16_t fight_seed, fight_t0;
extern uint8_t flow_gauntlet; /* the director asks the next duel to be a gauntlet of three (chapters 4 and 8) */
void fight_open(uint8_t faction, uint8_t level, uint8_t nemesis) BANKED;
void fight_view(
    crucible_cells *v) BANKED; /* the cells to draw: kind/id per cell, message = turning cells, sign = shimmer */
uint8_t fight_tick(uint8_t pressed, uint8_t dt) BANKED;
uint8_t fight_fx(void) BANKED;
uint8_t fight_state(void) BANKED;
uint16_t fight_lost(void) BANKED;
uint8_t fight_rival(void) BANKED;
void fight_shimmer(uint16_t *p) BANKED;
void fight_card(const char *big, uint8_t win, const char *l1,
                const char *l2) BANKED; /* the link's result in the fight's end frame */
#endif
