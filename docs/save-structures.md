# CRUCIBLE save structures: what to remember, and how small

Study for save v4: per-item try counts for super achievements, tried-pair memory, O(1) feats, recipe lookup, at
a target scale of **up to 4096 item ids (16-bit) and ~5,400 recipes**. The 57-item world and 256 ids are secondary
data points. Every number below was measured. Simulator numbers come from real encoders that were decoded and
checked. SM83 numbers are exact CPU clocks from the GBDK 4.5 builds running in PyBoy 2.7. The C module
(`core/test/reference/crucible_play.c`) and an independent reference implementation produce byte-identical saves.

The shipped catalogue (5,629 ids, 13,439 recipes) is past v4's limits, so the cartridge stores the same areas in the
WIDE placement and saves v5 (`core/include/crucible_core.h`, "Where the play memory sits"); the record, journal and
filter design below are unchanged by that.

Clocks: 70,224 per frame at normal speed and 140,448 in CGB double speed.

## Decision in one paragraph

Split the memory into **exact** parts and **approximate** parts. These parts are exact:

- the owned set (1 bit per id);
- the "recipe tried" set (1 bit per recipe, indexed by the recipe's row in the baked lookup table);
- per-item use counts (uint8 per id, plus a 32-entry spill table for items past 255);
- every achievement counter (kept incrementally in a 72-byte block inside the CRC record).

The only approximate part is the memory of **pairs that made nothing** ("duds", about 90% of all distinct pairs
at scale). It is a rotating two-generation Bloom filter, so its false-positive rate stays bounded. A false
positive can only ever show "makes nothing" on a pair that truly makes nothing, because recipe pairs are
answered exactly first. All area writes go through a write-ahead journal carried inside the CRC record, which
makes torn writes safe. The module uses **no static work RAM**. Recipe depth is baked at build time.

**Recommend 128 KB SRAM.** It lets the dud memory hold 21,845 to 43,690 recent duds at ≤2% false X. With
32 KB SRAM the same code works, but dud memory shrinks to the last 341–682 duds.

## How it was measured

- **Simulator** (part of a private benchmark toolchain, not in this repository): real catalogue (57 items, 70 recipes) and synthetic extensions to
  128/256/1024/2048/4096 ids with the same structure. The extensions keep 1 or 2 routes per item (the real
  38:16 split), 8.6% same+same recipes, loop recipes, and a depth that grows slowly (max 38 at 4096).
- **Four player models**:
  - random explorer;
  - greedy recipe-seeker (35% "insight" picks of untried recipe pairs, otherwise untried pairs built on the
    newest discovery);
  - completionist sweep;
  - Zipf-favourite grinder.
- **Runs**: 5 seeds up to 256 ids and 3 above, 50,000 mixes each, checkpoints at 500/2,000/10,000/50,000.
- **SM83** (a benchmark ROM from the same toolchain): C candidates compiled with lcc (`-Wm-yC`, MBC5+RAM+battery). Timing uses PyBoy
  hooks on begin/end markers reading the emulated clock counter. Interrupts are off and the 44-clock marker
  overhead is subtracted. Every case checks its answer against an expectation computed on the host.
- **Agreement**: the same command scripts run through the C module built on the host (both SRAM layouts) and the
  independent reference, which must be byte-identical. The SM83 build's SRAM after real histories must equal the
  reference image byte for byte (it does, all 4 builds).

## Scale facts that drive the design

|  ids | recipes | possible pairs | recipe share | owned after 10k mixes, median (max) | distinct pairs at 50k mixes, median (max) | of which duds |
| ---: | ------: | -------------: | -----------: | ----------------------------------: | ----------------------------------------: | ------------: |
|   57 |      70 |          1,653 |         4.2% |                             57 (57) |                             1,653 (1,653) |           96% |
|  256 |     333 |         32,896 |         1.0% |                           107 (256) |                           13,417 (32,896) |           98% |
| 4096 |   5,418 |      8,390,656 |       0.065% |                         121 (2,909) |                           16,123 (50,000) |         98.5% |

At 4096 ids, owned items depend heavily on the play style:

| model (4096 ids) | owned at 2k | at 10k | at 50k mixes |
| ---------------- | ----------: | -----: | -----------: |
| seeker           |         619 |  2,879 |        4,096 |
| explorer         |          62 |    108 |          194 |
| completionist    |          86 |    177 |          355 |
| grinder          |          33 |     62 |          125 |

Random play almost never hits a recipe at this scale (0.065%). Any memory proportional to possible pairs is out
of the question, and anything per tried pair grows with mixes, not with the catalogue.

## Tried pairs: exact encodings

Bytes, median (max) over all models and seeds:

| encoding                                       |   4096 ids @2k |            @10k |         @50k mixes |    57 ids @2k |  @50k |
| ---------------------------------------------- | -------------: | --------------: | -----------------: | ------------: | ----: |
| triangular bitmap by item id (v3 layout)       |      1,048,832 |       1,048,832 |          1,048,832 |           207 |   207 |
| triangle by discovery rank + rank table        | 8,461 (32,489) | 9,115 (537,267) | 10,858 (1,057,024) |     204 (264) |   264 |
| sorted list (3-byte keys)                      |  3,398 (6,002) | 13,370 (30,002) |   48,371 (150,002) | 1,724 (3,308) | 3,308 |
| Golomb-Rice gaps                               |  1,977 (3,454) |  6,552 (14,149) |    21,253 (57,348) |           210 |   210 |
| Elias-Fano                                     |  1,977 (3,490) |  6,622 (14,549) |    21,729 (58,196) |     317 (416) |   416 |
| split: recipe bits (exact) + duds Rice (exact) |  2,546 (3,921) |  7,061 (14,486) |    21,664 (57,613) |           219 |   219 |
| information floor log2 C(tri(O),k)             |    267 (2,009) |    893 (12,707) |     2,135 (55,181) |       1 (130) |     1 |

At 57 ids: rows-per-item (Roaring style) is 242 (289) B, and Rice over the rank triangle coding the smaller of
tried/untried is 61 (197) B.

SM83 cost per operation (clocks, net of the loop):

| candidate                                              |  57 ids |   256 ids |
| ------------------------------------------------------ | ------: | --------: |
| triangle test                                          |     261 |       256 |
| triangle test with the shift form `1<<(i&7)`, as in v3 |     338 |       330 |
| triangle mark                                          |     276 |       300 |
| rank triangle test                                     |     325 |       319 |
| sorted list: binary search                             |   5,526 |     6,162 |
| sorted list: insert                                    |  21,226 |    49,308 |
| Rice: query                                            | 242,077 | 1,092,324 |
| Rice: insert (re-encode)                               | 598,740 | 2,876,333 |
| popcount of the whole triangle                         |  82,244 | 1,628,880 |

Verdict:

- Exact bitmaps are O(1) but need memory proportional to pairs (1 MB at 4096 ids).
- Exact compressed sets (Rice, Elias-Fano) need 20–58 KB at 50k mixes and are O(k) per query: 1.1M clocks, 16
  frames per cursor move at 1,000 entries, and growing.
- Inserts into a compressed stream shift the whole stream. That is not idempotent, so it can't be made
  torn-write safe without double buffering.
- Exact dud memory at scale is therefore impractical. Only the recipe part (1 bit per recipe, 678 B at 5,418
  recipes) needs to be exact. The split design follows from that.

## Split design: what a dud filter costs the player

Each cell reads: false-X rate on untried no-result pairs / recipe-leak factor / tried duds forgotten / distinct
pairs never counted.

- **False X** is truthful ("makes nothing") but spoils an untried pair.
- **Recipe leak factor**: how much likelier an untried pair without an X is to be a recipe than with exact
  memory. A saturated filter turns "no X" into a recipe detector.
- **Forgotten**: a tried dud that lost its X, so the player may waste a retry.

All filters use Pearson-chain hashes (no multiply) and run the module's exact semantics (re-insert on retry).

| dud memory, 4096 ids (median)                 | 2k mixes             | 10k mixes             | 50k mixes              | worst case at 50k |
| --------------------------------------------- | -------------------- | --------------------- | ---------------------- | ----------------- |
| Bloom 2 KB, h3                                | 0.4% / 1.00 / 0% / 1 | 17% / 1.20 / 0% / 217 | 85% / 6.4 / 0% / 6,758 | 100% / >99 / 0%   |
| Bloom 8 KB, h3                                | 0%                   | 0.7% / 1.01 / 0% / 9  | 15% / 1.17 / 0% / 709  | 73% / 3.65 / 0%   |
| Bloom 32 KB, h3                               | 0%                   | 0%                    | 0.8% / 1.01 / 0% / 20  | 8% / 1.08 / 0%    |
| Bloom 64 KB, h4                               | 0%                   | 0%                    | 0.0% / 1.00 / 0% / 2   | 1% / 1.01 / 0%    |
| **rotating 2×512 B, h3** (32 KB-SRAM layout)  | 1% / 1.01 / 52% / 11 | 1% / 1.01 / 89% / 61  | 1% / 1.01 / 96% / 214  | 2% / 1.03 / 99%   |
| rotating 2×4 KB, h3                           | 0.1% / 1.00 / 0% / 0 | 1% / 1.01 / 34% / 32  | 1% / 1.01 / 81% / 184  | 3% / 1.03 / 93%   |
| **rotating 2×32 KB, h3** (128 KB-SRAM layout) | 0%                   | 0%                    | 0.8% / 1.01 / 0% / 20  | 2% / 1.02 / 51%   |
| cuckoo 32 KB, 8-bit fingerprints              | 0.1% / 1.00 / 0% / 1 | 0.5% / 1.00 / 0% / 12 | 2% / 1.02 / 0% / 144   | 4% / 1.04 / 32%   |

- A plain Bloom filter is excellent until its design capacity, then saturates. Below 8 KB at 4096 ids it
  reaches 100% false X, which leaks every recipe.
- The rotating filter keeps false X at ≤3% forever by forgetting the oldest duds.
- Cuckoo has a fixed ~4% floor (8-bit fingerprints) and forgets _new_ keys when full, which is worse than
  forgetting old ones.
- The approximate "distinct duds" counter errs both ways:
  - rotating 64 KB: −1.0%..0%;
  - rotating 1 KB: −1.4%..+239%, because explorers re-try forgotten duds and they get counted again.
- So no achievement should depend on it (see below).

Validation: on random keys the filters match Bloom theory (2 KB h3 at 1,000 keys: 0.45% measured vs 0.47%; 64 KB
h4 at 50,000 keys: 1.08% vs 1.01%). Clustered pair keys run up to 1.65× theory at 8 KB. That residual is
included above because the study hashes real keys.

## Per-item use counts ("tries of each item")

A mix counts once for each distinct item in it.

| counter                               | bytes at 4096 ids |                                     exact to |                                                          FAVOURITE tier wrong (1024–4096 ids) | mastery tier agreement |              SM83 clocks per increment |
| ------------------------------------- | ----------------: | -------------------------------------------: | --------------------------------------------------------------------------------------------: | ---------------------: | -------------------------------------: |
| uint8                                 |             4,096 |                                          255 |                                                                33/144 (1000-tier unreachable) |                   100% |                                    482 |
| **uint8 + 32-entry spill (chosen)**   |             4,224 | 255; beyond 255 exact for the first 32 items |                                                               **0/144** (0/240 at 57–256 ids) |                   100% | ≈ uint8, plus a 32-entry scan past 255 |
| 10-bit packed, 4 per 5 bytes          |             5,120 |                                        1,023 |                                                                                         0/144 |                   100% |                                  1,368 |
| 12-bit packed                         |             6,144 |                                        4,095 |                                                                                         0/144 |                   100% |                                  1,333 |
| uint16                                |             8,192 |                                       65,535 |                                                                                         0/144 |                   100% |                                    560 |
| exact to 127, then 1/8 sampled (HLS8) |             4,096 |                                          127 | 5/144; 1000-tier unlock off by 7,452 mixes on average (a tier never reached counts as 50,000) |                    84% |                                    976 |
| 2-bit tier + 6-bit progress           |             4,096 |                                           50 |                                                                                         6/144 |                    76% |                                      – |
| Morris 4-bit, base 2                  |             2,048 |                                            1 |                                                             89/144; VERSATILE wrong 122 times |                    36% |                                  2,685 |
| nibble + Space-Saving top-32          |             2,176 |                                           15 |                                                                                        36/144 |                     0% |                                      – |

The highest true per-item count reached 884 (median) and 15,177 (max) at 4096 ids after 50k mixes. A 1000-use
tier is realistic.

- Probabilistic counters (Morris, sampled) save at most half the bytes and get tiers wrong.
- Any exact counter to T needs ceil(log2 T) bits. "Tier + progress" in 8 bits is strictly weaker than a uint8.
- The spill table gives exactness past 255 where it matters (favourites) for 128 bytes. At 32 of 144 large
  checkpoints more than 32 items had passed 255. Those extra items stay at 255, and FAVOURITE was still right
  in every checkpoint.
- If guaranteed exactness to 1023 for every item is preferred, 10-bit packing is a drop-in alternative.
  It costs +896 B and 2.8× the clocks.

## Derived stats and O(1) feats

In v3, `check()` runs on every mix and every minute. It calls:

- `feats_pairs()`: 1,653 banked `save_tried` calls, **1,511,256 clocks**;
- `recipes()` twice: 123,160 each;
- plus mirror and category scans.

That is ≥1.76M clocks per check (**25 frames** at normal speed) at only 57 items, and it is what blocks the
screen. In v4 every value is maintained incrementally in the record's play block and read in **455 clocks**:

| counter (play block)   | updated when                                                                    | exact               |
| ---------------------- | ------------------------------------------------------------------------------- | ------------------- |
| owned                  | a mix makes an unowned result                                                   | yes                 |
| recipes tried, mirrors | a recipe pair's bit is first set                                                | yes                 |
| routes complete        | that set completes every recipe of its result (scan of the result's 1–3 routes) | yes                 |
| used ≥10, top uses     | an item's count reaches 10 / exceeds the maximum                                | yes                 |
| deepest                | a new item's baked depth exceeds it                                             | yes                 |
| distinct duds          | the filter did not know the key                                                 | approximate (above) |

Consistency on load: the counters are **persisted** in the CRC record and verified, not recomputed every boot.
After the journal replay, `play_load` sums the exact areas (5,760 bytes) and compares with the record's 16-bit
sum.

- Normal boot costs **285,908 clocks** (4.1 frames) plus the game's two record CRCs.
- On mismatch, every exact counter is recounted from the areas: 853,432 clocks at 57 ids and 2,263,340 at 256.
  Linear extrapolation gives about 32M clocks (≈7.6 s) at 4096 ids, and this path runs only after detected
  corruption.

Per-item partner coverage ("tried with every partner") needs exact dud memory, so it is offered only on small
catalogues, where the exact rank triangle is affordable (264 B at 57 ids).

## Recipe lookup in ROM (5,418 recipes, 4,096 ids)

Clocks per lookup, banked ROM including bank switching:

| layout                                                     |                                            ROM bytes |  miss: mean (min/max) |  hit: mean (min/max) |
| ---------------------------------------------------------- | ---------------------------------------------------: | --------------------: | -------------------: |
| **A: sorted 24-bit keys `(min<<12)\|max` + binary search** |                                               27,090 |   6,724 (6,068/7,392) |  7,158 (6,412/7,944) |
| B: CSR row of the smaller id                               |                                               24,448 |    2,587 (784/11,116) | 2,661 (1,704/14,604) |
| C: Pearson-hashed buckets (2,048)                          | 42,024 (31 KB if results are stored in bucket order) |   2,186 (1,436/3,676) |  2,472 (2,080/3,780) |
| D: v3's linear scan                                        |                                               27,090 | 1,432,167 (20 frames) |              684,957 |

- Recommend **A**. It has bounded time (13 probes, ≤8k clocks, 0.11 frame per preview) and the simplest bake.
  Its sorted index is also the recipe-tried bit and the order of the routes table.
- C is 3× faster if lookups are ever done in bulk.
- The linear scan (v3's `crucible_recipe`) is unusable at scale.

ROM cost with routes-by-result (19 KB) and depth (4 KB) is about 50 KB, roughly 12 B per object. In an 8 MB
MBC5 ROM that leaves ~2 KB per object for art, so lookup tables do not constrain the art budget.

## Recipe depth: recursion vs iteration

| algorithm                                     | 57 ids: SM83 clocks / passes / stack | 256 ids: clocks / passes / stack | 4096 ids (host)                     |
| --------------------------------------------- | ------------------------------------ | -------------------------------- | ----------------------------------- |
| relaxation sweeps                             | 136,292 / 3 / 6 B                    | 600,440 / 3 / 6 B                | 3 passes, 16,254 recipe visits      |
| worklist (FIFO by depth)                      | 177,524 / 1 / 13 B                   | 765,732 / 1 / 13 B               | 10,414 visits; needs an input index |
| BFS by levels                                 | 402,188 / 8 / 11 B                   | 3,784,688 / 17 / 11 B            | 39 passes                           |
| memoised recursion, repeated to a fixed point | 386,428 / 2 / 74 B                   | 3,183,236 / 4 / **410 B**        | 7 passes, **105 levels deep**       |

- Recursion needs repeated passes because the graph has cycles (ice + fire → water).
- Its stack grows about 14.6 B per level, roughly 1.5 KB at 4096 ids. The v3 build had about 900 B
  between heap and stack (0xDB75..0xDF00), so it would overflow.
- Relaxation is the fastest on-device option. Its pass count depends on recipe order: 3 in bake order, 39
  reversed.
- **Bake depth at build time** (1 byte per id in ROM, 0 clocks). Use relaxation only if downloaded recipes ever
  need a refresh.

## Owned set and variants at 4096 ids

- **Owned**: bitmap 512 B, versus 1.5 B per owned id as a list (309 B median, 6,144 B max). Choose the bitmap
  (fixed size, O(1)).
- **Variants** (2 bits per id) would be 1 KB at 4096. Keep the 64-byte legacy table for ids < 256 and derive
  newer ids' tint from a per-cartridge seed with `coin16(seed, id)`. That costs 0 B with the same 3-tint plus
  1/16-gilded distribution. The reference module leaves this out; the core implements it (`cru_variant`, with the
  tint seed at record bytes 244–245).

## Proposed save v4

### Record (512 B, two alternating copies with CRC-16, as v3)

The record keeps the v3 field layout except:

| bytes       | v4 content                                                                                                                                                                                                                                    |
| ----------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 14–15       | catalogue size, u16                                                                                                                                                                                                                           |
| 16–47       | reserved. Owned moves to an area; WRAM `save.owned` (32 B) can go.                                                                                                                                                                            |
| 48–111      | variants for ids < 256 (unchanged)                                                                                                                                                                                                            |
| **112–183** | **play block** (72 B, layout in `crucible_play.h`): owned, recipes, routes, used≥10, top uses, duds, mirrors, deepest, dud-filter generation, count and clearing flag, area sum, then the journal: 12 × {addr, value} plus a pending dud pair |
| 184–191     | super-achievement tiers (8 feats)                                                                                                                                                                                                             |
| 192–375     | reserved, written as zero                                                                                                                                                                                                                     |
| 376–493     | feats, stats, options, initials, session, leaderboard (unchanged)                                                                                                                                                                             |
| 510–511     | CRC                                                                                                                                                                                                                                           |

Use the 256-entry byte-table CRC: **172,280** clocks per record, against 403,664 for the v3 nibble table. It
computes the same CRC-16/CCITT, so v3 records still verify; the table costs 512 B of ROM.

### SRAM bank 0

| offset          |   bytes | 32 KB SRAM                                                            | 128 KB SRAM                                   |
| --------------- | ------: | --------------------------------------------------------------------- | --------------------------------------------- |
| 0x0000          |   1,024 | dud generations 0 and 1 (2 × 512 B); overwrites v1–v3 after migration | v1–v3 records kept untouched (downgrade-safe) |
| 0x0400          |     256 | free                                                                  | free                                          |
| 0x0500 / 0x0700 | 2 × 512 | v4 records A / B                                                      | same                                          |
| 0x0900          |     512 | OWNED (4,096 bits)                                                    | same                                          |
| 0x0B00          |   1,024 | RBITS (8,192 recipes)                                                 | same                                          |
| 0x0F00          |     128 | LIVE (working copy of the play block)                                 | same                                          |
| 0x0F80          |     128 | SPILL (32 × {id, extra})                                              | same                                          |
| 0x1000          |   4,096 | USES (uint8 per id)                                                   | same                                          |

With 128 KB SRAM the dud filter lives in banks 4–7 (generation 0) and 8–11 (generation 1). Banks 12–15 stay
free. Banks 1 and 3 (art cache) and bank 2 (fusion scratch) are untouched; migration borrows 264 B of bank 2
once. A 32 KB cart uses bank 0 completely (256 B spare).

### Torn-write protocol (proved by tests)

1. `play_mix(a,b,recipe,result)` changes no area. It stages at most 10 absolute byte writes, the dud pair and
   the counter updates in LIVE. Reads see staged bytes, and the area sum is updated as it stages.
2. The game writes the CRC record, which contains LIVE (the journal and new counters).
3. `play_commit()` applies the journal, clears a rotated filter generation if due, and inserts the dud.
4. `play_load()` copies the newest valid record's block and replays it. Absolute writes are idempotent, and an
   interrupted clear is redone. It then verifies the area sum.

Power can fail at any point:

- before the record: the mix is lost;
- mid-record: the CRC fails, so the previous record is used and the mix is lost;
- after the record, or mid-commit: replay completes the mix.

The reference tests injected all four failure points more than 300 times per scenario. Boot always reported
consistent areas and a state equal to just-before or just-after the mix, and the two implementations agreed byte for byte.

### Measured cost of the module on the SM83

|                                                |                                                            57 ids |                                                         256 ids |
| ---------------------------------------------- | ----------------------------------------------------------------: | --------------------------------------------------------------: |
| `play_mix`, mean (max) clocks                  |                                                   12,730 (23,580) |                                                 15,491 (23,704) |
| `play_commit`, mean (max) clocks               |                                                    5,561 (14,572) |                                                  6,913 (14,436) |
| `play_tried`, `play_uses`, `play_value` clocks |                                                ≈4,700 / 732 / 455 |                                              ≈4,200 / 732 / 455 |
| rotation clear                                 |                                        6,888 per 512 B generation | 396,376 per 32 KB generation, once per 21,845 duds (5.6 frames) |
| code size                                      | 4,528 B (incl. 256-B Pearson table); 4,580 B in the 128 KB layout |                                                                 |
| static WRAM                                    |                                           0 (no `_DATA` / `_BSS`) |                                                                 |
| peak stack                                     |                        45 B (`play_mix` + commit); 35 B (recount) |                                                                 |

For comparison, v3's record write (tried-set copy from bank 2 plus the nibble CRC over 510 bytes) costs 595,128 clocks per save.

### Migration from v3

1. `play_migrate_v3` copies ids < 256 owned from the v3 record. Tried pairs (ids < 64) become recipe bits.
   Use counts start at the exact lower bound "one mix per distinct pair".
2. The game writes the first v4 record.
3. `play_finish_migration` copies the v3 tried bitmap to bank-2 scratch, initialises the filter and inserts
   v3's dud pairs.

Proved by the reference tests, including agreement between the two implementations. If power fails between steps 2 and 3, the filter starts empty: v3's
dud X marks are lost, nothing else.

## Super achievements this supports (all exact)

| feat        | measure                                  | tiers (clamped to the catalogue, as in v3)          |
| ----------- | ---------------------------------------- | --------------------------------------------------- |
| FAVOURITE   | one item's use count                     | 25 / 100 / 250 / 1000                               |
| VERSATILE   | items used in ≥10 mixes                  | 10 / 50 / 250 / 1000                                |
| ROUTEFINDER | items made by every one of their recipes | 5 / 25 / 100 / 500                                  |
| DEEP        | deepest discovery (baked generation)     | 4 / 8 / 16 / max (7 in the 57-id world, 38 at 4096) |
| MARATHON    | `save.made` (u16)                        | 1,000 / 5,000 / 20,000 / 50,000                     |
| COLLECTOR+  | owned                                    | 100 / 500 / 2,000 / all                             |

- Keep RECIPES and MIRROR on the exact counters.
- Base EXPLORER ("different pairs") on recipes tried + `save.fails`, not on the dud counter, unless the
  128 KB layout is used (−1%..0% error).
- "Tried with every partner" stays a small-catalogue feat only.

## Integration notes

- `feats_pairs()` / `recipes()` become `play_value(PLAY_V_PAIRS / PLAY_V_RECIPES / PLAY_V_MIRRORS)`.
- `save_mark_tried()` becomes `play_mix()` before `save_store()`, and `play_commit()` after it.
- The preview uses `play_tried(a, b, recipe_index)`.
- The table generator must emit sorted recipe keys/results, routes by result, a depth table, and the four accessors
  listed in `crucible_play.h`.
- At 4096 ids, `OWNED()` must read the SRAM bitmap. Shelf and book scans should walk bitmap bytes with RAM
  enabled once, not call a banked accessor per id (about 600k clocks per step otherwise).
- Like v3's `save_tried`, the module leaves RAM disabled with bank 0 selected. Callers working in bank 2
  re-select it.
- In a banked build, keep `_HOME` (the BANKED trampolines) in bank 0. A benchmark ROM hit exactly this crash
  when non-banked code exceeded 16 KB.

## Honest caveats

- The player models are plausible, not measured from real players. Owned-set size at scale depends almost
  entirely on how much the game guides players: the seeker owns 4,096 by 50k mixes, random play only ~200.
  The design's worst cases do not depend on the model; the dud-filter quality does.
- The 128–4096 catalogues are synthetic, and their depth growth is a modelling choice.
- On the 32 KB layout the dud memory is a short "recent" memory (the last 341–682 duds). The X mark will
  disappear on older failed pairs.
- The spill table is exact only for the first 32 items to pass 255 uses. Measured FAVOURITE tiers were never
  wrong, but this is not a guarantee.
- The full-recount boot path at 4096 ids is extrapolated (≈32M clocks), not measured. The SM83 module runs were
  57- and 256-id histories (ROM space); 4096-id histories were checked on the host build against the independent reference.
- SM83 timings come from PyBoy's cycle counter. Bank switches and SRAM access are cycle-exact there, but
  PyBoy's model of real MBC5 timing was not separately verified.
- The SDCC code for byte loops is slow (128–380 clocks per byte). One loop (the boot sum) is hand-written
  assembly. Other loops could be tuned further if boot or recount time matters.

## Files and how to rerun

- The simulator, the SM83 candidate ROM and the independent reference belong to a private benchmark toolchain and
  are not in this repository; their results are recorded above.
- `core/test/reference/`: `crucible_play.{h,c}` (the reference module) and `harness/` (its catalogue accessors and
  minimal record writer).
- `core/test/test_diff.c` runs the same histories through the core and the reference module and checks every area
  byte in both SRAM layouts: `sh tools/core-tests.sh test`.
