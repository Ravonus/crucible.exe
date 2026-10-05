#ifndef CRUCIBLE_FUSION_H
#define CRUCIBLE_FUSION_H
#include <gb/gb.h>
#define FUSION_STEPS 16u
/* HD hook for host builds: after each fusion frame is placed, a host may record (step, centre, apart) and read the
 * three captured specimens from its SRAM bank 2 to redraw the merge at a higher resolution. Empty on the cartridge. */
#ifndef CRUCIBLE_HD_FUSION
#define CRUCIBLE_HD_FUSION(step, cx, cy, apart) ((void)0)
#endif
void crucible_capture_tiles(uint8_t *, uint8_t) BANKED;
void fusion_union(void) BANKED;
void fusion_frame(uint8_t step, uint8_t cx, uint8_t cy, uint8_t apart) BANKED;
void fusion_place(uint8_t which, int16_t x, int16_t y, uint8_t pal) BANKED;
void fusion_hide(void) BANKED;
#endif
