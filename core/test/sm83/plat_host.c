/* Host platform for the scenarios: a 128 KB memory image (bank 12 holds the backup), a write counter with a power-cut
 * budget. Prints scen_res as hex lines, the format scen_check.py compares with the ROM's bytes. */
#include <stdio.h>
#include <string.h>
#include "scen.h"
#include "../../generated/crucible_tables_world.h"

uint8_t scen_res[SCEN_RES];
crucible_core scen_core;
static uint8_t img[0x20000];
static uint32_t writes;
static uint16_t left;
static uint8_t cut;
static const crucible_tables world = CRUCIBLE_WORLD_TABLES(&cru_rules_world);

static uint8_t rd(void *ctx, uint32_t at) {
  (void)ctx;
  return img[at & 0x1ffffu];
}
static void wr(void *ctx, uint32_t at, uint8_t v) {
  (void)ctx;
  writes++;
  if (cut) {
    if (!left) return;
    left--;
  }
  img[at & 0x1ffffu] = v;
}
const crucible_tables *plat_tables(void) { return &world; }
void plat_store(crucible_store *s) {
  s->read = rd;
  s->write = wr;
  s->ctx = 0;
}
void plat_wipe(void) { memset(img, 0, sizeof img); }
void plat_budget(uint16_t n, uint8_t on) {
  left = n;
  cut = on;
}
uint32_t plat_writes(void) { return writes; }
void plat_backup(void) { memcpy(img + 0x18000, img, 0x2000); }
void plat_restore(void) { memcpy(img, img + 0x18000, 0x2000); }
uint8_t plat_peek(uint32_t at) { return img[at & 0x1ffffu]; }
void plat_poke(uint32_t at, uint8_t v) { img[at & 0x1ffffu] = v; }

int main(void) {
  unsigned i;
  scen_run_a();
  scen_run_c();
  scen_run_b();
  scen_res[0] = 'C';
  scen_res[1] = 'R';
  scen_res[2] = 'U';
  scen_res[3] = '1';
  for (i = 0; i < SCEN_RES; i++) printf("%02x%s", scen_res[i], (i & 31u) == 31u ? "\n" : "");
  printf("\n");
  fprintf(stderr, "host scenarios: %u checks, %u failed (first at line %u, scenario %u), %lu store writes\n",
          scen_res[4] | (scen_res[5] << 8), scen_res[6] | (scen_res[7] << 8), scen_res[8] | (scen_res[9] << 8),
          scen_res[10], (unsigned long)writes);
  return (scen_res[6] | scen_res[7]) ? 1 : 0;
}
