/* The result of a combination turns out of the glitch, and is already turning when the reveal shows it.
 *
 * The merge knows its result from its first frame (core.mix.result): reveal_prepare queues the result's turntable for
 * the art cache and marks it on screen (so nothing evicts it). The fuse and (for a new discovery) the fused/new
 * alternation keep the CPU to themselves; then comes the turn: the result itself, as sprites (OAM 0..15, OBJ
 * tiles 32..47, palette 3), turns through its views while it resolves from a light silhouette into its materials (the
 * ordered FORM mask) with rows jogging sideways, while the decoders work at full rate (two urgent art ticks and two
 * overlay steps a frame: the base turntable and, ahead of the reveal, its material overlay). The turn lasts at least
 * TURN_MIN real frames and until both are resident, capped at TURN_MAX real frames (never a hang); then the merge's
 * white flash, and the reveal's cell plays its turntable from its first frame with the overlay on it. A view not
 * decoded yet shows the keyframe for that step, so it never stalls.
 * See docs/game-flow.md (Reveal). */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_data.h"
#include "crucible_state.h"
#include "crucible_overlay.h"
#include "crucible_predict.h"
#ifndef SRAM_PTR
#define SRAM_PTR(a) (a)
#endif
#define BASE ((uint16_t *)SRAM_PTR(0xbd00u))
#define TURN_MIN 24u /* real frames: a few views, while the silhouette fills in */
#define TURN_MAX 300u /* five seconds at most (real frames, not loop turns: a busy frame never stretches it) */
#define TILE 32u /* the fusion's third slot: the result's sprite tiles */
#define STAGE_X 64u /* crucible.c's STAGE_X-16, STAGE_Y-16 */
#define STAGE_Y 32u
extern uint8_t room_dirty; /* crucible.c's pal_changed */
extern uint8_t crucible_art_urgent;
uint16_t reveal_id = 0xffffu; /* globals for the test harness */
uint16_t reveal_frames; /* real frames the turn has run */
static uint16_t t0;
static uint8_t started;
uint8_t reveal_after; /* 0 while turning; then 1 + frames since it finished */
static uint8_t variant, seed = 0x5bu;
static uint16_t shade, shown = 0xffffu, checked = 0xffffu;
static uint8_t ready,
    progress; /* the shown view's shade mask (the decoders overwrite crucible_shade_mask as they store) */
static uint8_t rnd(void) {
  seed ^= seed << 3;
  seed ^= seed >> 5;
  seed ^= seed << 1;
  return seed;
}
/* recipe-aware prefetch while the second ingredient is chosen: invisible, below the visible art's priority */
void reveal_predict(uint16_t a, uint16_t b) BANKED {
  uint16_t id;
  if (a >= core.items || b >= core.items) return;
  id = cru_recipe(&core, a, b);
  if (id < core.items) (void)crucible_art_ready(id, 1, 70u);
}
void reveal_prepare(uint16_t id, uint8_t v) BANKED {
  reveal_id = id;
  variant = v;
  reveal_frames = 0;
  reveal_after = 0;
  started = 0;
  shown = 0xffffu;
  checked = 0xffffu;
  ready = 0;
  progress = 0;
  seed ^= DIV_REG | 1u;
  if (id >= CRUCIBLE_ITEMS) {
    reveal_after = 1;
    return;
  }
  crucible_art_visible(&reveal_id, 1);
  (void)crucible_art_ready(id, 1, 125u);
}
static uint8_t resident(void) {
  uint8_t base, over = crucible_overlay_ahead(reveal_id) == 255u;
  progress = crucible_art_ready(reveal_id, 1, 125u);
  base = progress == 255u;
  if (!over) over = crucible_overlay_ahead(reveal_id) == 255u; /* two overlay steps a frame */
  return base && over;
}
/* Once per merge frame after the fuse: 0 while the result turns; then 1 + frames since (for the flash). */
uint8_t reveal_turn(void) BANKED {
  uint8_t i, view, form, jog, r1, r2;
  uint16_t p[4];
  if (reveal_after) {
    if (reveal_after < 255u) reveal_after++;
    return reveal_after;
  }
  if (!started) {
    started = 1;
    t0 = sys_time;
  }
  reveal_frames = (uint16_t)(sys_time - t0);
  if (started == 1u) {
    started = 2; /* the real colours on the result's palette; the second body's sprites go */
    crucible_get_palette(reveal_id, variant, p);
    ENABLE_RAM;
    SWITCH_RAM(2);
    memcpy(BASE + 32u + 3u * 4u, p, 8);
    DISABLE_RAM;
    room_dirty = 1;
    for (i = 16u; i < 32u; i++) move_sprite(i, 0, 0);
  }
  /* the decoders at full rate: the fusion is done, so the turn has the CPU (the main loop skips its art tick in a merge) */
  crucible_art_urgent = 1;
  crucible_art_tick();
  crucible_art_tick();
  if ((reveal_frames >> 3) != checked) {
    checked = reveal_frames >> 3;
    ready = resident();
  } else if (!ready)
    (void)crucible_overlay_ahead(reveal_id); /* the scans every 8 frames */
  if ((ready && reveal_frames >= TURN_MIN) || reveal_frames >= TURN_MAX) {
    reveal_after = 1;
    started = 0;
    return 1;
  }
  /* only views already decoded (the chain decodes in order): the turn widens as the turntable fills in. Asking for a view
   * behind the decoder would reopen its stream at the keyframe and start over. */
  view = (uint8_t)((reveal_frames >> 3) % (progress == 255u
                                               ? CRUCIBLE_VIEWS
                                               : 1u + (uint8_t)(((uint16_t)progress * (CRUCIBLE_VIEWS - 1u)) / 254u)));
  form = (uint8_t)(1u + (reveal_frames >= TURN_MIN
                             ? 16u
                             : (uint8_t)((reveal_frames * 2u) / 5u))); /* silhouette -> materials by TURN_MIN */
  /* the FORM pass walks every pixel: only when the view turns or the silhouette moves a step */
  if ((uint16_t)((view << 4) | (form >> 2)) != shown) {
    shown = (uint16_t)((view << 4) | (form >> 2));
    crucible_art(1u, reveal_id, 1, view, variant, TILE, 255u, (uint8_t)(CRUCIBLE_ART_SPRITE | form));
    shade = crucible_shade_mask;
  }
  /* place it, two rows jogging sideways while it is still mostly light */
  jog = form < 13u && (rnd() & 1u) ? (uint8_t)(rnd() & 7u) : 0u;
  r1 = rnd() & 3u;
  r2 = rnd() & 3u;
  for (i = 0; i < 16u; i++) {
    uint8_t row = i >> 2, x = (uint8_t)(STAGE_X + (i & 3u) * 8u + 8u);
    if (jog && (row == r1 || row == r2)) x = (uint8_t)(x + jog - 3u);
    set_sprite_tile(i, (uint8_t)(TILE + i));
    set_sprite_prop(i, (shade >> i) & 1u ? 6u : 3u);
    move_sprite(i, x, (uint8_t)(STAGE_Y + row * 8u + 16u));
  }
  return 0;
}
