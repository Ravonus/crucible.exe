/* Scene-boundary work lives outside the nearly full main code bank. */
#pragma bank 255
#include <gb/gb.h>
#include "keel_cgb_palette_lease.h"
#include "crucible_palette_lease.h"
void crucible_pal_lease_init(keel_cgb_palette_lease_t *s) BANKED { kcpl_init(s); }
void crucible_pal_lease_begin(keel_cgb_palette_lease_t *s, uint16_t *base, uint8_t slot) BANKED {
  kcpl_begin(s, base, slot);
}
uint8_t crucible_pal_lease_end(keel_cgb_palette_lease_t *s, uint16_t *base) BANKED { return kcpl_end(s, base); }
