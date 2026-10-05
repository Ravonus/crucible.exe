/* Test harness for the reference play module, portable to host builds and SM83:
 * catalogue accessors over plain arrays (recipes sorted by (min,max), so a recipe's index is its position),
 * and a minimal v4 record writer/boot that follows the game's scheme: two alternating 512-byte records at
 * 0x0500/0x0700 with magic C1 04, a sequence number, the 72-byte play block at 112, CRC-16/CCITT over 0..509. */
#ifndef PLAY_HARNESS_H
#define PLAY_HARNESS_H
#include <stdint.h>
#include "../crucible_play.h"
#include "play_test_catalog.h"
extern const uint16_t *cat_ra, *cat_rb, *cat_rr, *cat_rfirst, *cat_rlist;
extern const uint8_t *cat_depth;
extern uint16_t h_seq;
void h_catalog(uint16_t n, uint16_t recipes, const uint16_t *ra, const uint16_t *rb, const uint16_t *rr,
               const uint8_t *depth, const uint16_t *rfirst, const uint16_t *rlist);
uint16_t h_result(uint16_t ri);
void h_fresh(const uint16_t *starters, uint8_t count);
/* torn: 0 writes the whole record; k>0 = power fails after k bytes of the record */
void h_store(uint16_t torn);
uint8_t h_boot(void);
uint8_t h_mix(uint16_t a, uint16_t b);
/* torn mix: kind 0 power fails after play_mix (no record), 1 record torn after k bytes, 2 record written
 * but no commit, 3 record written and only the first k journal bytes applied */
void h_mix_torn(uint16_t a, uint16_t b, uint8_t kind, uint16_t k);
void h_migrate_v3(uint8_t items, const uint8_t *owned32, const uint8_t *tried264);
#endif
