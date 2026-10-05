/* The SM83 platform for the scenarios (bank 0): the store over real MBC5 cartridge RAM (16 x 8 KB banks), with a
 * write counter and a power-cut budget; bank 12 holds the backup of bank 0. main() runs every scenario, marks
 * scen_res "CRU1" and spins; scen_check.py reads scen_res (address from the .map) in PyBoy. */
#include <gb/gb.h>
#include <string.h>
#include "scen.h"

uint8_t scen_res[SCEN_RES];
crucible_core scen_core;
extern const crucible_tables cru_world_tables;
static uint32_t writes;
static uint16_t left;
static uint8_t cut;
static uint8_t chunk[256];
#define SRAM ((volatile uint8_t *)0xa000u)

static uint8_t bank_of(uint32_t at) { return (uint8_t)((uint16_t)(at >> 8) >> 5); }
static uint8_t sram_read(void *ctx, uint32_t at) NONBANKED {
  (void)ctx;
  SWITCH_RAM(bank_of(at));
  return SRAM[(uint16_t)at & 0x1fffu];
}
static void sram_write(void *ctx, uint32_t at, uint8_t v) NONBANKED {
  (void)ctx;
  writes++;
  if (cut) {
    if (!left) return;
    left--;
  }
  SWITCH_RAM(bank_of(at));
  SRAM[(uint16_t)at & 0x1fffu] = v;
}
const crucible_tables *plat_tables(void) NONBANKED { return &cru_world_tables; }
void plat_store(crucible_store *s) NONBANKED {
  s->read = sram_read;
  s->write = sram_write;
  s->ctx = 0;
}
void plat_wipe(void) NONBANKED {
  uint8_t b;
  for (b = 0; b < 16u; b++) {
    SWITCH_RAM(b);
    memset((uint8_t *)0xa000u, 0, 0x2000u);
  }
}
void plat_budget(uint16_t n, uint8_t on) NONBANKED {
  left = n;
  cut = on;
}
uint32_t plat_writes(void) NONBANKED { return writes; }
static void copy_bank(uint8_t from, uint8_t to) {
  uint16_t at;
  for (at = 0; at < 0x2000u; at += 256u) {
    SWITCH_RAM(from);
    memcpy(chunk, (uint8_t *)(0xa000u + at), 256);
    SWITCH_RAM(to);
    memcpy((uint8_t *)(0xa000u + at), chunk, 256);
  }
}
void plat_backup(void) NONBANKED { copy_bank(0, 12); }
void plat_restore(void) NONBANKED { copy_bank(12, 0); }
uint8_t plat_peek(uint32_t at) NONBANKED {
  SWITCH_RAM(bank_of(at));
  return SRAM[(uint16_t)at & 0x1fffu];
}
void plat_poke(uint32_t at, uint8_t v) NONBANKED {
  SWITCH_RAM(bank_of(at));
  SRAM[(uint16_t)at & 0x1fffu] = v;
}

void main(void) {
  disable_interrupts();
  ENABLE_RAM;
  scen_run_a();
  scen_run_c();
  scen_run_b();
  scen_res[0] = 'C';
  scen_res[1] = 'R';
  scen_res[2] = 'U';
  scen_res[3] = '1';
  while (1) {}
}
