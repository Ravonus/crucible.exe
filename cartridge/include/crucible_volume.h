#ifndef CRUCIBLE_VOLUME_H
#define CRUCIBLE_VOLUME_H
/* MUSIC and SFX volumes, 0..8, in the save without changing its layout (the core keeps bits 0-1 and 3-4 of the options
 * byte at 0..2 on load, so both fields stay inside that rule).
 *   options bit 7 set:   music = (bits 5-6) * 3 + (bits 0-1)
 *   options bit 7 clear: an older save's MUSIC ON / SOFT / OFF (bits 0-1 = 0, 1, 2) reads as 4, 2, 0
 *   options bit 2 set:   sfx = 0 (the old SOUND FX OFF, still read the same way)
 *   otherwise:           sfx = 8 - (core flags bits 4-7); an older save has them clear: 8
 * Bits 3-4 stay the turntable speed. */
#include <stdint.h>
#define VOL_STEPS 8u
#define VOL_MUSIC_DEFAULT 4u
static uint8_t vol_music(uint8_t o) {
  uint8_t lo = o & 3u;
  if (o & 0x80u) {
    lo = (uint8_t)(((o >> 5) & 3u) * 3u + lo);
    return lo > 8u ? 8u : lo;
  }
  return lo == 1u ? 2u : lo == 2u ? 0u : VOL_MUSIC_DEFAULT;
}
static uint8_t vol_music_set(uint8_t o, uint8_t m) {
  if (m > 8u) m = 8u;
  return (uint8_t)((o & 0x1cu) | 0x80u | (uint8_t)((m / 3u) << 5) | (m % 3u));
}
static uint8_t vol_sfx(uint8_t o, uint8_t flags) {
  uint8_t f = flags >> 4;
  if (o & 4u) return 0;
  return f > 8u ? 0u : (uint8_t)(8u - f);
}
/* returns the new options byte; *flags gets the new high nibble */
static uint8_t vol_sfx_set(uint8_t o, uint8_t *flags, uint8_t s) {
  if (s > 8u) s = 8u;
  *flags = (uint8_t)((*flags & 15u) | (uint8_t)((s ? 8u - s : 0u) << 4));
  return s ? (uint8_t)(o & ~4u) : (uint8_t)(o | 4u);
}
#endif
