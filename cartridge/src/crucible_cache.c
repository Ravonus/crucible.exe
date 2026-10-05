#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include "crucible_cache.h"
#include "crucible_codec.h"
#include "crucible_data.h"
/* SRAM addresses (0xA000-0xBFFF): identity on the cartridge; a host build maps them onto the switched bank (SRAM_PTR). */
#ifndef SRAM_PTR
#define SRAM_PTR(a) (a)
#endif
/* A handle includes the SRAM bank. Wire length reserves only compressed bytes. */
uint8_t crucible_asset_begin(uint32_t key, uint32_t version, uint8_t variant, uint16_t length) BANKED {
  uint8_t bank = 1u, slot;
  uint16_t a, b;
  ENABLE_RAM;
  SWITCH_RAM(1);
  a = cc_used((uint8_t *)SRAM_PTR(0xa000));
  SWITCH_RAM(3);
  b = cc_used((uint8_t *)SRAM_PTR(0xa000));
  if (b < a) bank = 3u;
  SWITCH_RAM(bank);
  slot = cc_begin((uint8_t *)SRAM_PTR(0xa000), key, version, variant, length);
  SWITCH_RAM(0);
  DISABLE_RAM;
  return slot == 255u ? 255u : slot + (bank == 3u ? 24u : 0u);
}
uint8_t crucible_asset_byte(uint8_t slot, uint8_t value) BANKED {
  uint8_t ok;
  if (slot >= 48u) return 0;
  ENABLE_RAM;
  SWITCH_RAM(slot < 24u ? 1u : 3u);
  ok = cc_write((uint8_t *)SRAM_PTR(0xa000), slot % 24u, value);
  SWITCH_RAM(0);
  DISABLE_RAM;
  return ok;
}
uint8_t crucible_asset_commit(uint8_t slot, uint16_t crc) BANKED {
  uint8_t ok;
  if (slot >= 48u) return 0;
  ENABLE_RAM;
  SWITCH_RAM(slot < 24u ? 1u : 3u);
  ok = cc_commit((uint8_t *)SRAM_PTR(0xa000), slot % 24u, crc, crucible_decoded);
  SWITCH_RAM(0);
  DISABLE_RAM;
  return ok;
}
uint8_t crucible_cached_frame(uint32_t key, uint32_t version, uint8_t variant, uint8_t frame, uint8_t tile,
                              uint8_t palette) BANKED {
  uint8_t slot, bank, i, ok = 0;
  uint8_t *p, *data;
  uint16_t length;
  ENABLE_RAM;
  for (i = 0; i < 2u && !ok; i++) {
    bank = i ? 3u : 1u;
    SWITCH_RAM(bank);
    slot = cc_find((uint8_t *)SRAM_PTR(0xa000), key, version, variant);
    if (slot != 255u) {
      p = cc_slot((uint8_t *)SRAM_PTR(0xa000), slot);
      data = (uint8_t *)SRAM_PTR(0xa000) + cc_u16(p + 16);
      length = cc_u16(p + 12);
      if (kr_asset_frame(data, length, frame & 7u, crucible_decoded)) {
        crucible_shade_mask = kr_u16(data + 12u + (uint16_t)(frame & 7u) * 2u);
        set_bkg_palette(palette, 1, (uint16_t *)(data + 4u));
        set_bkg_data(tile, 16, crucible_decoded);
        ok = 1;
      } else
        p[0] = 0;
    }
  }
  SWITCH_RAM(0);
  DISABLE_RAM;
  return ok;
}
