#ifndef CRUCIBLE_LOST_H
#define CRUCIBLE_LOST_H
#include <gb/gb.h>
/* Lost pieces on the bench (crucible_lost.c; the rule is the core's cru_lost.c). Every frame, from room_tick: while slot A
 * and the focus would make a lost piece still cooling (after its first glitched attempt), the result cell's "?" tears:
 * its tiles flicker into corrupt glyphs and stray palettes. Nothing says why. screen: crucible.c's (0 the bench). */
void crucible_lost_tick(uint8_t screen) BANKED;
/* During the merge, after it placed its sprites (from crucible_overlay_tick): a lost piece that will not form corrupts. */
void crucible_lost_merge(void) BANKED;
#endif
