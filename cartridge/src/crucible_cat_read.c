#pragma bank 255
#include <gb/gb.h>
#include "crucible_codec.h"
/* The catalogue is a consecutive sequence of data banks beginning at 32. Strings may cross their boundary. */
void crucible_cat_copy(uint32_t offset, uint8_t *dst, uint8_t n) BANKED {
  uint16_t at;
  uint8_t count;
  while (n) {
    at = (uint16_t)offset & 0x3fffu;
    count = n;
    if ((uint16_t)(0x4000u - at) < count) count = (uint8_t)(0x4000u - at);
    crucible_rom_copy(32u + (uint16_t)(offset >> 14), (const uint8_t *)(0x4000u | at), dst, count);
    offset += count;
    dst += count;
    n -= count;
  }
}
uint8_t crucible_cat_u8(uint32_t offset) BANKED {
  uint8_t n;
  crucible_cat_copy(offset, &n, 1);
  return n;
}
uint16_t crucible_cat_u16(uint32_t offset) BANKED {
  uint8_t n[2];
  crucible_cat_copy(offset, n, 2);
  return (uint16_t)n[0] | ((uint16_t)n[1] << 8);
}
