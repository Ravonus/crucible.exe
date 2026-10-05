#ifndef KEEL_RETRO_H
#define KEEL_RETRO_H
#include <stdint.h>
/* KEEL retro v1. Exactly 256 output bytes; overlapping backrefs are supported.
 * Input and output must not overlap. No allocation, floats or recursion. */
static uint16_t kr_u16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint8_t kr_decode(const uint8_t *src, uint16_t size, uint8_t *out) {
  uint16_t at = 1, w = 0, n, d;
  uint8_t mode, c, k, v;
  if (!size) return 0;
  mode = src[0];
  if (mode == 0) {
    if (size != 257u) return 0;
    while (w < 256u) {
      out[w] = src[at++];
      w++;
    }
    return 1;
  }
  if (mode != 1u && mode != 2u) return 0;
  while (at < size) {
    c = src[at++];
    if (mode == 1u) {
      k = (c & 128u) ? 1u : 0u;
      n = (c & 127u) + 1u;
    } else {
      k = c >> 6;
      n = (c & 63u) + ((k == 1u || k == 2u) ? 3u : 1u);
    }
    if (w + n > 256u) return 0;
    if (k == 0u) {
      if (at + n > size) return 0;
      while (n--) out[w++] = src[at++];
    } else if (k == 3u) {
      while (n--) out[w++] = 0;
    } else {
      if (at >= size) return 0;
      v = src[at++];
      if (k == 1u) {
        while (n--) out[w++] = v;
      } else {
        d = (uint16_t)v + 1u;
        if (d > w) return 0;
        while (n--) {
          out[w] = out[w - d];
          w++;
        }
      }
    }
  }
  return w == 256u;
}
/* Directory validation is cheap; full payload decoding is done at cache commit. */
static uint8_t kr_asset_valid(const uint8_t *p, uint16_t size) {
  uint8_t views, i;
  uint16_t table, head, last, at;
  if (size < 46u || p[0] != 75u || p[1] != 84u || p[2] != 1u) return 0;
  views = p[3];
  if (views != 8u && views != 16u) return 0;
  table = 12u + (uint16_t)views * 2u;
  head = 14u + (uint16_t)views * 4u;
  if (size < head) return 0;
  for (i = 0; i < 4u; i++)
    if (kr_u16(p + 4u + (uint16_t)i * 2u) > 32767u) return 0;
  last = head;
  for (i = 0; i <= views; i++) {
    at = kr_u16(p + table + (uint16_t)i * 2u);
    if ((i == 0u ? at != head : at <= last) || at > size) return 0;
    last = at;
  }
  return last == size;
}
static uint8_t kr_asset_frame(const uint8_t *p, uint16_t size, uint8_t view, uint8_t *out) {
  uint16_t table, a, b;
  if (!kr_asset_valid(p, size) || view >= p[3]) return 0;
  table = 12u + (uint16_t)p[3] * 2u;
  a = kr_u16(p + table + (uint16_t)view * 2u);
  b = kr_u16(p + table + (uint16_t)(view + 1u) * 2u);
  return kr_decode(p + a, b - a, out);
}
#endif
