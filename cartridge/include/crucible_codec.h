#ifndef CRUCIBLE_CODEC_H
#define CRUCIBLE_CODEC_H
#include <gb/gb.h>
extern uint8_t crucible_decoded[256];
uint8_t *crucible_decode_frame(const uint8_t *, const uint16_t *, const uint16_t *, uint16_t) NONBANKED;
/* Bank-0 trampolines for banked code that reads another ROM bank: copy n bytes, or decode a keel-retro-v1 frame. */
void crucible_rom_copy(uint16_t bank, const uint8_t *src, uint8_t *dst, uint8_t n) NONBANKED;
void crucible_rom_key(uint16_t bank, const uint8_t *src, uint16_t len, uint8_t *out) NONBANKED;
void crucible_cat_copy(uint32_t offset, uint8_t *dst, uint8_t n) BANKED;
uint8_t crucible_cat_u8(uint32_t offset) BANKED;
uint16_t crucible_cat_u16(uint32_t offset) BANKED;
/* 256-byte copy between a CGB WRAM bank (wram_bank, wram_at in D000-DFFF) and wram_buf (C000-CFFF). */
extern uint8_t wram_bank, wram_dir;
extern uint8_t *wram_at;
extern uint8_t *wram_buf;
void crucible_wram_copy(void) BANKED;
/* the CRUCIBLE core's store callbacks (bank 0) */
uint8_t crucible_sram_read(void *ctx, uint32_t at) NONBANKED;
void crucible_sram_write(void *ctx, uint32_t at, uint8_t v) NONBANKED;
uint8_t crucible_story_read(void *ctx, uint32_t at) NONBANKED;
void crucible_story_write(void *ctx, uint32_t at, uint8_t v) NONBANKED;
#endif
