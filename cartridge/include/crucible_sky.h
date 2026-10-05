#ifndef CRUCIBLE_SKY_H
#define CRUCIBLE_SKY_H
/* The sky over the room (crucible_sky.c): the moon's phase from a mean synodic month, the year's fixed days, a table of
 * eclipses that wraps by the Saros, the planets' backward months, and the sign marks with the loader that shows yours.
 * All of it feeds only rare flavour lines and the loader: nothing here weighs on the story. */
#include <stdint.h>
#include "crucible_time.h"
/* the day's sky event, the strongest first (sky_event) */
#define SKY_NONE 0u
#define SKY_ECLIPSE_SUN 1u
#define SKY_ECLIPSE_MOON 2u
#define SKY_BIRTHDAY 3u
#define SKY_BLUE 4u /* the month's second full moon */
#define SKY_SUPER 5u /* a full moon near perigee */
#define SKY_FULL 6u
#define SKY_NEW 7u
#define SKY_SEASON 8u /* an equinox or a solstice */
#define SKY_METEORS 9u /* a shower's peak night */
#define SKY_FRI13 10u
#define SKY_MERCURY 11u /* retrograde */
#define SKY_VENUS 12u
#define SKY_MARS 13u
#define SKY_EVENTS 14u
/* moon phases (sky_phase) */
#define MOON_NEW 0u
#define MOON_FULL 4u
uint8_t sky_phase(uint16_t date, uint16_t mod) BANKED; /* 0 new, 1 waxing crescent .. 4 full .. 7 waning crescent */
uint8_t sky_eclipse(uint16_t date) BANKED; /* 0 none, 1 the sun, 2 the moon */
uint8_t sky_retro(uint16_t date) BANKED; /* bit 0 Mercury, 1 Venus, 2 Mars */
uint8_t sky_event(const crucible_time_ctx *t) BANKED; /* SKY_*: today's (needs a known clock) */
/* the loader: the player's mark (sign 0..11, else the neutral one) with the moon beside it, a second of shimmer while a
 * game opens. OAM 0..4, OBJ tiles 0..4 (VRAM bank 0), palette 6 (the bust's sprites must be hidden). */
void sky_loader_open(void) BANKED; /* the player's sign (time_sign) */
uint8_t sky_loader_tick(void) BANKED; /* 1 when it is done (the sprites are hidden again) */
void sky_mark_tiles(uint8_t sign, uint8_t t, uint8_t *out) BANKED; /* 64 bytes: four 8x8 2bpp tiles TL TR BL BR */
#endif
