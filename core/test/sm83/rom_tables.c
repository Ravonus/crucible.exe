/* The cartridge pattern for the catalogue: the ROM tables and cru_tables.c (their only reader) in ONE translation
 * unit, so the autobanker keeps them in one bank. A catalogue that fits one bank would be generated as a file like this. */
#pragma bank 255
#include "../../generated/crucible_tables_world.h"
#include "../../src/cru_tables.c"
const crucible_tables cru_world_tables = CRUCIBLE_WORLD_TABLES(&cru_rules_world);
