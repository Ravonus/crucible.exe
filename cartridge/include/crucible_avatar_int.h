/* Internal to the avatar units (crucible_avatar.c: genome, live features, shaders; crucible_avatar_base.c: the
 * styles drawn once). The shared state is global; the primitives are static in each unit (each lives in its own bank). */
#ifndef CRUCIBLE_AVATAR_INT_H
#define CRUCIBLE_AVATAR_INT_H
#include "crucible_avatar.h"
#define W 48u
#define AURA_N 96u /* aura points kept (the first 96 of the outside edge, top down) */
/* the base layer lives in SRAM bank 2 scratch on the cartridge (0xB500..0xB73F, no save uses it): 576 bytes of WRAM
 * were the stack's margin. BASE_ON / BASE_OFF around every touch of it; on the host it is plain memory. */
#ifdef AVATAR_HOST
extern uint8_t base_[AVATAR_BYTES];
#define BASE_ON
#define BASE_OFF
#else
#define base_ ((uint8_t *)0xB500u)
#define BASE_ON                                                                                                        \
  ENABLE_RAM;                                                                                                          \
  SWITCH_RAM(2)
#define BASE_OFF DISABLE_RAM
#endif
extern uint8_t work_[AVATAR_BYTES];
extern uint16_t rng_;
extern uint8_t fx_, fxn_, fx_t_, bolt_, bolt_x_;
extern uint8_t aura_x_[AURA_N], aura_y_[AURA_N], smoke_x_[4], smoke_y_[4];
extern uint8_t style_, hair_, phase_, head_, eyes_, mouth_, gear_, pat_, neck_, hue_, rx_, ry_, cx_, cy_, ey_, es_, my_;
extern uint8_t blink_, blink_t_, gaze_t_, mouth_t_, open_, glint_, light_, dirty_, bob_, bob_t_;
extern int8_t gaze_;
extern uint8_t dither_;
extern uint8_t mark_; /* the player's mark (genome byte 4 bits 3..5): a 3x3 glyph on the forehead; 0 none */
extern uint16_t pal_[4];
extern uint8_t *buf_;
void avatar_base(void) BANKED; /* draws the face's base (style, gear, pattern) and finds its aura */
static uint8_t rnd(void) {
  rng_ ^= (uint16_t)(rng_ << 7);
  rng_ ^= (uint16_t)(rng_ >> 9);
  rng_ ^= (uint16_t)(rng_ << 8);
  return (uint8_t)rng_;
}
static uint8_t below(uint8_t n) { return (uint8_t)(((uint16_t)rnd() * n) >> 8); }

/* ---- primitives on 2bpp tiles (colour 0 clear, 1 ink, 2 face, 3 light) ---- */
static void px(uint8_t x, uint8_t y, uint8_t c) {
  uint8_t *p, bit;
  if (x >= W || y >= W) return;
  p = buf_ + (uint16_t)((y >> 3) * 6u + (x >> 3)) * 16u + (uint8_t)((y & 7u) << 1);
  bit = (uint8_t)(0x80u >> (x & 7u));
  if (c & 1u)
    p[0] |= bit;
  else
    p[0] &= (uint8_t)~bit;
  if (c & 2u)
    p[1] |= bit;
  else
    p[1] &= (uint8_t)~bit;
}
static uint8_t get(uint8_t x, uint8_t y) {
  const uint8_t *p;
  uint8_t bit;
  if (x >= W || y >= W) return 0;
  p = buf_ + (uint16_t)((y >> 3) * 6u + (x >> 3)) * 16u + (uint8_t)((y & 7u) << 1);
  bit = (uint8_t)(0x80u >> (x & 7u));
  return (uint8_t)(((p[0] & bit) ? 1u : 0u) | ((p[1] & bit) ? 2u : 0u));
}
/* a row of one colour: whole bytes where it can (the faces are mostly spans; a pixel at a time was most of a face's
 * cost) */
static void span(int16_t x0, int16_t x1, int16_t y, uint8_t c) {
  uint8_t *p, bit, lo, hi, x, e;
  if (y < 0 || y >= (int16_t)W) return;
  if (x0 < 0) x0 = 0;
  if (x1 >= (int16_t)W) x1 = W - 1;
  if (x0 > x1) return;
  x = (uint8_t)x0;
  e = (uint8_t)x1;
  p = buf_ + (uint16_t)(((uint8_t)y >> 3) * 6u + (x >> 3)) * 16u + (uint8_t)(((uint8_t)y & 7u) << 1);
  bit = (uint8_t)(0x80u >> (x & 7u));
  lo = (c & 1u) ? 0xffu : 0u;
  hi = (c & 2u) ? 0xffu : 0u;
  for (;;) {
    if (bit == 0x80u && (uint8_t)(x + 7u) <= e) {
      p[0] = lo;
      p[1] = hi;
      x = (uint8_t)(x + 8u);
      p += 16;
      if (x > e) return;
      continue;
    }
    p[0] = (uint8_t)((p[0] & (uint8_t)~bit) | (lo & bit));
    p[1] = (uint8_t)((p[1] & (uint8_t)~bit) | (hi & bit));
    if (x == e) return;
    x++;
    bit >>= 1;
    if (!bit) {
      bit = 0x80u;
      p += 16;
    }
  }
}
static void rect(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t c) {
  while (y0 <= y1) span(x0, x1, y0++, c);
}
/* dithering styles, one per face: 0 checker 1 ordered (Bayer gradient) 2 scanlines 3 diagonal hatch 4 stipple
 * 5 vertical lines. level 0..3: how far into the shade (or light) the pixel is. */
static const uint8_t BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
static uint8_t dith(int16_t x, int16_t y, uint8_t level) {
  switch (dither_) {
  case 1: return BAYER[((y & 3) << 2) | (x & 3)] < (uint8_t)((level + 1u) << 2);
  case 2: return level >= 3u ? 1u : (uint8_t)(!(y & 1) && (level || !(x & 3)));
  case 3: return (uint8_t)(((x + y) & 3) == 0 || (level >= 2u && ((x + y) & 3) == 2));
  case 4: {
    uint8_t h = (uint8_t)((x * 7 + y * 13 + ((x * y) >> 2)) & 7);
    return h < (uint8_t)(2u + level * 2u);
  }
  case 5: return level >= 3u ? (uint8_t)((x & 1) == 0) : (uint8_t)((x & 3) == 0 || (level >= 2u && (x & 3) == 2));
  default: return (uint8_t)((x + y) & 1);
  }
}
/* n / d for a quotient under 4 (the shading levels): subtraction, not the SM83's slow division */
static uint8_t quot(uint16_t n, uint16_t d) {
  uint8_t l = 0;
  while (n >= d && l < 4u) {
    n -= d;
    l++;
  }
  return l;
}
/* a face-lit span: light on the far left of the top half, dithered ink over the right quarter */
static void shaded(int16_t x0, int16_t x1, int16_t y, uint8_t top) {
  int16_t x, q = (int16_t)((x1 - x0) >> 2);
  if (y < 0 || y >= (int16_t)W) return;
  span(x0, x1, y, 2); /* the face, then the two dithered ends (they never overlap: q is a quarter) */
  for (x = x1 - q + 1; x <= x1; x++)
    if (x >= 0 && x < (int16_t)W && dith(x, y, quot((uint16_t)((x - (x1 - q)) << 2), (uint16_t)(q + 1))))
      px((uint8_t)x, (uint8_t)y, 1);
  if (top)
    for (x = x0 + 2; x < x0 + q - 1; x++)
      if (x >= 0 && x < (int16_t)W && dith(x, y, quot((uint16_t)((x0 + q - x) * 3), (uint16_t)(q + 1))))
        px((uint8_t)x, (uint8_t)y, 3);
}
static const uint8_t ROUND[17] = {16, 16, 16, 16, 15, 15, 15, 14, 14, 13, 12, 12, 11, 9, 8, 6, 0};
static uint8_t half(uint8_t r, uint8_t d, uint8_t rad) { /* half-width of an ellipse row d rows from the centre */
  uint8_t i;
  if (d > rad) return 0;
  i = (uint8_t)(((uint16_t)d * 16u + (rad >> 1)) / rad);
  if (i > 16u) i = 16;
  return (uint8_t)(((uint16_t)r * ROUND[i] + 8u) >> 4);
}
/* mode 0 solid c, 1 face (outlined and shaded), 2 ring of c */
static void ellipse(int16_t cx, int16_t cy, uint8_t rx, uint8_t ry, uint8_t c, uint8_t mode) {
  int16_t dy;
  uint8_t h, hi;
  for (dy = -(int16_t)ry - (mode == 1u); dy <= (int16_t)ry + (mode == 1u); dy++) {
    uint8_t d = (uint8_t)(dy < 0 ? -dy : dy);
    if (mode == 1u) {
      h = half((uint8_t)(rx + 1u), d, (uint8_t)(ry + 1u));
      span(cx - h, cx + h, cy + dy, 1);
      if (d <= ry) {
        h = half(rx, d, ry);
        if (h) shaded(cx - h + 1, cx + h - 1, cy + dy, dy < 0);
      }
    } else if (mode == 2u) {
      h = half(rx, d, ry);
      hi = rx > 1u && ry > 1u ? half((uint8_t)(rx - 1u), d, (uint8_t)(ry - 1u)) : 0;
      if (d >= ry || !hi)
        span(cx - h, cx + h, cy + dy, c);
      else {
        span(cx - h, cx - hi, cy + dy, c);
        span(cx + hi, cx + h, cy + dy, c);
      }
    } else if (mode == 3u) {
      h = half(rx, d, ry);
      if (h) shaded(cx - h, cx + h, cy + dy, dy < 0);
    } else {
      h = half(rx, d, ry);
      span(cx - h, cx + h, cy + dy, c);
    }
  }
}
/* a filled triangle; c 4: dithered shadow (face and ink) */
static void tri(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t c) {
  /* fixed-point edges (8.8): one division per edge, then additions per row (the SM83 has no divide) */
  int16_t t, y, x, xa, xb, ea, eb, sa, sb;
  if (y1 < y0) {
    t = y0;
    y0 = y1;
    y1 = t;
    t = x0;
    x0 = x1;
    x1 = t;
  }
  if (y2 < y0) {
    t = y0;
    y0 = y2;
    y2 = t;
    t = x0;
    x0 = x2;
    x2 = t;
  }
  if (y2 < y1) {
    t = y1;
    y1 = y2;
    y2 = t;
    t = x1;
    x1 = x2;
    x2 = t;
  }
  if (y2 == y0) return;
  sa = (int16_t)(((x2 - x0) << 8) / (y2 - y0));
  ea = (int16_t)(x0 << 8);
  sb = y1 > y0 ? (int16_t)(((x1 - x0) << 8) / (y1 - y0)) : 0;
  eb = (int16_t)(y1 > y0 ? x0 << 8 : x1 << 8);
  for (y = y0; y <= y2; y++) {
    if (y == y1) {
      sb = y2 > y1 ? (int16_t)(((x2 - x1) << 8) / (y2 - y1)) : 0;
      eb = (int16_t)(x1 << 8);
    }
    xa = (int16_t)(ea >> 8);
    xb = (int16_t)(eb >> 8);
    if (xa > xb) {
      t = xa;
      xa = xb;
      xb = t;
    }
    if (y >= 0 && y < (int16_t)W) {
      if (c == 4u) {
        for (x = xa; x <= xb; x++)
          if (x >= 0 && x < (int16_t)W) px((uint8_t)x, (uint8_t)y, dith(x, y, 2) ? 1 : 2);
      } else
        span(xa, xb, y, c);
    }
    ea += sa;
    eb += sb;
  }
}
static void box_face(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t cut) {
  int16_t y;
  uint8_t k;
  for (y = y0 - 1; y <= y1 + 1; y++) {
    k = (uint8_t)(y - y0 < (int16_t)cut ? cut - (y - y0) : y1 - y < (int16_t)cut ? cut - (y1 - y) : 0);
    if (y < y0 || y > y1) k = cut;
    span(x0 - 1 + k, x1 + 1 - k, y, 1);
  }
  for (y = y0; y <= y1; y++) {
    k = (uint8_t)(y - y0 < (int16_t)cut ? cut - (y - y0) : y1 - y < (int16_t)cut ? cut - (y1 - y) : 0);
    shaded(x0 + k, x1 - k, y, y < (y0 + y1) / 2);
  }
}
static void dot(int16_t x, int16_t y, uint8_t c) {
  if (x >= 0 && y >= 0 && x < (int16_t)W && y < (int16_t)W && !get((uint8_t)x, (uint8_t)y))
    px((uint8_t)x, (uint8_t)y, c);
}
#endif
