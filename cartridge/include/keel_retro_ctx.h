#ifndef KEEL_RETRO_CTX_H
#define KEEL_RETRO_CTX_H
#include <stdint.h>
#include <string.h>
/* KEEL retro ctx v2 decoder. A stream decodes one P-frame chain in place
 * into its own 2bpp frame, a few rows per call, so a game can spread one frame over several main-loop ticks.
 * No allocation, multiply, floats or recursion; reads past the end of a stream are zero bits, so any bytes decode
 * without overrunning anything (bad bytes only make wrong pixels).
 * The includer provides:
 *   KRC_MODEL            const uint8_t[8200], the trained model (crucible_ctx_model.h)
 *   krc_fill(s)          copies up to KRC_IN bytes of the stream into s->in, advancing s->src/s->left, returns count
 * and may define KRC_FN (e.g. BANKED) for the entry points. One stream decodes at a time (shared row scratch). */
#ifndef KRC_IN
#define KRC_IN 16u
#endif
/* model layout: 8192 context bytes, then d2, tile[2], row[4], mask */
#define KRC_CTX 8192u
#ifndef KRC_FN
#define KRC_FN
#endif
typedef struct krc_stream {
  uint8_t tiles[256]; /* the frame: the previous frame until rows are decoded over it */
  uint16_t r, v, flags, prev_flags, mask, left;
  const uint8_t *src;
  uint16_t bank; /* the rest of the stream (bank: platform meaning) */
  uint8_t in[KRC_IN], in_at, in_n, byte, bits;
  uint8_t rows[16], y; /* changed rows per tile of the frame being decoded; next row (32 = done) */
  uint8_t eqy, eqb[4]; /* across calls: row eqy's unchanged columns (bit per X), for the row below */
} krc_stream;
static uint8_t krc_fill(krc_stream *s);
/* LPS sizes per class (16) and range bucket (4): CTX_QTAB. */
static const uint16_t krc_q[64] = {
    27648, 33792, 39936, 46080, 17417, 21288, 25158, 29029, 10972, 13410, 15849, 18287, 6912, 8448, 9984, 11520,
    4354,  5322,  6290,  7257,  2743,  3353,  3962,  4572,  1728,  2112,  2496,  2880,  1089, 1330, 1572, 1814,
    686,   838,   991,   1143,  432,   528,   624,   720,   272,   333,   393,   454,   171,  210,  248,  286,
    108,   132,   156,   180,   68,    83,    98,    113,   43,    52,    62,    71,    27,   33,   39,   45};
/* The two values left after v0 and v1 (index v0|v1<<2), lower in the low bits. */
static const uint8_t krc_rest[16] = {0x0e, 0x0e, 0x0d, 0x09, 0x0e, 0x0e, 0x0c, 0x08,
                                     0x0d, 0x0c, 0x0d, 0x04, 0x09, 0x08, 0x04, 0x09};
/* Row scratch, interleaved per column X (0..33, borders stay 0): previous frame row y, current frame row y-1,
 * current frame row y, and whether row y-1 is unchanged at X. A pixel's context is then sequential bytes. */
#define KRC_PR 0
#define KRC_UR 1
#define KRC_CR 2
#define KRC_EQ 3
static uint8_t krc_row[34 * 4];
static const uint8_t krc_bitm[8] = {1, 2, 4, 8, 16, 32, 64, 128};
/* The stream being decoded (one at a time). Its coder lives in globals while it decodes (SDCC compiles globals
 * into direct loads; its codegen for pointer-to-struct bit tests has misread values). */
static krc_stream *krc_s;
static uint16_t krc_r, krc_v;
static uint8_t krc_byte, krc_bits, krc_d2, krc_cnt;
static uint8_t *krc_tp;
static uint8_t *krc_dp;
static void krc_load(krc_stream *s) {
  krc_s = s;
  krc_r = s->r;
  krc_v = s->v;
  krc_byte = s->byte;
  krc_bits = s->bits;
}
static void krc_store(void) {
  krc_stream *s = krc_s;
  s->r = krc_r;
  s->v = krc_v;
  s->byte = krc_byte;
  s->bits = krc_bits;
}
static uint8_t krc_next(void) {
  krc_stream *s = krc_s;
  if (s->in_at >= s->in_n) {
    s->in_n = s->left ? krc_fill(s) : 0u;
    s->in_at = 0;
  }
  return s->in_at < s->in_n ? s->in[s->in_at++] : 0u;
}
#if defined(__SDCC) && (defined(__PORT_sm83) || defined(__PORT_gbz80))
/* SM83: the coder, the eight-pixel segment and row (un)packing in assembly; about 10x the compiled C. */
#define KRC_PASTE2(a, b) a##b
#define KRC_PASTE(a, b) KRC_PASTE2(a, b)
#define KRC_MODEL_ASM KRC_PASTE(_, KRC_MODEL)
// clang-format off
static void krc_renorm(void) __naked { __asm
	bit	7, h
	jr	nz, 00209$
00201$:
	add	hl, hl
	ld	a, (#_krc_bits)
	or	a, a
	jr	nz, 00202$
	push	hl
	push	de
	push	bc
	call	_krc_next
	pop	bc
	pop	de
	pop	hl
	ld	(#_krc_byte), a
	ld	a, #8
00202$:
	dec	a
	ld	(#_krc_bits), a
	ld	a, (#_krc_byte)
	add	a, a
	ld	(#_krc_byte), a
	rl	e
	rl	d
	bit	7, h
	jr	z, 00201$
00209$:
	ld	a, l
	ld	(#_krc_r), a
	ld	a, h
	ld	(#_krc_r + 1), a
	ld	a, e
	ld	(#_krc_v), a
	ld	a, d
	ld	(#_krc_v + 1), a
	ret
__endasm; }
// clang-format on
/* One binary decision (class in A): returns 1 when the less probable symbol was coded. Preserves BC. */
// clang-format off
static uint8_t krc_dec(uint8_t cls) __naked { cls; __asm
	add	a, a
	add	a, a
	ld	e, a
	ld	a, (#_krc_r + 1)
	swap	a
	rrca
	and	a, #0x03
	or	a, e
	add	a, a
	ld	e, a
	ld	d, #0
	ld	hl, #_krc_q
	add	hl, de
	ld	a, (hl+)
	ld	e, a
	ld	d, (hl)
	ld	a, (#_krc_r)
	sub	a, e
	ld	l, a
	ld	a, (#_krc_r + 1)
	sbc	a, d
	ld	h, a
	ld	a, (#_krc_v)
	sub	a, l
	ld	a, (#_krc_v + 1)
	sbc	a, h
	jr	nc, 00221$
	bit	7, h
	jr	z, 00222$
	ld	a, l
	ld	(#_krc_r), a
	ld	a, h
	ld	(#_krc_r + 1), a
	xor	a, a
	ret
00222$:
	ld	a, (#_krc_v)
	ld	e, a
	ld	a, (#_krc_v + 1)
	ld	d, a
	call	_krc_renorm
	xor	a, a
	ret
00221$:
	ld	a, (#_krc_v)
	sub	a, l
	ld	l, a
	ld	a, (#_krc_v + 1)
	sbc	a, h
	ld	h, a
	push	hl
	ld	h, d
	ld	l, e
	pop	de
	call	_krc_renorm
	ld	a, #1
	ret
__endasm; }
// clang-format on
/* Eight pixels of the current row: p = &krc_row[(X0-1)*4]. Context low byte L | U<<2 | UR<<4 | P<<6, high bits
 * PR | UL<<2 | EQ<<4. */
// clang-format off
static void krc_seg(uint8_t*p) __naked { p; __asm
	ld	h, d
	ld	l, e
	ld	a, #8
	ld	(#_krc_cnt), a
00231$:
	inc	hl
	ld	a, (hl+)
	add	a, a
	add	a, a
	ld	d, a
	ld	a, (hl+)
	ld	e, a
	inc	hl
	ld	a, (hl+)
	rrca
	rrca
	or	a, e
	ld	e, a
	ld	a, (hl+)
	add	a, a
	add	a, a
	or	a, e
	ld	e, a
	inc	hl
	ld	a, (hl+)
	swap	a
	or	a, d
	ld	d, a
	ld	a, (hl+)
	or	a, d
	ld	d, a
	ld	a, (hl)
	swap	a
	or	a, e
	ld	e, a
	dec	hl
	dec	hl
	dec	hl
	push	hl
	ld	hl, #KRC_MODEL_ASM
	add	hl, de
	ld	a, (hl)
	ld	c, a
	swap	a
	and	a, #0x0f
	call	_krc_dec
	or	a, a
	jr	nz, 00232$
	ld	a, c
	and	a, #0x03
	jr	00235$
00232$:
	ld	a, (#_krc_d2)
	call	_krc_dec
	or	a, a
	jr	nz, 00233$
	ld	a, c
	rrca
	rrca
	and	a, #0x03
	jr	00235$
00233$:
	ld	a, c
	and	a, #0x0f
	ld	e, a
	ld	d, #0
	ld	hl, #_krc_rest
	add	hl, de
	ld	c, (hl)
	ld	a, #1
	call	_krc_dec
	or	a, a
	ld	a, c
	jr	z, 00234$
	rrca
	rrca
00234$:
	and	a, #0x03
00235$:
	pop	hl
	ld	(hl-), a
	dec	hl
	ld	a, (#_krc_cnt)
	dec	a
	ld	(#_krc_cnt), a
	jr	nz, 00231$
	ret
__endasm; }
// clang-format on
/* Unpack the 32 pixels of tile row krc_tp into krc_row from krc_dp (stride 4): krc_unpack2 writes each value
 * twice (previous and current row y), krc_unpack1 writes the row above and marks it unchanged (EQ = 1). */
// clang-format off
static void krc_unpack2(void) __naked { __asm
	ld	hl, #_krc_tp
	ld	a, (hl+)
	ld	e, a
	ld	d, (hl)
	ld	hl, #_krc_dp
	ld	a, (hl+)
	ld	h, (hl)
	ld	l, a
	ld	c, #4
00241$:
	ld	a, (de)
	ld	b, a
	inc	de
	ld	a, (de)
	push	de
	ld	d, a
	xor	a, a
	sla	d
	rla
	sla	b
	rla
	ld	(hl+), a
	inc	hl
	ld	(hl+), a
	inc	hl
	xor	a, a
	sla	d
	rla
	sla	b
	rla
	ld	(hl+), a
	inc	hl
	ld	(hl+), a
	inc	hl
	xor	a, a
	sla	d
	rla
	sla	b
	rla
	ld	(hl+), a
	inc	hl
	ld	(hl+), a
	inc	hl
	xor	a, a
	sla	d
	rla
	sla	b
	rla
	ld	(hl+), a
	inc	hl
	ld	(hl+), a
	inc	hl
	xor	a, a
	sla	d
	rla
	sla	b
	rla
	ld	(hl+), a
	inc	hl
	ld	(hl+), a
	inc	hl
	xor	a, a
	sla	d
	rla
	sla	b
	rla
	ld	(hl+), a
	inc	hl
	ld	(hl+), a
	inc	hl
	xor	a, a
	sla	d
	rla
	sla	b
	rla
	ld	(hl+), a
	inc	hl
	ld	(hl+), a
	inc	hl
	xor	a, a
	sla	d
	rla
	sla	b
	rla
	ld	(hl+), a
	inc	hl
	ld	(hl+), a
	inc	hl
	pop	de
	ld	a, e
	add	a, #15
	ld	e, a
	ld	a, d
	adc	a, #0
	ld	d, a
	dec	c
	jr	nz, 00241$
	ret
__endasm; }
// clang-format on
// clang-format off
static void krc_unpack1(void) __naked { __asm
	ld	hl, #_krc_tp
	ld	a, (hl+)
	ld	e, a
	ld	d, (hl)
	ld	hl, #_krc_dp
	ld	a, (hl+)
	ld	h, (hl)
	ld	l, a
	ld	c, #4
00251$:
	ld	a, (de)
	ld	b, a
	inc	de
	ld	a, (de)
	push	de
	ld	d, a
	xor	a, a
	sla	d
	rla
	sla	b
	rla
	ld	(hl+), a
	inc	hl
	ld	(hl), #1
	inc	hl
	inc	hl
	xor	a, a
	sla	d
	rla
	sla	b
	rla
	ld	(hl+), a
	inc	hl
	ld	(hl), #1
	inc	hl
	inc	hl
	xor	a, a
	sla	d
	rla
	sla	b
	rla
	ld	(hl+), a
	inc	hl
	ld	(hl), #1
	inc	hl
	inc	hl
	xor	a, a
	sla	d
	rla
	sla	b
	rla
	ld	(hl+), a
	inc	hl
	ld	(hl), #1
	inc	hl
	inc	hl
	xor	a, a
	sla	d
	rla
	sla	b
	rla
	ld	(hl+), a
	inc	hl
	ld	(hl), #1
	inc	hl
	inc	hl
	xor	a, a
	sla	d
	rla
	sla	b
	rla
	ld	(hl+), a
	inc	hl
	ld	(hl), #1
	inc	hl
	inc	hl
	xor	a, a
	sla	d
	rla
	sla	b
	rla
	ld	(hl+), a
	inc	hl
	ld	(hl), #1
	inc	hl
	inc	hl
	xor	a, a
	sla	d
	rla
	sla	b
	rla
	ld	(hl+), a
	inc	hl
	ld	(hl), #1
	inc	hl
	inc	hl
	pop	de
	ld	a, e
	add	a, #15
	ld	e, a
	ld	a, d
	adc	a, #0
	ld	d, a
	dec	c
	jr	nz, 00251$
	ret
__endasm; }
// clang-format on
/* The row above, from the current row just decoded: ur = cr, EQ = (cr == pr), for the 32 columns. */
// clang-format off
static void krc_cr2ur(void) __naked { __asm
	ld	hl, #(_krc_row + 4)
	ld	c, #32
00271$:
	ld	a, (hl+)
	ld	b, a
	inc	hl
	ld	a, (hl-)
	ld	(hl+), a
	inc	hl
	cp	a, b
	ld	a, #1
	jr	z, 00272$
	xor	a, a
00272$:
	ld	(hl+), a
	dec	c
	jr	nz, 00271$
	ret
__endasm; }
// clang-format on
// clang-format off
static uint8_t krc_flag(uint8_t c) __naked { c; __asm
	ld	c, a
	and	a, #0x0f
	call	_krc_dec
	bit	7, c
	ret	z
	xor	a, #1
	ret
__endasm; }
// clang-format on
/* Pack eight current-row pixels from krc_dp (stride 4) into the tile row bytes at krc_tp. */
// clang-format off
static void krc_pack(void) __naked { __asm
	ld	hl, #_krc_dp
	ld	a, (hl+)
	ld	h, (hl)
	ld	l, a
	ld	bc, #0
	ld	e, #8
00261$:
	ld	a, (hl+)
	inc	hl
	inc	hl
	inc	hl
	rra
	rl	c
	rra
	rl	b
	dec	e
	jr	nz, 00261$
	ld	hl, #_krc_tp
	ld	a, (hl+)
	ld	h, (hl)
	ld	l, a
	ld	a, c
	ld	(hl+), a
	ld	(hl), b
	ret
__endasm; }
// clang-format on
#else
/* Portable C (the reference the assembly mirrors). */
static uint8_t krc_dec(uint8_t cls) {
  uint8_t hi = (uint8_t)(krc_r >> 8), lps;
  uint16_t q = krc_q[(uint8_t)((uint8_t)(cls << 2) | (uint8_t)((hi >> 5) & 3u))], m = krc_r - q;
  if (krc_v < m) {
    krc_r = m;
    lps = 0;
  } else {
    krc_v -= m;
    krc_r = q;
    lps = 1;
  }
  while (!(krc_r & 0x8000u)) {
    krc_r <<= 1;
    if (!krc_bits) {
      krc_byte = krc_next();
      krc_bits = 8;
    }
    krc_v <<= 1;
    if (krc_byte & 0x80u) krc_v |= 1u;
    krc_byte <<= 1;
    krc_bits--;
  }
  return lps;
}
static void krc_seg(uint8_t *p) {
  uint8_t i, c, e, v;
  for (i = 0; i < 8u; i++, p += 4) {
    c = (uint8_t)(p[2] | (uint8_t)(p[5] << 2) | (uint8_t)(p[9] << 4) | (uint8_t)(p[4] << 6));
    e = KRC_MODEL[((uint16_t)(uint8_t)(p[8] | (uint8_t)(p[1] << 2) | (uint8_t)(p[7] << 4)) << 8) | c];
    if (!krc_dec(e >> 4))
      v = e & 3u;
    else if (!krc_dec(krc_d2))
      v = (e >> 2) & 3u;
    else {
      v = krc_rest[e & 15u];
      v = krc_dec(1) ? (uint8_t)(v >> 2) : (uint8_t)(v & 3u);
    }
    p[6] = v;
  }
}
static void krc_unpackn(uint8_t twice) {
  const uint8_t *t = krc_tp;
  uint8_t *a = krc_dp, tx, x, lo, hi, v;
  for (tx = 0; tx < 4u; tx++, t += 16) {
    lo = t[0];
    hi = t[1];
    for (x = 0; x < 8u; x++, a += 4) {
      v = (uint8_t)(((lo & 0x80u) ? 1u : 0u) | ((hi & 0x80u) ? 2u : 0u));
      a[0] = v;
      if (twice)
        a[2] = v;
      else
        a[2] = 1;
      lo <<= 1;
      hi <<= 1;
    }
  }
}
static void krc_unpack2(void) { krc_unpackn(1); }
static void krc_unpack1(void) { krc_unpackn(0); }
static void krc_cr2ur(void) {
  uint8_t x;
  uint8_t *r;
  for (x = 1; x <= 32u; x++) {
    r = krc_row + x * 4u;
    r[KRC_UR] = r[KRC_CR];
    r[KRC_EQ] = r[KRC_CR] == r[KRC_PR] ? 1u : 0u;
  }
}
static uint8_t krc_flag(uint8_t c) {
  uint8_t b = krc_dec(c & 15u);
  return (c & 0x80u) ? (uint8_t)(b ^ 1u) : b;
}
static void krc_pack(void) {
  const uint8_t *a = krc_dp;
  uint8_t i, lo = 0, hi = 0;
  for (i = 0; i < 8u; i++, a += 4) {
    lo = (uint8_t)((lo << 1) | (*a & 1u));
    hi = (uint8_t)((hi << 1) | (*a >> 1));
  }
  krc_tp[0] = lo;
  krc_tp[1] = hi;
}
#endif
/* Open a chain from its keyframe (already decoded 2bpp tiles and shade mask). */
KRC_FN void krc_open(krc_stream *s, const uint8_t *src, uint16_t len, uint16_t bank, const uint8_t *key,
                     uint16_t key_mask) {
  uint8_t k;
  if (key != s->tiles) memcpy(s->tiles, key, 256);
  s->src = src;
  s->left = len;
  s->bank = bank;
  s->in_at = s->in_n = 0;
  s->bits = 0;
  s->byte = 0;
  s->r = 0xffffu;
  s->v = 0;
  krc_load(s);
  for (k = 0; k < 16u; k++) {
    if (!krc_bits) {
      krc_byte = krc_next();
      krc_bits = 8;
    }
    krc_v <<= 1;
    if (krc_byte & 0x80u) krc_v |= 1u;
    krc_byte <<= 1;
    krc_bits--;
  }
  krc_store();
  s->prev_flags = 0xffffu;
  s->mask = key_mask;
  s->y = 32;
}
/* Begin the next frame: changed tiles and the shade mask. Rows follow with krc_rows. */
KRC_FN void krc_frame(krc_stream *s) {
  uint8_t t, lo = 0, hi = 0, c;
  uint16_t x = 0;
  krc_load(s);
  for (t = 0; t < 8u; t++) {
    c = KRC_MODEL[KRC_CTX + 1u + (((uint8_t)s->prev_flags & krc_bitm[t]) ? 1u : 0u)];
    if (krc_flag(c)) lo |= krc_bitm[t];
  }
  for (t = 0; t < 8u; t++) {
    c = KRC_MODEL[KRC_CTX + 1u + (((uint8_t)(s->prev_flags >> 8) & krc_bitm[t]) ? 1u : 0u)];
    if (krc_flag(c)) hi |= krc_bitm[t];
  }
  s->prev_flags = s->flags = (uint16_t)lo | ((uint16_t)hi << 8);
  memset(s->rows, 0, 16);
  s->eqy = 255;
  if (krc_flag(KRC_MODEL[KRC_CTX + 7u])) {
    for (t = 0; t < 16u; t++)
      if (krc_dec(1)) x |= (uint16_t)1u << t;
    s->mask ^= x;
  }
  krc_store();
  s->y = 0;
}
/* Decode up to n rows that have changed tiles (rows without any are skipped for free); returns 1 once the frame
 * is complete. */
KRC_FN uint8_t krc_rows(krc_stream *s, uint8_t n) {
  uint8_t y, ry, tx, t, any, k, rv, tf, last = 255;
  uint8_t *tp;
  krc_load(s);
  krc_d2 = KRC_MODEL[KRC_CTX] & 15u;
  while (n && s->y < 32u) {
    y = s->y;
    s->y = (uint8_t)(y + 1u);
    ry = y & 7u;
    any = 0;
    tf = (y & 16u) ? (uint8_t)(s->flags >> 8) : (uint8_t)s->flags;
    if (y & 8u) tf >>= 4;
    if (!(tf & 15u)) continue;
    n--;
    tp = s->tiles + ((uint16_t)(y >> 3) << 6) + (uint8_t)(ry << 1);
    for (tx = 0; tx < 4u; tx++) {
      if (!(tf & krc_bitm[tx])) continue;
      t = (uint8_t)(((y >> 3) << 2) + tx);
      rv = s->rows[t];
      k = ry ? ((rv & krc_bitm[ry - 1u]) ? 2u : 0u) : 2u;
      if (ry == 7u) k |= 1u;
      if (!krc_flag(KRC_MODEL[KRC_CTX + 3u + k])) continue;
      if (!any) {
        any = 1;
        /* the row above is the current row just decoded when there was one; otherwise it is still the tiles */
        if (last == (uint8_t)(y - 1u) && y)
          krc_cr2ur();
        else {
          krc_dp = krc_row + 4 + KRC_UR;
          if (y) {
            krc_tp = tp - (ry ? 2 : (64 - 14));
            krc_unpack1();
            /* the row above was decoded in an earlier call: restore its unchanged-column bits */
            if (s->eqy == (uint8_t)(y - 1u)) {
              uint8_t x, *u = krc_row + 4 + KRC_EQ;
              for (x = 0; x < 32u; x++, u += 4) *u = (s->eqb[x >> 3] & krc_bitm[x & 7u]) ? 1u : 0u;
            }
          } else {
            uint8_t *u = krc_dp, x;
            for (x = 0; x < 32u; x++, u += 4) {
              u[0] = 0;
              u[2] = 1;
            }
          }
        }
        krc_tp = tp;
        krc_dp = krc_row + 4 + KRC_PR;
        krc_unpack2();
        last = y;
      }
      s->rows[t] = (uint8_t)(rv | krc_bitm[ry]);
      krc_seg(krc_row + (uint16_t)(tx << 5));
      krc_dp = krc_row + (uint16_t)(tx << 5) + 4u + KRC_CR;
      krc_tp = tp + (uint16_t)(tx << 4);
      krc_pack();
    }
  }
  /* stopping right after a decoded row: keep its unchanged-column bits for the next call's first row */
  if (last != 255u && (uint8_t)(last + 1u) == s->y && s->y < 32u) {
    uint8_t x;
    const uint8_t *r = krc_row + 4;
    memset(s->eqb, 0, 4);
    for (x = 0; x < 32u; x++, r += 4)
      if (r[KRC_CR] == r[KRC_PR]) s->eqb[x >> 3] |= krc_bitm[x & 7u];
    s->eqy = last;
  }
  krc_store();
  return s->y >= 32u;
}
#endif
