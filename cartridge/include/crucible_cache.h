#ifndef CRUCIBLE_CACHE_H
#define CRUCIBLE_CACHE_H
#include <stdint.h>
#include <string.h>
#include "keel_retro.h"
#define CRUCIBLE_CACHE_SLOTS 24u
#define CRUCIBLE_CACHE_DIRECTORY 592u
#define CRUCIBLE_CACHE_BYTES 8192u
#define CRUCIBLE_ASSET_MAX 4188u
/* Variable-length committed records. Banks 1 and 3 are independent arenas.
 * Ownership bank 0 and graphical-fusion bank 2 are never touched here.
 * A new header discards only the previous, expendable three-slot artwork cache. */
static uint16_t cc_u16(const uint8_t *p) { return kr_u16(p); }
static uint32_t cc_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void cc_p16(uint8_t *p, uint16_t n) {
  p[0] = n;
  p[1] = n >> 8;
}
static void cc_p32(uint8_t *p, uint32_t n) {
  p[0] = n;
  p[1] = n >> 8;
  p[2] = n >> 16;
  p[3] = n >> 24;
}
static uint16_t cc_crc(const uint8_t *p, uint16_t n) {
  uint16_t crc = 0xffff;
  uint8_t i;
  while (n--) {
    crc ^= (uint16_t)*p++ << 8;
    for (i = 0; i < 8; i++) crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
  }
  return crc;
}
static uint8_t *cc_slot(uint8_t *ram, uint8_t i) { return ram + 16u + (uint16_t)i * 24u; }
static void cc_mount(uint8_t *ram) {
  uint8_t i;
  if (ram[0] == 0x4bu && ram[1] == 0x43u && ram[2] == 2u && ram[3] == 24u) return;
  ram[0] = 0;
  for (i = 0; i < 24u; i++) cc_slot(ram, i)[0] = 0;
  ram[1] = 0x43u;
  ram[2] = 2;
  ram[3] = 24;
  ram[0] = 0x4b;
}
static uint8_t cc_bounds(const uint8_t *p) {
  uint16_t a = cc_u16(p + 16), n = cc_u16(p + 12);
  return a >= CRUCIBLE_CACHE_DIRECTORY && n >= 46u && n <= CRUCIBLE_ASSET_MAX && a <= 8192u - n;
}
static uint16_t cc_clock(uint8_t *ram) {
  uint8_t i;
  uint16_t max = 0, age;
  for (i = 0; i < 24u; i++) {
    uint8_t *p = cc_slot(ram, i);
    if (p[0] == 0xc4u) {
      age = cc_u16(p + 14);
      if (age > max) max = age;
    }
  }
  if (max == 65535u) {
    for (i = 0; i < 24u; i++) {
      uint8_t *p = cc_slot(ram, i);
      if (p[0] == 0xc4u) cc_p16(p + 14, cc_u16(p + 14) >> 1);
    }
    max = 32767u;
  }
  return max + 1u;
}
static uint16_t cc_used(uint8_t *ram) {
  uint8_t i;
  uint16_t n = 0;
  cc_mount(ram);
  for (i = 0; i < 24u; i++) {
    uint8_t *p = cc_slot(ram, i);
    if (p[0] == 0xc4u && cc_bounds(p)) {
      if (n > 7600u - cc_u16(p + 12)) return 7600u;
      n += cc_u16(p + 12);
    }
  }
  return n;
}
static uint8_t cc_find(uint8_t *ram, uint32_t key, uint32_t version, uint8_t variant) {
  uint8_t i;
  cc_mount(ram);
  for (i = 0; i < 24u; i++) {
    uint8_t *p = cc_slot(ram, i);
    if (p[0] == 0xc4u && cc_u32(p + 2) == key && cc_u32(p + 6) == version && p[1] == variant) {
      if (!cc_bounds(p) || cc_u16(p + 18) != cc_crc(ram + cc_u16(p + 16), cc_u16(p + 12)) ||
          !kr_asset_valid(ram + cc_u16(p + 16), cc_u16(p + 12))) {
        p[0] = 0;
        continue;
      }
      cc_p16(p + 14, cc_clock(ram));
      return i;
    }
  }
  return 255;
}
static uint8_t cc_begin(uint8_t *ram, uint32_t key, uint32_t version, uint8_t variant, uint16_t length) {
  uint8_t i, slot = 255u, old = 255u;
  uint16_t used, min, age, cursor, from, next, n;
  uint8_t *p;
  if (!key || variant > 3u || length < 46u || length > CRUCIBLE_ASSET_MAX) return 255;
  cc_mount(ram);
  /* Abandoned staging is expendable. Keep live records invisible during moves;
  * any interrupted move stays uncommitted and will fail CRC on a later read. */
  for (i = 0; i < 24u; i++) {
    p = cc_slot(ram, i);
    if (p[0] != 0xc4u || !cc_bounds(p)) p[0] = 0;
  }
  for (;;) {
    used = 0;
    slot = 255u;
    old = 255u;
    min = 65535u;
    for (i = 0; i < 24u; i++) {
      p = cc_slot(ram, i);
      if (p[0] != 0xc4u) {
        if (slot == 255u) slot = i;
      } else {
        used = used > 7600u - cc_u16(p + 12) ? 7600u : used + cc_u16(p + 12);
        age = cc_u16(p + 14);
        if (old == 255u || age < min) {
          min = age;
          old = i;
        }
      }
    }
    if (slot != 255u && used <= 8192u - CRUCIBLE_CACHE_DIRECTORY - length) break;
    if (old == 255u) return 255;
    cc_slot(ram, old)[0] = 0;
  }
  /* Compact surviving extents in their existing physical order. */
  cursor = CRUCIBLE_CACHE_DIRECTORY;
  from = CRUCIBLE_CACHE_DIRECTORY;
  for (;;) {
    old = 255u;
    next = 65535u;
    for (i = 0; i < 24u; i++) {
      p = cc_slot(ram, i);
      n = cc_u16(p + 16);
      if (p[0] == 0xc4u && n >= from && n < next) {
        next = n;
        old = i;
      }
    }
    if (old == 255u) break;
    p = cc_slot(ram, old);
    n = cc_u16(p + 12);
    from = next + n;
    if (next != cursor) {
      p[0] = 0;
      memmove(ram + cursor, ram + next, n);
      cc_p16(p + 16, cursor);
      p[0] = 0xc4u;
    }
    cursor += n;
  }
  p = cc_slot(ram, slot);
  p[0] = 0;
  p[1] = variant;
  cc_p32(p + 2, key);
  cc_p32(p + 6, version);
  cc_p16(p + 12, length);
  cc_p16(p + 14, cc_clock(ram));
  cc_p16(p + 16, cursor);
  cc_p16(p + 20, 0);
  p[0] = 0x7eu;
  return slot;
}
static uint8_t cc_write(uint8_t *ram, uint8_t slot, uint8_t value) {
  uint8_t *p;
  uint16_t at;
  if (slot >= 24u) return 0;
  p = cc_slot(ram, slot);
  at = cc_u16(p + 20);
  if (p[0] != 0x7eu || !cc_bounds(p) || at >= cc_u16(p + 12)) return 0;
  ram[cc_u16(p + 16) + at] = value;
  cc_p16(p + 20, at + 1u);
  return 1;
}
static uint8_t cc_commit(uint8_t *ram, uint8_t slot, uint16_t expected, uint8_t *scratch) {
  uint8_t *p, i;
  const uint8_t *asset;
  uint16_t length;
  if (slot >= 24u) return 0;
  p = cc_slot(ram, slot);
  length = cc_u16(p + 12);
  if (p[0] != 0x7eu || !cc_bounds(p) || cc_u16(p + 20) != length) return 0;
  asset = ram + cc_u16(p + 16);
  if (cc_crc(asset, length) != expected || !kr_asset_valid(asset, length)) return 0;
  for (i = 0; i < asset[3]; i++)
    if (!kr_asset_frame(asset, length, i, scratch)) return 0;
  cc_p16(p + 18, expected);
  p[0] = 0xc4u;
  return 1;
}
uint8_t crucible_asset_begin(uint32_t, uint32_t, uint8_t, uint16_t) BANKED;
uint8_t crucible_asset_byte(uint8_t, uint8_t) BANKED;
uint8_t crucible_asset_commit(uint8_t, uint16_t) BANKED;
uint8_t crucible_cached_frame(uint32_t, uint32_t, uint8_t, uint8_t, uint8_t, uint8_t) BANKED;
#endif
