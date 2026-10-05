#pragma bank 255
/* Text on the cartridge: lines packed against one dictionary of common words. crucible_text_line expands one line into characters, filling its slots ({NAME}, {ITEM},
 * ...) from the caller's strings; pauses, new lines and glitch marks come out as the control characters
 * CRUCIBLE_TEXT_PAUSE / _LINE / _GLITCH for the typewriter. The tables are generated into banks of their
 * own (cartridge/data/text: crucible_lines_dict.c, crucible_lines_text*.c); a line is copied here and expanded. */
#include <gb/gb.h>
#include <string.h>
#include "crucible_lines.h"
#include "crucible_lines_data.h"

#define T_END 0u
#define T_SLOT0 1u
#define T_PAUSE 7u
#define T_GLITCH 8u
#define T_LINE 9u
#define T_OTHER 0x0au
#define T_WORD2 0x0fu
#define T_LIT0 0x10u
#define T_WORD0 0x60u
#define ONE 160u

static uint8_t raw_[100];
static uint8_t put_word(uint16_t w, char *out, uint8_t at, uint8_t n) {
  return (uint8_t)(at + crucible_dict_word(w, out + at, (uint8_t)(n - 1u - at)));
}
uint8_t crucible_text_line(uint16_t id, char *out, uint8_t n, const char *const *slots) BANKED {
  const uint8_t *p;
  uint8_t at = 0, prev_word = 0, b;
  if (!n) return 0;
  if (id >= TEXT_LINES) {
    out[0] = 0;
    return 0;
  }
  crucible_line_raw(id, raw_, sizeof raw_);
  p = raw_;
  while ((b = *p++) != T_END && at + 1u < n) {
    if (b >= T_WORD0 || b == T_WORD2) {
      uint16_t w = b == T_WORD2 ? (uint16_t)(ONE + *p++) : (uint16_t)(b - T_WORD0);
      if (prev_word && at + 1u < n) out[at++] = ' ';
      at = put_word(w, out, at, n);
      prev_word = 1;
      continue;
    }
    prev_word = 0;
    if (b >= T_LIT0) {
      out[at++] = (char)(0x20u + b - T_LIT0);
      continue;
    }
    if ((b >= T_SLOT0 && b < T_SLOT0 + 6u) || b == T_OTHER) {
      const char *s = slots ? slots[b == T_OTHER ? 6u : b - T_SLOT0] : 0;
      if (s)
        while (*s && at + 1u < n) out[at++] = *s++;
      continue;
    }
    if (b == T_PAUSE)
      out[at++] = CRUCIBLE_TEXT_PAUSE;
    else if (b == T_GLITCH)
      out[at++] = CRUCIBLE_TEXT_GLITCH;
    else if (b == T_LINE)
      out[at++] = CRUCIBLE_TEXT_LINE;
  }
  out[at] = 0;
  return at;
}
/* A line for a speaker type and intent (WHO_* / SAY_* codes, cartridge/include/crucible_lines.h): one of the lines for
 * that type or for anyone, chosen by r. */
uint16_t crucible_line_pick(uint8_t who, uint8_t intent, uint16_t r) BANKED {
  uint16_t i, n = 0, k;
  for (i = 0; i < TEXT_LINES; i++) {
    uint8_t m = crucible_line_meta(i);
    if ((m & 15u) == intent && ((m >> 4) == who || (m >> 4) == 0u)) n++;
  }
  if (!n) return 0;
  k = r % n;
  for (i = 0; i < TEXT_LINES; i++) {
    uint8_t m = crucible_line_meta(i);
    if ((m & 15u) == intent && ((m >> 4) == who || (m >> 4) == 0u)) {
      if (!k) return i;
      k--;
    }
  }
  return 0;
}
