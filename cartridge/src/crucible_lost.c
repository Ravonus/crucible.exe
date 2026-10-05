/* Lost pieces on the bench and in the merge.
 *
 * The bench: the result cell glitches while the pair on the bench would make a piece that was lost and
 * has not cooled down yet (the core's cru_lost.c decides; cru_bench_cells reports the cell as CRU_K_GLITCH, which
 * crucible.c draws as a plain "?"). This tears that "?" a few times a second: its middle flickers between the "?" and
 * corrupt glyphs in stray palettes and flips, and now and then a ring row takes a wrong palette. It writes only the 4x4
 * cell's map; crucible.c redraws the cell clean whenever the cell's kind changes (another pair, a merge, a screen). */
#pragma bank 255
#include <gb/gb.h>
#include "crucible_state.h"
#include "crucible_ui.h"
#include "crucible_lost.h"
#define CR_X 15u /* crucible.c's bench_x[CR], bench_y[CR] */
#define CR_Y 4u
#define EVERY 3u /* frames between tears */
#ifndef SRAM_PTR
#define SRAM_PTR(a) (a)
#endif
#define SPECIMEN(slot)                                                                                                 \
  ((uint8_t *)SRAM_PTR(0xa000u + (uint16_t)(slot) * 256u)) /* crucible_fusion.c's captures (bank 2) */
static uint16_t a_ = CRU_NONE, b_ = CRU_NONE, seed_ = 0x6c4bu;
static uint8_t cue_, t_;
static uint8_t rnd(void) {
  seed_ ^= seed_ << 7;
  seed_ ^= seed_ >> 9;
  seed_ ^= seed_ << 8;
  return (uint8_t)seed_;
}
static void tear(void) {
  uint8_t map[16], attrs[16], i, r, col, k = rnd(), clean = (uint8_t)((k & 3u) == 0u), x;
  for (i = 0; i < 16u; i++) {
    r = i >> 2;
    col = i & 3u;
    if ((r == 1u || r == 2u) && (col == 1u || col == 2u)) {
      map[i] = (uint8_t)(UI_QUESTION + (r - 1u) * 2u + (col - 1u));
      attrs[i] = 0;
      if (clean) continue;
      x = rnd();
      if (x & 1u) {
        map[i] = GLYPH((char)('!' + (rnd() & 31u)));
        attrs[i] = (uint8_t)((x >> 1) & 0x67u);
      } /* static */
      else if (!(x & 6u))
        attrs[i] = (uint8_t)((x >> 3) & 0x67u); /* the ? torn */
    } else {
      map[i] = (r == 0u || r == 3u) ? ((col == 0u || col == 3u) ? UI_RINGC : UI_RINGH) : UI_RINGV;
      attrs[i] = (uint8_t)((col >= 2u ? 0x20u : 0u) | (r >= 2u ? 0x40u : 0u));
      if (!clean && (k & 0x1cu) == 0x1cu && r == ((k >> 5) & 3u))
        attrs[i] = (uint8_t)(attrs[i] | (rnd() & 7u)); /* a row slips */
    }
  }
  VBK_REG = 1;
  set_bkg_tiles(CR_X, CR_Y, 4, 4, attrs);
  VBK_REG = 0;
  set_bkg_tiles(CR_X, CR_Y, 4, 4, map);
}
void crucible_lost_tick(uint8_t screen) BANKED {
  if (screen != 0u || core.slot_a == CRU_NONE) {
    a_ = CRU_NONE;
    cue_ = 0;
    return;
  } /* not the bench: ask again on return */
  if (core.slot_a != a_ || core.focus != b_) {
    a_ = core.slot_a;
    b_ = core.focus;
    cue_ = cru_lost_cue(&core, a_, b_);
    t_ = 0;
  }
  if (!cue_ || ++t_ < EVERY) return;
  t_ = 0;
  tear();
}

/* The merge: a lost piece that will not form (core.mix.lost). Called from crucible_overlay_tick, which runs after the merge
 * has placed its sprites (OAM 0..31) for the frame, so what this does stays on screen until the next frame's placement:
 * one sprite in four jogs sideways, some take a stray palette (3 is the lost piece's own, set by begin_merge) and a flip,
 * and two tiles a frame are overwritten by a damaged tile of a capture: either ingredient's, or the lost piece's (slot 2),
 * so pieces of the thing that should have formed flicker through the bodies. crucible_fusion.c is untouched. */
static const uint8_t TEAR_PAL[4] = {1u, 2u, 3u, 6u};
void crucible_lost_merge(void) BANKED {
  uint8_t i, r, t, s, k, buf[16];
  const uint8_t *src;
  if (!core.mix.open || !core.mix.lost) return;
  for (i = 0; i < 32u; i++) {
    r = rnd();
    if (r & 3u) continue;
    scroll_sprite(i, (int8_t)((int8_t)((r >> 2) & 7u) - 3) * 2, 0);
    if (r & 0x20u) set_sprite_prop(i, (uint8_t)(TEAR_PAL[(r >> 6) & 3u] | ((r & 0x10u) ? 0x20u : 0u)));
  }
  ENABLE_RAM;
  SWITCH_RAM(2);
  for (k = 0; k < 2u; k++) {
    t = rnd() & 31u;
    s = rnd() & 3u;
    if (s == 3u) s = 2u;
    src = SPECIMEN(s) + ((uint16_t)(t & 15u) << 4);
    for (i = 0; i < 16u; i++) buf[i] = src[i];
    buf[rnd() & 15u] ^= rnd();
    buf[rnd() & 15u] ^= rnd();
    set_sprite_data(t, 1, buf);
    if (s == 2u) set_sprite_prop(t, 3u); /* the lost piece, in its own colours */
  }
  DISABLE_RAM;
}
