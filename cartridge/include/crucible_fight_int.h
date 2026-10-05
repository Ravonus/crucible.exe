#ifndef CRUCIBLE_FIGHT_INT_H
#define CRUCIBLE_FIGHT_INT_H
/* What crucible_fight.c (the duel and versus screens, the flow) and crucible_fight_bossui.c (the boss screen) share.
 * Only numbers and RAM cross between them: a string literal belongs to its own bank. */
extern uint8_t fui_fx, fui_faction, fui_flip, fui_partner, fui_intro_t, fui_end_line, fui_gained, fui_pips, fui_ready,
    fui_won, fui_lost, fui_tlen, fui_tat, fui_tt;
extern uint16_t fui_pip_t, fui_show_t, fui_xp;
extern char fui_ticker[96], fui_who[12];
extern crucible_boss *fui_b;
extern uint8_t fight_fuse, fight_hold, fight_tier, fight_tutor, fight_end;
extern uint16_t fight_seed, fight_t0;
#define fx_ fui_fx
#define faction_ fui_faction
#define flip_ fui_flip
#define partner_ fui_partner
#define intro_t_ fui_intro_t
#define end_line_ fui_end_line
#define gained_ fui_gained
#define pips_ fui_pips
#define ready_ fui_ready
#define won_turns_ fui_won
#define lost_turns_ fui_lost
#define ticker_len_ fui_tlen
#define ticker_at_ fui_tat
#define ticker_t_ fui_tt
#define pip_t_ fui_pip_t
#define show_t_ fui_show_t
#define xp_ fui_xp
#define ticker_ fui_ticker
#define who_ fui_who
#define b_ fui_b
#define fuse_ fight_fuse
#define hold_b_ fight_hold
#define UI_INTRO 0u
#define UI_CHOOSE 1u
#define UI_WAIT 2u
#define UI_SHOW 3u
void fbui_open_shared(void) BANKED; /* crucible_fight_bossui.c: the partner's boss, from its setup */
#define UI_END 4u
#define UI_KIT 5u
#define UI_LEVEL 6u
#define UI_BOSS 7u
#define UI_PLAN 8u
#define UI_CHOICE 9u
#define UI_DOORS 10u /* between a gauntlet's duels: two doors */
void fui_hand_draw(void) BANKED;
void fui_me_block(void) BANKED;
void fui_answer_line(void) BANKED;
uint8_t fui_live_stance(uint8_t i) BANKED;
uint8_t fui_fuse_act(void) BANKED;
uint16_t fui_fuse_product(void) BANKED;
void fui_face_open(uint8_t f) BANKED;
void fui_draw_duel(void) BANKED;
uint8_t fui_arena(void) BANKED;
uint8_t fui_equip(uint8_t k) BANKED;
void fui_say(uint8_t intent, uint16_t item) BANKED;
void fui_hp_bar(uint8_t x, uint8_t y, int8_t hp, uint8_t max) BANKED;
uint8_t fui_cosm(void) BANKED;
extern uint8_t fbui_outc, fbui_nemesis;
void fbui_open(uint8_t f, uint8_t level, uint8_t nemesis) BANKED;
void fbui_top(void) BANKED;
void fbui_round(void) BANKED;
void fbui_ready(void) BANKED;
void fbui_choose(uint8_t pressed, uint8_t dt) BANKED;
void fbui_end(uint8_t r) BANKED;
uint8_t fbui_choice(uint8_t pressed) BANKED;
#endif
