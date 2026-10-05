/* The battery save belongs to the CRUCIBLE core (core/src/cru_save.c, docs/save-structures.md): save v4 with two CRC-16 records, a write-ahead
 * journal and the play-memory areas in SRAM bank 0, migrating v3/v2/v1 saves of earlier cartridges at power-on.
 * The cartridge only provides the byte store (crucible_codec.c, bank 0) and keeps the one crucible_core. */
#pragma bank 255
#include <gb/gb.h>
#include "crucible_data.h"
#include "crucible_state.h"
#include "crucible_codec.h"
#include "crucible_dialogue.h"
#include "crucible_player.h"
crucible_core core;
extern const crucible_tables cru_world_tables;
void save_boot(void) BANKED {
  /* the store descriptor is built in RAM: the core runs in other banks and must not read this bank's constants */
  crucible_store s;
  s.read = crucible_sram_read;
  s.write = crucible_sram_write;
  s.ctx = 0;
  cru_init(&core, &cru_world_tables, &s, CRU_LAYOUT_128K);
  player_classic_attach(); /* the player card rides in the Classic record */
  if (cru_load(&core, DIV_REG) == CRU_LOAD_FRESH) dialogue_reset_free();
  player_classic_loaded();
}
