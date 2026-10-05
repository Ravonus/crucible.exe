#ifndef KEEL_MUSIC_H
#define KEEL_MUSIC_H
/* KEEL chip-groove score format, version 4 (game-owned; scores are compiled into
 * crucible_music_data.h by the music toolchain, not part of this repository). No hardware registers here: crucible_sound.c plays it.
 *
 * Tables (crucible_music_data.h):
 *   km_inst[n][4]     NR10 sweep (pulse 1), NRx1 (duty<<6 | length; wave/noise: length), NRx2 envelope (wave: NR32
 *                     level), NRx4 flags (0x40: the length counter gates the note)
 *   km_wave[n][16]    wave RAM images
 *   km_pat[], km_pat_at[]  patterns: (row 0..15, note, instrument) triples, rows rising, KM_PAT_END ends. Note: a pitch
 *                     index (0 = C2 on a pulse; the wave sounds an octave lower), KM_OFF silences, noise: NR43 itself
 *   km_song[], km_song_at[KM_SONGS]
 * Song (offsets from its start): 0 frames per row (a 16th), 1 NR51 pan, 2 wave, 3 order count, 4 loop index,
 *   5.. order[] (section index), then section count and u16 LE section offsets.
 * Section: bar count, alternate section (KM_NONE: none), then per bar four pattern ids (pulse 1, pulse 2, wave,
 *   noise; KM_NONE: the channel rests that bar). */
#include <stdint.h>
#define KM_OFF 0x7fu
#define KM_NONE 0xffu
#define KM_PAT_END 0xffu
#define KM_S_FRAMES 0u
#define KM_S_PAN 1u
#define KM_S_WAVE 2u
#define KM_S_COUNT 3u
#define KM_S_LOOP 4u
#define KM_S_ORDER 5u
/* GB pulse period registers for C2..B7 (the wave channel plays the same value an octave lower) */
static const uint16_t km_period[72] = {
    44,   157,  263,  363,  457,  547,  631,  711,  786,  856,  923,  986,  1046, 1102, 1155, 1205, 1253, 1297,
    1339, 1379, 1417, 1452, 1486, 1517, 1547, 1575, 1602, 1627, 1650, 1673, 1694, 1714, 1732, 1750, 1767, 1783,
    1798, 1812, 1825, 1837, 1849, 1860, 1871, 1881, 1890, 1899, 1907, 1915, 1923, 1930, 1936, 1943, 1949, 1954,
    1959, 1964, 1969, 1974, 1978, 1982, 1985, 1989, 1992, 1995, 1998, 2001, 2004, 2006, 2009, 2011, 2013, 2015};
static uint16_t km_u16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
/* xorshift16; the music's own stream (never gameplay RNG) */
static uint8_t km_rnd(uint16_t *s) {
  uint16_t x = *s;
  x ^= x << 7;
  x ^= x >> 9;
  x ^= x << 8;
  *s = x;
  return (uint8_t)x;
}
#endif
