/* Link check of the banked core on the cartridge: bank-0 SRAM store callbacks, the core autobanked across ROM banks
 * with BANKED entry points and BANKED internal calls (-DCORE_LOCAL=BANKED), the tables in their own bank. It plays a
 * short scripted session and reboots; the results land in `results` (inspect them in an emulator's memory view). */
#include <gb/gb.h>
#include "crucible_core.h"

extern const crucible_tables cru_world_tables;
static crucible_core core;
uint8_t results[16];

/* linear cartridge-RAM address -> bank (at >> 13) and offset in 0xA000..0xBFFF; bank 0, called through pointers */
static uint8_t sram_read(void *ctx, uint32_t at) NONBANKED {
  (void)ctx;
  SWITCH_RAM((uint8_t)((uint16_t)(at >> 8) >> 5));
  return ((volatile uint8_t *)0xa000u)[(uint16_t)at & 0x1fffu];
}
static void sram_write(void *ctx, uint32_t at, uint8_t v) NONBANKED {
  (void)ctx;
  SWITCH_RAM((uint8_t)((uint16_t)(at >> 8) >> 5));
  ((volatile uint8_t *)0xa000u)[(uint16_t)at & 0x1fffu] = v;
}

void main(void) {
  crucible_store s;
  crucible_cells cells;
  s.read = sram_read;
  s.write = sram_write;
  s.ctx = 0;
  ENABLE_RAM;
  cru_init(&core, &cru_world_tables, &s, CRU_LAYOUT_32K);
  results[0] = cru_load(&core, DIV_REG);
  results[1] = cru_mix_begin(&core, 2, 1, DIV_REG); /* FIRE + WATER */
  results[2] = cru_mix_finish(&core); /* +10 */
  results[3] = cru_mix_begin(&core, 1, 2, DIV_REG); /* KNOWN */
  results[4] = cru_mix_finish(&core);
  results[5] = cru_mix_begin(&core, 3, 3, DIV_REG); /* AIR + AIR: NOTHING */
  results[6] = cru_mix_finish(&core);
  cru_bench_cells(&core, &cells);
  results[7] = cells.kind[CRU_CF];
  cru_tick(&core, 8);
  results[8] = cru_load(&core, DIV_REG); /* power-on again: V4 */
  results[9] = (uint8_t)core.points;
  results[10] = (uint8_t)core.found[0]; /* 5 */
  results[11] = cru_owned(&core, 4); /* STEAM */
  DISABLE_RAM;
  while (1) vsync();
}
