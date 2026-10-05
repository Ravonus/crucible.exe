#ifndef CRUCIBLE_TIME_H
#define CRUCIBLE_TIME_H
/* Time awareness (crucible_time.c). The cartridge is MBC5: there is no real-time clock. The player says what time it
 * is (day of week, hour, minute) the first time the cartridge runs, and confirms or adjusts it at every power-on; while
 * the cartridge runs, the clock advances by counting VBlanks (4389/262144 s each: exactly 70224 clocks at 4.194304 MHz,
 * 59.7275 Hz). Time away is whatever the player reports. "NOT SURE" keeps the estimate and is never held against
 * anyone: an unknown absence triggers nothing. See docs/time-awareness.md (the SRAM record is in bank 15). */
#include <stdint.h>

/* part of the day */
#define CT_NIGHT 0u /* 22:00-04:59 */
#define CT_DAWN 1u /* 05:00-07:59 */
#define CT_DAY 2u /* 08:00-17:59 */
#define CT_DUSK 3u /* 18:00-21:59 */
/* how long the player was away, by their own report at power-on */
#define CT_AWAY_NONE 0u /* not asked yet this power-on */
#define CT_AWAY_FIRST 1u /* the clock was just set for the first time */
#define CT_AWAY_UNKNOWN 2u /* "not sure", or a correction backwards: nothing reacts */
#define CT_AWAY_NOW 3u /* under 5 minutes (the time was confirmed as it stood) */
#define CT_AWAY_MINUTES 4u /* under an hour */
#define CT_AWAY_HOURS 5u /* under a day */
#define CT_AWAY_DAY 6u /* a day to a week */
#define CT_AWAY_WEEK 7u /* a week or more */
/* special minutes, read on a 12-hour face (1:11 is 01:11 and 13:11) */
#define CT_SP_NONE 0u
#define CT_SP_111 1u
#define CT_SP_222 2u
#define CT_SP_333 3u /* the strongest at night */
#define CT_SP_444 4u
#define CT_SP_555 5u
#define CT_SP_1111 6u
#define CT_SP_1234 7u
#define CT_SP_404 8u
#define CT_SP_1010 9u
#define CT_SP_MIDNIGHT 10u
#define CT_SP_NOON 11u
/* context flags */
#define CT_F_KNOWN 1u /* the player has set the clock at least once (else time-of-day effects stay off) */
#define CT_F_REPORTED 2u /* this power-on's report was given (away is meaningful) */
#define CT_F_RETURNING 4u /* within the first 10 minutes after the report: "for a bit" */
#define CT_F_DEEP 8u /* a special minute at night (3:33 AM and kin) */
#define CT_F_PAUSED 16u /* the special minute is happening while the menu is open */
typedef struct crucible_time_ctx {
  uint8_t flags; /* CT_F_* */
  uint8_t part; /* CT_NIGHT .. CT_DUSK */
  uint8_t weekday; /* 0 SUN .. 6 SAT */
  uint8_t hour, minute;
  uint8_t away; /* CT_AWAY_* */
  uint8_t marathon; /* 0; 1 two hours in; 2 four hours or more ("forever") */
  uint8_t special; /* CT_SP_* for the current minute */
  uint8_t streak; /* days in a row the cartridge was played (by the player's clock) */
  uint8_t session_min; /* minutes played since power-on, saturated at 255 */
} crucible_time_ctx;

/* the ask page (crucible_menu.c) */
#define CT_ASK_DONE 0u /* answered this power-on */
#define CT_ASK_FIRST 1u /* the clock was never set: "WHAT TIME IS IT?" */
#define CT_ASK_RETURN 2u /* confirm or adjust the estimate */
#define CT_ASK_ADJUST 3u /* from SETUP: a correction, never an absence */
#define CT_ANSWER_OK 0u
#define CT_ANSWER_SKIP 1u /* "NOT SURE": keep the estimate */
/* menu effects (time_menu) */
#define CT_MENU_SAME 0u
#define CT_MENU_ENTER 1u /* a special minute began with the menu open: redraw, the music stops if deep */
#define CT_MENU_LEAVE 2u /* it passed: redraw, restore */
#define CT_MENU_MINUTE 3u /* an ordinary minute turned (SETUP's clock row) */

void time_poll(void) BANKED; /* any frame; counts VBlanks since the last call */
uint8_t time_ask_mode(void) BANKED; /* CT_ASK_* for power-on */
void time_ask_prefill(uint8_t *day, uint8_t *hour, uint8_t *minute) BANKED;
void time_answer(uint8_t mode, uint8_t answer, uint8_t day, uint8_t hour, uint8_t minute, uint8_t weeks) BANKED;
void time_format(char *out, uint8_t day, uint8_t hour,
                 uint8_t minute) BANKED; /* "SUN 12:00 PM"; day 7: "12:00 PM"; day | 0x80: hour padded to two */
void time_now_text(char *out, uint8_t with_day) BANKED; /* the clock now, as time_format */
uint8_t
time_menu(uint8_t where) BANKED; /* each menu frame (0 other page, 1 main page; 2 once when it opens): CT_MENU_* */
uint8_t time_menu_special(void) BANKED; /* CT_SP_* shown on the menu, | 0x80 when deep (night): 0 none */
uint8_t time_whisper_ready(uint8_t allowed) BANKED; /* each frame from feats_toast: 1 when a whisper should show */
uint8_t time_whisper(char *line1, char *line2) BANKED; /* pops one: 16 and 18 chars at most; 0 none */
void time_story_opened(uint8_t slot, uint8_t resumed) BANKED; /* a run opened: an absence reaches it once */
void crucible_time_context(crucible_time_ctx *out) BANKED;
#endif
