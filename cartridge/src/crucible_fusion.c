/* Native graphical fusion: the two real ingredient sprites stretch a neck toward each other, lose the
 * outlines where they overlap (one contour), curl along a moving seam that keeps each material on its own
 * side, and reshape row by row into the actual result contour. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_data.h"
#include "crucible_fusion.h"
/* SRAM bank 2 is scratch, separate from ownership (0) and downloaded art (1, 3). */
/* SRAM addresses (0xA000-0xBFFF): identity on the cartridge; a host build maps them onto the switched bank (SRAM_PTR). */
#ifndef SRAM_PTR
#define SRAM_PTR(a) (a)
#endif
#define specimens ((uint8_t *)SRAM_PTR(0xa000u))
#define SPECIMEN(slot) (specimens + (uint16_t)(slot) * 256u)
#define OUT(slot) ((uint8_t *)SRAM_PTR(0xa300u + (uint16_t)(slot) * 0x0e00u))
#define spans ((uint8_t *)SRAM_PTR(0xa400u))
#define SPAN(slot, row, edge) spans[(uint16_t)(slot) * 64u + (uint16_t)(row) * 2u + (edge)]
#define bounds ((uint8_t *)SRAM_PTR(0xa4c0u))
#define BOUND(slot, edge) bounds[(slot) * 2u + (edge)]
#define decoded ((uint8_t *)SRAM_PTR(0xa500u))
#define PIXELS(slot) (decoded + (uint16_t)(slot) * 1024u)
static uint16_t shades[3], masks_out[2];
static int8_t necks[17], bends[7];
static uint8_t last_step = 255u, last_apart = 255u;
static const uint8_t bits[8] = {128, 64, 32, 16, 8, 4, 2, 1}, col_bit[4] = {1, 2, 4, 8};
static const uint16_t tile_bits[16] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768};
static const int8_t curl[32] = {0,  0,  1,  1, 2, 2, 3, 3, 3, 2, 2, 1, 0, -1, -2, -3,
                                -3, -2, -1, 0, 1, 2, 3, 3, 2, 2, 1, 1, 0, 0,  0,  0};
/* Unpack one 32x32 capture (2bpp tiles) into row-major colour bytes in SM83 assembly (~52 cycles a
 * pixel), then take per-row spans and vertical bounds from the packed bytes with zero-count tables. */
uint8_t *up_src, *up_dst;
#ifdef __SDCC
static void unpack(void) __naked {
  // clang-format off
 __asm
	ld	hl, #_up_dst
	ld	a, (hl+)
	ld	h, (hl)
	ld	l, a
	ld	c, #0x00
1$:
	ld	a, c
	and	a, #0x18
	add	a, a
	add	a, a
	add	a, a
	ld	e, a
	ld	a, c
	and	a, #0x07
	add	a, a
	add	a, e
	ld	e, a
	ld	a, (_up_src)
	add	a, e
	ld	e, a
	ld	a, (_up_src+1)
	adc	a, #0x00
	ld	d, a
	ld	b, #0x04
2$:
	push	bc
	ld	a, (de)
	ld	c, a
	inc	de
	ld	a, (de)
	ld	b, a
	push	de
	ld	d, #0x08
3$:
	xor	a, a
	sla	b
	rla
	sla	c
	rla
	ld	(hl+), a
	dec	d
	jr	NZ, 3$
	pop	de
	ld	a, e
	add	a, #0x0f
	ld	e, a
	ld	a, d
	adc	a, #0x00
	ld	d, a
	pop	bc
	dec	b
	jr	NZ, 2$
	inc	c
	ld	a, c
	cp	a, #0x20
	jr	NZ, 1$
	ret
 __endasm;
  // clang-format on
}
#else
/* Host builds: the same bytes in C. Row c of the 32x32 capture: tile row c>>3, line c&7 of each of its four tiles; each
 * pixel is (high plane bit << 1) | low plane bit, leftmost pixel first. */
static void unpack(void) {
  uint8_t *d = up_dst, c, t, k, lo, hi;
  const uint8_t *s;
  for (c = 0; c < 32u; c++) {
    s = up_src + ((uint16_t)(c & 0x18u) << 3) + ((uint16_t)(c & 7u) << 1);
    for (t = 0; t < 4u; t++, s += 16) {
      lo = s[0];
      hi = s[1];
      for (k = 0; k < 8u; k++) {
        *d++ = (uint8_t)(((hi >> 7) << 1) | (lo >> 7));
        lo = (uint8_t)(lo << 1);
        hi = (uint8_t)(hi << 1);
      }
    }
  }
}
#endif
static const uint8_t clz8[256] = {8, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                                  3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
                                  2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                     ctz8[256] = {8, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2,
                                  0, 1, 0, 5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0,
                                  1, 0, 2, 0, 1, 0, 6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1,
                                  0, 3, 0, 1, 0, 2, 0, 1, 0, 5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0,
                                  2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 7, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
                                  0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0,
                                  1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1,
                                  0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5, 0, 1, 0, 2, 0, 1, 0,
                                  3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0};
static void decode(uint8_t slot) {
  uint8_t *src = SPECIMEN(slot), *sp = spans + ((uint16_t)slot << 6), row, col, m, l, r, top = 31, bottom = 0, any = 0;
  const uint8_t *t;
  up_src = src;
  up_dst = PIXELS(slot);
  unpack();
  for (row = 0; row < 32u; row++) {
    l = 32;
    r = 0;
    t = src + ((uint16_t)(row & 0x18u) << 3) + ((row & 7u) << 1);
    for (col = 0; col < 4u; col++, t += 16) {
      m = t[0] | t[1];
      if (m) {
        if (l == 32u) l = (col << 3) + clz8[m];
        r = (col << 3) + 7u - ctz8[m];
      }
    }
    sp[row << 1] = l;
    sp[(row << 1) + 1u] = r;
    if (l < 32u) {
      if (!any) {
        top = row;
        any = 1;
      }
      bottom = row;
    }
  }
  BOUND(slot, 0) = any ? top : 15;
  BOUND(slot, 1) = any ? bottom : 16;
}
/* One output row in SM83 assembly. Only the drawn span is visited: HL walks the 32-aligned source row by an
 * 8.8 step (fraction in C), colour bits rotate straight into the tile bytes (D low plane, E high plane),
 * empty source pixels take fr_fill (the joining body), and every eighth pixel stores one tile byte pair.
 * Inputs: fr_src (first source pixel) and fr_frac (its fraction), fr_dst (tile byte pair of the first pixel), fr_lead (pixels before the
 * span in that byte), fr_n (span length), fr_sf/fr_si (step fraction/integer), fr_fill. Bytes outside the
 * span are already zero. About 190 cycles a pixel instead of thousands in SDCC's C. */
uint8_t *fr_src, *fr_dst;
uint8_t fr_lead, fr_n, fr_sf, fr_si, fr_fill, fr_k, fr_frac;
#ifdef __SDCC
static void row_render(void) __naked {
  // clang-format off
 __asm
	ld	a, (_fr_n)
	or	a, a
	ret	Z
	ld	b, a
	ld	de, #0x0000
	ld	a, (_fr_lead)
	ld	(_fr_k), a
	ld	hl, #_fr_src
	ld	a, (hl+)
	ld	h, (hl)
	ld	l, a
	ld	a, (_fr_frac)
	ld	c, a
1$:
	ld	a, (hl)
	or	a, a
	jr	NZ, 2$
	ld	a, (_fr_fill)
2$:
	rra
	rl	d
	rra
	rl	e
	ld	a, (_fr_sf)
	add	a, c
	ld	c, a
	ld	a, (_fr_si)
	adc	a, l
	ld	l, a
	ld	a, (_fr_k)
	inc	a
	ld	(_fr_k), a
	cp	a, #0x08
	jr	NZ, 3$
	call	4$
3$:
	dec	b
	jr	NZ, 1$
	ld	a, (_fr_k)
	or	a, a
	ret	Z
	ld	c, a
5$:
	ld	a, c
	cp	a, #0x08
	jr	Z, 4$
	sla	d
	sla	e
	inc	c
	jr	5$
4$:
	push	hl
	ld	hl, #_fr_dst
	ld	a, (hl+)
	ld	h, (hl)
	ld	l, a
	ld	a, d
	ld	(hl+), a
	ld	(hl), e
	ld	hl, #_fr_dst
	ld	a, (hl)
	add	a, #0x10
	ld	(hl+), a
	ld	a, (hl)
	adc	a, #0x00
	ld	(hl), a
	xor	a, a
	ld	(_fr_k), a
	ld	de, #0x0000
	pop	hl
	ret
 __endasm;
  // clang-format on
}
#else
/* Host builds: the same register machine in C. D/E collect the low/high plane bits; the source pointer advances by the
 * 8.8 step with only its low byte (L) moving, wrapping inside its 256-byte page exactly as `adc a,l` does; a partial last
 * byte is padded with zero pixels; every completed byte pair is stored and fr_dst moves to the next tile (+16). */
static void row_store(uint8_t d, uint8_t e) {
  fr_dst[0] = d;
  fr_dst[1] = e;
  fr_dst += 16;
  fr_k = 0;
}
static void row_render(void) {
  uint8_t b = fr_n, c, a, d = 0, e = 0, l, nl;
  uint8_t *hl;
  uint16_t sum;
  if (!b) return;
  fr_k = fr_lead;
  hl = fr_src;
  c = fr_frac;
  do {
    a = *hl;
    if (!a) a = fr_fill;
    d = (uint8_t)((d << 1) | (a & 1u));
    e = (uint8_t)((e << 1) | ((a >> 1) & 1u));
    sum = (uint16_t)fr_sf + c;
    c = (uint8_t)sum;
    l = (uint8_t)(hl - specimens);
    nl = (uint8_t)(l + fr_si + (uint8_t)(sum >> 8));
    hl += (int16_t)nl - (int16_t)l;
    if (++fr_k == 8u) {
      row_store(d, e);
      d = e = 0;
    }
  } while (--b);
  if (!fr_k) return;
  for (c = fr_k; c != 8u; c++) {
    d = (uint8_t)(d << 1);
    e = (uint8_t)(e << 1);
  }
  row_store(d, e);
}
#endif
/* FUSION_STEPS is 16: interpolation is a shift, never a division. */
static int16_t between(int16_t a, int16_t b, uint8_t step) { return a + (((b - a) * (int16_t)step) >> 4); }
/* lerp[d+32] = (d*step)>>4 for this frame's step, built by addition: rows use lookups, not multiplies. */
static int8_t lerp[65];
static void build_lerp(uint8_t step) {
  int16_t acc = -32 * (int16_t)step;
  uint8_t i;
  for (i = 0; i < 65u; i++) {
    lerp[i] = (int8_t)(acc >> 4);
    acc += step;
  }
}
#define LERP(a, b) ((int16_t)(a) + lerp[(int16_t)(b) - (int16_t)(a) + 32])
static const uint16_t recip[33] = {0,  256, 128, 85, 64, 51, 43, 37, 32, 28, 26, 23, 21, 20, 18, 17, 16,
                                   15, 14,  13,  13, 12, 12, 11, 11, 10, 10, 9,  9,  9,  9,  8,  8};
void crucible_capture_tiles(uint8_t *data, uint8_t first) BANKED {
  uint8_t slot = first >> 4;
  last_step = 255u;
  ENABLE_RAM;
  SWITCH_RAM(2);
  memcpy(SPECIMEN(slot), data, 256);
  shades[slot] = crucible_shade_mask;
  decode(slot);
  DISABLE_RAM;
  set_sprite_data(first, 16, data);
}
/* With no result the target is the union of both bodies: they can join and part again. */
void fusion_union(void) BANKED {
  uint16_t i;
  ENABLE_RAM;
  SWITCH_RAM(2);
  for (i = 0; i < 256u; i++) SPECIMEN(2)[i] = SPECIMEN(0)[i] | SPECIMEN(1)[i];
  shades[2] = 0;
  decode(2);
  DISABLE_RAM;
}
static void place(uint8_t first, uint8_t tile, int16_t x, int16_t y, uint16_t gray, uint8_t pal) {
  uint8_t i;
  int16_t sx, sy;
  for (i = 0; i < 16u; i++, gray >>= 1) {
    sx = x + (i & 3u) * 8 + 8;
    sy = y + (i >> 2) * 8 + 16;
    set_sprite_tile(first + i, tile + i);
    set_sprite_prop(first + i, (gray & 1u) ? 6u : pal);
    if (sx <= 0 || sx >= 168 || sy <= 0 || sy >= 160)
      move_sprite(first + i, 0, 0);
    else
      move_sprite(first + i, (uint8_t)sx, (uint8_t)sy);
  }
}
void fusion_place(uint8_t which, int16_t x, int16_t y, uint8_t pal) BANKED {
  place(which == 2u ? 0u : which * 16u, which * 16u, x, y, which == 2u ? shades[2] : masks_out[which], pal);
}
void fusion_hide(void) BANKED {
  uint8_t i;
  for (i = 0; i < 32u; i++) move_sprite(i, 0, 0);
}
/* step 0..FUSION_STEPS; apart is half the distance between the two body centres around (cx,cy). */
void fusion_frame(uint8_t step, uint8_t cx, uint8_t cy, uint8_t apart) BANKED {
  uint8_t slot, y, sy, ty, half = FUSION_STEPS / 2u;
  uint8_t *source_row, *out;
  int16_t top, bottom, sl, sr, tl, tr, dl, dr, seam, bend, neck, position, shift;
  uint16_t at, source_pos, source_step, row_offset;
  if (step > FUSION_STEPS) step = FUSION_STEPS;
  /* Same pose: only re-place the sprites. */
  if (step == last_step && apart == last_apart) {
    place(0, 0, (int16_t)cx - 16 - apart, (int16_t)cy - 16, masks_out[0], 1);
    place(16, 16, (int16_t)cx - 16 + apart, (int16_t)cy - 16, masks_out[1], 2);
    CRUCIBLE_HD_FUSION(step, cx, cy, apart);
    return;
  }
  last_step = step;
  last_apart = apart;
  ENABLE_RAM;
  SWITCH_RAM(2);
  /* Per-frame tables: neck width by distance from the middle row, bend by curl value. */
  {
    uint8_t d;
    int16_t k = (int16_t)step * (FUSION_STEPS - step);
    for (d = 0; d < 17u; d++) necks[d] = (int8_t)(k * (16 - d) / 160);
    for (d = 0; d < 7u; d++) bends[d] = (int8_t)(((int16_t)d - 3) * k / 48);
  }
  for (slot = 0; slot < 2u; slot++) {
    uint16_t sy_fp, ty_fp, sy_step, ty_step;
    out = OUT(slot);
    memset(out, 0, 256);
    top = between(BOUND(slot, 0), BOUND(2, 0), step);
    bottom = between(BOUND(slot, 1), BOUND(2, 1), step);
    if (bottom <= top) bottom = top + 1;
    /* Rows of the deforming body sample source and target rows by fixed-point steps (one division each). */
    sy_step = ((uint16_t)(BOUND(slot, 1) - BOUND(slot, 0)) << 8) / (uint16_t)(bottom - top);
    ty_step = ((uint16_t)(BOUND(2, 1) - BOUND(2, 0)) << 8) / (uint16_t)(bottom - top);
    sy_fp = (uint16_t)BOUND(slot, 0) << 8;
    ty_fp = (uint16_t)BOUND(2, 0) << 8;
    for (y = top; y <= bottom && y < 32u; y++, sy_fp += sy_step, ty_fp += ty_step) {
      sy = (uint8_t)(sy_fp >> 8);
      ty = (uint8_t)(ty_fp >> 8);
      if (sy > 31u) sy = 31u;
      if (ty > 31u) ty = 31u;
      sl = SPAN(slot, sy, 0);
      sr = SPAN(slot, sy, 1);
      tl = SPAN(2, ty, 0);
      tr = SPAN(2, ty, 1);
      if (sl > sr) {
        sl = sr = 16;
      }
      if (tl > tr) {
        tl = tr = 16;
      }
      dl = between(sl, tl, step);
      dr = between(sr, tr, step);
      /* Opposing lobes stretch a neck toward the other body, bend, then close into the new contour. */
      neck = necks[y > 16u ? y - 16u : 16u - y];
      bend = bends[curl[(y + step) & 31u] + 3];
      if (slot == 0u)
        dr += neck;
      else
        dl -= neck;
      dl += bend;
      dr += bend;
      if (dl < 0) dl = 0;
      if (dr > 31) dr = 31;
      seam = 16 + curl[(y + step * 2u) & 31u] / 2;
      row_offset = (uint16_t)(y >> 3) * 64u + (y & 7u) * 2u;
      source_row = PIXELS(slot) + ((uint16_t)sy << 5);
      source_pos = (uint16_t)sl << 8;
      source_step = dr > dl && dr - dl < 33 ? (uint16_t)(sr - sl) * recip[dr - dl] : 0;
      /* Past the midpoint each material keeps its own side of a curling seam. */
      if (step > half) {
        if (slot == 0u) {
          position = between(32, seam, (step - half) * 2u) - 1;
          if (dr > position) dr = position;
        } else {
          position = between(0, seam, (step - half) * 2u);
          if (dl < position) {
            source_pos += (position - dl) * source_step;
            dl = position;
          }
        }
      }
      if (dr >= dl) {
        fr_src = source_row + (uint8_t)(source_pos >> 8);
        fr_frac = (uint8_t)source_pos;
        fr_dst = out + row_offset + ((uint16_t)(dl & 0x18) << 1);
        fr_lead = (uint8_t)dl & 7u;
        fr_n = (uint8_t)(dr - dl) + 1u;
        fr_sf = (uint8_t)source_step;
        fr_si = (uint8_t)(source_step >> 8);
        fr_fill = step >= half ? 2u : 0u;
        row_render();
      }
    }
    masks_out[slot] = step < half ? shades[slot] : shades[2];
  }
  /* One contour: where the left body's outline lies over the right body's matter, the outline goes.
  * Per row and tile column: right body's hi plane (matter), shifted into the left body's frame. */
  shift = (int16_t)apart * 2;
  if (shift < 32) {
    uint8_t s8 = (uint8_t)shift >> 3, o = (uint8_t)shift & 7u, col, k, b0, b1, body;
    uint8_t *a = OUT(0), *b = OUT(1);
    for (y = 0; y < 32u; y++) {
      row_offset = (uint16_t)(y >> 3) * 64u + (y & 7u) * 2u;
      for (col = s8; col < 4u; col++) {
        k = col - s8;
        b0 = b[row_offset + ((uint16_t)k << 4) + 1u];
        b1 = k ? b[row_offset + ((uint16_t)(k - 1u) << 4) + 1u] : 0u;
        body = (uint8_t)(b0 >> o) | (o ? (uint8_t)(b1 << (8u - o)) : 0u);
        at = row_offset + ((uint16_t)col << 4);
        a[at] &= (uint8_t)~(a[at] & (uint8_t)~a[at + 1u] & body);
      }
    }
  }
  set_sprite_data(0, 16, OUT(0));
  set_sprite_data(16, 16, OUT(1));
  place(0, 0, (int16_t)cx - 16 - apart, (int16_t)cy - 16, masks_out[0], 1);
  place(16, 16, (int16_t)cx - 16 + apart, (int16_t)cy - 16, masks_out[1], 2);
  CRUCIBLE_HD_FUSION(step, cx, cy, apart);
  DISABLE_RAM;
}
