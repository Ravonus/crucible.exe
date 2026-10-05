#pragma bank 0
#include <gb/gb.h>
#include <string.h>
/* SM83 builds decode ROM packets with the assembly fast path; host builds use KEEL's validating
 * C decoder, which produces the same 256 bytes. */
#ifdef __SDCC
#include "keel_retro_rom.h"
#define KR_DECODE(src, size, out) kr_rom_decode(src, out)
#else
#include "keel_retro.h"
#define KR_DECODE(src, size, out) ((void)kr_decode(src, size, out))
#endif
#include "crucible_codec.h"
#ifndef CORE_BANKED
#define CORE_BANKED BANKED
#define CORE_LOCAL BANKED
#endif
#include "crucible_core.h"
uint8_t crucible_decoded[256];
static uint8_t anchor[256];
/* NONBANKED preserves the caller's ROM bank while reading its frame stream.
 * Two fixed WRAM buffers; no deep stacks, recursive deltas or cross-bank refs. */
uint8_t *crucible_decode_frame(const uint8_t *data, const uint16_t *offsets, const uint16_t *bases,
                               uint16_t frame) NONBANKED {
  uint16_t i, base = bases[frame];
  KR_DECODE(data + offsets[frame], offsets[frame + 1u] - offsets[frame], crucible_decoded);
  if (base != 65535u) {
    KR_DECODE(data + offsets[base], offsets[base + 1u] - offsets[base], anchor);
    for (i = 0; i < 256u; i++) crucible_decoded[i] ^= anchor[i];
  }
  return crucible_decoded;
}
/* Only data lives above bank 255. Disable interrupts while its ninth MBC5 bit is active, and restore both mapper
 * registers before returning to banked code. SDCC's BANKED trampoline continues to own the low-bank code ABI. */
void crucible_rom_copy(uint16_t bank, const uint8_t *src, uint8_t *dst, uint8_t n) NONBANKED {
  uint8_t save = _current_bank;
  CRITICAL {
    rROMB1 = (uint8_t)(bank >> 8);
    rROMB0 = (uint8_t)bank;
    while (n--) *dst++ = *src++;
    rROMB1 = 0;
    rROMB0 = save;
  }
}
/* An object's keyframe (keel-retro-v1, len bytes at src in ROM bank `bank`) decoded straight from ROM into out. */
void crucible_rom_key(uint16_t bank, const uint8_t *src, uint16_t len, uint8_t *out) NONBANKED {
  uint8_t save = _current_bank;
  (void)len;
  CRITICAL {
    rROMB1 = (uint8_t)(bank >> 8);
    rROMB0 = (uint8_t)bank;
    KR_DECODE(src, len, out);
    rROMB1 = 0;
    rROMB0 = save;
  }
}
/* The CRUCIBLE core's byte store over cartridge SRAM: at = bank*0x2000 + offset. Bank 0 (called through pointers from
 * banked core code); RAM stays enabled while the core runs. */
/* SRAM addresses (0xA000-0xBFFF): identity on the cartridge; a host build maps them onto the switched bank (SRAM_PTR). */
#ifndef SRAM_PTR
#define SRAM_PTR(a) (a)
#endif
uint8_t crucible_sram_read(void *ctx, uint32_t at) NONBANKED {
  (void)ctx;
  ENABLE_RAM;
  SWITCH_RAM((uint8_t)(at >> 13));
  return ((volatile uint8_t *)SRAM_PTR(0xA000u))[(uint16_t)at & 0x1FFFu];
}
void crucible_sram_write(void *ctx, uint32_t at, uint8_t v) NONBANKED {
  (void)ctx;
  ENABLE_RAM;
  SWITCH_RAM((uint8_t)(at >> 13));
  ((volatile uint8_t *)SRAM_PTR(0xA000u))[(uint16_t)at & 0x1FFFu] = v;
}
/* A story run's store (the same mapping as core cru_story.c, here in bank 0 because the core calls it through a
 * pointer from other banks). A slot is one SRAM bank (15, 1 or 3) holding the records and play block (0x0000), OWNED
 * (0x0B00) and RBITS (0x1000); every other address is dropped, so the reject path is byte compares only. */
static uint16_t story_off(uint32_t at) NONBANKED {
  uint8_t hi = ((const uint8_t *)&at)[2];
  uint16_t lo = (uint16_t)at;
  if (hi == 0u) {
    if (lo >= 0x0500u && lo < 0x1000u) return (uint16_t)(lo - 0x0500u);
  } else if (hi == 1u) {
    if (lo >= 0x8000u && lo < 0x8500u) return (uint16_t)(lo - 0x8000u + 0x0B00u);
    if (lo >= 0x8800u && lo < 0x9300u) return (uint16_t)(lo - 0x8800u + 0x1000u);
  }
  return 0xffffu;
}
static uint8_t story_bank(void *ctx) NONBANKED {
  uint8_t s = ((const crucible_story_link *)ctx)->slot;
  return s == 1u ? 1u : s == 2u ? 3u : 15u;
}
uint8_t crucible_story_read(void *ctx, uint32_t at) NONBANKED {
  uint16_t o = story_off(at);
  if (o == 0xffffu) {
    uint8_t hi = ((const uint8_t *)&at)[2];
    uint16_t lo = (uint16_t)at;
    return (hi == 1u && lo >= 0xD800u && lo < 0xD880u) ? 0xffu : 0u;
  } /* empty spill table */
  ENABLE_RAM;
  SWITCH_RAM(story_bank(ctx));
  return ((volatile uint8_t *)SRAM_PTR(0xA000u))[o];
}
void crucible_story_write(void *ctx, uint32_t at, uint8_t v) NONBANKED {
  uint16_t o = story_off(at);
  if (o == 0xffffu) return;
  ENABLE_RAM;
  SWITCH_RAM(story_bank(ctx));
  ((volatile uint8_t *)SRAM_PTR(0xA000u))[o] = v;
}
