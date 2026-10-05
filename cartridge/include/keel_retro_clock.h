/* Frame playback without a division on every display pass. Produces exactly
 * (time / ticks) & mask. time is the host's monotonically increasing phase
 * counter, which may reset/wrap; mask is a power-of-two frame count minus one.
 * Reset when an object or its ticks changes. No bank, palette or upload policy. */
#ifndef KEEL_RETRO_CLOCK_H
#define KEEL_RETRO_CLOCK_H
#include <stdint.h>
typedef struct {
  uint16_t last;
  uint8_t remaining, frame;
} kr_frame_clock;
static void kr_clock_reset(kr_frame_clock *c) {
  c->last = 0;
  c->remaining = 0;
  c->frame = 0;
}
static uint8_t kr_clock_frame(kr_frame_clock *c, uint16_t time, uint8_t ticks, uint8_t mask) {
  uint16_t elapsed, q;
  if (!ticks) ticks = 1;
  elapsed = time - c->last;
  /* Large skips use one division instead of an unbounded catch-up loop. */
  if (!c->remaining || time < c->last || elapsed >= ((uint16_t)ticks << 3)) {
    q = time / ticks;
    c->frame = (uint8_t)q;
    c->remaining = ticks - (uint8_t)(time - q * ticks);
  } else if (elapsed < c->remaining)
    c->remaining -= (uint8_t)elapsed;
  else {
    elapsed -= c->remaining;
    c->frame++;
    while (elapsed >= ticks) {
      elapsed -= ticks;
      c->frame++;
    }
    c->remaining = ticks - (uint8_t)elapsed;
  }
  c->last = time;
  return c->frame & mask;
}
#endif
