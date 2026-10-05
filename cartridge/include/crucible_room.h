#ifndef CRUCIBLE_ROOM_H
#define CRUCIBLE_ROOM_H
/* Living rooms (crucible_room.c): the bench and reveal backdrops composed per save, place and time from a small
 * grammar, with palette states driven by play (Pac-Man style: the room flashes at a find,
 * dims and flickers at a miss, turns hostile in a fight, takes a new family at a new chapter), slow ambient cycling,
 * drifters and critters that wander and wrap through the screen edges, rare glitch rooms, and unannounced anomalies
 * with secret interactions.
 *
 * Palettes go through crucible.c's pipeline: the room writes base colours into SRAM bank 2 (ROOM_PAL_BASE) and the
 * plaster, then raises room_dirty (crucible.c's pal_changed); prepare_palettes, the fade LUT, the fight lease and
 * the VBlank upload handle the rest as for any other palette. */
#include <gb/gb.h>
#define ROOM_PAL_BASE 0xbd00u /* BG palettes at +0 (4 colours each), sprite palettes at +32 */
/* crucible.c's screens the room cares about (crucible.c checks they match its enum) */
#define ROOM_S_BENCH 0u
#define ROOM_S_MERGE 1u
#define ROOM_S_REVEAL 2u
#define ROOM_S_FIGHT 8u
extern uint16_t room_plaster; /* crucible.c's plaster: colour 0 behind every object */
extern uint8_t room_dirty; /* crucible.c's pal_changed */
extern uint16_t room_salt; /* folded into the save seed (0; a link partner or a test may set it) */
/* scene_draw's hooks: compose and draw the bench (0) or reveal (1) room; give the palettes back to other screens */
uint8_t room_draw(uint8_t kind) BANKED; /* 0: the home room (the baked scene draws) */
void room_leave(void) BANKED;
uint8_t room_twinkle(uint8_t phase) BANKED; /* 1: the room handled the sky's twinkle */
/* Every frame, before the screen's input is handled: returns pressed, minus the directions an anomaly holds. */
uint8_t room_tick(uint8_t screen, uint8_t pressed) BANKED;
#endif
