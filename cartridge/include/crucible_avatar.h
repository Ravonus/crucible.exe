#ifndef CRUCIBLE_AVATAR_H
#define CRUCIBLE_AVATAR_H
/* Procedural talking faces (crucible_avatar.c): 48x48, 36 sprite tiles, no art data. */
#define AVATAR_BYTES 576u
/* Genome byte 5 bits 3..4: optional human look, independent of cosmetic unlocks. */
#define AV_LOOK_DREAM 0u
#define AV_LOOK_BOY 1u
#define AV_LOOK_GIRL 2u
#define AV_LOOK_MASK 0x18u
void avatar_make(uint16_t seed, uint8_t faction) BANKED; /* faction 0..5 (CRU_FAC_*) */
void avatar_say(char c) BANKED; /* the letter being typed: shapes the mouth */
uint8_t avatar_tick(uint8_t dt) BANKED; /* 1: the face changed (stream it) */
void avatar_glitch(uint8_t in) BANKED; /* 1: glitch in (rows settle), 0: glitch out (rows tear away) */
void avatar_fx(uint8_t fx) BANKED; /* a shader on top (not the aura) */
uint8_t avatar_bob(void) BANKED; /* 0/1 pixel idle bob */
void avatar_palette(uint16_t *out) BANKED;
uint8_t avatar_flash(void) BANKED; /* 1 on the frame a lightning bolt strikes */
void avatar_chunk(uint8_t chunk, uint8_t tile, uint8_t bank) BANKED;
extern uint8_t avatar_still; /* 1: the face does not bob (fights) */
void avatar_make_genome(const uint8_t *g) BANKED; /* the player's face from its 6-byte genome (fight spec 8.1) */
void avatar_bg(uint8_t x, uint8_t y) BANKED; /* the face as 36 BG tiles at (x, y), palette 4 */
void avatar_bg_end(void) BANKED;
void avatar_bg_map(uint8_t x, uint8_t y) BANKED;
void avatar_bg_genome(const uint8_t *g, uint8_t x, uint8_t y) BANKED;
/* a genome's face as BG tiles, from SRAM when it was drawn before */
/* the face's map again (its tiles are still loaded) */ /* palette 4 back */
uint8_t avatar_cached(const uint8_t *g) BANKED; /* the player's face is in the SRAM cache */
void avatar_cache(const uint8_t *g) BANKED; /* put it there (draws: never while a talker's face lives) */
/* the creator (crucible_avatar_edit.c, fight spec 8.2) */
extern uint8_t av_genome[6], av_genome_set;
void av_edit_open(uint16_t seed) BANKED;
uint8_t av_edit_tick(uint8_t pressed) BANKED; /* 0 open, 1 done (av_genome), 2 back to the name */
#endif
