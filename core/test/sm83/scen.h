/* Scenarios that run identically on the host (clang) and on a real SM83 (GBDK ROM in PyBoy): the same sessions and
 * save paths as the host tests, sized for a Game Boy (hashes instead of snapshots, sampled power cuts). Each
 * platform supplies the byte store and a few image helpers; the results land in scen_res[], a byte block whose
 * bytes 4.. must be identical on both platforms.
 *
 * Results (scen_res, little-endian):
 *   0..3  "CRU1" once finished          4..5 checks      6..7 failures     8..9 line of the first failure
 *   10    scenario of the first failure 11 scenarios run 12..29 failures per scenario
 *   32..103  state hash per scenario (CRC-32 of the observable core state)
 *   104..175 image hash per scenario (CRC-32 of the store image)
 *   176..207 torn-write counts: per case, cuts that booted to the state before / after (u16 + u16)
 *   208      scenario running now (progress) */
#ifndef SCEN_H
#define SCEN_H
#include <stdint.h>
#include "crucible_core.h"
#include "../../generated/crucible_world_ids.h"

#ifndef SCEN_FN
#define SCEN_FN /* BANKED on the ROM: the scenario files are autobanked */
#endif
#ifndef PLAT_FN
#define PLAT_FN /* NONBANKED on the ROM: the platform lives in bank 0 */
#endif
#define SCEN_N 18u
#define SCEN_RES 216u
extern uint8_t scen_res[SCEN_RES];
extern crucible_core scen_core;

/* ---- platform ---- */
const crucible_tables *plat_tables(void) PLAT_FN;
void plat_store(crucible_store *s) PLAT_FN;
void plat_wipe(void) PLAT_FN; /* zero the whole 128 KB image */
void plat_budget(uint16_t writes, uint8_t on) PLAT_FN; /* on: writes after `writes` more are lost (power cut) */
uint32_t plat_writes(void) PLAT_FN; /* store writes so far */
void plat_backup(void) PLAT_FN; /* bank 0 (0x0000..0x1FFF) to a spare bank */
void plat_restore(void) PLAT_FN;
uint8_t plat_peek(uint32_t at) PLAT_FN; /* direct, uncounted */
void plat_poke(uint32_t at, uint8_t v) PLAT_FN;

/* ---- checks and hashes (scen_util.c) ---- */
void sc_begin(uint8_t scenario) SCEN_FN;
void sc_pass(void) SCEN_FN;
void sc_fail(uint16_t line) SCEN_FN;
#define CHECK(x)                                                                                                       \
  do {                                                                                                                 \
    if (x)                                                                                                             \
      sc_pass();                                                                                                       \
    else                                                                                                               \
      sc_fail((uint16_t)__LINE__);                                                                                     \
  } while (0)
#define EQ(a, b) CHECK((a) == (b))
void sc_end(uint8_t layout) SCEN_FN; /* state + image hashes of this scenario */
void sc_torn(uint8_t index, uint16_t before, uint16_t after) SCEN_FN;
uint32_t sc_state(uint8_t full) SCEN_FN; /* CRC-32 of the observable state (full: every pair's tried bit) */

uint8_t sc_boot(uint8_t layout, uint8_t entropy) SCEN_FN;
uint8_t sc_mix(uint16_t a, uint16_t b) SCEN_FN; /* begin, a second of merge, finish */
void sc_tick(uint16_t frames) SCEN_FN;
void sc_drain(void) SCEN_FN;
uint8_t sc_has_toast(uint8_t code) SCEN_FN;
uint16_t sc_feat_points(void) SCEN_FN; /* tier points of every tier reached */
void sc_make_v3(uint16_t at, uint16_t seq, uint16_t points) SCEN_FN;
void sc_crc_fix(uint16_t at) SCEN_FN; /* recompute a v4 record's CRC after a test edit */

void scen_run_a(void) SCEN_FN; /* sessions 0..5 */
void scen_run_c(void) SCEN_FN; /* sessions 6..10 */
void scen_run_b(void) SCEN_FN; /* save, power cuts, migration, layouts */
#endif
