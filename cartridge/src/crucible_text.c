/* Shared text helpers for screens outside the main bank. Font tiles 128..223: colour 3 (ink on plaster,
 * cream on wood); bank 1 holds a colour-2 copy for secondary text. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_ui.h"
#include "crucible_state.h"
void ui_put(uint8_t x, uint8_t y, uint8_t tile, uint8_t attr) BANKED {
  VBK_REG = 1;
  set_bkg_tiles(x, y, 1, 1, &attr);
  VBK_REG = 0;
  set_bkg_tiles(x, y, 1, 1, &tile);
}
void ui_text(uint8_t x, uint8_t y, const char *s, uint8_t width, uint8_t attr) BANKED {
  uint8_t tiles[20], attrs[20], i = 0, c;
  if (!width) return;
  if (width > 20u) width = 20u;
  memset(attrs, attr, width);
  while (i < width) {
    c = *s;
    if (c) s++;
    tiles[i++] = GLYPH(c);
  }
  VBK_REG = 1;
  set_bkg_tiles(x, y, width, 1, attrs);
  VBK_REG = 0;
  set_bkg_tiles(x, y, width, 1, tiles);
}
void ui_field(uint8_t x, uint8_t y, uint8_t width, const char *s, uint8_t attr) BANKED {
  uint8_t n = strlen(s);
  if (n > width) n = width;
  ui_text(x, y, " ", width, attr);
  ui_text(x + (width - n) / 2u, y, s, n, attr);
}
void ui_number(char *out, uint16_t n, uint8_t width) BANKED {
  out[width] = 0;
  while (width) {
    out[--width] = '0' + (uint8_t)(n % 10u);
    n /= 10u;
  }
}
/* m:ss for under an hour, h:mm beyond */
void ui_time(char *out, uint16_t s) BANKED {
  uint16_t m = s / 60u;
  if (m < 60u) {
    ui_number(out, m, m >= 10u ? 2u : 1u);
    strcat(out, ":");
    ui_number(out + strlen(out), s % 60u, 2);
  } else {
    ui_number(out, m / 60u, m >= 600u ? 2u : 1u);
    strcat(out, "H");
    ui_number(out + strlen(out), m % 60u, 2);
  }
}
