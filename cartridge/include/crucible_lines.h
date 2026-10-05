#ifndef CRUCIBLE_LINES_H
#define CRUCIBLE_LINES_H
#include <gb/gb.h>
/* Packed lines (crucible_lines.c): slots in order NAME, ITEM, CAT, TAG, SECTOR, PLAYER, OTHER. */
#define CRUCIBLE_TEXT_SLOTS 7u
#define CRUCIBLE_TEXT_PAUSE '\x01' /* the typewriter waits a beat */
#define CRUCIBLE_TEXT_LINE '\x02' /* a new line */
#define CRUCIBLE_TEXT_GLITCH '\x03' /* the next word types out corrupted, then resolves */
/* Expands line id into out (at most n-1 characters and a 0); returns the length. */
uint8_t crucible_text_line(uint16_t id, char *out, uint8_t n, const char *const *slots) BANKED;
/* speaker types and intents, in the order the line tables were generated with */
enum { WHO_ANY, WHO_PROGRAM, WHO_DAEMON, WHO_GHOST, WHO_AI, WHO_OPERATOR, WHO_RELIC, WHO_TYPES };
enum {
  SAY_GREET,
  SAY_ASK,
  SAY_PLEASED,
  SAY_HURT,
  SAY_LORE,
  SAY_GLITCH,
  SAY_FAREWELL,
  SAY_TAUNT,
  SAY_NOTICE,
  SAY_EGG,
  SAY_UNAWARE,
  SAY_TRUTH,
  SAY_END
};
uint16_t crucible_line_pick(uint8_t who, uint8_t intent, uint16_t r) BANKED;
#endif
