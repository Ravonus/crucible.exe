/* Internal to core: shared helpers, the save v4 store layout and the play block offsets.
 *
 * Banking (the cartridge): the core is larger than one 16 KB ROM bank, so it is written to be split across banks.
 *  - Only cru_tables.c reads catalogue tables, and only through CRU_T8/CRU_T16 below: with the default near reads
 *    it shares a bank with the tables (or the tables sit in bank 0); with far reads it can sit in any bank. Every
 *    other file reaches the catalogue through its CORE_LOCAL accessors.
 *  - Each file's const data (rule tables, Pearson bytes, CRC table, names) is read only by that file.
 *  - Calls between files carry CORE_LOCAL: empty by default (one image, plain calls); a split cartridge build
 *    defines -DCORE_LOCAL=BANKED so they go through GBDK's bank-switching trampolines.
 *  - The store callbacks are called through pointers, so they live in bank 0 (NONBANKED). */
#ifndef CRU_INTERNAL_H
#define CRU_INTERNAL_H
#include "crucible_core.h"

#ifndef CORE_LOCAL
#define CORE_LOCAL
#endif
/* ---- the table-read hook (core/README.md, Wiring) ----
 * CRU_T8(c, TAB_X, i) / CRU_T16(c, TAB_X, i): element i of table TAB_X (crucible_core.h enum), u8 or u16 by table.
 * The core always passes the literal TAB_* token and a plain index expression, keeps no pointer into a table and does
 * no arithmetic on one. The defaults read the crucible_tables arrays; a platform defines both macros before this
 * header (e.g. a wrapper translation unit that defines them and #includes cru_tables.c) to read by (id, index). */
#define CRU_NEAR_TAB_CATEGORY(c) ((c)->t->category)
#define CRU_NEAR_TAB_TRAITS(c) ((c)->t->traits)
#define CRU_NEAR_TAB_STARTERS(c) ((c)->t->starters)
#define CRU_NEAR_TAB_SHELF(c) ((c)->t->shelf)
#define CRU_NEAR_TAB_SHELF_POS(c) ((c)->t->shelf_pos)
#define CRU_NEAR_TAB_RECIPE_A(c) ((c)->t->recipe_a)
#define CRU_NEAR_TAB_RECIPE_B(c) ((c)->t->recipe_b)
#define CRU_NEAR_TAB_RECIPE_R(c) ((c)->t->recipe_r)
#define CRU_NEAR_TAB_RECIPE_BIT(c) ((c)->t->recipe_bit)
#define CRU_NEAR_TAB_RECIPE_SWAP(c) ((c)->t->recipe_swap)
#define CRU_NEAR_TAB_DEPTH(c) ((c)->t->depth)
#define CRU_NEAR_TAB_ROUTE_FIRST(c) ((c)->t->route_first)
#define CRU_NEAR_TAB_ROUTE_LIST(c) ((c)->t->route_list)
#define CRU_NEAR_TAB_TRAIT_SHELF(c) ((c)->t->trait_shelf)
#define CRU_NEAR_TAB_TRAIT_RANK(c) ((c)->t->trait_rank)
#ifndef CRU_T8
#define CRU_T8(c, tab, i) (CRU_NEAR_##tab(c)[i])
#endif
#ifndef CRU_T16
#define CRU_T16(c, tab, i) (CRU_NEAR_##tab(c)[i])
#endif
/* One bit of a byte, from a table (SM83 has no barrel shifter); each file keeps its own 8-byte copy.
 * SDCC safety rule for every bit test in the core (this SDCC miscompiled `(s->rows[t]>>(ry-1))&1` inside a ternary
 * in the cartridge): load the byte into a plain local, take the mask from bit_mask[], test with if/else; never a
 * variable shift, never a ternary around a load through a pointer. */
#define CRI_BITS static const uint8_t bit_mask[8] = {1, 2, 4, 8, 16, 32, 64, 128}

/* ---- store layout (linear cartridge-RAM offsets; crucible_play.h and docs/save-structures.md) ---- */
#define V1_A 0x0000u /* 16-byte records at 0x00 / 0x20 */
#define V1_B 0x0020u
#define V2_A 0x0080u /* 64-byte records at 0x80 / 0xC0 */
#define V2_B 0x00c0u
#define V3_A 0x0100u /* 512-byte records at 0x0100 / 0x0300 */
#define V3_B 0x0300u
#define P_V4_A 0x0500u
#define P_V4_B 0x0700u
/* v4 (32K and 128K placements): the areas in bank 0 */
#define P_OWNED 0x0900u /* 1 bit per id, 512 B */
#define P_RBITS 0x0b00u /* 1 bit per recipe, 1024 B */
#define P_LIVE 0x0f00u /* working copy of the record's play block (every placement) */
#define P_SPILL 0x0f80u /* 32 x {id, extra} use counts past 255 */
#define P_USES 0x1000u /* uint8 use count per id, 4096 B */
/* v5 (WIDE): the areas in banks 12-15, as offsets in the 0x10000 window (so journal entries stay 16-bit) */
#define W_OWNED 0x8000u /* 1 bit per shelf position, 2 KB slot (0x18000) */
#define W_RBITS 0x8800u /* 1 bit per recipe, 4 KB slot (0x18800) */
#define W_USES 0x9800u /* uint8 per id, 16 KB slot (0x19800) */
#define W_SPILL 0xd800u /* 128 B (0x1D800) */
#define W_END 0xd880u
#define P_SCRATCH 0x5e00u /* bank 2, 0x1E00: the v3 tried bitmap, once, during migration */
#define P_REC_AT 112u /* play block offset inside a v4 record */
#define P_BLOCK 72u
#define P_JOURNAL 12u
#define P_SPILL_N 32u
#define REC_SIZE 512u
/* play block (LIVE) offsets, little-endian u16 unless noted */
#define L_VERSION 0u
#define L_FLAGS 1u /* bit 0: dud filter initialised */
#define L_OWNED 2u
#define L_RECIPES 4u
#define L_ROUTES 6u
#define L_USED10 8u
#define L_TOP 10u
#define L_DUDS 12u
#define L_MIRRORS 14u
#define L_DEEP 16u /* u8 */
#define L_GEN 17u /* u8: active dud generation */
#define L_GCOUNT 18u
#define L_CLEARING 20u /* u8 */
#define L_SUM 22u /* 16-bit byte sum of OWNED + RBITS + SPILL + USES */
#define L_JN 24u /* u8: journal entries */
#define L_JDUD 25u /* u8: insert the dud (JA,JB) at commit */
#define L_JA 26u
#define L_JB 28u
#define L_JOURNAL 30u /* 12 x {area offset lo, hi, value} */
#define L_PLACE 66u /* u8: placement that wrote the block (0 in the 32K placement, so v4 there stays byte-identical) */

/* the byte store: a file that touches it says CRI_STORE once and gets its own two helpers (no cross-bank call per
 * byte); CRI_SAT likewise gives a saturating 16-bit add */
#define CRI_READ                                                                                                       \
  static uint8_t cri_rd(crucible_core *c, uint16_t at) { return c->store.read(c->store.ctx, (uint32_t)at); }
#define CRI_STORE                                                                                                      \
  CRI_READ                                                                                                             \
  static void cri_wr(crucible_core *c, uint16_t at, uint8_t v) { c->store.write(c->store.ctx, (uint32_t)at, v); }
/* the play-memory areas: 16-bit offsets in the placement's area window (bank 0, or 0x10000.. for WIDE) */
#define CRI_AREA_RD                                                                                                    \
  static uint8_t cri_ar(crucible_core *c, uint16_t rel) {                                                              \
    uint32_t at = rel;                                                                                                 \
    if (c->area_hi) at |= 0x10000ul;                                                                                   \
    return c->store.read(c->store.ctx, at);                                                                            \
  }
#define CRI_AREA                                                                                                       \
  CRI_AREA_RD                                                                                                          \
  static void cri_aw(crucible_core *c, uint16_t rel, uint8_t v) {                                                      \
    uint32_t at = rel;                                                                                                 \
    if (c->area_hi) at |= 0x10000ul;                                                                                   \
    c->store.write(c->store.ctx, at, v);                                                                               \
  }
#define CRI_SAT                                                                                                        \
  static uint16_t cri_sat_add(uint16_t a, uint16_t b) {                                                                \
    uint16_t s = (uint16_t)(a + b);                                                                                    \
    if (s < a) s = 0xffffu;                                                                                            \
    return s;                                                                                                          \
  }

/* catalogue accessors (cru_tables.c, the only reader of crucible_tables) */
void cri_tables_init(crucible_core *c, const crucible_tables *t) CORE_LOCAL;
uint8_t cri_category(crucible_core *c, uint16_t id) CORE_LOCAL;
uint16_t cri_traits(crucible_core *c, uint16_t id) CORE_LOCAL;
uint16_t cri_shelf(crucible_core *c, uint16_t at) CORE_LOCAL;
uint16_t cri_shelf_pos(crucible_core *c, uint16_t id) CORE_LOCAL;
uint8_t cri_starter(crucible_core *c, uint16_t id) CORE_LOCAL;
uint8_t cri_starter_byte(crucible_core *c, uint16_t i) CORE_LOCAL;
uint8_t cri_depth(crucible_core *c, uint16_t id) CORE_LOCAL;
uint16_t cri_find(crucible_core *c, uint16_t a, uint16_t b) CORE_LOCAL;
uint16_t cri_recipe_a(crucible_core *c, uint16_t row) CORE_LOCAL;
uint16_t cri_recipe_b(crucible_core *c, uint16_t row) CORE_LOCAL;
uint16_t cri_result(crucible_core *c, uint16_t row) CORE_LOCAL;
uint16_t cri_rbit(crucible_core *c, uint16_t row) CORE_LOCAL;
uint8_t cri_swapped(crucible_core *c, uint16_t row) CORE_LOCAL;
uint16_t cri_route_first(crucible_core *c, uint16_t id) CORE_LOCAL;
uint16_t cri_route(crucible_core *c, uint16_t i) CORE_LOCAL;
uint8_t cri_match(crucible_core *c, uint8_t f, uint16_t id) CORE_LOCAL;
uint16_t cri_step(crucible_core *c, uint16_t id, int8_t d) CORE_LOCAL;
/* play memory and tints (cru_play.c) */
void cri_set_place(crucible_core *c, uint8_t place) CORE_LOCAL;
uint8_t cri_owned(crucible_core *c, uint16_t id) CORE_LOCAL;
uint8_t cri_tried(crucible_core *c, uint16_t a, uint16_t b, uint16_t row) CORE_LOCAL;
uint8_t cri_play_mix(crucible_core *c, uint16_t a, uint16_t b, uint16_t row, uint16_t r) CORE_LOCAL;
void cri_play_commit(crucible_core *c) CORE_LOCAL;
uint16_t cri_value(crucible_core *c, uint8_t which) CORE_LOCAL;
void cri_play_init(crucible_core *c, const uint8_t *extra4) CORE_LOCAL;
uint8_t cri_play_load(crucible_core *c, uint16_t rec) CORE_LOCAL;
void cri_play_recount(crucible_core *c) CORE_LOCAL;
void cri_play_drop(crucible_core *c, uint16_t id) CORE_LOCAL;
uint16_t cri_story_pick(crucible_story *s, uint16_t n) CORE_LOCAL; /* 0..n-1 from the run seed */
void cri_migrate_v3(crucible_core *c, uint16_t rec, uint8_t items) CORE_LOCAL;
void cri_finish_migration(crucible_core *c, uint16_t rec, uint8_t items) CORE_LOCAL;
void cri_widen(crucible_core *c, uint16_t rec, uint8_t rec_place, uint16_t old_items, uint16_t old_recipes) CORE_LOCAL;
void cri_filter_reset(crucible_core *c) CORE_LOCAL;
void cri_play_grant(crucible_core *c, uint16_t id) CORE_LOCAL;
uint8_t cri_mod3(uint16_t v) CORE_LOCAL;
uint8_t cri_variant(const crucible_core *c, uint16_t id) CORE_LOCAL;
void cri_set_variant(crucible_core *c, uint16_t id, uint8_t v) CORE_LOCAL;
uint8_t cri_roll(crucible_core *c, uint8_t entropy) CORE_LOCAL;
/* feats (cru_feats.c, which holds the rule tables) */
void cri_feats_reset(crucible_core *c) CORE_LOCAL;
void cri_derive(crucible_core *c) CORE_LOCAL;
void cri_count_new(crucible_core *c, uint16_t id) CORE_LOCAL;
void cri_sum_mark(crucible_core *c, uint16_t pos) CORE_LOCAL;
void cri_feats_mix(crucible_core *c, uint16_t a, uint16_t b, uint16_t result, uint8_t is_new, uint8_t fresh) CORE_LOCAL;
uint8_t cri_check(crucible_core *c) CORE_LOCAL;
uint8_t cri_points(crucible_core *c, uint8_t outcome) CORE_LOCAL;
/* lost pieces (cru_lost.c) */
void cri_lost_mark(crucible_core *c, uint16_t id) CORE_LOCAL;
void cri_lost_regain(crucible_core *c, uint16_t id) CORE_LOCAL;
uint8_t cri_lost_check(crucible_core *c, uint16_t id) CORE_LOCAL;
void cri_lost_warn(crucible_core *c, uint16_t id) CORE_LOCAL;
void cri_lost_combo(crucible_core *c) CORE_LOCAL;
void cri_lost_settle(crucible_core *c) CORE_LOCAL;
/* records and save */
void cri_board_update(crucible_core *c) CORE_LOCAL;
void cri_write(crucible_core *c) CORE_LOCAL;
void cri_save(crucible_core *c) CORE_LOCAL;
#endif
