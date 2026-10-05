/* CRUCIBLE core: the game's rules, once, for the Game Boy Color cartridge (GBDK-2020 / SDCC, SM83); portable C99 that
 * host builds (clang, gcc) also compile.
 *
 * The core owns pure decisions and state: catalogue lookups, the shelf and filters, mixing outcomes and scoring,
 * the 24 feats and 56 titles, stats rows, the leaderboard, and save v4 (two CRC records, the write-ahead journal,
 * the play memory areas, migration from v1/v2/v3). It draws nothing, plays no sound and does no I/O of its own:
 *  - the host fills a crucible_tables (catalogue + rule tables; the cartridge from its baked ROM tables, a host build
 *    from its own catalogue), and
 *  - a crucible_store (a byte store: cartridge SRAM banks, or a file image on a host build), and
 *  - keeps one crucible_core per player (all state lives there: the core has no static RAM).
 *
 * SM83 rules the code follows: integer only; no multiply or divide (shifts and tables; constant shifts only); 32-bit
 * values only in cold paths (the 128 KB layout's dud-filter address, its generation clear); no recursion; small
 * stack frames; const tables; no malloc. Every public entry point carries CORE_BANKED (empty by default; the
 * cartridge builds with -DCORE_BANKED=BANKED -DCORE_LOCAL=BANKED, which also banks the calls between core files,
 * since the core spans two ROM banks). See core/README.md for the wiring on both hosts. */
#ifndef CRUCIBLE_CORE_H
#define CRUCIBLE_CORE_H
#include <stdint.h>
#if defined(__SDCC) && !defined(BANKED)
#include <asm/types.h> /* GBDK: BANKED, NONBANKED */
#endif

#if defined(__SDCC) && !defined(CORE_BANKED)
#error                                                                                                                 \
    "crucible_core.h on SDCC: define CORE_BANKED (BANKED for the banked core, or empty for a one-bank build): without it calls to cru_* are plain calls into another bank"
#endif
#ifndef CORE_BANKED
#define CORE_BANKED
#endif

/* ---- limits and fixed indices ---------------------------------------------------------------------------- */
#define CRU_NONE 0xffffu /* no id, no recipe row */
#define CRU_NONE8 0xffu
#define CRU_MAX_ITEMS 16383u /* item ids are 16-bit and append-only (14 bits used) */
#define CRU_MAX_RECIPES 32767u
#define CRU_COMPACT_ITEMS 4096u /* save v4 (the 32K layout, and 128K for small catalogues) holds up to this... */
#define CRU_COMPACT_RECIPES 8192u /* ...and this; a larger catalogue needs the 128K layout and saves v5 */
#define CRU_CATEGORIES 7u /* ELEMENT MATTER WEATHER ENERGY LIFE CRAFT PLACE */
#define CRU_TRAITS 12u /* HOT COLD WET AIRY STONE SHINY GLOWS ALIVE GREEN MADE BIG MAGIC (bit order) */
#define CRU_FILTERS 20u /* 0 ALL, 1..7 categories, 8..19 traits */
#define CRU_FEATS 24u
#define CRU_TIERS 4u
#define CRU_TITLE_TRAITS 8u /* title rows */
#define CRU_TITLE_DOMAINS 7u /* title columns */
#define CRU_TITLES 56u
#define CRU_BOARD_ROWS 8u
#define CRU_BOARD_ROW 10u /* initials[3], points gained (2), new finds (1), session (2), title, spare */
#define CRU_NAME 8u /* player name; the first three letters are the leaderboard initials */
#define CRU_TOASTS 8u
#define CRU_STATS 13u
#define CRU_FRIEND 0xffffu /* a leaderboard row's session for a linked friend */

/* options byte: bits 0-1 music (on, soft, off), bit 2 sound effects off, bits 3-4 turn speed (normal, slow, fast) */
#define CRU_OPT_SFX_OFF 4u
/* save flags */
#define CRU_FLAG_BOOK_SEEN 1u
#define CRU_FLAG_MIGRATED 2u

/* store layouts: where the dud filter lives (save-structures.md). 32K: two 512 B generations in bank 0 over the
 * old v1-v3 records. 128K: two 32 KB generations in banks 4-7 and 8-11. */
#define CRU_LAYOUT_32K 0u
#define CRU_LAYOUT_128K 1u
/* Where the play memory sits (crucible_core.place), from the layout and the catalogue's size at cru_init:
 *   32K   v4; areas and both dud generations in SRAM bank 0                     (catalogue <= 4096 ids, 8192 recipes)
 *   128K  v4; areas in bank 0, dud generations in banks 4-11                     (the same small catalogues)
 *   WIDE  v5; areas in banks 12-15 (OWNED in shelf order), generations in 4-11   (up to 16383 ids, 32767 recipes)
 * The core writes only these SRAM banks (CRU_BANKS_*), plus 264 bytes of bank 2 at 0x1E00 once, while it migrates
 * a v3 save. Banks 1-3 belong to the cartridge (art cache, fusion scratch). */
#define CRU_PLACE_32K 1u
#define CRU_PLACE_128K 2u
#define CRU_PLACE_WIDE 3u
#define CRU_BANKS_32K 0x0001u /* bank 0 */
#define CRU_BANKS_128K 0xfff1u /* banks 0 and 4-15 */
#define CRU_SCRATCH_AT 0x5e00u /* bank 2, 0x1E00: the v3 migration's 264-byte borrow */
#define CRU_SCRATCH_LEN 264u

/* mix outcomes (and cru_bench_a's extra answer) */
#define CRU_NEW 0u /* result not owned: +10 */
#define CRU_ROUTE 1u /* result owned, this pair never tried: a new recipe, +3 */
#define CRU_KNOWN 2u /* result owned, pair tried: resolves on the bench, 0 */
#define CRU_NOTHING 3u /* no recipe: 0 */
#define CRU_DENY 4u /* an ingredient is not owned */
#define CRU_PICKED 5u /* cru_bench_a: slot A was set, no merge */

/* bench cells and what they show */
#define CRU_CA 0u /* slot A */
#define CRU_CB 1u /* slot B (live preview of the focus) */
#define CRU_CR 2u /* result */
#define CRU_CL 3u /* previous shelf neighbour */
#define CRU_CF 4u /* focus (turning) */
#define CRU_CN 5u /* next shelf neighbour */
#define CRU_CELLS 6u
#define CRU_K_BLANK 0u
#define CRU_K_ITEM 1u
#define CRU_K_EMPTY 2u /* ring */
#define CRU_K_QUESTION 3u /* ring + ? */
#define CRU_K_TRIED 4u /* ring + x: tried, makes nothing */
#define CRU_K_GLITCH 5u /* ring + ?, glitched: the pair would make a lost piece still cooling (cru_lost.c) */
/* the name sign */
#define CRU_MSG_NAME 0u /* the focus name */
#define CRU_MSG_NO_RESULT 1u /* "NO RESULT" */
#define CRU_MSG_RESULT 2u /* "=" + the result name */
/* bench input */
#define CRU_LEFT 1u
#define CRU_RIGHT 2u
#define CRU_UP 3u
#define CRU_DOWN 4u

/* the book caption's recipe line */
#define CRU_BOOK_UNKNOWN 0u /* not owned */
#define CRU_BOOK_RECIPE 1u /* the first tried recipe that makes it (ingredients in authored order) */
#define CRU_BOOK_ELEMENT 2u /* a starter, no tried recipe */
#define CRU_BOOK_SHARED 3u /* owned some other way */

/* cru_load results */
#define CRU_LOAD_V4 0u
#define CRU_LOAD_RECOUNTED 1u /* v4, but the areas or the catalogue changed: every counter was recounted */
#define CRU_LOAD_V3 2u
#define CRU_LOAD_V2 3u
#define CRU_LOAD_V1 4u
#define CRU_LOAD_FRESH 5u
#define CRU_LOAD_BAD_LAYOUT 6u /* the catalogue needs the 128K layout (or a v5 save met the 32K one): nothing loaded */
#define CRU_LOAD_WIDENED 7u /* a v4 save moved into the WIDE placement (saved as v5) */

/* feat statistics (crucible_rules.feat_stat) */
#define CRU_S_FOUND 0u
#define CRU_S_RECIPES 1u
#define CRU_S_GAP 2u /* frames between two finds, lower is better */
#define CRU_S_FLURRY 3u
#define CRU_S_STREAK 4u
#define CRU_S_CHAIN 5u
#define CRU_S_PAIRS 6u
#define CRU_S_FAILS 7u
#define CRU_S_MIRROR 8u
#define CRU_S_GILDED 9u
#define CRU_S_CAT 10u /* 10..16: percent of category k found */
#define CRU_S_BOOK 17u
#define CRU_S_MINUTES 18u
#define CRU_S_COMPLETE 19u /* minutes to find everything, lower is better */
#define CRU_S_FIRST 20u /* seconds from power-on to the fifth find, lower is better */
#define CRU_S_NOFAIL 21u
#define CRU_S_POINTS 22u
#define CRU_S_SWIFT 23u
#define CRU_S_LINKS 24u
/* value units (feats and stats rows) */
#define CRU_UNIT_COUNT 0u
#define CRU_UNIT_FRAMES 1u /* a time in 60 Hz frames; shown as m:ss of value/60 */
#define CRU_UNIT_MINUTES 2u
#define CRU_UNIT_PERCENT 3u
#define CRU_UNIT_SECONDS 4u
#define CRU_UNIT_OF 5u /* value / of */
#define CRU_UNIT_PLAYTIME 6u /* value minutes, of seconds */

/* toasts: a feat tier (f<<2)|tier, or CRU_TOAST_TITLE|title */
#define CRU_TOAST_TITLE 0x80u
#define CRU_TOAST_NONE 0xffu

/* play-memory counters (cru_play_value) */
#define CRU_PV_OWNED 0u
#define CRU_PV_RECIPES 1u /* distinct recipes tried */
#define CRU_PV_ROUTES 2u /* items with every recipe that makes them tried */
#define CRU_PV_USED10 3u /* items used in 10 or more mixes */
#define CRU_PV_TOP 4u /* largest use count */
#define CRU_PV_DUDS 5u /* distinct no-result pairs (approximate: the dud filter forgets old ones) */
#define CRU_PV_MIRRORS 6u
#define CRU_PV_DEEP 7u /* deepest generation owned */
#define CRU_PV_PAIRS 8u /* recipes + duds */
/* play flags of a mix (crucible_mix.flags) */
#define CRU_PLAY_NEW_PAIR 1u
#define CRU_PLAY_NEW_RECIPE 2u
#define CRU_PLAY_NEW_ITEM 4u
#define CRU_PLAY_NEW_ROUTES 8u
#define CRU_PLAY_SEEN_DUD 16u

/* lost pieces (cru_lost.c): an owned piece that stops being owned (given, stolen, lost in a fight, destroyed by a miss,
 * split) cools down: its result cannot form again until CRU_LOST_MIN..CRU_LOST_MAX other mixes have made something.
 * c->lost, saved in the record at bytes 246..263 (CRC-covered, A/B, written with the drop's journal):
 *   [0] version (CRU_LOST_VERSION; 0 in a save from before it: read as empty)   [1] loss serial (wraps)
 *   [2 + 4k] entry k, newest first: id (u16 LE), mixes left (0: the entry is free), flags (bit 0: warned) */
#define CRU_LOST_SLOTS 4u
#define CRU_LOST_HEAD 2u
#define CRU_LOST_BYTES 18u /* CRU_LOST_HEAD + 4 * CRU_LOST_SLOTS */
#define CRU_LOST_VERSION 1u
#define CRU_LOST_MIN 2u
#define CRU_LOST_MAX 10u
#define CRU_LOST_REC_AT 246u /* where c->lost sits in the save record */
#define CRU_CARD_AT 264u /* the host's player card in the save record (264..345; c->card) */
#define CRU_CARD_BYTES 82u
#define CRU_LOST_WARNED 1u /* entry flag: the first attempt has glitched */
/* crucible_mix.lost / cru_lost_state */
#define CRU_LOST_NONE 0u
#define CRU_LOST_FIRST 1u /* the first attempt since the loss: the merge plays and glitches, an avatar speaks */
#define CRU_LOST_AGAIN 2u /* warned already: the bench's result cell glitches (CRU_K_GLITCH); a mix fails quietly */

/* ---- the byte store -------------------------------------------------------------------------------------- *
 * Addresses are linear cartridge-RAM offsets: bank*0x2000 + offset (bank 0 = 0x0000..0x1FFF). The 32K layout uses
 * 0x0000..0x1FFF and, once during v3 migration, 264 bytes at 0x5E00 (bank 2, 0x1E00); the 128K layout also uses
 * 0x8000..0x17FFF. A host build's save file is the same image as a cartridge .sav. On the cartridge the two callbacks are
 * called through pointers from banked code, so they must live in bank 0 (or in the core's own bank). */
typedef struct crucible_store {
  uint8_t (*read)(void *ctx, uint32_t at);
  void (*write)(void *ctx, uint32_t at, uint8_t v);
  void *ctx;
} crucible_store;

/* ---- rule tables (cru_rules_world is the game's; a host points crucible_tables.rules at it) ---------------- */
typedef struct crucible_rules {
  const uint8_t *feat_stat; /* [CRU_FEATS] CRU_S_* */
  const uint8_t *feat_lower; /* [CRU_FEATS] 1: smaller is better (and 0 means "not yet") */
  const uint16_t *feat_tier; /* [CRU_FEATS*4] thresholds at (f<<2)|tier; above the catalogue's maximum = clamped */
  const uint8_t *tier_points; /* [4] points per tier reached */
  const uint8_t *trait_feat; /* [CRU_TITLE_TRAITS] feat behind each title row */
  const uint8_t *domain_feat; /* [CRU_TITLE_DOMAINS] feat behind each title column */
  uint8_t points_new, points_route;
} crucible_rules;
extern const crucible_rules cru_rules_world; /* defined in cru_feats.c, beside the code that reads it */

/* ---- catalogue tables --------------------------------------------------------------------------------------- *
 * The core reads every table through CRU_T8(c, TAB_*, i) / CRU_T16(c, TAB_*, i) (src/cru_internal.h). By default
 * those index the arrays below (host builds, small cartridges); a cartridge whose tables span many ROM banks
 * defines them as far reads by table id and index (core/README.md, Wiring), and leaves the pointers NULL. */
enum {
  TAB_CATEGORY, /* u8  [items] 0..categories-1 */
  TAB_TRAITS, /* u16 [items] trait mask, bit k = trait k */
  TAB_STARTERS, /* u8  [(items+7)>>3] bit per id */
  TAB_SHELF, /* u16 [items] ids in shelf order: category, then id */
  TAB_SHELF_POS, /* u16 [items] shelf position of each id */
  TAB_RECIPE_A, /* u16 [recipes] sorted by (a,b), a<=b */
  TAB_RECIPE_B, /* u16 [recipes] */
  TAB_RECIPE_R, /* u16 [recipes] result id */
  TAB_RECIPE_BIT, /* u16 [recipes] the row's stable tried bit: its authored (append-only) index */
  TAB_RECIPE_SWAP, /* u8  [(recipes+7)>>3] bit set: authored as (b,a) (book order) */
  TAB_DEPTH, /* u8  [items] generation depth from the starters (baked) */
  TAB_ROUTE_FIRST, /* u16 [items+1] */
  TAB_ROUTE_LIST, /* u16 [recipes] rows by result: route_list[route_first[id] .. route_first[id+1]-1] */
  TAB_TRAIT_SHELF, /* u8  [12*S] trait k's bitmap in shelf order at k*S + (pos>>3), S = (items+7)>>3 */
  TAB_TRAIT_RANK, /* u16 [12*B] trait k: positions with it before block b at k*B + b, B = (items+63)>>6 */
  TAB_COUNT
};
typedef struct crucible_tables {
  uint16_t items, recipes;
  uint8_t categories;
  const uint8_t *category; /* [items] 0..categories-1 */
  const uint16_t *traits; /* [items] trait mask, bit k = trait k */
  const uint8_t *starters; /* [(items+7)>>3] bit per id */
  const uint16_t *shelf; /* [items] ids in shelf order: category, then id */
  const uint16_t *shelf_pos; /* [items] shelf position of each id */
  const uint16_t *recipe_a; /* [recipes] sorted by (a,b), a<=b */
  const uint16_t *recipe_b;
  const uint16_t *recipe_r; /* result id */
  const uint16_t *recipe_bit; /* [recipes] the row's stable tried bit (a permutation of 0..recipes-1) */
  const uint8_t *recipe_swap; /* [(recipes+7)>>3] bit set: authored as (b,a) (book order) */
  const uint8_t *depth; /* [items] generation depth from the starters (baked) */
  const uint16_t *route_first; /* [items+1] */
  const uint16_t *route_list; /* [recipes] rows by result: route_list[route_first[id] .. route_first[id+1]-1] */
  const crucible_rules *rules;
  const uint8_t *trait_shelf; /* TAB_TRAIT_SHELF */
  const uint16_t *trait_rank; /* TAB_TRAIT_RANK */
  /* catalogue facts, baked with the tables (read once, at cru_init) */
  uint16_t mirrors; /* same + same recipes */
  uint16_t cat_first[CRU_CATEGORIES + 1]; /* first shelf position of each category; [CRU_CATEGORIES] = items */
  uint16_t totals[CRU_FILTERS]; /* items per filter (0 ALL, 1..7 categories, 8..19 traits) */
} crucible_tables;

/* ---- a mix in progress ------------------------------------------------------------------------------------ */
typedef struct crucible_mix {
  uint16_t a, b; /* ingredients */
  uint16_t row; /* sorted recipe row, or CRU_NONE */
  uint16_t result; /* result id, or CRU_NONE */
  uint8_t outcome; /* CRU_NEW .. CRU_DENY, decided by cru_mix_begin */
  uint8_t variant; /* the result's tint (a NEW result's is rolled by cru_mix_begin) */
  uint8_t awarded; /* points (cru_mix_finish) */
  uint8_t fresh; /* the pair was never tried before (cru_mix_finish) */
  uint8_t flags; /* CRU_PLAY_* (cru_mix_finish) */
  uint8_t toasts; /* toasts this mix queued (cru_mix_finish) */
  uint8_t open; /* 1 between begin and finish */
  uint8_t lost; /* CRU_LOST_*: the result is a lost piece still cooling (outcome NOTHING, nothing made) */
} crucible_mix;

/* ---- what the host reads for one screen ------------------------------------------------------------------- */
typedef struct crucible_cells {
  uint8_t kind[CRU_CELLS]; /* CRU_K_* per cell CRU_CA..CRU_CN */
  uint16_t id[CRU_CELLS]; /* the item of a CRU_K_ITEM cell */
  uint8_t message; /* CRU_MSG_* */
  uint16_t sign; /* the id whose name the sign shows (focus, or the result for CRU_MSG_RESULT) */
} crucible_cells;

typedef struct crucible_stat {
  uint16_t value, of;
  uint8_t unit; /* CRU_UNIT_* */
  uint8_t known; /* 0: show "--" (not yet) */
} crucible_stat;

typedef struct crucible_board_row {
  char name[CRU_NAME + 1]; /* trailing blanks trimmed */
  uint16_t points; /* points gained in that run; 0: an empty row ("---") */
  uint16_t session; /* CRU_FRIEND for a linked friend */
  uint8_t finds; /* new finds in that run */
  uint8_t title; /* worn title then: 0 none, else title + 1 */
  uint8_t mine; /* this power-on's run */
} crucible_board_row;

/* ---- the player's state (caller-provided; read freely, change through the functions; options is the host's) -- */
typedef struct crucible_core {
  const crucible_tables *t; /* read only by cru_tables.c (on a banked cartridge, in the tables' bank) */
  const crucible_rules *rules; /* read only by cru_feats.c (on the cartridge, cru_rules_world, in that bank) */
  uint16_t items, recipes; /* catalogue size, copied at cru_init */
  crucible_store store;
  uint8_t layout;
  uint8_t lean; /* a story run: no dud filter, no use counts, no sum check (its store keeps none) */
  /* the placement (CRU_PLACE_*) and its play-memory areas (16-bit offsets in the area window: bank 0 for 32K and
   * 128K, 0x10000.. for WIDE); the catalogue facts the tables carry */
  uint8_t place, area_hi;
  uint16_t a_owned, a_rbits, a_uses, a_spill;
  uint16_t cat_first[CRU_CATEGORIES + 1];
  uint16_t total[CRU_FILTERS], mirror_total; /* per filter: all items */
  /* saved in the record */
  uint16_t sequence, points, rng, made, session, variant_seed;
  uint16_t minutes, fails, book_views, complete, best_gap, best_first, last_new;
  uint8_t title, filter, flags, routes, options, seconds;
  uint8_t streak, best_streak, chain, best_chain, best_flurry, nofail, best_nofail, best_swift, links;
  char name[CRU_NAME];
  uint8_t feats[CRU_FEATS];
  uint8_t variants[64]; /* two bits per id < 256 (ids above derive their tint from variant_seed) */
  uint8_t board[CRU_BOARD_ROWS * CRU_BOARD_ROW];
  uint8_t tails[CRU_BOARD_ROWS * 5u]; /* name letters 4..8 of each leaderboard row */
  /* derived at load and kept up to date */
  uint16_t found[CRU_FILTERS]; /* per filter: owned; found[0] = items owned */
  uint16_t gilded, pair_cap;
  uint8_t shelf_sum[32]; /* bit b: some item at shelf positions 64b..64b+63 is owned (rebuilt at every load) */
  /* this power-on */
  uint16_t boot_points, boot_found, clock, since_find, session_s;
  uint8_t frames, session_finds, found_this_session;
  uint16_t swift_t[24], flurry_t[8];
  uint8_t swift_first, swift_n, flurry_first, flurry_n;
  uint8_t toast[CRU_TOASTS], toast_first, toast_n;
  /* the bench */
  uint16_t focus, slot_a, slot_b;
  uint8_t message;
  crucible_mix mix;
  /* lost pieces still cooling (cru_lost.c), saved in the record at bytes 246..263 exactly as laid out here */
  uint8_t lost[CRU_LOST_BYTES];
  /* the host's player card (CRU_CARD_BYTES at record byte 264, covered by the record's CRC): when set, cru_save writes
   * it and cru_load reads it back (left alone when no record loads); NULL writes zeros, as before. cru_init clears it;
   * cru_load keeps it. */
  uint8_t *card;
} crucible_core;

/* ---- lifecycle and save ----------------------------------------------------------------------------------- */
void cru_init(crucible_core *c, const crucible_tables *t, const crucible_store *s, uint8_t layout) CORE_BANKED;
/* Power-on: newest valid v4 record (journal replayed, areas verified), else migrate v3, v2 or v1, else a fresh
 * save. Starts this run (session + 1), runs the boot feat check (and saves if a tier was reached). entropy seeds
 * a fresh save's tints (DIV_REG on the cartridge). Returns CRU_LOAD_*. */
uint8_t cru_load(crucible_core *c, uint8_t entropy) CORE_BANKED;
/* Writes the record (the leaderboard row of this run first). The core saves by itself after a mix, a filter
 * change, wearing a title and a tier reached at a minute; the host calls it after changing settings or the name. */
void cru_save(crucible_core *c) CORE_BANKED;
void cru_stir(crucible_core *c, uint8_t entropy) CORE_BANKED; /* folds input/timer noise into the rng */
void cru_set_name(crucible_core *c, const char *name) CORE_BANKED; /* up to 8 letters; blank becomes YOU */
void cru_get_name(const crucible_core *c, char *out) CORE_BANKED; /* out[CRU_NAME+1] */

/* ---- catalogue -------------------------------------------------------------------------------------------- */
uint16_t cru_recipe_row(crucible_core *c, uint16_t a, uint16_t b) CORE_BANKED; /* unordered; CRU_NONE */
uint16_t cru_recipe(crucible_core *c, uint16_t a, uint16_t b) CORE_BANKED; /* result id or CRU_NONE */
uint8_t cru_is_starter(crucible_core *c, uint16_t id) CORE_BANKED;
uint8_t cru_owned(crucible_core *c, uint16_t id) CORE_BANKED;
uint8_t cru_variant(const crucible_core *c, uint16_t id) CORE_BANKED; /* 0..2 tints, 3 gilded */
uint8_t cru_tried(crucible_core *c, uint16_t a, uint16_t b) CORE_BANKED; /* exact for recipes */

/* ---- shelf and filter ------------------------------------------------------------------------------------- */
uint16_t cru_shelf_step(crucible_core *c, uint16_t id, int8_t d) CORE_BANKED; /* next/previous owned, filtered, wraps */
uint16_t cru_shelf_group(crucible_core *c, uint16_t id,
                         int8_t d) CORE_BANKED; /* first owned of the next/previous group */
uint8_t cru_filter_match(crucible_core *c, uint8_t f, uint16_t id) CORE_BANKED;
uint16_t cru_filter_found(const crucible_core *c, uint8_t f) CORE_BANKED;
uint16_t cru_filter_total(const crucible_core *c, uint8_t f) CORE_BANKED;
uint8_t cru_filter_apply(crucible_core *c, uint8_t f) CORE_BANKED; /* 0: denied (none found), 1: applied */
void cru_filter_close(crucible_core *c) CORE_BANKED; /* after the filter screen: refocus */

/* ---- the bench -------------------------------------------------------------------------------------------- */
uint8_t cru_bench_move(crucible_core *c, uint8_t dir) CORE_BANKED; /* CRU_LEFT.. ; 1 if the focus changed */
uint8_t cru_bench_a(crucible_core *c,
                    uint8_t entropy) CORE_BANKED; /* CRU_PICKED, or the outcome of the merge it starts */
uint8_t cru_bench_b(crucible_core *c) CORE_BANKED; /* 1: slot A cleared */
void cru_bench_cells(crucible_core *c, crucible_cells *out) CORE_BANKED;
void cru_reveal_close(crucible_core *c) CORE_BANKED; /* after a NEW/ROUTE reveal: focus the result */

/* ---- mixing ----------------------------------------------------------------------------------------------- */
uint8_t cru_mix_begin(crucible_core *c, uint16_t a, uint16_t b, uint8_t entropy) CORE_BANKED;
/* Commits c->mix: play memory staged, points, feats, record written, journal applied. A KNOWN or NOTHING mix
 * started from the bench lands on it (slot A stays, the second pick takes focus, the sign's message). */
uint8_t cru_mix_finish(crucible_core *c) CORE_BANKED;

/* ---- recipe book ------------------------------------------------------------------------------------------ */
uint16_t cru_book_rows(crucible_core *c, uint16_t *out,
                       uint16_t max) CORE_BANKED; /* shelf order, filtered; returns all */
/* A scrolling book at any catalogue size: the filtered row count, rows from filtered index start (returns how many
 * were written), and the filtered index of an id (CRU_NONE when the filter hides it), to centre on the focus. */
uint16_t cru_book_count(crucible_core *c) CORE_BANKED;
uint16_t cru_book_rows_from(crucible_core *c, uint16_t start, uint16_t *out, uint16_t max) CORE_BANKED;
uint16_t cru_book_index(crucible_core *c, uint16_t id) CORE_BANKED;
uint16_t cru_sram_banks(const crucible_core *c) CORE_BANKED; /* the SRAM banks this placement writes (CRU_BANKS_*) */
uint8_t cru_book_route(crucible_core *c, uint16_t id, uint16_t *ab) CORE_BANKED; /* CRU_BOOK_* */
void cru_book_open(crucible_core *c) CORE_BANKED; /* marks the book seen and counts a view */
void cru_book_view(crucible_core *c) CORE_BANKED; /* each cursor move (SCHOLAR) */
uint8_t cru_book_use(crucible_core *c, uint16_t id) CORE_BANKED; /* A on a row: 1 focus it, 0 deny */

/* ---- feats, clocks, toasts -------------------------------------------------------------------------------- */
void cru_tick(crucible_core *c, uint8_t dt) CORE_BANKED; /* dt frames (the cartridge caps it at 8) */
void cru_link(crucible_core *c) CORE_BANKED; /* a link exchange happened (LINKED) */
uint8_t cru_toast_peek(const crucible_core *c) CORE_BANKED; /* CRU_TOAST_NONE when empty */
void cru_toast_done(crucible_core *c) CORE_BANKED;
uint8_t cru_feat_tier(const crucible_core *c, uint8_t f) CORE_BANKED;
uint16_t cru_feat_value(crucible_core *c, uint8_t f) CORE_BANKED;
uint16_t cru_feat_threshold(const crucible_core *c, uint8_t f, uint8_t tier) CORE_BANKED;
uint8_t cru_feat_unit(const crucible_core *c, uint8_t f) CORE_BANKED;
uint8_t cru_feat_lower(const crucible_core *c, uint8_t f) CORE_BANKED;
uint8_t cru_feats_total(const crucible_core *c) CORE_BANKED; /* tiers reached, of 96 */

/* ---- titles ----------------------------------------------------------------------------------------------- */
uint8_t cru_title_grade(const crucible_core *c, uint8_t t) CORE_BANKED; /* min(trait feat tier, domain feat tier) */
uint8_t cru_title_needs(const crucible_core *c, uint8_t t) CORE_BANKED; /* bit 0 trait feat locked, bit 1 domain */
uint8_t cru_title_feat(const crucible_core *c, uint8_t t, uint8_t axis) CORE_BANKED; /* axis 0 trait, 1 domain */
uint8_t cru_titles_earned(const crucible_core *c) CORE_BANKED;
uint8_t cru_title_wear(crucible_core *c, uint8_t t) CORE_BANKED; /* 0 deny, 1 worn, 2 taken off (saves) */

/* ---- records ---------------------------------------------------------------------------------------------- */
void cru_stat(crucible_core *c, uint8_t row, crucible_stat *out) CORE_BANKED; /* STATS rows 0..12 */
void cru_board_row(const crucible_core *c, uint8_t i, crucible_board_row *out) CORE_BANKED;
uint8_t cru_board_current(const crucible_core *c) CORE_BANKED; /* this run's row or CRU_NONE8 */
uint16_t cru_board_run_points(const crucible_core *c) CORE_BANKED;
uint16_t cru_play_value(crucible_core *c, uint8_t which) CORE_BANKED; /* CRU_PV_* */
uint16_t cru_play_uses(crucible_core *c, uint16_t id) CORE_BANKED;

/* ---- names (copied into a caller buffer) ------------------------------------------------------------------ */
void cru_feat_name(uint8_t f, char *out) CORE_BANKED; /* out[11] */
void cru_feat_desc(uint8_t f, char *out) CORE_BANKED; /* out[19] */
void cru_tag_name(uint8_t k, char *out) CORE_BANKED; /* trait k: HOT .. MAGIC; out[6] */
void cru_title_part(uint8_t axis, uint8_t i, char *out) CORE_BANKED; /* axis 0 trait row, 1 domain column; out[11] */
void cru_title_name(uint8_t t, char *out) CORE_BANKED; /* "SWIFT ADEPT"; out[19] */

/* ---- grants (cru_grant.c) --------------------------------------------------------------------------------- */
#define CRU_GRANT_SYNC 0u /* found on another device of the same account */
#define CRU_GRANT_IMPORT 1u /* carried over from an older save format */
/* An item owned without a mix here (a discovery synced from another device, or an older save's). No mix points and no
 * pace (streaks, chains, clocks); its tint is rolled (source stirs the roll); found/total, gilded and the feats stay
 * consistent (a tier it completes is awarded with its points and toast, as at power-on); not a find of this run on the
 * leaderboard. Saved through the journal like a mix. 1: now owned; 0: owned already, out of range, or a mix is open
 * (grant it after cru_mix_finish). */
uint8_t cru_grant(crucible_core *c, uint16_t id, uint8_t source) CORE_BANKED;
uint8_t cru_drop_any(crucible_core *c, uint16_t id) CORE_BANKED; /* a story run's destruction: even a starter can go */
/* The reverse: id stops being owned (a split, or a story run's loss); never a starter. Its discovery history stays.
 * Saved through the journal. 1: dropped. */
uint8_t cru_drop(crucible_core *c, uint16_t id) CORE_BANKED;

/* ---- lost pieces (cru_lost.c) ----------------------------------------------------------------------------- *
 * Every drop (cru_drop, cru_drop_any: a gift, a theft, a fight, a miss, a split) of a piece some recipe makes starts its
 * cooldown, N in CRU_LOST_MIN..CRU_LOST_MAX from the save's tint seed, the id and the loss serial (deterministic). A mix
 * that would make it opens as CRU_NOTHING with mix.lost set: nothing is made, scored, tried or counted. Every other mix
 * that makes something (NEW, ROUTE, KNOWN) counts one down; at zero it is free again. Getting it back (cru_grant) ends
 * it. */
uint8_t cru_lost_state(crucible_core *c, uint16_t id) CORE_BANKED; /* CRU_LOST_NONE / FIRST / AGAIN */
uint8_t cru_lost_left(crucible_core *c, uint16_t id) CORE_BANKED; /* mixes still to make (0: not cooling) */
uint8_t cru_lost_cue(crucible_core *c, uint16_t a,
                     uint16_t b) CORE_BANKED; /* 1: a + b would make a warned lost piece */
uint8_t cru_lost_span(uint16_t seed, uint16_t id, uint8_t serial) CORE_BANKED; /* the cooldown N for that loss */

/* ---- reset (cru_reset.c) ---------------------------------------------------------------------------------- */
/* RESET GAME: all progress goes; the save starts over as a fresh one (the starters owned with
 * new tints, entropy seeds them) keeping the options byte, the name and the record sequence; the bench opens as at
 * power-on. Saves. Not torn-write safe. */
void cru_reset(crucible_core *c, uint8_t entropy) CORE_BANKED;

/* ---- story runs (cru_story.c) ------------------------------------------------------------------------------ *
 * A second game beside Classic, in its own save (SRAM bank 15, which Classic never writes): a crucible_core opened
 * on cru_story_store, plus this record. The run seed is re-hashed with every event, so runs branch apart. */
#define CRU_STORY_VERSION 5u
#define CRU_STORY_GENTLE 0u /* loss scale: a mistake costs clock only */
#define CRU_STORY_NORMAL 1u /* one element */
#define CRU_STORY_HARSH 2u /* one to three, the last locked for the run */
#define CRU_STORY_LOCKS 8u
#define CRU_STORY_FLAGS 256u
#define CRU_STORY_MEMORY 8u
#define CRU_STORY_SPLIT_COST 30u /* clock seconds */
#define CRU_STORY_MISTAKE_COST 60u
#define CRU_EV_MIX 0u
#define CRU_EV_SPLIT 1u
#define CRU_EV_LOSS 2u
#define CRU_EV_GAIN 3u
#define CRU_EV_FLAG 4u
#define CRU_EV_CHOICE 5u
#define CRU_EV_MOVE 6u
#define CRU_EV_FIGHT 7u
#define CRU_EV_LINK 8u /* a link partner's seed and alignment cell drift into the run */
/* factions: the archetypes of the machine's people (cru_saga.c) */
#define CRU_FAC_PROGRAM 0u /* the grid's programs (Tron): like CRAFT, made / glowing / shiny things */
#define CRU_FAC_DAEMON 1u /* background processes: ENERGY, heat, weight */
#define CRU_FAC_GHOST 2u /* echoes in the shell: WEATHER, air, cold, magic */
#define CRU_FAC_AI 3u /* the machine's minds: MATTER, stone, sheen, cold */
#define CRU_FAC_OPERATOR 4u /* the humans jacked in: LIFE, green, wet */
#define CRU_FAC_RELIC 5u /* old things that remember: PLACE, stone, magic, made */
#define CRU_FACTIONS 6u
#define CRU_TIER_HOSTILE 0u /* standing <= -50 */
#define CRU_TIER_WARY 1u /* <= -15 */
#define CRU_TIER_NEUTRAL 2u
#define CRU_TIER_FRIEND 3u /* >= 15 */
#define CRU_TIER_ALLY 4u /* >= 50 */
#define CRU_SAY_AGREE 0u
#define CRU_SAY_REFUSE 1u
#define CRU_SAY_OFFER 2u /* give them an element */
#define CRU_REACT_HATE 0u
#define CRU_REACT_COLD 1u
#define CRU_REACT_WARM 2u
#define CRU_REACT_LOVE 3u
#define CRU_END_LOOP 0u /* no ally: the dream starts over; else 1 + the leading ally's faction; */
#define CRU_END_WAKE 7u /* everyone at least a friend: you wake */
#define CRU_END_SCHISM 8u /* allied to two rivals: the machine tears */
#define CRU_STORY_CHAPTERS 8u
/* the truths: you start not knowing you are stuck, or what you are. Nothing is rolled: every element you make and
 * every act (cru_story_act) leans the truth matrix, and the truth on each axis is whatever your play leans to most
 * (the seed only tilts ties). Clues are dealt about the current leader; if play turns, old clues point elsewhere. An
 * ending answers what is clued and clear, hedges between two close truths, and asks about the rest. */
#define CRU_TRUTH_WHAT 0u /* PROGRAM PERSON GHOST COPY CHILD AI DREAMER NOBODY */
#define CRU_TRUTH_WHERE 1u /* CARTRIDGE BACKROOMS HOSPITAL ARCADE SERVER BEDROOM GRID NOWHERE */
#define CRU_TRUTH_WHY 2u /* ASLEEP POWERCUT NOCLIP UPLOADED FORGET PLAYED BATTERY NEVERLEFT */
#define CRU_KNOW_NOTHING 0u
#define CRU_KNOW_SUSPECT 1u /* two clues */
#define CRU_KNOW_ANSWER 2u /* four, and clearly leading */
#define CRU_ACT_AGREE 0u
#define CRU_ACT_REFUSE 1u
#define CRU_ACT_GIFT 2u
#define CRU_ACT_SPLIT 3u
#define CRU_ACT_LOSS 4u
#define CRU_ACT_WIN 5u /* a boss beaten */
#define CRU_ACT_HIT 6u /* a boss's attack landed */
#define CRU_ACT_LOOP 7u /* the same thing again */
#define CRU_ACT_EGG 8u /* a secret found */
#define CRU_ACT_IDLE 9u /* standing still in the dream */
#define CRU_ACT_DEEP 10u /* lucidity low */
#define CRU_ACT_CYCLE 11u /* going on past an ending */
#define CRU_ACT_SPARE 12u /* a broken boss let go */
#define CRU_ACT_TAKE 13u /* a broken boss's element kept */
#define CRU_RUN_ON 0u /* the run goes on */
#define CRU_RUN_LOST_DREAM 1u /* lucidity ran out: lost in the dream */
#define CRU_RUN_LOST_DELETED 2u /* four factions hostile: the machine deletes you */
#define CRU_RUN_LOST_HOLLOW 3u /* fewer than two elements left: nothing can be combined, the run is over for good */
#define CRU_RUN_END 4u /* the last chapter closed: an ending (cru_story_ending, cru_story_grade) */
/* a boss: a hostile faction's champion that combines its own hand (and your elements) into attacks */
#define CRU_BOSS_HIT 0u /* no counter: the attack lands */
#define CRU_BOSS_PARRY 1u /* a weak counter: 1 damage */
#define CRU_BOSS_COUNTER 2u /* a strong counter: 2+ damage */
#define CRU_BOSS_PHASE 3u /* it took the hit and changed (a new phase: faster, angrier) */
#define CRU_BOSS_FLED 4u /* the nemesis escaped, to return later */
#define CRU_BOSS_DOWN 5u /* defeated */
typedef struct crucible_boss {
  uint16_t hand[4]; /* what it holds; its results join the hand, so it grows through the fight */
  uint16_t next, next_a, next_b; /* the attack it is making (telegraphed) and from what; CRU_NONE between rounds */
  uint16_t stolen; /* one of yours it took, CRU_NONE */
  uint8_t faction, hp, max, phase, cue, level, met, spare;
} crucible_boss;
typedef struct crucible_story {
  uint8_t version, scale, sector, lucid; /* lucid: 255 clear .. 0 deep dream (slots drift, scenes glitch) */
  uint32_t seed;
  uint16_t clock;
  uint16_t locks[CRU_STORY_LOCKS];
  uint8_t flags[CRU_STORY_FLAGS / 8u];
  int8_t stand[8]; /* standing with each faction, -100..100 */
  uint16_t memory[CRU_STORY_MEMORY]; /* recent elements made, offered or lost (a ring; CRU_NONE empty) */
  char name[8]; /* the player's name, for the slot screen */
  uint16_t gift; /* what the last cru_story_choose gave back (CRU_NONE: nothing) */
  uint8_t mem_at, chapter, cycle,
      fails; /* chapter 0..CRU_STORY_CHAPTERS; cycle: times past an ending; fails: misses in a row */
  int16_t lean[3][8]; /* the truth matrix: how much play has leaned to each truth (what, where, why) */
  uint8_t clue[12]; /* clues found per truth, a nibble each */
  crucible_boss nemesis; /* the recurring boss (nemesis.met: 0 none yet) */
} crucible_story;
#define CRU_STORY_SLOTS 3u
/* where a run lives: the battery store and the slot (0..2). The caller owns it; it must outlive the run's core. */
typedef struct crucible_story_link {
  const crucible_store *base;
  uint8_t slot;
} crucible_story_link;
uint8_t cru_story_peek(const crucible_store *base, uint8_t slot,
                       crucible_story *out) CORE_BANKED; /* 1: a run is saved there */
void cru_story_wipe(const crucible_store *base, uint8_t slot) CORE_BANKED;
void cru_story_store(crucible_story_link *link, crucible_store *out) CORE_BANKED;
/* Opens the run in c (story_store must outlive c; left zeroed it becomes cru_story_store's mapping, or the caller
 * fills it with its own: a banked cartridge's store functions must sit in bank 0 to be called through pointers). 1: a new run was started (scale applies); 0: the saved run resumed. */
uint8_t cru_story_open(crucible_core *c, crucible_story *s, const crucible_tables *t, crucible_story_link *link,
                       crucible_store *story_store, uint8_t entropy, uint8_t scale) CORE_BANKED;
void cru_story_save(const crucible_story_link *link, const crucible_story *s) CORE_BANKED;
void cru_story_event(crucible_story *s, uint8_t kind, uint16_t a, uint16_t b) CORE_BANKED;
uint8_t cru_story_split(crucible_core *c, crucible_story *s, uint16_t id, uint16_t *ab) CORE_BANKED;
uint8_t cru_story_loss(crucible_core *c, crucible_story *s, uint16_t *lost, uint8_t max) CORE_BANKED;
uint8_t cru_story_gain(crucible_core *c, crucible_story *s, uint16_t id) CORE_BANKED;
uint8_t cru_story_flag(const crucible_story *s, uint8_t f) CORE_BANKED;
void cru_story_set_flag(crucible_story *s, uint8_t f) CORE_BANKED;
/* factions (cru_saga.c) */
int8_t cru_affinity(crucible_core *c, uint8_t faction,
                    uint16_t id) CORE_BANKED; /* how much a faction likes an element */
uint8_t cru_story_tier(const crucible_story *s, uint8_t faction) CORE_BANKED; /* CRU_TIER_* */
uint8_t cru_story_shift(crucible_story *s, uint8_t faction,
                        int8_t d) CORE_BANKED; /* standing += d (clamped); 1 if the tier changed */
uint8_t cru_faction_rival(uint8_t faction) CORE_BANKED;
/* An element made or kept: every faction notices (their standing drifts by affinity). Returns a bit per faction
 * whose tier changed, for a "THE GHOSTS NOTICE YOU" moment. */
uint8_t cru_story_notice(crucible_core *c, crucible_story *s, uint16_t id) CORE_BANKED;
/* A reply in a conversation with a faction's character: CRU_SAY_*; offer is the element given (dropped when they
 * accept it). Helping a faction cools its rival. Returns CRU_REACT_*. */
uint8_t cru_story_choose(crucible_core *c, crucible_story *s, uint8_t faction, uint8_t say, uint16_t offer) CORE_BANKED;

uint8_t cru_story_ending(const crucible_story *s) CORE_BANKED; /* CRU_END_* */
uint8_t cru_story_truth(const crucible_story *s, uint8_t k) CORE_BANKED; /* axis k's leading truth 0..7 */
uint8_t cru_story_runner_up(const crucible_story *s, uint8_t k) CORE_BANKED; /* the second, for a hedged ending */
void cru_story_act(crucible_story *s, uint8_t act) CORE_BANKED; /* CRU_ACT_*: an act leans the matrix */
void cru_story_lean(crucible_core *c, crucible_story *s,
                    uint16_t id) CORE_BANKED; /* an element made leans it (notice calls it) */
/* A clue shown about axis k: it counts for the truth leading now. Returns how many clues that truth has. */
uint8_t cru_story_clue(crucible_story *s, uint8_t k) CORE_BANKED;
uint8_t cru_story_knows(const crucible_story *s, uint8_t k) CORE_BANKED; /* CRU_KNOW_* */
/* Which truth the next clue should touch (the least known, by the seed), or 0xff when a clue would be too soon:
 * clues come slowly at first (chapter 0 none: you do not know you are stuck), faster the deeper and stranger it gets. */
uint8_t cru_story_clue_due(crucible_story *s) CORE_BANKED;
/* How the run stands: CRU_RUN_ON, a loss (you can lose early), or CRU_RUN_END after the last chapter. */
uint8_t cru_story_state(crucible_core *c, const crucible_story *s) CORE_BANKED;
/* How well it went (0..3), never shown until the ending: lucidity kept, what you made, no faction left hostile. */
uint8_t cru_story_grade(crucible_core *c, const crucible_story *s) CORE_BANKED;
/* The next chapter: the dream thins faster each time, grudges grow (hostile and wary factions cool further). Returns
 * cru_story_state. */
uint8_t cru_story_advance(crucible_core *c, crucible_story *s) CORE_BANKED;
/* Difficulty: chapter + 2 per cycle. Boss levels, cue length and puzzles follow it. */
uint8_t cru_story_pressure(const crucible_story *s) CORE_BANKED;
/* Past an ending, keep going: a new cycle, chapter 0, standing halved, less lucidity to start with. */
void cru_story_continue(crucible_story *s) CORE_BANKED;
/* A combination that made nothing: at least an even chance one of the two is destroyed (starters too), and every
 * miss in a row raises it (by the loss scale). Returns 1 and *lost when an element was destroyed. A real make
 * (cru_story_notice) breaks the chain. */
uint8_t cru_story_fail(crucible_core *c, crucible_story *s, uint16_t a, uint16_t b, uint16_t *lost) CORE_BANKED;
uint8_t cru_story_fail_odds(const crucible_story *s) CORE_BANKED; /* the chance of the next miss destroying, of 256 */
/* the dream: memory, drifting slots, glitches */
void cru_story_remember(crucible_story *s, uint16_t id) CORE_BANKED;
uint16_t cru_story_recall(const crucible_story *s, uint8_t back) CORE_BANKED; /* 0 newest; CRU_NONE */
void cru_story_lucid(crucible_story *s, int8_t d) CORE_BANKED; /* clamps 0..255 */
/* What a line says for id: itself while lucid; deeper in the dream, something related (what it is made of, what it
 * makes, a cousin in its category with a shared trait, or a memory): wrong, but wrong in a way that means something. */
uint16_t cru_dream_slot(crucible_core *c, crucible_story *s, uint16_t id) CORE_BANKED;
/* A glitch for the next beat of a scene: 0 none, 1 letters flicker, 2 a word swaps, 3 the scene tears. cut: a cutscene
 * (glitches more). */
uint8_t cru_dream_glitch(crucible_story *s, uint8_t cut) CORE_BANKED;
/* bosses */
uint8_t cru_counter(crucible_core *c, uint16_t attack, uint16_t answer) CORE_BANKED; /* how well answer beats attack */
void cru_boss_begin(crucible_core *c, crucible_story *s, crucible_boss *b, uint8_t faction, uint8_t level) CORE_BANKED;
/* The boss picks its next attack: two of its hand combined (or one of its with one of yours, stolen into the mix).
 * Sets next / next_a / next_b and cue (rounds of warning: fewer in later phases). Returns next. */
uint16_t cru_boss_plan(crucible_core *c, crucible_story *s, crucible_boss *b) CORE_BANKED;
/* Your answer to the telegraphed attack (an element you own; CRU_NONE when the cue ran out). Returns CRU_BOSS_*;
 * lost (may be NULL) receives what the attack cost you. */
uint8_t cru_boss_answer(crucible_core *c, crucible_story *s, crucible_boss *b, uint16_t answer,
                        uint16_t *lost) CORE_BANKED;

/* ---- encounters (cru_encounter.c) ------------------------------------------------------------------------ *
 * The encounter director's rules (docs/game-flow.md, "Encounters"): talks and fights are never menu items;
 * after every mix the run's signal decides whether someone comes, and how: forced (announced, then it comes by itself)
 * or waiting at the bench's edge (a talker above, UP; a fighter below, DOWN) until approached or faded. No screens, no
 * hardware, no save: the host draws the hint and the telegraph, plays the transition and opens its own talk or fight
 * (the two narrow hooks). Fights come only in a story run, never in free play; nothing comes while linked.
 * Rolls are seeded (the host stirs in its own entropy); all counts are in mixes. */
#define CRU_ENC_NONE 0u
#define CRU_ENC_TALK 1u /* someone to talk to: waits above (UP); arg = the visitor's faction */
#define CRU_ENC_FIGHT 2u /* a champion: waits below (DOWN); arg = faction | 0x80 for the nemesis */
#define CRU_ENC_TALK_FADED 3u /* cru_enc_tick: a waiting talker was ignored until it faded */
#define CRU_ENC_FIGHT_FADED 4u /* ... a waiting fighter (arg still names it) */
#define CRU_ENC_FREE 0u /* modes (cru_enc_start): free play, visitors only */
#define CRU_ENC_STORY 1u /* a story run: visitors, champions, rivals, the nemesis, chapter gatekeepers */
#define CRU_ENC_LINKED 2u /* two players on a cable: nothing comes */
#define CRU_ENC_NO_FIGHTS 0x80u /* or'd into a mode: a machine without a fight scene (every fight becomes a voice) */
#define CRU_ENC_SIG_NONE 0u /* a mix's story signal (the cartridge's STORY_*) */
#define CRU_ENC_SIG_LOSS 1u /* the machine's own lines, at once (returned unchanged) */
#define CRU_ENC_SIG_OVER 2u
#define CRU_ENC_SIG_VISIT 3u /* someone steps in */
#define CRU_ENC_SIG_BOSS 4u /* a hostile faction's champion comes */
#define CRU_ENC_LIFE 1200u /* frames a waiting hint stays on the bench (about twenty seconds) */
#define CRU_ENC_FADE 180u /* ...its last three seconds flicker */
#define CRU_ENC_TELEGRAPH 120u /* frames a forced one is announced before it comes by itself (two seconds) */
/* what the run looks like after a mix; cru_enc_view fills it from a story, a host without factions leaves it zeroed
 * with rival/hostile 0xff */
typedef struct crucible_enc_view {
  uint8_t chapter;
  uint8_t hostile; /* a faction at standing <= -50 (it sends champions), 0xff none */
  uint8_t rival; /* the coldest faction at standing <= -15, 0xff none */
  uint8_t coldest; /* the coldest faction (a gatekeeper when there is no rival) */
  uint8_t met; /* times the nemesis was met (0 never; 3: done with) */
  uint8_t nemesis; /* its faction */
  uint8_t lost; /* the mix was a lost piece that would not form (a rival "has it") */
} crucible_enc_view;
typedef struct crucible_encounter {
  uint8_t hint; /* waiting: CRU_ENC_TALK / CRU_ENC_FIGHT, or NONE */
  uint8_t force; /* announced, coming by itself: CRU_ENC_TALK / CRU_ENC_FIGHT, or NONE */
  uint8_t arg; /* who: the faction (| 0x80 the nemesis) */
  uint8_t mode; /* CRU_ENC_FREE / STORY / LINKED, | CRU_ENC_NO_FIGHTS */
  uint8_t since, fgap, tgap; /* mixes since the last encounter, fight, talk */
  uint8_t owe; /* a visit that could not come yet is kept */
  uint8_t beat; /* mixes until a new chapter's gatekeeper (0xff none) */
  uint8_t chapter; /* the chapter last seen (0xff not yet) */
  uint8_t visit; /* mixes since the last visit signal (cru_enc_signal) */
  uint8_t count; /* encounters begun (talks + fights) */
  uint16_t t; /* frames the hint or the announcement has shown */
  uint16_t seed;
} crucible_encounter;
/* A run begun or left, a cable in or out: nothing carries over. seed: the host's entropy. */
void cru_enc_start(crucible_encounter *e, uint8_t mode, uint16_t seed) CORE_BANKED;
/* The view of a story run (lost: the mix was a lost piece). */
void cru_enc_view(const crucible_story *s, uint8_t lost, crucible_enc_view *v) CORE_BANKED;
/* The run's signal for a host without the cartridge's story-run module: made = the mix formed something. Free play: a
 * visitor now and then (from 6 makes, 1 in 4); a story run: a hostile faction's champion (1 in 4 makes), else a visitor
 * (from 3 makes, more often the deeper the chapter). stir: the host's entropy. */
uint8_t cru_enc_signal(crucible_encounter *e, const crucible_enc_view *v, uint8_t made, uint16_t stir) CORE_BANKED;
/* After every mix: decides what comes and how. Returns LOSS / OVER unchanged (the host acts on them at once), else 0. */
uint8_t cru_enc_after_mix(crucible_encounter *e, const crucible_enc_view *v, uint8_t signal, uint16_t stir) CORE_BANKED;
/* A clock's reason for someone to wait above (back after hours, a special minute). 1: it waits. */
uint8_t cru_enc_wait(crucible_encounter *e) CORE_BANKED;
/* Every bench frame (dt frames since the last): approach = the player pressed the shown one's direction (UP for a
 * talk, DOWN for a fight). Returns CRU_ENC_TALK / FIGHT when it begins now, CRU_ENC_*_FADED when a waiting one was
 * ignored (a story host then applies cru_enc_ignored), else NONE. */
uint8_t cru_enc_tick(crucible_encounter *e, uint8_t dt, uint8_t approach) CORE_BANKED;
/* An ignored one, in a story run: a fighter's faction holds a grudge (standing -3); a voice ignored leans the dream to
 * standing still (CRU_ACT_IDLE). faded: cru_enc_tick's result. */
void cru_enc_ignored(crucible_story *s, uint8_t faded, uint8_t arg) CORE_BANKED;
/* What the bench shows: the kind showing (waiting or announced), 0 none; *flicker set while it blinks (an announcement,
 * a hint's last seconds). */
uint8_t cru_enc_shown(const crucible_encounter *e, uint8_t *flicker) CORE_BANKED;

#endif
