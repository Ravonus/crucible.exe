#ifndef CRUCIBLE_PALETTE_LEASE_H
#define CRUCIBLE_PALETTE_LEASE_H
#define KEEL_CGB_PALETTE_LEASE_TYPES_ONLY
#include "keel_cgb_palette_lease.h"
#undef KEEL_CGB_PALETTE_LEASE_TYPES_ONLY
void crucible_pal_lease_init(keel_cgb_palette_lease_t *) BANKED;
void crucible_pal_lease_begin(keel_cgb_palette_lease_t *, uint16_t *, uint8_t) BANKED;
uint8_t crucible_pal_lease_end(keel_cgb_palette_lease_t *, uint16_t *) BANKED;
#endif
