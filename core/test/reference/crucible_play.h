/* CRUCIBLE play memory, save v4: built for catalogues of up to 4096 item ids (16-bit) and 8192 recipes.
 *
 *  exact   OWNED    1 bit per id                       512 B
 *  exact   RBITS    1 bit per recipe ("tried")          1024 B   (recipe index = row of the baked lookup)
 *  exact   USES     uint8 per id, saturating at 255     4096 B   + SPILL: 32 x {id, extra} 128 B for items
 *                   that pass 255 (exact up to 65790 for the first 32 such items)
 *  approx  DUDS     pairs that made nothing: rotating two-generation Bloom filter, 3 Pearson hashes of the
 *                   24-bit key (min<<12)|max. A false positive only ever shows "makes nothing" on a pair that
 *                   truly makes nothing (recipes are answered exactly first); an old generation is cleared
 *                   when the active one is full, so the false-positive rate stays bounded and the oldest
 *                   no-result pairs are forgotten.
 *  record  LIVE     72-byte play block (counters, filter state, journal), stored inside each v4 record
 *
 * SRAM bank 0 (32 KB cartridge RAM, default):          with PLAY_SRAM_128K (MBC5 RAM size $04):
 *   0x0000 DUDS gen 0 (512 B)  reuses the v1/v2/v3       DUDS gen 0 = banks 4-7, gen 1 = banks 8-11
 *   0x0200 DUDS gen 1 (512 B)  area after migration      (32 KB each)
 *   0x0500, 0x0700 v4 records (512 B each, CRC)
 *   0x0900 OWNED  0x0B00 RBITS  0x0F00 LIVE  0x0F80 SPILL  0x1000 USES (to 0x1FFF)
 *
 * Torn-write safety: play_mix() changes nothing in the exact areas; it stages absolute byte writes (at most
 * 12) and the dud key in the LIVE journal. The game writes the CRC record (which carries LIVE), then calls
 * play_commit(), which applies the journal, rotates/clears the filter if due and inserts the dud. play_load()
 * replays the newest valid record's journal (idempotent), finishes an interrupted filter clear, and checks
 * the exact areas against the record's byte sum; a mismatch recounts every counter from the areas.
 * No static work RAM is used.
 *
 * The game provides (beside crucible_recipe_row):
 *   uint8_t  crucible_depth(uint16_t id) BANKED;                      generation depth from the starters
 *   uint16_t crucible_route(uint16_t id,uint8_t k) BANKED;            k-th recipe index making id, or PLAY_NONE16
 *   uint16_t crucible_recipe_at(uint16_t ri,uint16_t*ab) BANKED;     inputs (ab) and result of recipe ri
 *   uint16_t crucible_recipe_find(uint16_t a,uint16_t b) BANKED;     recipe index of a pair, or PLAY_NONE16 */
#ifndef CRUCIBLE_PLAY_H
#define CRUCIBLE_PLAY_H
#include <stdint.h>
#ifdef PLAY_HOST
#define PLAY_BANKED
#elif defined(PLAY_NO_BANK)
#include <gb/gb.h>
#define PLAY_BANKED
#else
#include <gb/gb.h>
#define PLAY_BANKED BANKED
#endif

#define PLAY_NONE16 0xffffu
#define PLAY_V4_A 0x0500u
#define PLAY_V4_B 0x0700u
#define PLAY_OWNED 0x0900u
#define PLAY_RBITS 0x0B00u
#define PLAY_LIVE 0x0F00u
#define PLAY_SPILL 0x0F80u
#define PLAY_USES 0x1000u
#define PLAY_MAX_IDS 4096u
#define PLAY_MAX_RECIPES 8192u
#define PLAY_SPILL_N 32u
#define PLAY_REC_AT 112u /* play block offset inside a v4 record */
#define PLAY_BLOCK 72u
#define PLAY_JOURNAL 12u
#ifdef PLAY_SRAM_128K
#define PLAY_DUD_BITS_LOG 18u /* 32 KB per generation */
#define PLAY_DUD_CAP 21845u /* keys per generation: 12 bits per key */
#else
#define PLAY_DUD_BITS_LOG 12u /* 512 B per generation */
#define PLAY_DUD_CAP 341u
#endif
/* LIVE / play block layout (little-endian u16) */
#define L_VERSION 0u
#define L_FLAGS 1u /* bit0: dud filter initialised */
#define L_OWNED 2u /* items owned */
#define L_RECIPES 4u /* distinct recipes tried */
#define L_ROUTES 6u /* items with every recipe that makes them tried */
#define L_USED10 8u /* items used in 10 or more mixes */
#define L_TOP 10u /* largest use count of any item */
#define L_DUDS 12u /* distinct no-result pairs counted (approximate: a filter hit is not counted) */
#define L_MIRRORS 14u /* distinct same+same recipes tried */
#define L_DEEP 16u /* deepest generation among owned items (u8) */
#define L_GEN 17u /* active dud generation 0/1 (u8) */
#define L_GCOUNT 18u /* keys in the active generation */
#define L_CLEARING 20u /* 1 while the active generation must be cleared before use (u8) */
#define L_SUM 22u /* 16-bit byte sum of OWNED + RBITS + SPILL + USES */
#define L_JN 24u /* journal byte writes 0..12 */
#define L_JDUD 25u /* 1: insert the dud key (JA,JB) at commit */
#define L_JA 26u
#define L_JB 28u
#define L_JOURNAL 30u /* 12 x {addr lo, addr hi, value} = 36 bytes, to 65 */
/* play_mix flags */
#define PLAY_NEW_PAIR 1u
#define PLAY_NEW_RECIPE 2u
#define PLAY_NEW_ITEM 4u
#define PLAY_NEW_ROUTES 8u
#define PLAY_SEEN_DUD 16u
enum {
  PLAY_V_OWNED,
  PLAY_V_RECIPES,
  PLAY_V_ROUTES,
  PLAY_V_USED10,
  PLAY_V_TOP,
  PLAY_V_DUDS,
  PLAY_V_MIRRORS,
  PLAY_V_DEEP,
  PLAY_V_PAIRS
};

void play_fresh(const uint16_t *starters, uint8_t count) PLAY_BANKED;
uint8_t play_load(uint16_t record) PLAY_BANKED;
uint8_t play_block_byte(uint8_t i) PLAY_BANKED;
uint8_t play_mix(uint16_t a, uint16_t b, uint16_t recipe, uint16_t result) PLAY_BANKED;
void play_commit(void) PLAY_BANKED;
uint8_t play_owned(uint16_t id) PLAY_BANKED;
uint8_t play_tried(uint16_t a, uint16_t b, uint16_t recipe) PLAY_BANKED;
uint16_t play_uses(uint16_t id) PLAY_BANKED;
uint16_t play_value(uint8_t stat) PLAY_BANKED;
/* migration from v3 (ids < 64 tried pairs, ids < 256 owned): v3 record offset, the recipe index of each
 * v3 pair is found through crucible_recipe_at. Returns distinct v3 pairs carried over. */
uint16_t play_migrate_v3(uint16_t v3_record, uint8_t v3_items) PLAY_BANKED;
void play_finish_migration(uint16_t v3_record, uint8_t v3_items) PLAY_BANKED;
#ifdef PLAY_HOST
extern uint8_t play_sram[16u * 8192u];
#endif
#endif
