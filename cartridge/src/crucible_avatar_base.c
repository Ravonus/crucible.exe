/* Avatars, the base layer (the genome and the live features are in crucible_avatar.c): the face drawn once from its
 * genome into the 48x48 2bpp sprite canvas: head, neck, gear, face pattern, the non-robot styles (human, polygon,
 * pixel, cloud, sheet ghost, flame, AI eye) and the silhouette's edge for the shaders.
 * Pure C: tools build it on the host to preview faces (AVATAR_HOST). */
#ifdef AVATAR_HOST
#include <stdint.h>
#include <string.h>
#define BANKED
#else
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#endif
#include "crucible_avatar_int.h"

static void draw_gear_back(void) {
  int16_t top = (int16_t)cy_ - ry_ - 1;
  if (gear_ == 1u) {
    rect(cx_ + 3, top - 7, cx_ + 3, top, 1);
  } else if (gear_ == 2u) {
    uint8_t k;
    for (k = 0; k < 6u; k++) {
      px((uint8_t)(cx_ - 4 - k), (uint8_t)(top - k), 1);
      px((uint8_t)(cx_ + 4 + k), (uint8_t)(top - k), 1);
    }
  } else if (gear_ == 4u) {
    uint8_t k;
    for (k = 0; k < 6u; k++) {
      span(cx_ - rx_ + 2 + (k >> 1) - 2, cx_ - rx_ + 4 - (k >> 1) + 1, top + 2 - k, k < 2u ? 1 : 2);
      span(cx_ + rx_ - 4 + (k >> 1) - 1, cx_ + rx_ - 2 - (k >> 1) + 2, top + 2 - k, k < 2u ? 1 : 2);
    }
  } else if (gear_ == 5u) {
    int16_t x0 = (int16_t)cx_ - 1, x1 = (int16_t)cx_ + 1, y0 = top - 5, y1 = top + 2;
    rect(x0 - 1, y0 - 1, x1 + 1, y1, 1);
    rect(x0, y0, x1, y1, 3);
  } /* named: sdcc lost a high byte here */
  else if (gear_ == 6u) {
    ellipse(cx_, cy_, (uint8_t)(rx_ + 3u), (uint8_t)(ry_ + 3u), 1, 2);
  }
}
static void draw_gear_front(void) {
  int16_t top = (int16_t)cy_ - ry_ - 1;
  if (gear_ == 3u)
    ellipse(cx_, top - 3, (uint8_t)(rx_ - 3u), 2, 3, 2);
  else if (gear_ == 6u) {
    rect(cx_ - rx_ - 4, cy_ - 4, cx_ - rx_, cy_ + 4, 1);
    rect(cx_ + rx_, cy_ - 4, cx_ + rx_ + 4, cy_ + 4, 1);
    rect(cx_ - rx_ - 3, cy_ - 3, cx_ - rx_ - 2, cy_ + 3, 2);
  } else if (gear_ == 7u) {
    uint8_t y;
    for (y = cy_; y < W; y++) {
      px((uint8_t)(cx_ - rx_ + 1 + ((y >> 2) & 1u)), y, 1);
      px((uint8_t)(cx_ + rx_ - 1 - ((y >> 2) & 1u)), y, 1);
    }
  }
}
static void draw_head(void) {
  switch (head_) {
  case 1: box_face(cx_ - rx_, cy_ - ry_, cx_ + rx_, cy_ + ry_, 3); break;
  case 2: {
    int16_t dy;
    for (dy = -(int16_t)ry_ - 1; dy <= (int16_t)ry_ + 1; dy++) {
      uint8_t d = (uint8_t)(dy < 0 ? -dy : dy);
      uint8_t h = (uint8_t)(rx_ - (dy > 0 ? (d * rx_) / (ry_ + ry_ / 2u + 1u) : d / 3u));
      span(cx_ - h - 1, cx_ + h + 1, cy_ + dy, 1);
      if (d <= ry_) shaded(cx_ - h, cx_ + h, cy_ + dy, dy < 0);
    }
  } break;
  case 3:
    ellipse(cx_, cy_ - ry_ + rx_, rx_, rx_, 2, 1);
    ellipse(cx_, cy_ + ry_ - rx_, rx_, rx_, 2, 1);
    box_face(cx_ - rx_, cy_ - ry_ + rx_, cx_ + rx_, cy_ + ry_ - rx_, 0);
    break;
  case 4:
    box_face(cx_ - rx_, cy_ - ry_, cx_ + rx_, cy_ + ry_, 1);
    rect(cx_ - rx_ + 2, cy_ - ry_ + 2, cx_ + rx_ - 2, cy_ + ry_ - 2, 1);
    {
      uint8_t y;
      for (y = (uint8_t)(cy_ - ry_ + 3u); y <= cy_ + ry_ - 3u; y++)
        span(cx_ - rx_ + 3, cx_ + rx_ - 3, y, (y & 1u) ? 2 : 1);
    }
    break;
  case 5:
    ellipse(cx_, cy_ - 2, (uint8_t)(rx_ + 2u), (uint8_t)(ry_ + 2u), 1, 0);
    ellipse(cx_, cy_ - 3, (uint8_t)(rx_ + 1u), (uint8_t)(ry_ + 1u), 2, 2);
    ellipse(cx_, cy_ + 2, (uint8_t)(rx_ - 3u), (uint8_t)(ry_ - 3u), 2, 1);
    break;
  case 6:
    ellipse(cx_, cy_ - 3, rx_, (uint8_t)(ry_ - 3u), 2, 1);
    box_face(cx_ - rx_ / 2 - 1, cy_ + 2, cx_ + rx_ / 2 + 1, cy_ + ry_, 2);
    ellipse(cx_, cy_ - 3, (uint8_t)(rx_ - 1u), (uint8_t)(ry_ - 4u), 2, 0);
    {
      int16_t dy;
      for (dy = -(int16_t)ry_ + 4; dy < 0; dy++) {
        uint8_t h = half((uint8_t)(rx_ - 1u), (uint8_t)-dy, (uint8_t)(ry_ - 4u));
        shaded(cx_ - h, cx_ + h, cy_ - 3 + dy, 1);
      }
    }
    break;
  case 7: {
    uint8_t r = rx_ < ry_ ? rx_ : ry_;
    ellipse(cx_, cy_, r, r, 2, 1);
    ellipse(cx_, cy_ + r + 3, (uint8_t)(r - 2u), 1, 1, 2);
  } break;
  default: ellipse(cx_, cy_, rx_, ry_, 2, 1);
  }
}
static void draw_neck(void) {
  if (neck_ == 0u || head_ == 7u) return; /* floating */
  {
    int16_t y0 = (int16_t)cy_ + ry_ - 1;
    rect(cx_ - 4, y0, cx_ + 4, W - 8, 1);
    rect(cx_ - 3, y0, cx_ + 2, W - 8, 2);
  }
  if (neck_ >= 2u) {
    ellipse(cx_, W + 3, 21, 11, 2, 1);
    if (neck_ == 3u) {
      rect(cx_ - 5, W - 9, cx_ + 5, W - 7, 3);
    }
  } else
    rect(cx_ - 7, W - 7, cx_ + 7, W - 6, 1);
}
static void draw_pattern(void) {
  uint8_t k, x, y;
  switch (pat_) {
  case 1:
    for (y = (uint8_t)(cy_ - ry_); y <= cy_ + ry_; y = (uint8_t)(y + 3u))
      for (x = (uint8_t)(cx_ - rx_); x <= cx_ + rx_; x++)
        if (get(x, y) == 2u && (x & 1u)) px(x, y, 1);
    break;
  case 2:
    for (k = 0; k < 3u; k++) {
      x = (uint8_t)(cx_ - rx_ + 2u + below((uint8_t)(rx_ * 2u - 4u)));
      y = (uint8_t)(cy_ - ry_ + 2u + below((uint8_t)(ry_)));
      while (y < cy_ + ry_ && get(x, y) == 2u) px(x, y++, 3);
      while (x < cx_ + rx_ && get(x, y - 1u) == 2u) px(x++, (uint8_t)(y - 1u), 3);
    }
    break;
  case 3:
    for (k = 0; k < 3u; k++) {
      px((uint8_t)(cx_ - es_ - 2 + k), (uint8_t)(ey_ + 4u + k), 1);
      px((uint8_t)(cx_ + es_ + k), (uint8_t)(ey_ + 4u + k), 1);
    }
    break;
  case 4:
    for (k = 0; k < 4u; k++) {
      x = (uint8_t)(cx_ - rx_ + below((uint8_t)(rx_ * 2u)));
      y = (uint8_t)(cy_ - ry_ + below((uint8_t)(ry_ * 2u)));
      if (get(x, y)) {
        uint8_t c = below(2) ? 3 : 1, w = below(4);
        rect(x, y, x + 2 + w, y, c);
      }
    }
    break; /* the colour rolled first, as sdcc orders the arguments */
  case 5:
    for (k = 0; k < 7u; k++) {
      x = (uint8_t)(cx_ - es_ - 3 + below((uint8_t)(es_ * 2u + 6u)));
      y = (uint8_t)(ey_ + 3u + below(4));
      if (get(x, y) == 2u) px(x, y, 1);
    }
    break;
  case 6:
    for (y = (uint8_t)(cy_ - ry_); y <= cy_ + ry_; y++)
      for (x = cx_; x <= cx_ + rx_; x++)
        if (get(x, y) == 2u && dith(x, y, 1)) px(x, y, 1);
    break;
  case 7:
    px(cx_, (uint8_t)(ey_ - 6u), 3);
    span(cx_ - 1, cx_ + 1, ey_ - 5, 3);
    px(cx_, (uint8_t)(ey_ - 4u), 3);
    break;
  }
}
/* ---- styles beyond the robot heads ---- */
/* a shirt in the face's dark and light tones: 0 tee 1 hoodie 2 stripes 3 grid suit (Tron) 4 jacket */
static void shirt(void) {
  uint8_t kind = below(5), d, h, c;
  int16_t y, x, cy = (int16_t)W + 4;
  for (y = cy - 12; y < (int16_t)W; y++) {
    d = (uint8_t)(cy - y);
    h = half(21, d, 12);
    span(cx_ - h, cx_ + h, y, 1);
    h = half(20, d, 11);
    if (!h) continue;
    for (x = cx_ - h; x <= cx_ + h; x++) {
      int16_t dx = x - cx_;
      uint8_t edge = (uint8_t)(x > cx_ + h - (h >> 2) && dith(x, y, 1));
      switch (kind) {
      case 0: c = edge ? 1 : 3; break;
      case 1:
        c = (x < cx_ - h + (h >> 2) && dith(x, y, 1)) ? 2 : 1;
        if ((dx == -3 || dx == 3) && y < cy - 3) c = 3;
        break;
      case 2: c = ((y >> 1) & 1) ? 3 : (edge ? 1 : 2); break;
      case 3:
        c = 1;
        if (dx == 0 || y == cy - 8 || ((dx == 9 || dx == -9) && y > cy - 8)) c = 3;
        break;
      default:
        c = 1;
        if (dx == (cy - 2 - y) || -dx == (cy - 2 - y)) c = 3;
      }
      if ((kind == 0u || kind == 4u) && (dx < 0 ? -dx : dx) < (int16_t)(cy - 7 - y) && y < cy - 6)
        c = 2; /* the neck's V */
      px((uint8_t)x, (uint8_t)y, c);
    }
  }
}
static void draw_human(void) {
  int16_t top = (int16_t)cy_ - ry_;
  rect(cx_ - 3, cy_ + ry_ - 2, cx_ + 3, W - 7, 1);
  rect(cx_ - 2, cy_ + ry_ - 2, cx_ + 2, W - 7, 2); /* neck */
  shirt();
  if (hair_ == 2u) rect(cx_ - rx_ - 2, cy_ - 4, cx_ + rx_ + 2, cy_ + ry_ + 4, 1); /* long hair behind */
  ellipse(cx_ - rx_, cy_ + 1, 2, 3, 2, 1);
  ellipse(cx_ + rx_, cy_ + 1, 2, 3, 2, 1); /* ears */
  ellipse(cx_, cy_, rx_, ry_, 2, 1);
  switch (hair_) { /* hair over the head */
  case 0: break; /* bald: a shine */
  case 3: ellipse(cx_, top - 3, 5, 4, 1, 0); /* a bun */ /* fall through */
  case 1:
  case 2: {
    int16_t dy;
    for (dy = 0; dy <= (int16_t)(ry_ >> 1); dy++) {
      uint8_t h = half((uint8_t)(rx_ + 1u), (uint8_t)(ry_ - dy), (uint8_t)(ry_ + 1u));
      span(cx_ - h, cx_ + h, top + dy, 1);
    }
    span(cx_ - rx_ + 2, cx_ - 2, top + (ry_ >> 1) + 1, 1);
  } break;
  default: {
    uint8_t k;
    for (k = 0; k < 5u; k++)
      tri(cx_ - rx_ + k * (rx_ >> 1) - 1, top + 4, cx_ - rx_ + k * (rx_ >> 1) + 5, top + 4,
          cx_ - rx_ + k * (rx_ >> 1) + 2, top - 4, 1);
    rect(cx_ - rx_ + 1, top, cx_ + rx_ - 1, top + 4, 1);
  } /* spikes */
  }
  if (!hair_) rect(cx_ - rx_ / 2, top + 3, cx_ - rx_ / 2 + 2, top + 3, 3);
  span(cx_, cx_ + 1, ey_ + 4, 1);
  px((uint8_t)(cx_ + 1), (uint8_t)(ey_ + 3), 1); /* nose */
}
static void draw_poly(void) { /* low poly: a fan of facets around a nose point, lit from the top left */
  int16_t vx[8], vy[8], nx = cx_ + 2, ny = cy_ + 1;
  uint8_t k;
  static const int8_t DX[8] = {0, 11, 16, 12, 0, -12, -16, -11}, DY[8] = {-16, -12, 0, 11, 16, 11, 0, -12};
  static const uint8_t SH[8] = {3, 2, 4, 4, 4, 2, 3, 3};
  for (k = 0; k < 8u; k++) {
    vx[k] = (int16_t)(cx_ + ((DX[k] * (int16_t)rx_) >> 4) + (int16_t)below(3) - 1);
    vy[k] = (int16_t)(cy_ + ((DY[k] * (int16_t)ry_) >> 4) + (int16_t)below(3) - 1);
  }
  rect(cx_ - 3, cy_ + ry_ - 3, cx_ + 3, W - 7, 4 == 4 ? 1 : 1);
  tri(cx_ - 18, W, cx_ + 18, W, cx_, W - 10, 2);
  tri(cx_, W - 10, cx_ + 18, W, cx_ + 6, W, 4);
  for (k = 0; k < 8u; k++)
    tri(vx[k] - (vx[k] < cx_), vy[k] - (vy[k] < cy_), vx[(k + 1u) & 7u] + (vx[(k + 1u) & 7u] > cx_),
        vy[(k + 1u) & 7u] + (vy[(k + 1u) & 7u] > cy_), nx, ny, 1);
  for (k = 0; k < 8u; k++) tri(vx[k], vy[k], vx[(k + 1u) & 7u], vy[(k + 1u) & 7u], nx, ny, SH[k]);
}
static void draw_pixel(void) { /* a symmetric 8x8 sprite at 4x: the old space-invader trick */
  uint8_t r, c, x0 = 8, y0 = 6, rows[8], bit;
  for (r = 0; r < 8u; r++) rows[r] = (uint8_t)(rnd() & 15u);
  rows[0] |= 6;
  rows[2] |= 15;
  rows[3] |= 15;
  rows[4] |= 15;
  rows[5] |= 14;
  for (r = 0; r < 8u; r++)
    for (c = 0; c < 8u; c++) {
      bit = c < 4u ? (uint8_t)(c) : (uint8_t)(7u - c);
      if (rows[r] & (1u << (3u - bit))) {
        rect(x0 + c * 4, y0 + r * 4, x0 + c * 4 + 3, y0 + r * 4 + 3, 2);
        span(x0 + c * 4, x0 + c * 4 + 3, y0 + r * 4 + 3, 1);
        px((uint8_t)(x0 + c * 4 + 3), (uint8_t)(y0 + r * 4 + 2), 1);
        if (r < 3u && c < 4u) px((uint8_t)(x0 + c * 4), (uint8_t)(y0 + r * 4), 3);
      }
    }
}
static void draw_cloud(void) {
  static const int8_t BX[6] = {-9, 0, 9, -14, 14, 0}, BY[6] = {-5, -9, -5, 3, 3, 4};
  static const uint8_t BR[6] = {8, 9, 8, 7, 7, 13};
  uint8_t k;
  for (k = 0; k < 6u; k++)
    ellipse(cx_ + BX[k], cy_ + BY[k], (uint8_t)(BR[k] + 1u), (uint8_t)(BR[k] - (k == 5u ? 4u : 0u) + 1u), 1, 0);
  for (k = 0; k < 6u; k++) ellipse(cx_ + BX[k], cy_ + BY[k], BR[k], (uint8_t)(BR[k] - (k == 5u ? 4u : 0u)), 2, 3);
  ellipse(cx_, W - 6, 10, 1, 1, 2);
}
static void draw_sheet(void) {
  int16_t y;
  ellipse(cx_, cy_ - 2, rx_, rx_, 2, 1);
  for (y = cy_ - 2; y < (int16_t)(W - 6); y++) {
    int16_t h = rx_ + ((y - cy_) >> 3);
    span(cx_ - h - 1, cx_ + h + 1, y, 1);
    shaded(cx_ - h, cx_ + h, y, 0);
  }
  ellipse(cx_, cy_ - 2, (uint8_t)(rx_ - 1u), (uint8_t)(rx_ - 1u), 2, 3);
}
static void draw_flame(void) {
  int16_t y, top = 3;
  uint8_t h;
  for (y = top; y < (int16_t)(W - 4); y++) {
    if (y < cy_ + 4)
      h = (uint8_t)(((y - top) * rx_) / (cy_ + 4 - top));
    else
      h = half(rx_, (uint8_t)(y - cy_ - 4), (uint8_t)(W - 4 - cy_ - 4));
    span(cx_ - h - 1, cx_ + h + 1, y, 1);
    if (h) {
      int16_t x;
      for (x = cx_ - h; x <= cx_ + h; x++)
        px((uint8_t)x, (uint8_t)y,
           (uint8_t)((x - cx_) * (x - cx_) + (y - cy_ - 8) * (y - cy_ - 8) < 60 ? 3
                     : ((x > cx_ + (h >> 1)) && dith(x, y, 1))                  ? 1
                                                                                : 2));
    }
  }
}
static void draw_aieye(void) {
  ellipse(cx_, cy_, (uint8_t)(rx_ + 3u), (uint8_t)(rx_ + 3u), 1, 2);
  ellipse(cx_, cy_, rx_, rx_, 2, 1);
  ellipse(cx_, cy_, (uint8_t)(rx_ - 4u), (uint8_t)(rx_ - 4u), 1, 2);
}

/* the silhouette's outside edge, once, from the base: a clear pixel with a drawn one beside, above or below, on every
 * other diagonal, top to bottom and left to right, up to 128. A row at a time as bytes (6 per row): the drawn pixels
 * of the rows above, at and below, shifted for the left and right neighbours. */
static void row_mask(uint8_t y, uint8_t *m) {
  const uint8_t *p = buf_ + (uint16_t)((y >> 3) * 6u) * 16u + (uint8_t)((y & 7u) << 1);
  uint8_t k;
  for (k = 0; k < 6u; k++) {
    m[k] = (uint8_t)(p[0] | p[1]);
    p += 16;
  }
}
static void aura_find(void) {
  uint8_t up[6], at[6], dn[6], y, k, e, bit, x, side;
  fxn_ = 0;
  row_mask(0, at);
  row_mask(1, dn);
  for (y = 1; y < W - 1u; y++) {
    memcpy(up, at, 6);
    memcpy(at, dn, 6);
    row_mask((uint8_t)(y + 1u), dn);
    for (k = 0; k < 6u; k++) {
      side = (uint8_t)((at[k] >> 1) | (at[k] << 1));
      if (k) side |= (uint8_t)(at[k - 1u] << 7);
      if (k < 5u) side |= (uint8_t)(at[k + 1u] >> 7);
      e = (uint8_t)(~at[k] & (side | up[k] | dn[k]) & ((y & 1u) ? 0xaau : 0x55u));
      if (!k) e &= 0x7fu;
      if (k == 5u) e &= 0xfeu;
      if (!e) continue;
      for (bit = 0x80u, x = (uint8_t)(k << 3); bit; bit >>= 1, x++)
        if (e & bit) {
          if (fxn_ >= sizeof aura_x_) return;
          aura_x_[fxn_] = x;
          aura_y_[fxn_] = y;
          fxn_++;
        }
    }
  }
}
void avatar_base(void) BANKED {
  memset(base_, 0, AVATAR_BYTES);
  buf_ = base_;
  switch (style_) {
  case 1:
    draw_human();
    draw_gear_front();
    break;
  case 2: draw_poly(); break;
  case 3: draw_pixel(); break;
  case 4:
    draw_cloud();
    draw_gear_front();
    break;
  case 5:
    draw_gear_back();
    draw_aieye();
    draw_gear_front();
    break;
  case 6:
    draw_sheet();
    draw_gear_front();
    break;
  case 7: draw_flame(); break;
  case 8:
    ellipse(cx_, cy_, (uint8_t)(rx_ + 1u), (uint8_t)(ry_ + 1u), 3, 2);
    ellipse(cx_, cy_, rx_, ry_, 2, 1);
    break;
  default:
    draw_neck();
    draw_gear_back();
    draw_head();
    draw_pattern();
    draw_gear_front();
  }
  if (mark_) { /* the mark: a 3x3 glyph on the forehead, ink on the face */
    static const uint16_t MARK[8] = {0, 0x0aau, 0x1bau, 0x092u, 0x145u, 0x038u, 0x1efu, 0x111u};
    uint8_t i, j, x, y;
    uint16_t m = MARK[mark_], bit = 0x100u;
    for (j = 0; j < 3u; j++)
      for (i = 0; i < 3u; i++, bit >>= 1)
        if (m & bit) {
          x = (uint8_t)(cx_ - 1u + i);
          y = (uint8_t)(ey_ - 6u + j);
          if (get(x, y)) px(x, y, 1);
        }
  }
  if (fx_ == 1u) aura_find();
}
