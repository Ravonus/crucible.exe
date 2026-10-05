/* Avatars: talking faces generated on the cartridge from primitives, so every character the program sends you can
 * have its own face for no art bytes. A face is a genome (head shape, eyes, mouth, headgear, a face pattern, neck,
 * hue, size) rolled from a seed and biased by faction (programs get screens and visors, ghosts hollow eyes and no
 * mouth, daemons horns...), with a wild roll now and then because it is a dream. It is drawn once, as 48x48 2bpp
 * sprite tiles (outlined, dithered on the shadow side, lit on the top left), and only the features are redrawn as
 * it lives: blinks, gaze, a mouth that follows the typewriter's letters, a visor glint, a blinking antenna light.
 * The host streams the 36 tiles into the hidden VRAM bank a third per frame (avatar_chunk), as with the busts.
 * Pure C apart from avatar_chunk: tools build it on the host to preview faces (AVATAR_HOST). */
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

#ifdef AVATAR_HOST
uint8_t base_[AVATAR_BYTES];
#endif
uint8_t work_[AVATAR_BYTES];
uint16_t rng_;
uint8_t fx_, fxn_, fx_t_, bolt_, bolt_x_;
uint8_t aura_x_[AURA_N], aura_y_[AURA_N], smoke_x_[4], smoke_y_[4];
uint8_t style_, hair_, phase_, head_, eyes_, mouth_, gear_, pat_, neck_, hue_, rx_, ry_, cx_, cy_, ey_, es_, my_;
uint8_t blink_, blink_t_, gaze_t_, mouth_t_, open_, glint_, light_, dirty_, bob_, bob_t_;
int8_t gaze_;
uint8_t dither_, mark_;
uint8_t avatar_still; /* 1: no idle bob (a fight: rows of a half-moved face would put 12 sprites on a line) */
static uint8_t warp_, warp_out_; /* a glitch entrance (rows drop in) or exit (rows tear away) */
uint16_t pal_[4];
uint8_t *buf_;

/* ---- the genome: faction tastes, with a wild roll ---- */
/* heads: 0 oval 1 box 2 taper 3 capsule 4 screen 5 hood 6 mask 7 orb
 * eyes: 0 dots 1 visor 2 cyclops 3 pixels 4 crosses 5 slits 6 rings 7 three
 * mouths: 0 line 1 grille 2 none 3 o 4 speaker 5 smile 6 teeth 7 wave
 * gear: 0 none 1 antenna 2 two antennae 3 halo 4 horns 5 crest 6 headphones 7 cables
 * patterns: 0 none 1 scanlines 2 circuit 3 cheek marks 4 glitch 5 freckles 6 split 7 gem */
static const uint8_t TASTE[6][4] = {
    /* per faction: a nibble pair of favourite heads, eyes, mouths, gear */
    {0x14, 0x13, 0x17, 0x15}, /* PROGRAM: box/screen, visor/pixels, grille/wave, antenna/crest */
    {0x62, 0x54, 0x16, 0x47}, /* DAEMON: mask/taper, slits/crosses, grille/teeth, horns/cables */
    {0x75, 0x62, 0x23, 0x30}, /* GHOST: orb/hood, rings/cyclops, none/o, halo/none */
    {0x70, 0x21, 0x72, 0x31}, /* AI: orb/oval, cyclops/visor, wave/none, halo/antenna */
    {0x03, 0x05, 0x50, 0x67}, /* OPERATOR: oval/capsule, dots/slits, smile/line, headphones/cables */
    {0x41, 0x37, 0x43, 0x12}}; /* RELIC: screen/box, pixels/three, speaker/o, antennae */
static const uint8_t HUES[6][2] = {{8, 9}, {0, 1}, {11, 12}, {10, 6}, {5, 3}, {2, 14}};
static const uint16_t MID[16] = {/* RGB555 mid tones around the wheel */
                                 0x10DC, 0x0D5E, 0x027E, 0x0332, 0x0B0B, 0x2AE6, 0x4AA6, 0x5A08,
                                 0x6D86, 0x6D6C, 0x5D32, 0x6118, 0x611C, 0x41DE, 0x31F9, 0x4210};
/* styles: 0 robot 1 human 2 polygon 3 pixel 4 cloud 5 AI eye 6 sheet ghost 7 flame */
/* 8 radiant (a face in turning rays) */
static const uint8_t STYLES[6][4] = {{0, 3, 2, 8}, {7, 0, 2, 7}, {6, 4, 1, 8},
                                     {5, 2, 8, 5}, {1, 1, 3, 1}, {3, 0, 4, 1}};
static const uint8_t FX[6][4] = {{4, 1, 0, 5}, {3, 2, 2, 0}, {2, 4, 6, 0}, {5, 4, 1, 0}, {0, 7, 0, 6}, {6, 7, 0, 2}};
static uint8_t pick(uint8_t pair) { return below(2) ? (uint8_t)(pair >> 4) : (uint8_t)(pair & 15u); }
static uint16_t shade(uint16_t c, uint8_t light) {
  uint8_t k, v;
  uint16_t o = 0;
  for (k = 0; k < 3u; k++) {
    v = (uint8_t)((c >> (k * 5u)) & 31u);
    v = light ? (uint8_t)(v + (((31u - v) * 2u) / 3u)) : (uint8_t)((v * 2u) / 7u);
    o |= (uint16_t)v << (k * 5u);
  }
  return o;
}

/* ---- shaders: effects any face can wear, run over the composed face each frame they change ----
 * 0 none 1 aura (marching light around the silhouette) 2 smoke 3 lightning 4 hologram 5 orbiting lights
 * 6 sparkles 7 static */
static const int8_t SIN16[16] = {0, 6, 11, 15, 16, 15, 11, 6, 0, -6, -11, -15, -16, -15, -11, -6};
static void rays(void) { /* the radiant head: eight light spikes turning around it */
  uint8_t k, a, b;
  int16_t r0 = (int16_t)rx_ + 1, r1;
  for (k = 0; k < 8u; k++) {
    a = (uint8_t)((k * 2u + (phase_ >> 1)) & 15u);
    b = (uint8_t)((a + 1u) & 15u);
    r1 = r0 + 6 + ((k + (phase_ >> 2)) & 1u) * 3;
    tri(cx_ + ((SIN16[(a + 4u) & 15u] * r0) >> 4), cy_ + ((SIN16[a] * r0) >> 4),
        cx_ + ((SIN16[(b + 4u) & 15u] * r0) >> 4), cy_ + ((SIN16[b] * r0) >> 4),
        cx_ + ((SIN16[(a + 4u) & 15u] * r1) >> 4), cy_ + ((SIN16[a] * r1) >> 4), k & 1u ? 3 : 2);
  }
}
static void shader(void) {
  uint8_t k, x, y;
  switch (fx_) {
  case 1:
    for (k = (uint8_t)(phase_ & 1u); k < fxn_; k = (uint8_t)(k + 2u)) px(aura_x_[k], aura_y_[k], 3);
    break;
  case 2:
    for (k = 0; k < 4u; k++) {
      int16_t sx = smoke_x_[k] + (SIN16[(smoke_y_[k] + k * 4u) & 15u] >> 3);
      dot(sx, smoke_y_[k], 2);
      dot(sx + 1, smoke_y_[k], (smoke_y_[k] & 1u) ? 1 : 2);
      dot(sx, smoke_y_[k] - 1, 2);
      if (smoke_y_[k] < 30u) dot(sx - 1, smoke_y_[k] + 1, 1);
    }
    break;
  case 3:
    if (bolt_) {
      int16_t bx = bolt_x_;
      rng_ ^= bolt_x_;
      for (y = 0; y < 16u; y++) {
        bx += (int16_t)below(3) - 1;
        dot(bx, y, 3);
        dot(bx + 1, y, 1);
      }
    }
    break;
  case 4: {
    uint8_t band = (uint8_t)((phase_ * 2u) % W);
    for (y = band; y < band + 3u && y < W; y++)
      for (x = (uint8_t)(y & 1u); x < W; x = (uint8_t)(x + 2u))
        if (get(x, y)) px(x, y, 0);
    for (x = 0; x < W; x = (uint8_t)(x + 4u))
      if (get(x, (uint8_t)((band + 20u) % W)) == 2u) px(x, (uint8_t)((band + 20u) % W), 3);
  } break;
  case 5:
    for (k = 0; k < 3u; k++) {
      uint8_t a = (uint8_t)((phase_ + k * 5u) & 15u);
      int16_t ox = cx_ + ((SIN16[(a + 4u) & 15u] * ((int16_t)rx_ + 6)) >> 4), oy = (int16_t)cy_ - 6 + (SIN16[a] >> 2);
      if (SIN16[a] >= 0) {
        rect(ox, oy, ox + 1, oy + 1, 3);
      } else {
        dot(ox, oy, 3);
      }
    }
    break;
  case 6:
    for (k = 0; k < 3u; k++) {
      int16_t sx = (int16_t)below(W), sy = (int16_t)below(W);
      if (!get((uint8_t)sx, (uint8_t)sy)) {
        dot(sx, sy, 3);
        dot(sx - 1, sy, 2);
        dot(sx + 1, sy, 2);
        dot(sx, sy - 1, 2);
        dot(sx, sy + 1, 2);
      }
    }
    break;
  case 7:
    for (k = 0; k < 14u; k++) {
      x = below(W);
      y = below(W);
      if (get(x, y)) px(x, y, (uint8_t)(1u + below(3)));
    }
    break;
  }
}
static void fx_step(void) {
  uint8_t k;
  phase_++;
  if (fx_ == 2u)
    for (k = 0; k < 4u; k++) {
      if (smoke_y_[k] < 3u || smoke_y_[k] >= W) {
        smoke_y_[k] = (uint8_t)(W - 8u - below(10));
        smoke_x_[k] = (uint8_t)(cx_ - rx_ - 4u + below((uint8_t)(rx_ * 2u + 8u)));
      } else
        smoke_y_[k]--;
    }
  if (fx_ == 3u) {
    if (bolt_)
      bolt_--;
    else if (!below(24)) {
      bolt_ = 3;
      bolt_x_ = (uint8_t)(6u + below(36));
    }
  }
}
uint8_t avatar_flash(void) BANKED { return fx_ == 3u && bolt_ == 3u; }

/* ---- the live features ---- */
static void eye_at(int16_t x, uint8_t big) {
  int16_t y = ey_;
  int8_t g = gaze_;
  if (blink_) {
    span(x - 2 - big, x + 2 + big, y, 1);
    return;
  }
  switch (eyes_) {
  case 0:
    rect(x - 1, y - 1, x + 1, y + 1, 3);
    px((uint8_t)(x + g), (uint8_t)y, 1);
    break;
  case 2:
    ellipse(x, y, 4, 3, 1, 0);
    ellipse(x, y, 3, 2, 3, 0);
    ellipse(x + g * 2, y, 1, 1, 1, 0);
    break;
  case 3:
    rect(x - 2, y - 2, x + 1, y + 1, 3);
    rect(x - 1 + g, y - 1, x + g, y, 1);
    break;
  case 4: {
    int8_t k;
    for (k = -2; k <= 2; k++) {
      px((uint8_t)(x + k), (uint8_t)(y + k), 1);
      px((uint8_t)(x + k), (uint8_t)(y - k), 1);
    }
  } break;
  case 5:
    span(x - 3, x + 2, y, 3);
    span(x - 3, x + 2, y + 1, 1);
    break;
  case 6:
    ellipse(x, y, 3, 3, 1, 2);
    px((uint8_t)(x + g), (uint8_t)y, 3);
    break;
  case 8:
    rect(x - 2, y - 1, x + 2, y, 3);
    rect(x + g - (g > 0), y - 1, x + g + (g >= 0), y, 1);
    span(x - 2, x + 2, y - 3 - (open_ > 1u), 1);
    break; /* human: whites, iris, brow (raised on wide vowels) */
  case 9:
    ellipse(x, y, (uint8_t)(rx_ - 6u), (uint8_t)(rx_ - 6u), 3, 0);
    ellipse(x + g * 3, y, 3, 3, 1, 0);
    px((uint8_t)(x + g * 3 - 1), (uint8_t)(y - 1), 3);
    break; /* the AI's great eye */
  default: rect(x - 1, y - 1, x + 1, y + 1, 3); px((uint8_t)(x + g), (uint8_t)y, 1);
  }
}
static void draw_eyes(void) {
  if (eyes_ == 1u) { /* visor: a band with a glint that sweeps */
    rect(cx_ - rx_ + 2, ey_ - 2, cx_ + rx_ - 2, ey_ + 1, 1);
    if (!blink_) {
      span(cx_ - rx_ + 3, cx_ + rx_ - 3, ey_, 2);
      rect(cx_ - rx_ + 3 + glint_, ey_ - 1, cx_ - rx_ + 5 + glint_, ey_, 3);
    }
    return;
  }
  if (eyes_ == 2u || eyes_ == 9u) {
    eye_at(cx_, 2);
    return;
  }
  eye_at(cx_ - es_, 0);
  eye_at(cx_ + es_, 0);
  if (eyes_ == 7u) {
    int16_t y = ey_;
    ey_ = (uint8_t)(ey_ - 6u);
    eye_at(cx_, 0);
    ey_ = (uint8_t)y;
  }
}
static void draw_mouth(void) {
  int16_t x = cx_, y = my_, w = (int16_t)(rx_ >> 1);
  uint8_t k, o = open_;
  switch (mouth_) {
  case 0:
    rect(x - w + 1, y, x + w - 1, y + (o ? o : 0), 1);
    if (o > 1u) span(x - w + 2, x + w - 2, y + 1, 3);
    break;
  case 1:
    for (k = 0; k <= (uint8_t)(w * 2); k += 2) rect(x - w + k, y - 1, x - w + k, y + 1 + o, 1);
    break;
  case 3: ellipse(x, y, (uint8_t)(1u + (o >> 1)), (uint8_t)(1u + o), 1, 2); break;
  case 4:
    for (k = 0; k < 3u; k++) {
      px((uint8_t)(x - 2 + k * 2), (uint8_t)y, o ? 3 : 1);
      px((uint8_t)(x - 2 + k * 2), (uint8_t)(y + 2), o ? 3 : 1);
    }
    break;
  case 5:
    if (o)
      ellipse(x, y, (uint8_t)w, o, 1, 0);
    else {
      span(x - w + 2, x + w - 2, y + 1, 1);
      px((uint8_t)(x - w + 1), (uint8_t)y, 1);
      px((uint8_t)(x + w - 1), (uint8_t)y, 1);
    }
    break;
  case 6:
    rect(x - w, y - 1, x + w, y + 1 + o, 1);
    for (k = 0; k <= (uint8_t)(w * 2); k += 2) {
      px((uint8_t)(x - w + k), (uint8_t)(y - 1), 3);
      px((uint8_t)(x - w + k), (uint8_t)(y + 1 + o), 3);
    }
    break;
  case 7:
    rect(x - w - 1, y - 2, x + w + 1, y + 2, 1);
    for (k = 0; k <= (uint8_t)(w * 2); k++)
      px((uint8_t)(x - w + k), (uint8_t)(y + (o ? (int8_t)(below(3)) - 1 : 0)), 3);
    break;
  }
}
static void compose(void) {
  BASE_ON;
  memcpy(work_, base_, AVATAR_BYTES);
  BASE_OFF;
  buf_ = work_;
  if (style_ == 7u) {
    uint8_t k;
    for (k = 0; k < 4u; k++) {
      uint8_t x = (uint8_t)(cx_ - rx_ + 2u + below((uint8_t)(rx_ * 2u - 4u))), y = (uint8_t)(4u + below(10));
      if (get(x, (uint8_t)(y + 1u))) span(x, x + 1, y, below(2) ? 3 : 2);
    }
  } /* flicker */
  if (style_ == 6u) {
    uint8_t x;
    for (x = (uint8_t)(cx_ - rx_ - 3u); x <= cx_ + rx_ + 3u; x++)
      if (((x + phase_) >> 2) & 1u) span(x, x, W - 6, 0);
  } /* the hem waves */
  if (style_ == 5u && light_) ellipse(cx_, cy_, (uint8_t)(rx_ + 3u), (uint8_t)(rx_ + 3u), 3, 2); /* the ring pulses */
  if (warp_) { /* rows drop out or smear from a neighbour, fewer as it arrives, more as it leaves */
    uint8_t y, k, *row, *src;
    for (y = 0; y < W; y++)
      if ((rnd() & 15u) < warp_) {
        row = work_ + (uint16_t)((y >> 3) * 6u) * 16u + (uint8_t)((y & 7u) << 1);
        if (rnd() & 1u) {
          for (k = 0; k < 6u; k++) {
            row[k * 16u] = 0;
            row[k * 16u + 1u] = 0;
          }
        } else {
          uint8_t z = (uint8_t)((y + 3u + (rnd() & 7u)) % W);
          src = work_ + (uint16_t)((z >> 3) * 6u) * 16u + (uint8_t)((z & 7u) << 1);
          for (k = 0; k < 5u; k++) {
            row[k * 16u] = src[(k + 1u) * 16u];
            row[k * 16u + 1u] = src[(k + 1u) * 16u + 1u];
          }
        }
      }
  }
  if (style_ == 8u) rays();
  draw_eyes();
  draw_mouth();
  shader();
  if (gear_ == 1u) {
    int16_t top = (int16_t)cy_ - ry_ - 1;
    rect(cx_ + 2, top - 9, cx_ + 4, top - 7, light_ ? 3 : 1);
  } else if (gear_ == 2u) {
    uint8_t ty = (uint8_t)(cy_ - ry_ - 7u);
    px((uint8_t)(cx_ - 10u), ty, light_ ? 3 : 1);
    px((uint8_t)(cx_ + 10u), ty, light_ ? 1 : 3);
  } /* 8 bits: sdcc dropped the int16 top here */
}

/* ---- API ---- */
/* the per-style geometry, palette, shader and the live features: shared by a rolled face and a genome's */
static void finish(uint16_t mid, uint8_t fx) {
  if (style_) {
    ey_ = (uint8_t)(cy_ - (ry_ >> 2));
    my_ = (uint8_t)(cy_ + (ry_ >> 1) + 1u);
    es_ = (uint8_t)(3u + (rx_ >> 2));
  }
  if (style_ == 3u) {
    ey_ = 19;
    es_ = 6;
    my_ = 31;
  }
  if (style_ == 5u) {
    ey_ = cy_;
    my_ = (uint8_t)(cy_ + rx_ + 6u);
  }
  if (style_ == 6u) {
    ey_ = (uint8_t)(cy_ - 3u);
    my_ = (uint8_t)(cy_ + 4u);
  }
  if (style_ == 7u) {
    ey_ = (uint8_t)(cy_ + 4u);
    my_ = (uint8_t)(cy_ + 11u);
  }
  if (style_ == 4u) {
    ey_ = (uint8_t)(cy_ - 1u);
    my_ = (uint8_t)(cy_ + 5u);
  }
  if (hue_ != 0xffu) mid = MID[hue_];
  if (style_ == 4u || style_ == 6u || style_ == 8u) mid = shade(mid, 1); /* clouds and ghosts are pale */
  pal_[0] = 0;
  pal_[1] = shade(mid, 0);
  pal_[2] = mid;
  pal_[3] = shade(mid, 1);
  fx_ = fx;
  if (style_ == 8u && !fx_) fx_ = 1;
  BASE_ON;
  avatar_base();
  BASE_OFF;
  {
    uint8_t k;
    for (k = 0; k < 4u; k++) {
      smoke_y_[k] = (uint8_t)(10u + k * 9u);
      smoke_x_[k] = (uint8_t)(cx_ - rx_ + below((uint8_t)(rx_ * 2u)));
    }
  }
  bolt_ = 0;
  fx_t_ = 0;
  blink_ = 0;
  blink_t_ = (uint8_t)(60u + below(120));
  gaze_t_ = (uint8_t)(90u + below(120));
  gaze_ = 0;
  open_ = 0;
  mouth_t_ = 0;
  glint_ = 0;
  light_ = 0;
  bob_ = 0;
  bob_t_ = 40;
  compose();
  dirty_ = 0;
}
void avatar_make(uint16_t seed, uint8_t faction) BANKED {
  uint16_t mid;
  uint8_t wild;
  rng_ = seed ? seed : 0x1d2bu;
  rnd();
  rnd();
  mark_ = 0;
  if (faction > 5u) faction = 0;
  wild = below(5) == 0u;
  dither_ = below(6);
  style_ = wild ? below(9) : STYLES[faction][below(4)];
  head_ = wild ? below(8) : pick(TASTE[faction][0]);
  eyes_ = wild ? below(8) : pick(TASTE[faction][1]);
  mouth_ = wild ? below(8) : pick(TASTE[faction][2]);
  gear_ = wild ? below(8) : pick(TASTE[faction][3]);
  pat_ = below(3) ? below(8) : 0;
  neck_ = below(4);
  rx_ = (uint8_t)(11u + below(5));
  ry_ = (uint8_t)(12u + below(5));
  if (head_ == 3u && ry_ <= rx_ + 2u) ry_ = (uint8_t)(rx_ + 3u);
  cx_ = 24;
  cy_ = (uint8_t)(head_ == 7u ? 22u : 21u);
  ey_ = (uint8_t)(cy_ - (ry_ >> 2));
  es_ = (uint8_t)(3u + (rx_ >> 2) + below(2));
  my_ = (uint8_t)(cy_ + (ry_ >> 1) + 1u);
  if (head_ == 5u) {
    ey_ = (uint8_t)(ey_ + 2u);
    my_ = (uint8_t)(my_ + 1u);
  }
  hue_ = wild ? below(16) : HUES[faction][below(2)];
  mid = 0;
  hair_ = below(5);
  phase_ = 0;
  switch (style_) { /* features that suit each style */
  case 1:
    eyes_ = 8;
    mouth_ = below(2) ? 5 : 0;
    gear_ = below(4) == 0u ? 6 : 0;
    {
      static const uint16_t SKIN[6] = {0x2E7F, 0x223B, 0x19B7, 0x1135, 0x08CF, 0x3EDF};
      hue_ = 0xffu;
      mid = SKIN[below(6)];
    }
    break;
  case 2:
    eyes_ = below(2) ? 5 : 3;
    mouth_ = below(2) ? 0 : 7;
    gear_ = 0;
    break;
  case 3:
    eyes_ = 3;
    mouth_ = below(2) ? 1 : 4;
    gear_ = 0;
    rx_ = 16;
    ry_ = 15;
    cx_ = 24;
    cy_ = 21;
    break;
  case 4:
    eyes_ = below(2) ? 0 : 6;
    mouth_ = below(2) ? 5 : 3;
    gear_ = below(3) == 0u ? 3 : 0;
    rx_ = 16;
    ry_ = 12;
    cy_ = 22;
    break;
  case 5:
    eyes_ = 9;
    mouth_ = below(2) ? 2 : 7;
    gear_ = below(2) ? 3 : 0;
    rx_ = 14;
    ry_ = 14;
    cy_ = 20;
    break;
  case 6:
    eyes_ = below(2) ? 6 : 0;
    mouth_ = below(2) ? 3 : 2;
    gear_ = below(3) == 0u ? 3 : 0;
    rx_ = 12;
    cy_ = 18;
    break;
  case 7:
    eyes_ = below(2) ? 5 : 4;
    mouth_ = below(2) ? 6 : 1;
    gear_ = 0;
    rx_ = 15;
    cy_ = 24;
    break;
  case 8:
    eyes_ = below(2) ? 0 : 6;
    mouth_ = below(2) ? 0 : 3;
    gear_ = 0;
    rx_ = 11;
    ry_ = 12;
    cy_ = 22;
    break; /* radiant: a face in a ring of turning light */
  }
  {
    uint8_t fx = wild ? below(8) : FX[faction][below(4)];
    finish(mid, fx);
  }
}
void avatar_say(char c) BANKED {
  uint8_t o = (c == 'A' || c == 'O')               ? 2u
              : (c == 'E' || c == 'I' || c == 'U') ? 1u
              : (c == ' ' || c == '.' || c == ',') ? 0u
                                                   : (uint8_t)(below(2));
  mouth_t_ = 6;
  if (o != open_) {
    open_ = o;
    dirty_ = 1;
  }
}
uint8_t avatar_tick(uint8_t dt) BANKED {
  while (dt--) {
    if (warp_ && !(bob_t_ & 1u)) {
      if (warp_out_) {
        if (warp_ < 16u) warp_++;
      } else
        warp_--;
      dirty_ = 1;
    }
    if (blink_) {
      if (!--blink_) dirty_ = 1;
    } else if (!--blink_t_) {
      blink_ = 5;
      blink_t_ = (uint8_t)(70u + below(160));
      dirty_ = 1;
      if (!below(6)) blink_t_ = 9; /* a double blink */
    }
    if (!--gaze_t_) {
      gaze_ = (int8_t)(below(3)) - 1;
      gaze_t_ = (uint8_t)(80u + below(160));
      dirty_ = 1;
    }
    if (mouth_t_ && !--mouth_t_ && open_) {
      open_ = 0;
      dirty_ = 1;
    }
    if (!--bob_t_) {
      bob_ = avatar_still ? 0u : (uint8_t)(bob_ ^ 1u);
      bob_t_ = 40;
    }
    if (eyes_ == 1u && !(rng_ & 3u)) {
      glint_ = (uint8_t)(glint_ + 1u);
      if (glint_ > (uint8_t)(rx_ * 2u - 8u)) glint_ = 0;
      dirty_ = 1;
    }
    if ((gear_ == 1u || gear_ == 2u || style_ == 5u) && !(rnd() & 63u)) {
      light_ ^= 1u;
      dirty_ = 1;
    }
    if ((style_ == 7u || style_ == 6u || style_ == 8u || fx_) && ++fx_t_ >= (style_ == 8u ? 8u : 4u)) {
      fx_t_ = 0;
      fx_step();
      dirty_ = 1;
    }
  }
  if (!dirty_) return 0;
  dirty_ = 0;
  compose();
  return 1;
}
void avatar_glitch(uint8_t in) BANKED {
  warp_ = in ? 15u : 1u;
  warp_out_ = !in;
  dirty_ = 1;
}
void avatar_fx(uint8_t fx) BANKED {
  if (fx != 1u) {
    fx_ = fx;
    dirty_ = 1;
  }
}
uint8_t avatar_bob(void) BANKED { return bob_; }
void avatar_palette(uint16_t *out) BANKED {
  uint8_t i;
  for (i = 0; i < 4u; i++) out[i] = pal_[i];
}
const uint8_t *avatar_tiles(void) { return work_; }
/* A face from a genome (docs/fight-system.md 8.1), the player's: the same generator with the creator's choices
 * instead of rolls, mapped onto what each style allows (a human's eyes are always its own; a pixel face has square
 * eyes...). The genome's bytes also seed what stays alive (blinks, smoke, sparks). */
static const uint16_t SKIN6[6] = {0x2E7F, 0x223B, 0x19B7, 0x1135, 0x08CF, 0x3EDF};
void avatar_make_genome(const uint8_t *g) BANKED {
  uint8_t sz, m, e, gr, look = (uint8_t)((g[5] & AV_LOOK_MASK) >> 3);
  uint16_t mid = 0;
  rng_ = (uint16_t)(0x1d2bu ^ g[0] ^ ((uint16_t)g[1] << 8) ^ g[2] ^ ((uint16_t)g[3] << 5) ^ g[4]);
  if (!rng_) rng_ = 0x1d2bu;
  rnd();
  rnd();
  warp_ = 0;
  warp_out_ = 0; /* a still face: no glitch left over from the last one */
  style_ = (uint8_t)(g[0] & 15u);
  if (style_ > 8u) style_ = 0;
  if (look == AV_LOOK_BOY || look == AV_LOOK_GIRL) style_ = 1;
  hue_ = (uint8_t)(g[0] >> 4);
  head_ = (uint8_t)(g[1] & 7u);
  e = (uint8_t)((g[1] >> 3) & 15u);
  eyes_ = e > 7u ? 0u : e;
  m = (uint8_t)(g[2] & 7u);
  mouth_ = m;
  gr = (uint8_t)((g[2] >> 3) & 7u);
  gear_ = gr;
  neck_ = (uint8_t)(g[2] >> 6);
  pat_ = (uint8_t)(g[3] & 7u);
  dither_ = (uint8_t)((g[3] >> 3) & 7u);
  if (dither_ > 5u) dither_ = 0;
  sz = (uint8_t)(g[3] >> 6);
  if (sz > 2u) sz = 1;
  rx_ = (uint8_t)(11u + sz + sz);
  ry_ = (uint8_t)(12u + sz + sz);
  if (head_ == 3u && ry_ <= rx_ + 2u) ry_ = (uint8_t)(rx_ + 3u);
  cx_ = 24;
  cy_ = (uint8_t)(head_ == 7u ? 22u : 21u);
  ey_ = (uint8_t)(cy_ - (ry_ >> 2));
  es_ = (uint8_t)(3u + (rx_ >> 2));
  my_ = (uint8_t)(cy_ + (ry_ >> 1) + 1u);
  if (head_ == 5u) {
    ey_ = (uint8_t)(ey_ + 2u);
    my_ = (uint8_t)(my_ + 1u);
  }
  hair_ = (uint8_t)(g[5] & 3u);
  phase_ = 0;
  mark_ = (uint8_t)((g[4] >> 3) & 7u);
  switch (style_) { /* what each style allows: the genome's bit picks between the two the generator would roll */
  case 1:
    eyes_ = 8;
    mouth_ = (m & 1u) ? 5 : 0;
    gear_ = gr == 6u ? 6 : 0;
    if (gr < 5u) hair_ = gr;
    if (look == AV_LOOK_BOY) hair_ = (gr & 1u) ? 4u : 1u;
    if (look == AV_LOOK_GIRL) hair_ = (gr & 1u) ? 3u : 2u;
    hue_ = 0xffu;
    mid = SKIN6[(uint8_t)(g[0] >> 4) % 6u];
    break; /* a human's crown is its hair */
  case 2:
    eyes_ = (e & 1u) ? 5 : 3;
    mouth_ = (m & 1u) ? 0 : 7;
    gear_ = 0;
    break;
  case 3:
    eyes_ = 3;
    mouth_ = (m & 1u) ? 1 : 4;
    gear_ = 0;
    rx_ = 16;
    ry_ = 15;
    cx_ = 24;
    cy_ = 21;
    break;
  case 4:
    eyes_ = (e & 1u) ? 0 : 6;
    mouth_ = (m & 1u) ? 5 : 3;
    gear_ = gr == 3u ? 3 : 0;
    rx_ = 16;
    ry_ = 12;
    cy_ = 22;
    break;
  case 5:
    eyes_ = 9;
    mouth_ = (m & 1u) ? 2 : 7;
    gear_ = gr == 3u ? 3 : 0;
    rx_ = 14;
    ry_ = 14;
    cy_ = 20;
    break;
  case 6:
    eyes_ = (e & 1u) ? 6 : 0;
    mouth_ = (m & 1u) ? 3 : 2;
    gear_ = gr == 3u ? 3 : 0;
    rx_ = 12;
    cy_ = 18;
    break;
  case 7:
    eyes_ = (e & 1u) ? 5 : 4;
    mouth_ = (m & 1u) ? 6 : 1;
    gear_ = 0;
    rx_ = 15;
    cy_ = 24;
    break;
  case 8:
    eyes_ = (e & 1u) ? 0 : 6;
    mouth_ = (m & 1u) ? 0 : 3;
    gear_ = 0;
    rx_ = 11;
    ry_ = 12;
    cy_ = 22;
    break;
  }
  if (g[5] & 4u) {
    hue_ = 0xffu;
    mid = 0x0690u;
  } /* the DMG green (a secret's swatch) */
  finish(mid, (uint8_t)(g[4] & 7u));
}
#ifndef AVATAR_HOST
/* The face as 36 background tiles at map (x, y), palette 4: the fight's spare cell tiles (CL and CN are unused there),
 * VRAM bank 1 tiles 48..63 and 80..95 and bank 0 tiles 48..51; the palette's colour 0 is the screen's. avatar_bg_end
 * gives palette 4 back. */
static uint16_t bg_pal_[4];
static uint8_t bg_saved_;
extern uint8_t room_dirty;
void avatar_bg_map(uint8_t x, uint8_t y) BANKED {
  uint8_t i, r, map[6], at[6], t;
  for (r = 0; r < 6u; r++) {
    for (i = 0; i < 6u; i++) {
      t = (uint8_t)(r * 6u + i);
      map[i] = t < 16u ? (uint8_t)(48u + t) : t < 32u ? (uint8_t)(64u + t) : (uint8_t)(16u + t);
      at[i] = t < 32u ? 0x0cu : 0x04u;
    }
    VBK_REG = 1;
    set_bkg_tiles(x, (uint8_t)(y + r), 6, 1, at);
    VBK_REG = 0;
    set_bkg_tiles(x, (uint8_t)(y + r), 6, 1, map);
  }
}
void avatar_bg(uint8_t x, uint8_t y) BANKED {
  uint16_t p[4], *base = (uint16_t *)0xbd00u;
  VBK_REG = 1;
  set_bkg_data(48, 16, work_);
  set_bkg_data(80, 16, work_ + 256u);
  VBK_REG = 0;
  set_bkg_data(48, 4, work_ + 512u);
  avatar_bg_map(x, y);
  ENABLE_RAM;
  SWITCH_RAM(2);
  if (!bg_saved_) {
    memcpy(bg_pal_, base + 16u, 8);
    bg_saved_ = 1;
  }
  p[0] = base[28];
  p[1] = pal_[1];
  p[2] = pal_[2];
  p[3] = pal_[3];
  memcpy(base + 16u, p, 8);
  DISABLE_RAM;
  room_dirty = 1;
}
/* The player's face is the same fight after fight: kept in SRAM bank 2 at 0xB200 (scratch no save uses: 'F', the
 * genome, the palette, the 36 tiles), so a fight opens with it at once and only the opponent's face is drawn anew. */
#define FACE_CACHE ((uint8_t *)0xB200u)
/* 1: the cache holds this genome's face; avatar_cache draws it there when it does not (the live face is redrawn: call
 * it where no talker is on screen) */
uint8_t avatar_cached(const uint8_t *g) BANKED {
  uint8_t hit;
  ENABLE_RAM;
  SWITCH_RAM(2);
  hit = FACE_CACHE[0] == 'F' && !memcmp(FACE_CACHE + 1, g, 6);
  DISABLE_RAM;
  return hit;
}
void avatar_cache(const uint8_t *g) BANKED {
  if (avatar_cached(g)) return;
  avatar_make_genome(g);
  ENABLE_RAM;
  SWITCH_RAM(2);
  FACE_CACHE[0] = 0;
  memcpy(FACE_CACHE + 1, g, 6);
  memcpy(FACE_CACHE + 7, pal_, 8);
  memcpy(FACE_CACHE + 15, work_, AVATAR_BYTES);
  FACE_CACHE[0] = 'F';
  DISABLE_RAM;
}
void avatar_bg_genome(const uint8_t *g, uint8_t x, uint8_t y) BANKED {
  uint8_t hit;
  uint16_t p[4], *base = (uint16_t *)0xbd00u;
  ENABLE_RAM;
  SWITCH_RAM(2);
  hit = FACE_CACHE[0] == 'F' && !memcmp(FACE_CACHE + 1, g, 6);
  DISABLE_RAM;
  if (!hit) {
    avatar_make_genome(g);
    ENABLE_RAM;
    SWITCH_RAM(2);
    FACE_CACHE[0] = 0;
    memcpy(FACE_CACHE + 1, g, 6);
    memcpy(FACE_CACHE + 7, pal_, 8);
    memcpy(FACE_CACHE + 15, work_, AVATAR_BYTES);
    FACE_CACHE[0] = 'F';
    DISABLE_RAM;
  }
  ENABLE_RAM;
  SWITCH_RAM(2);
  VBK_REG = 1;
  set_bkg_data(48, 16, FACE_CACHE + 15);
  set_bkg_data(80, 16, FACE_CACHE + 15 + 256u);
  VBK_REG = 0;
  set_bkg_data(48, 4, FACE_CACHE + 15 + 512u);
  memcpy(p, FACE_CACHE + 7, 8);
  if (!bg_saved_) {
    memcpy(bg_pal_, base + 16u, 8);
    bg_saved_ = 1;
  }
  p[0] = base[28];
  memcpy(base + 16u, p, 8);
  DISABLE_RAM;
  room_dirty = 1;
  avatar_bg_map(x, y);
}
void avatar_bg_end(void) BANKED {
  if (!bg_saved_) return;
  ENABLE_RAM;
  SWITCH_RAM(2);
  memcpy((uint16_t *)0xbd00u + 16u, bg_pal_, 8);
  DISABLE_RAM;
  bg_saved_ = 0;
  room_dirty = 1;
}
/* a third of the face (12 tiles) into sprite tiles tile.. of VRAM bank */
void avatar_chunk(uint8_t chunk, uint8_t tile, uint8_t bank) BANKED {
  VBK_REG = bank;
  set_sprite_data((uint8_t)(tile + chunk * 12u), 12u, work_ + (uint16_t)chunk * 192u);
  VBK_REG = 0;
}
#endif
