#ifndef CRUCIBLE_EGGS_H
#define CRUCIBLE_EGGS_H
/* Easter eggs (crucible_eggs.c). */
typedef struct egg_info {
  char visitor[11];
  uint16_t line; /* first line id (crucible_text_line) */
  uint8_t lines, faction, glitch;
  uint16_t item; /* the {ITEM}: the element made, or the one granted; 0xffff none */
} egg_info;
void egg_boot(void) BANKED; /* after the save loads: found eggs, name eggs */
void egg_input(uint8_t pressed, uint8_t title, uint8_t dt) BANKED; /* every frame: codes, taps, idle (title only) */
void egg_talk_select(void) BANKED; /* SELECT while a relic talks */
void egg_talk_reset(void) BANKED;
void egg_made(uint16_t id) BANKED; /* an element made */
uint8_t egg_take(void) BANKED; /* an egg to send (k + 1), or 0 */
void egg_get(uint8_t k, egg_info *out) BANKED;
uint8_t egg_count(uint8_t *total) BANKED; /* found so far */
uint8_t egg_dmg(void) BANKED; /* 1: the four-green DMG look is on */
void egg_dmg_filter(uint16_t *bg, uint16_t *sp) BANKED;
uint8_t egg_cameo(uint8_t r) BANKED; /* a found egg's visitor, or 0xff */
#endif
