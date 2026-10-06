#ifndef CRUCIBLE_FIGHT_INT_H
#define CRUCIBLE_FIGHT_INT_H
/* What the fight's files share: crucible_fight.c (the screen and the turn), crucible_fight_story.c (who comes, what
 * they bring, what a fight gives and costs), crucible_fight_kit.c (the bag and the level card). Only numbers and RAM
 * cross between them: a string literal belongs to its own bank. */
#include "crucible_crux.h"
/* the opponent (side 1; on the link's guest the engine is swapped so side 0 is always this cartridge's player) */
extern uint8_t fo_faction, fo_tier, fo_nemesis, fo_xp_lo, fo_end, fo_took; /* fo_took: it hijacked you this fight */
extern uint16_t fo_seed, fo_know, fo_skill, fo_plan, fo_xp;
extern char fo_name[10];
extern crucible_boss *fo_b;
/* the line in the box's second row, and its colour (T_CREAM / T_BRASS) */
extern char fui_line[20];
extern uint8_t fui_line_attr, fui_fx, fui_gained;
/* crucible_fight_story.c */
uint8_t fs_bag(uint16_t *bag) BANKED; /* the player's bag (pinned, then the auto bag): its size */
void fs_open_story(uint8_t f, uint8_t level, uint8_t nemesis, uint8_t duel) BANKED; /* sets cx and fo_* */
void fs_open_gauntlet_next(void) BANKED;
void fs_open_shared(void) BANKED;
void fs_face(void) BANKED;
void fs_open_bag(void) BANKED; /* the partner's boss (co-op 9.4) */
uint16_t fs_plan(void) BANKED;
uint8_t fs_prep_step(void) BANKED; /* the opponent's bag, a few shelf positions a call: 1 when ready */
extern uint8_t fs_ready; /* the boss's telegraphed forge (CX_NONE: none) */
void fs_end(uint8_t r) BANKED; /* the story's gains and costs; fight_ui after */
uint8_t fs_choice(uint8_t pressed) BANKED; /* the boss's end choice: 1 when chosen */
void fs_say(uint8_t intent, uint16_t item) BANKED; /* a line of the opponent's into fui_line */
uint8_t fs_made(uint16_t *out) BANKED; /* what this side made in the fight (new to it), at most 3 */
void fs_note_made(uint16_t id) BANKED;
#define FS_MADE 3u
extern uint16_t fs_made_ids[FS_MADE];
extern uint8_t fs_made_n;
/* the end screen's words (fs_end, fs_choice) */
extern char fe_big[14], fe_sub[22], fe_stat[24];
extern uint8_t fe_win;
extern uint16_t fe_lost;
/* crucible_fight_kit.c */
uint8_t kit_open(uint8_t where) BANKED;
uint8_t kit_tick(uint8_t pressed) BANKED;
uint8_t level_open(void) BANKED;
uint8_t level_tick(uint8_t pressed) BANKED;
/* faction twists */
#define FO_PROGRAM 0u
#define FO_DAEMON 1u
#define FO_GHOST 2u
#define FO_AI 3u
#define FO_OPERATOR 4u
#define FO_RELIC 5u
#endif
