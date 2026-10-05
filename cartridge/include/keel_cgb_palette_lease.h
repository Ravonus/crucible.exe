#ifndef KEEL_CGB_PALETTE_LEASE_H
#define KEEL_CGB_PALETTE_LEASE_H
/* One scene-owned BG palette lease. Caller owns/switched memory and upload
 * scheduling; this helper never touches hardware, sound, sprites or shades.
 * State may live in SRAM scratch: four saved words plus an inactive=255 slot.
 * A second lease cannot overwrite the original scene's restoration data. */
#include <stdint.h>
#include <string.h>
typedef struct {
  uint16_t saved[4];
  uint8_t slot;
} keel_cgb_palette_lease_t;
#ifndef KEEL_CGB_PALETTE_LEASE_TYPES_ONLY
static void kcpl_init(keel_cgb_palette_lease_t *s) { s->slot = 255u; }
static uint8_t kcpl_begin(keel_cgb_palette_lease_t *s, uint16_t *base, uint8_t slot) {
  if (slot >= 8u || s->slot != 255u) return 0;
  memcpy(s->saved, base + (uint8_t)(slot << 2), 8);
  s->slot = slot;
  return 1;
}
static uint8_t kcpl_end(keel_cgb_palette_lease_t *s, uint16_t *base) {
  if (s->slot >= 8u) return 0;
  memcpy(base + (uint8_t)(s->slot << 2), s->saved, 8);
  s->slot = 255u;
  return 1;
}
static uint8_t kcpl_slot(const keel_cgb_palette_lease_t *s, uint8_t fallback) {
  return s->slot < 8u ? s->slot : fallback;
}
#endif
#endif
