# core: CRUCIBLE's rules, once

One portable C99 library that holds CRUCIBLE's game rules. The Game Boy Color cartridge (GBDK-2020 / SDCC, SM83)
compiles it, and host builds (clang or gcc) compile the same sources, so a rule exists in one place.

The references it agrees with:

- `core/test/reference/crucible_play.c`, the save v4 play memory. The tests check that the core leaves the same
  SRAM bytes as this module after every mix;
- `docs/save-structures.md`.

## What the core owns

The core makes decisions and keeps state. It does no drawing, plays no sound, does no I/O of its own and knows
nothing about art or assets.

| Area        | What it decides                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| ----------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Catalogue   | Recipe lookup: a binary search over recipe keys sorted by (a,b), at most 13 probes for 8,192 recipes. Starters, depth, routes by result, all read from host-filled tables (depth is baked, never computed).                                                                                                                                                                                                                                                                                                    |
| Shelf       | Shelf order (category, then id). ◀▶ moves to the next or previous owned item that passes the filter, and wraps. ▲▼ moves to the first owned item of the previous or next group. The CL/CN neighbour rules (blank when equal to the focus, or CN equal to CL).                                                                                                                                                                                                                                                  |
| Filter      | Indices: 0 ALL, 1..7 categories, 8..19 traits. found/total for every tag in O(1), kept up to date as items are found. Apply is denied when nothing is found and saves on a change. Closing the screen refocuses the bench.                                                                                                                                                                                                                                                                                     |
| Mixing      | Outcome at merge start: NEW, ROUTE, KNOWN or NOTHING (or DENY). A NEW result's tint is rolled then. Scoring: NEW +10, ROUTE +3, KNOWN/NOTHING 0. A pair is "fresh" if it was never tried. The CR preview (`?`, `×` or the result). The sign's messages (`=RESULT` / `NO RESULT`). How the bench lands after each outcome.                                                                                                                                                                                      |
| Feats       | All 24 feats with their tiers, tier points (5/10/20/40), clamping to the catalogue, and the clocks (fastest pair, five finds, flurry, pairs per minute). Streak, chain, clean run, fails and book views are counted only for fresh pairs (as `feats_mix(..., fresh)`). Every event is O(1) in catalogue size. Newly reached tiers come out as toasts in a ring of 8.                                                                                                                                           |
| Titles      | 8 traits × 7 domains. A title's grade is min(trait feat tier, domain feat tier). Wear and take-off rules. The `NEEDS …` reason bits. Each newly earned title is announced.                                                                                                                                                                                                                                                                                                                                     |
| Records     | The 13 STATS rows (with a "not yet" flag). The leaderboard: one row per power-on run (points gained), 10-byte v3 rows plus name letters 4–8, 8 rows, sorted.                                                                                                                                                                                                                                                                                                                                                   |
| Book        | Rows in shelf order under the filter. The caption's recipe: the first tried route, ingredients in authored order, or ELEMENT / SHARED. USE / DENY. SCHOLAR views.                                                                                                                                                                                                                                                                                                                                              |
| Lost pieces | A piece that stops being owned (`cru_drop` / `cru_drop_any`: a gift, a theft, a fight, a miss, a split) cools down for N = 2..10 mixes that make something (deterministic from the tint seed, the id and a loss serial). Until then a pair that would make it opens as NOTHING with `mix.lost` set (nothing made, scored, tried or counted): the first attempt `CRU_LOST_FIRST`, later ones `CRU_LOST_AGAIN`, when the bench's result cell is `CRU_K_GLITCH`. Four at a time, newest first; a regain ends one. |
| Save v4     | Two 512-byte CRC-16 records. The write-ahead journal and torn-write recovery. The OWNED / RBITS / USES+SPILL areas and the two-generation dud Bloom filter (both SRAM layouts). Migration from v3, v2 and v1. Recounting after corruption or a catalogue update.                                                                                                                                                                                                                                               |

The host owns: input mapping, every animation and timeline, text layout, sound, art, the menu and settings
screens. The settings live in `c->options` and the name in `cru_set_name`; the host calls `cru_save` after
changing them.

## Files

```
include/crucible_core.h        the whole public API (types, constants, functions)
src/cru_internal.h             store layout, play-block offsets, CORE_LOCAL internal declarations
src/cru_tables.c               the ONLY reader of crucible_tables: accessors, binary search, filter rule, shelf step
src/cru_play.c                 play memory (port of crucible_play.c over the store) + tints (Pearson table)
src/cru_feats.c                the rule tables (cru_rules_world), derived counts, feats, clocks, toasts, titles
src/cru_save.c                 v4 record (table-driven writer/reader, CRC), power-on/migration, init, name
src/cru_records.c              leaderboard, STATS rows
src/cru_bench.c                bench, mixing and scoring, filter apply/close, book
src/cru_text.c                 names: feats, descriptions, trait tags, title rows/columns
src/cru_grant.c                cru_grant: owned from another device or an older save (journalled like a mix)
src/cru_reset.c                cru_reset: RESET GAME (a fresh save keeping options, name and sequence)
src/cru_lost.c                 lost pieces: the cooldown after a drop, its table in the record (bytes 246..263)
generated/crucible_tables_world.h   the bundled 57-id world (70 recipes) the tests run on; generated, do not edit
test/                          host tests (harness, sessions, save, agreement with test/reference/crucible_play.c)
test/sm83/                     the link ROM, and the scenarios run on host and on SM83 (scen_*.c, plat_*.c, scen_check.py)
generated/crucible_world_ids.h ids and catalogue facts as constants (CW_STEAM, CW_ROUTED...): no arrays
../tools/core-tests.sh         test | sm83 | sm83-test (no argument: all three)
```

## API summary

```c
typedef struct crucible_store {               /* a byte store: SRAM banks, or a file image */
  uint8_t (*read)(void *ctx, uint32_t at);    /* at = bank*0x2000 + offset (a cartridge .sav image) */
  void (*write)(void *ctx, uint32_t at, uint8_t v);
  void *ctx;
} crucible_store;

typedef struct crucible_tables {              /* const pointers the host fills */
  uint16_t items, recipes; uint8_t categories;
  const uint8_t *category; const uint16_t *traits; const uint8_t *starters;      /* starters: bit per id */
  const uint16_t *shelf, *shelf_pos;
  const uint16_t *recipe_a, *recipe_b, *recipe_r;   /* sorted by (a,b), a<=b */
  const uint16_t *recipe_bit;                        /* stable tried bit per row (NULL: the row) */
  const uint8_t *recipe_swap;                        /* authored (b,a): book order (may be NULL) */
  const uint8_t *depth; const uint16_t *route_first, *route_list;
  const crucible_rules *rules;                       /* &cru_rules_world */
} crucible_tables;

void     cru_init(crucible_core*, const crucible_tables*, const crucible_store*, uint8_t layout);
uint8_t  cru_load(crucible_core*, uint8_t entropy);          /* CRU_LOAD_V4 / RECOUNTED / V3 / V2 / V1 / FRESH */
uint8_t  cru_bench_move(crucible_core*, uint8_t dir);        /* ◀ ▶ ▲ ▼ */
uint8_t  cru_bench_a(crucible_core*, uint8_t entropy);       /* CRU_PICKED or the merge's outcome */
uint8_t  cru_bench_b(crucible_core*);
void     cru_bench_cells(crucible_core*, crucible_cells*);   /* CA CB CR CL CF CN + the sign */
uint8_t  cru_mix_begin(crucible_core*, uint16_t a, uint16_t b, uint8_t entropy);
uint8_t  cru_mix_finish(crucible_core*);                     /* commits, saves; returns points awarded */
uint8_t  cru_grant(crucible_core*, uint16_t id, uint8_t source); /* owned from another device or an older save: no mix
                                                                  * points or pace, feats consistent, saved like a mix */
void     cru_reset(crucible_core*, uint8_t entropy);         /* RESET GAME: a fresh save keeping options and name */
uint8_t  cru_lost_state(crucible_core*, uint16_t id);        /* CRU_LOST_NONE / FIRST / AGAIN: a lost piece cooling */
uint8_t  cru_lost_left(crucible_core*, uint16_t id);         /* mixes still to make before it can form again */
uint8_t  cru_lost_cue(crucible_core*, uint16_t a, uint16_t b); /* 1: the pair would make a warned lost piece */
void     cru_reveal_close(crucible_core*);
void     cru_tick(crucible_core*, uint8_t dt);               /* frames; minute ticks may reach a tier and save */
uint8_t  cru_toast_peek(const crucible_core*);  void cru_toast_done(crucible_core*);
uint8_t  cru_filter_apply(crucible_core*, uint8_t f);  void cru_filter_close(crucible_core*);
uint16_t cru_filter_found(const crucible_core*, uint8_t f), cru_filter_total(const crucible_core*, uint8_t f);
uint8_t  cru_title_grade / cru_title_needs / cru_title_wear / cru_titles_earned (...);
uint16_t cru_feat_value / cru_feat_threshold (...);  uint8_t cru_feat_tier / cru_feat_unit / cru_feat_lower (...);
void     cru_stat(crucible_core*, uint8_t row, crucible_stat*);   /* value, of, unit, known */
void     cru_board_row(const crucible_core*, uint8_t i, crucible_board_row*);
uint16_t cru_book_rows(...);  uint8_t cru_book_route(...), cru_book_use(...);  void cru_book_open/view(...);
void     cru_feat_name/desc, cru_tag_name, cru_title_part, cru_title_name (..., char *out);
```

All state lives in the caller's `crucible_core`: 549 bytes on SM83, 592 on a 64-bit host. The library has no
static RAM. The host can read the struct's fields freely (`focus`, `slot_a`, `message`, `points`, `filter`, `title`,
`found[]`…) but should change them only through the functions; `options` is the one field the host writes.

## Wiring

### Cartridge (GBDK)

1. **Banking.** The core is about 27 KB of SM83 code, which is more than one 16 KB bank, so it is written to be split
   across banks:
   - Build every core file with `-DCORE_BANKED=BANKED -DCORE_LOCAL=BANKED` and let the autobanker place them
     (`-Wf-bo255` / `#pragma bank 255`). Every entry point and every call between core files is then a banked call.
   - Each file's own const data (rule tables, Pearson bytes, CRC table, names) is read only by that file.
2. **Tables.** `cru_tables.c` is the only file that dereferences `crucible_tables`. It must sit in the same bank as
   the ROM tables. The way to guarantee that is one translation unit, as `test/sm83/rom_tables.c` shows: a file that
   `#include`s the generated tables and `cru_tables.c` and defines the `crucible_tables`. Every other file goes
   through `cru_tables.c`'s accessors. The game's catalogue is larger than one bank, so the cartridge uses far reads
   instead (`CRU_T8` / `CRU_T16` over the banked catalogue in `cartridge/data/catalogue`).
   - `CRU_T8` / `CRU_T16` wrap every table read. A catalogue whose tables outgrow one bank can redefine them as far
     reads.
   - The rules struct must be `cru_rules_world` (it lives beside the code that reads it).
3. **Store.** The `read` and `write` callbacks must be `NONBANKED` (bank 0), because banked code calls them through
   pointers. Each one maps `at` to `SWITCH_RAM(at >> 13)` and `0xA000 + (at & 0x1FFF)`. Leave RAM enabled while the
   core runs, or have the callbacks enable it.
   - The 32K layout uses bank 0 for everything.
   - v3 migration borrows bank 2 at 0x1E00 (264 bytes), once.
   - Banks 1 and 3 (art cache) are never touched.
4. **Strings.** Pass RAM buffers, never ROM literals from another bank: `cru_set_name` reads its argument, and the
   name functions write into `out`.
5. **Where the cartridge calls it** (`cartridge/src/crucible.c` and its screens):
   - power-on → `cru_load`
   - `begin_merge` → `cru_mix_begin` (or `cru_bench_a`)
   - `finish_merge` → `cru_mix_finish` (play staging, points, feats, the record, commit)
   - `back_to_bench` → `cru_reveal_close`
   - shelf moves → `cru_bench_move` / `cru_shelf_*`
   - `bench_cells` → `cru_bench_cells`
   - `filter_tick` → `cru_filter_apply` / `cru_filter_close`
   - the frame tick → `cru_tick`
   - `feats_toast`'s queue → `cru_toast_peek` / `cru_toast_done`
   - the records pages → `cru_feat_*`, `cru_title_*`, `cru_stat`, `cru_board_row`
   - `OWNED()` → `cru_owned`
   - points and counters → `core.points` etc.
6. `sh tools/core-tests.sh sm83` compiles every file this way, reports sizes, and links `core_link.gbc` (MBC5 + RAM +
   battery, CGB) to prove that the banked calls resolve.

### Host builds

- **Tables.** Fill the tables from the catalogue plus the rule tables:
  - sorted recipe keys with `recipe_bit` set to the recipe's append-only index;
  - routes by result in authored order;
  - BFS depth;
  - `rules = &cru_rules_world`.

  `generated/crucible_tables_world.h` shows every table filled for a small world.

- **Store.** Back it with a 128 KB file image: the same bytes as a cartridge `.sav`, so a cartridge save can be
  imported as is.
  - Use `CRU_LAYOUT_128K`: its dud memory holds 21,845 duds per generation, with ≤ 2% false "×".
  - The 32K layout works too, if the file should stay byte-compatible with a 32 KB cartridge.
- **Writes.** Write the bytes through to the file (or to a mapped file). The write-ahead journal only protects
  against power loss if each byte write reaches the medium in order. Batching is fine if the batch is flushed whole,
  in order, before `cru_mix_finish` returns.
- **Build flags.** `CORE_BANKED` and `CORE_LOCAL` stay empty, which gives one image and plain calls.

## Save v4 record (512 B, two alternating copies at 0x0500 / 0x0700)

| Bytes   | Content                                                                                                                                                                                           |
| ------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 0–1     | `C1 04`                                                                                                                                                                                           |
| 2–3     | sequence                                                                                                                                                                                          |
| 4–13    | points, title, filter, rng, made, flags, routes (as v3)                                                                                                                                           |
| 14–15   | catalogue size (u16)                                                                                                                                                                              |
| 16–47   | zero (v3's owned bitmap; OWNED is an area in v4)                                                                                                                                                  |
| 48–111  | tints of ids < 256 (ids above derive theirs from the seed at 244)                                                                                                                                 |
| 112–183 | the play block (LIVE): counters, dud-filter state, journal                                                                                                                                        |
| 184–191 | reserved for super-feat tiers                                                                                                                                                                     |
| 192–199 | player name (8)                                                                                                                                                                                   |
| 200–201 | newest find (u16)                                                                                                                                                                                 |
| 202–241 | leaderboard name letters 4–8 (8 × 5)                                                                                                                                                              |
| 242–243 | recipe count                                                                                                                                                                                      |
| 244–245 | tint seed                                                                                                                                                                                         |
| 246–263 | lost pieces still cooling (`c->lost`, cru_lost.c): version (1; 0 in a save from before them, read as empty), loss serial, 4 × {id u16, mixes left (0 = free), flags (bit 0 warned)}, newest first |
| 264–375 | zero                                                                                                                                                                                              |
| 376–509 | feats, stats, options, initials (the name's first 3 letters), session, leaderboard (v3 layout; 417 keeps the newest find as a byte)                                                               |
| 510–511 | CRC-16/CCITT (init FFFF, poly 1021)                                                                                                                                                               |

SRAM areas are as in `crucible_play.h`:

- OWNED 0x0900
- RBITS 0x0B00
- LIVE 0x0F00
- SPILL 0x0F80
- USES 0x1000
- dud generations: 0x0000 / 0x0200 (32K), or banks 4–7 / 8–11 (128K)

**A mix, step by step:**

1. Stage up to 10 area bytes and the dud key in the journal (LIVE).
2. Write the record.
3. Apply the journal, rotate the filter and insert the dud.

**Power-on:**

1. Take the newest valid record.
2. Replay its journal.
3. Finish any interrupted generation clear.
4. Verify the areas' byte sum. On a mismatch, or after a catalogue update, recount every exact counter and write a
   fresh record.

## Constraints and how they are checked

- **Integer only; no multiply, divide or modulo.**
  - Tables replace variable shifts (bit masks, trait bits).
  - `mod 3` folds digits (256 ≡ 16 ≡ 4 ≡ 1).
  - Category percentages use two decimal long-division steps in 16 bits: x·10 = (x≪3)+(x≪1).
  - `n(n+1)/2` is a running sum.
  - Title row and column come from subtraction.
  - `tools/core-tests.sh sm83` fails if any `mul`/`div`/`mod` helper call appears in the generated asm. None do: the only
    runtime calls are GBDK's banked-call (`___sdcc_bcall_ehl`) and pointer-call (`___sdcc_call_hl`) trampolines.
- **No 32-bit math in hot paths.** `uint32_t` appears only in the 128K dud-filter address, its generation clear, and
  the zero-extended store address.
- **Constant shifts only. No recursion. No malloc.**
- **No static RAM.** `tools/core-tests.sh sm83` checks that every object's DATA/BSS is 0.
- **Small stacks.** The largest frame any function reserves is 25 bytes.
- **const tables. CORE_BANKED on every entry point.**
- **SDCC-safe bit tests.** SDCC has miscompiled `(s->rows[t]>>(ry-1))&1` inside a ternary (it computed the address
  and never loaded the value). The core follows one rule everywhere:
  - load the byte or word into a plain local first;
  - take the mask from `bit_mask[8]` (a const table);
  - test with `if`/`else`;
  - never a variable shift, and never a ternary around a load through a pointer.
- **Proved on a real SM83.** `tools/core-tests.sh sm83-test` runs the same scenarios as a GBDK ROM in PyBoy and compares the
  results with the host build (see Tests).

## Tests

`sh tools/core-tests.sh test` builds with clang (or `CC=gcc`) and ASan/UBSan. The library itself is also built with
`-Werror -Wconversion`.

- **Sessions** over the real catalogue (`test_core.c`):
  - outcomes and scoring;
  - the bench flow and the CR preview;
  - shelf and group navigation and neighbours;
  - filter counts against brute force (before and after a reboot);
  - every feat family reaching tiers, title grades and needs over the whole matrix, wear and take-off;
  - the clocks;
  - a gilded roll;
  - a complete run (57/57, 70/70, every category IV, SPEEDRUN);
  - toast order and the toast cap;
  - the book (authored ingredient order);
  - the leaderboard over 13 power-ons with an 8-letter name and a worn title;
  - the STATS rows;
  - grants (`cru_grant`, `test_grant.c`): no mix points or pace, counts and feats consistent, not a find of this run,
    refused while a mix is open, a later mix of the pair is a ROUTE; a power cut at every write of a grant;
  - RESET GAME (`cru_reset`, `test_grant.c`): fresh but for the options, the name and the sequence, in both layouts;
  - lost pieces (`test_lost.c`): N in 2..10 and deterministic; the first attempt glitches without making anything; later
    ones are cued (`CRU_K_GLITCH`) and fail without counting; N other makes clear it; the table round-trips in both layouts
    and in a story slot; an old (zero), unknown-version or damaged table reads clean; a full table drops the oldest; a
    regain ends a cooldown; a power cut at every write of a drop; every story loss path (split, scale loss, gift, miss).
- **Save** (`test_save.c`):
  - a round trip, with the record layout pinned byte by byte;
  - a power cut after every single write of a NEW, ROUTE, KNOWN, fresh-dud and repeated-dud mix and of a grant, in
    both layouts, and of a 32K dud-generation rotation. Each boot must be consistent and equal to the state just before or just
    after the mix, with no flip back;
  - v3 migration (both layouts, a newest-of-two choice, v3 nibble CRC), and power cuts throughout it;
  - v2 and v1 migration;
  - a catalogue update;
  - a corrupted area;
  - a corrupted newest record.
- **Agreement** (`test_diff.c`, built for both layouts): 4,600 mixes (random explorers plus a 600-mix grinder past
  255 uses) and a v3 migration, run on the core and on `core/test/reference/crucible_play.c`. Every area byte must
  match after every mix.

- **SM83** (`tools/core-tests.sh sm83-test`). `test/sm83/scen_*.c` are 18 scenarios, built for the host and as a banked GBDK
  ROM (MBC5 with 128 KB RAM, and a store write counter that drops every write after N to simulate a power cut):
  - the sessions above;
  - a round trip with the record pinned;
  - sampled power cuts through a NEW, ROUTE, KNOWN, fresh-dud and repeat mix, plus every cut through the record's
    end and the commit;
  - a cut inside a 32K dud-generation rotation;
  - v3 migration in both layouts, with cuts inside it;
  - v2 and v1 migration;
  - a catalogue update, a corrupted area and a corrupted record;
  - RESET GAME;
  - the 128K layout, and a grant.

  PyBoy runs the ROM headless. Its check counts and its CRC-32 hashes of the core state and the store image, per
  scenario, must equal the host's byte for byte.

## Rule decisions

1. **Names to 8 letters.**
   - Leaderboard rows stay the 10-byte v3 format: initials = the first 3 letters, plus title.
   - The v4 record adds the player name (8) and, per row, letters 4–8.
2. **Recipe tried bits.** A sorted row index moves when a recipe is inserted, so `recipe_bit` gives each row a stable
   append-only bit (the authored index). NULL keeps crucible_play.c's "bit = row".
3. **Book order.** Sorting loses the authored ingredient order (FIRE + WATER). `recipe_swap` and authored-order
   `route_list` keep the caption exactly as the cartridge shows it.
4. **EXPLORER ("different pairs")** = recipes tried + duds (`CRU_PV_PAIRS`). With the dud filter, "fresh fails" and
   "new duds" count the same events, so this equals recipes + fails. On the 32K layout it can overcount when a
   forgotten dud is retried.
5. **Feat rules:**
   - A run's session number is stored only when that run saves (unsaved runs reuse the number).
   - The boot check can award catch-up tiers (e.g. after migration) and so put the run on the board.
   - The bench opens on the first owned shelf item without applying the saved filter.
6. **Counters and queues:**
   - The toast queue is a ring of 8.
   - points, made, routes and the session find counter saturate instead of wrapping.
   - The 60-second windows compare exact frame stamps.
   - A recount at load is written back.
   - Ids ≥ 256 derive their tint from a seed (save-structures.md's suggestion).
7. **Corruption after commit.** If the newest record is corrupted _after_ its commit (not torn by a power cut), the
   older record loads and the areas are recounted: the newer find stays owned, but its points are lost. This is
   inherent to the record and journal design. Torn writes are always exact.
8. **Super achievements** (FAVOURITE, VERSATILE, ROUTEFINDER, DEEP, MARATHON) are not implemented. Their counters
   exist (`cru_play_value`, `cru_play_uses`) and record bytes 184–191 are reserved.
