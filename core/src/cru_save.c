/* Save v4: two alternating 512-byte records with a sequence number and CRC-16/CCITT (init 0xFFFF, poly 0x1021: the
 * v3 checksum, computed with a byte table), at 0x0500 / 0x0700. A torn record fails its CRC and the other one is
 * used. The record keeps v3's field offsets and adds:
 *    14-15 catalogue size (u16)       48-111 tints of ids < 256        112-183 the play block (LIVE, journal)
 *   184-191 reserved (super-feat tiers)  192-199 player name (8)     200-201 newest find (u16 id)
 *   202-241 leaderboard name letters 4..8 (8 x 5)   242-243 recipe count   244-245 tint seed for ids >= 256
 *   246-263 lost pieces still cooling (c->lost, cru_lost.c; zero in a save from before them)   264-375 zero
 *   376-509 feats, stats, options, initials, session, leaderboard (v3 layout)   510-511 CRC
 * Power-on prefers the newest valid v4 record; otherwise it migrates v3 (0x0100/0x0300), v2 (64 B at 0x80/0xC0)
 * or v1 (16 B at 0x00/0x20), or starts fresh. */
#include <stddef.h>
#include "cru_internal.h"

CRI_BITS;
CRI_STORE

static const uint16_t crc_table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7, 0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad,
    0xe1ce, 0xf1ef, 0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6, 0x9339, 0x8318, 0xb37b, 0xa35a,
    0xd3bd, 0xc39c, 0xf3ff, 0xe3de, 0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485, 0xa56a, 0xb54b,
    0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d, 0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
    0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc, 0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861,
    0x2802, 0x3823, 0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b, 0x5af5, 0x4ad4, 0x7ab7, 0x6a96,
    0x1a71, 0x0a50, 0x3a33, 0x2a12, 0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a, 0x6ca6, 0x7c87,
    0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41, 0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
    0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70, 0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a,
    0x9f59, 0x8f78, 0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f, 0x1080, 0x00a1, 0x30c2, 0x20e3,
    0x5004, 0x4025, 0x7046, 0x6067, 0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e, 0x02b1, 0x1290,
    0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256, 0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
    0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405, 0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e,
    0xc71d, 0xd73c, 0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634, 0xd94c, 0xc96d, 0xf90e, 0xe92f,
    0x99c8, 0x89e9, 0xb98a, 0xa9ab, 0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3, 0xcb7d, 0xdb5c,
    0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a, 0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
    0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9, 0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83,
    0x1ce0, 0x0cc1, 0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8, 0x6e17, 0x7e36, 0x4e55, 0x5e74,
    0x2e93, 0x3eb2, 0x0ed1, 0x1ef0};
static uint16_t crc_step(uint16_t crc, uint8_t b) {
  return (uint16_t)((crc << 8) ^ crc_table[(uint8_t)((crc >> 8) ^ b)]);
}

/* ---- the record, as a field table (one loop writes it, one reads it) ----
 * kind: a byte count (1..59), or U16 (a little-endian 16-bit field), V4 (absent from v3), or a special field */
#define U16 0x80u
#define V4 0x40u
#define K_LIVE 0x3fu /* the play block, copied from LIVE */
#define K_NEW8 0x3eu /* the newest find as a byte (v3's field; 0xFF none) */
#define K_ITEMS 0x3du /* catalogue size */
#define K_RECIPES 0x3cu /* recipe count */
#define K_CARD 0x3bu /* the host's player card (c->card, CRU_CARD_BYTES; zeros without one) */
/* CRU_NO_CARD: a host without a player card (the C64, which has no byte to spare) leaves the field out: record bytes
 * 264..345 stay as the gap they were before the card (zeros, as with no card) */
#ifdef CRU_NO_CARD
#define FIELDS 32u
#define CARD_(x)
#else
#define FIELDS 33u
#define CARD_(x) x,
#endif
#define OFF(f) ((uint16_t)offsetof(crucible_core, f))
static const uint16_t rec_at[FIELDS] = {2,
                                        4,
                                        6,
                                        7,
                                        8,
                                        10,
                                        12,
                                        13,
                                        14,
                                        48,
                                        80,
                                        112,
                                        192,
                                        200,
                                        202,
                                        242,
                                        244,
                                        CRU_LOST_REC_AT,
                                        CARD_(CRU_CARD_AT) 376,
                                        400,
                                        402,
                                        403,
                                        405,
                                        407,
                                        409,
                                        411,
                                        413,
                                        417,
                                        418,
                                        423,
                                        424,
                                        427};
static const uint16_t rec_off[FIELDS] = {OFF(sequence),
                                         OFF(points),
                                         OFF(title),
                                         OFF(filter),
                                         OFF(rng),
                                         OFF(made),
                                         OFF(flags),
                                         OFF(routes),
                                         0,
                                         OFF(variants),
                                         OFF(variants) + 32u,
                                         0,
                                         OFF(name),
                                         OFF(last_new),
                                         OFF(tails),
                                         0,
                                         OFF(variant_seed),
                                         OFF(lost),
                                         CARD_(0) OFF(feats),
                                         OFF(minutes),
                                         OFF(seconds),
                                         OFF(fails),
                                         OFF(book_views),
                                         OFF(complete),
                                         OFF(best_gap),
                                         OFF(best_first),
                                         OFF(streak),
                                         0,
                                         OFF(best_flurry),
                                         OFF(options),
                                         OFF(name),
                                         OFF(session)};
static const uint8_t rec_kind[FIELDS] = {U16,
                                         U16,
                                         1,
                                         1,
                                         U16,
                                         U16,
                                         1,
                                         1,
                                         V4 | K_ITEMS,
                                         32,
                                         32,
                                         V4 | K_LIVE,
                                         V4 | CRU_NAME,
                                         V4 | U16,
                                         V4 | (CRU_BOARD_ROWS * 5u),
                                         V4 | K_RECIPES,
                                         V4 | U16,
                                         V4 | CRU_LOST_BYTES,
                                         CARD_(V4 | K_CARD) CRU_FEATS,
                                         U16,
                                         1,
                                         U16,
                                         U16,
                                         U16,
                                         U16,
                                         U16,
                                         4,
                                         K_NEW8,
                                         5,
                                         1,
                                         3,
                                         U16};
#define BOARD_AT 429u /* then the leaderboard, 80 bytes, to 509 */

typedef struct {
  uint16_t at, crc;
} writer;
static void put(crucible_core *c, writer *w, uint8_t b) {
  cri_wr(c, w->at, b);
  w->at++;
  w->crc = crc_step(w->crc, b);
}

/* Writes the next record (sequence + 1, the other slot). */
void cri_write(crucible_core *c) CORE_LOCAL {
  writer w;
  uint16_t base, v = 0;
  uint8_t i, k, n, *p;
  c->sequence++;
  base = P_V4_A;
  if (c->sequence & 1u) base = P_V4_B;
  w.at = base;
  w.crc = 0xffffu;
  put(c, &w, 0xc1u);
  if (c->place == CRU_PLACE_WIDE)
    put(c, &w, 5u); /* v5: the WIDE placement */
  else
    put(c, &w, 4u);
  for (i = 0; i < FIELDS; i++) {
    while ((uint16_t)(w.at - base) < rec_at[i]) put(c, &w, 0);
    k = rec_kind[i];
    p = (uint8_t *)c + rec_off[i];
    switch (k & (uint8_t)~V4) {
    case K_LIVE:
      for (n = 0; n < P_BLOCK; n++) put(c, &w, cri_rd(c, (uint16_t)(P_LIVE + n)));
      continue;
    case K_NEW8:
      v = c->last_new;
      if (v > 255u) v = 255u;
      put(c, &w, (uint8_t)v);
      continue;
#ifndef CRU_NO_CARD
    case K_CARD:
      for (n = 0; n < CRU_CARD_BYTES; n++) put(c, &w, c->card ? c->card[n] : 0);
      continue;
#endif
    case K_ITEMS: v = c->items; break;
    case K_RECIPES: v = c->recipes; break;
    case U16: v = *(const uint16_t *)p; break;
    default:
      for (n = (uint8_t)(k & 0x3fu); n; n--) put(c, &w, *p++);
      continue;
    }
    put(c, &w, (uint8_t)v);
    put(c, &w, (uint8_t)(v >> 8));
  }
  for (p = c->board; p < c->board + CRU_BOARD_ROWS * CRU_BOARD_ROW; p++) put(c, &w, *p);
  put(c, &w, 0); /* 509 */
  cri_wr(c, w.at, (uint8_t)w.crc);
  cri_wr(c, (uint16_t)(w.at + 1u), (uint8_t)(w.crc >> 8));
}
void cri_save(crucible_core *c) CORE_LOCAL {
  cri_board_update(c);
  cri_write(c);
}
void cru_save(crucible_core *c) CORE_BANKED { cri_save(c); }

/* ---- readers ---- */
static uint8_t g8(crucible_core *c, uint16_t rec, uint16_t o) {
  uint8_t v = cri_rd(c, (uint16_t)(rec + o));
  return v;
}
static uint16_t g16(crucible_core *c, uint16_t rec, uint16_t o) {
  uint8_t lo = g8(c, rec, o), hi = g8(c, rec, (uint16_t)(o + 1u));
  return (uint16_t)(lo | ((uint16_t)hi << 8));
}
static void gn(crucible_core *c, uint16_t rec, uint16_t o, uint8_t *p, uint8_t n) {
  while (n--) *p++ = g8(c, rec, o++);
}
/* the fields of a v3 or v4 record into the state (v3 has no V4 fields; its newest find is a byte) */
static void read_record(crucible_core *c, uint16_t rec, uint8_t version) {
  uint8_t i, k, *p, b;
  for (i = 0; i < FIELDS; i++) {
    k = rec_kind[i];
    if ((k & V4) && version < 4u) continue;
    p = (uint8_t *)c + rec_off[i];
    switch (k & (uint8_t)~V4) {
    case K_LIVE:
    case K_ITEMS:
    case K_RECIPES: break;
#ifndef CRU_NO_CARD
    case K_CARD:
      if (c->card) gn(c, rec, rec_at[i], c->card, CRU_CARD_BYTES);
      break;
#endif
    case K_NEW8:
      if (version < 4u) {
        b = g8(c, rec, rec_at[i]);
        if (b == 0xffu)
          c->last_new = CRU_NONE;
        else
          c->last_new = b;
      }
      break;
    case U16: *(uint16_t *)p = g16(c, rec, rec_at[i]); break;
    default: gn(c, rec, rec_at[i], p, (uint8_t)(k & 0x3fu)); break;
    }
  }
  gn(c, rec, BOARD_AT, c->board, CRU_BOARD_ROWS * CRU_BOARD_ROW);
  if (version < 4u) { /* v3 kept three initials and three-letter board names */
    for (i = 3; i < CRU_NAME; i++) c->name[i] = ' ';
    for (i = 0; i < CRU_BOARD_ROWS * 5u; i++) c->tails[i] = ' ';
  }
}
static uint8_t valid(crucible_core *c, uint16_t at, uint8_t version, uint16_t size) {
  uint16_t crc = 0xffffu, i;
  uint8_t x, y;
  x = cri_rd(c, at);
  y = cri_rd(c, (uint16_t)(at + 1u));
  if (x != 0xc1u || y != version) return 0;
  for (i = 0; i < size - 2u; i++) {
    x = cri_rd(c, (uint16_t)(at + i));
    crc = crc_step(crc, x);
  }
  x = g8(c, at, (uint16_t)(size - 2u));
  y = g8(c, at, (uint16_t)(size - 1u));
  if (x != (uint8_t)crc) return 0;
  if (y != (uint8_t)(crc >> 8)) return 0;
  return 1;
}
/* the newer valid v4 or v5 record of the two (its version in *version), or CRU_NONE */
static uint16_t newest45(crucible_core *c, uint8_t *version) {
  uint16_t best = CRU_NONE, seq = 0, s, at = P_V4_A;
  uint8_t k, v;
  for (k = 0; k < 2u; k++) {
    for (v = 4; v <= 5u; v++) {
      if (!valid(c, at, v, REC_SIZE)) continue;
      s = g16(c, at, 2);
      if (best == CRU_NONE || (int16_t)(s - seq) > 0) {
        best = at;
        seq = s;
        *version = v;
      }
    }
    at = P_V4_B;
  }
  return best;
}
/* the valid record of the two with the newer sequence number (wrapping), or CRU_NONE */
static uint16_t newest(crucible_core *c, uint16_t a, uint16_t b, uint8_t version, uint16_t size) {
  uint16_t best = CRU_NONE, seq = 0, s;
  if (valid(c, a, version, size)) {
    best = a;
    seq = g16(c, a, 2);
  }
  if (valid(c, b, version, size)) {
    s = g16(c, b, 2);
    if (best == CRU_NONE || (int16_t)(s - seq) > 0) best = b;
  }
  return best;
}

static void set_name(crucible_core *c, const char *name) {
  uint8_t i, blank = 1;
  char ch;
  for (i = 0; i < CRU_NAME; i++) {
    ch = *name;
    if (ch)
      name++;
    else
      ch = ' ';
    c->name[i] = ch;
    if (ch != ' ') blank = 0;
  }
  if (blank) {
    c->name[0] = 'Y';
    c->name[1] = 'O';
    c->name[2] = 'U';
  }
}
static void clear_state(crucible_core *c) {
  uint8_t *p = (uint8_t *)&c->sequence, *e = (uint8_t *)&c->card; /* the card pointer is the host's: kept */
  while (p < e) *p++ = 0;
  c->last_new = c->focus = c->slot_a = c->slot_b = CRU_NONE;
}

uint8_t cru_load(crucible_core *c, uint8_t entropy) CORE_BANKED {
  uint16_t rec, i;
  uint8_t kind, o[4], v, version = 0, rec_place, place = c->place;
  clear_state(c);
  if (place == CRU_PLACE_32K && (c->items > CRU_COMPACT_ITEMS || c->recipes > CRU_COMPACT_RECIPES))
    return CRU_LOAD_BAD_LAYOUT;
  rec = newest45(c, &version);
  if (rec != CRU_NONE) {
    read_record(c, rec, 4u);
    rec_place = g8(c, rec, (uint16_t)(P_REC_AT + L_PLACE));
    kind = CRU_LOAD_V4;
    if (version == 5u && place != CRU_PLACE_WIDE) {
      clear_state(c);
      return CRU_LOAD_BAD_LAYOUT;
    }
    if (version == 4u && place == CRU_PLACE_WIDE) {
      /* the catalogue outgrew v4: move the play memory into the WIDE slots, then save v5 */
      cri_widen(c, rec, rec_place, g16(c, rec, 14), g16(c, rec, 242));
      cri_write(c);
      cri_play_commit(c);
      cri_write(c);
      kind = CRU_LOAD_WIDENED;
    } else {
      if (cri_play_load(c, rec)) kind = CRU_LOAD_RECOUNTED;
      /* a v4 save from the other v4 placement (a header changed between 32 and 128 KB): its dud filter lived
       * elsewhere, so it is forgotten; every exact area is where it was */
      if (version == 4u &&
          ((place == CRU_PLACE_32K && rec_place != 0u) || (place == CRU_PLACE_128K && rec_place != CRU_PLACE_128K))) {
        v = 0;
        if (place == CRU_PLACE_128K) v = CRU_PLACE_128K;
        cri_wr(c, (uint16_t)(P_LIVE + L_PLACE), v);
        cri_filter_reset(c);
        cri_play_commit(c);
        kind = CRU_LOAD_RECOUNTED;
      }
      /* a catalogue update (ids and recipes are append-only) can complete or reopen routes and deepen items */
      if (g16(c, rec, 14) != c->items || g16(c, rec, 242) != c->recipes) {
        cri_play_recount(c);
        kind = CRU_LOAD_RECOUNTED;
      }
      if (kind == CRU_LOAD_RECOUNTED) cri_write(c); /* keep the repaired counters (and the new catalogue size) */
    }
  } else if ((rec = newest(c, V3_A, V3_B, 3u, REC_SIZE)) != CRU_NONE) {
    read_record(c, rec, 3u);
    c->variant_seed = c->rng;
    v = g8(c, rec, 14); /* v3's catalogue size */
    cri_migrate_v3(c, rec, v);
    cri_write(c); /* the exact parts are safe before the filter clear overwrites the v3 records */
    cri_finish_migration(c, rec, v);
    cri_write(c);
    kind = CRU_LOAD_V3;
  } else {
    /* v2/v1 records hold ids 0..31 owned and the points; they sit inside the 32K layout's filter area, so they are
     * read before anything is written */
    c->rng = (uint16_t)(0x93d1u ^ entropy);
    kind = CRU_LOAD_FRESH;
    if ((rec = newest(c, V2_A, V2_B, 2u, 64u)) != CRU_NONE) {
      gn(c, rec, 4, o, 4);
      c->points = g16(c, rec, 8);
      c->rng = g16(c, rec, 10);
      c->sequence = g16(c, rec, 2);
      for (i = 0; i < 26u; i++) {
        v = g8(c, rec, (uint16_t)(12u + i));
        if (v > 3u)
          v = 0;
        else
          v = cri_mod3(v);
        cri_set_variant(c, i, v);
      }
      kind = CRU_LOAD_V2;
    } else if ((rec = newest(c, V1_A, V1_B, 1u, 16u)) != CRU_NONE) {
      gn(c, rec, 4, o, 4);
      c->points = g16(c, rec, 8);
      c->sequence = g16(c, rec, 2);
      for (i = 0; i < 26u; i++) {
        v = o[i >> 3];
        if (!(v & bit_mask[i & 7u])) continue;
        v = cri_roll(c, entropy);
        cri_set_variant(c, i, v);
      }
      kind = CRU_LOAD_V1;
    }
    if (kind != CRU_LOAD_FRESH) c->flags = CRU_FLAG_MIGRATED;
    if (kind == CRU_LOAD_FRESH)
      cri_play_init(c, (uint8_t *)0);
    else
      cri_play_init(c, o);
    if (kind == CRU_LOAD_FRESH)
      for (i = 0; i < (uint16_t)((c->items + 7u) >> 3); i++) { /* the starters' tints: a byte of the bitmap at a time */
        uint8_t sb = cri_starter_byte(c, i), b;
        if (sb)
          for (b = 0; b < 8u; b++)
            if (sb & (uint8_t)(1u << b)) {
              v = cri_roll(c, entropy);
              cri_set_variant(c, (uint16_t)((i << 3) + b), v);
            }
      }
    c->variant_seed = c->rng;
    cri_write(c);
    cri_play_commit(c); /* initialises the dud filter */
    cri_write(c);
  }
  /* settle every field */
  v = 0;
  for (i = 0; i < CRU_NAME; i++) {
    o[0] = (uint8_t)c->name[i];
    if (o[0] && o[0] != ' ') v = 1;
  }
  if (!v) set_name(c, "YOU");
  if (c->title > CRU_TITLES) c->title = 0;
  if (c->filter >= CRU_FILTERS) c->filter = 0;
  if ((c->options & 3u) > 2u) c->options &= (uint8_t)~3u;
  if (((c->options >> 3) & 3u) > 2u) c->options &= (uint8_t)~(3u << 3);
  if (c->last_new != CRU_NONE && c->last_new >= c->items) c->last_new = CRU_NONE;
  cri_lost_settle(c);
  /* every power-on is a new run on the leaderboard */
  c->session++;
  if (c->session == CRU_FRIEND) c->session = 1;
  cri_derive(c);
  c->boot_points = c->points;
  c->boot_found = c->found[0];
  /* the bench opens on the first owned shelf item */
  c->focus = cri_shelf(c, 0);
  if (!cri_owned(c, c->focus)) c->focus = cri_step(c, c->focus, 1);
  c->slot_a = c->slot_b = CRU_NONE;
  c->message = CRU_MSG_NAME;
  cri_feats_reset(c);
  if (cri_check(c)) cri_save(c);
  return kind;
}

void cru_init(crucible_core *c, const crucible_tables *t, const crucible_store *s, uint8_t layout) CORE_BANKED {
  uint8_t place = CRU_PLACE_32K;
  cri_tables_init(c, t);
  c->store.read = s->read;
  c->store.write = s->write;
  c->store.ctx = s->ctx;
  c->layout = layout;
  c->lean = 0;
  c->card = 0; /* a host that keeps a player card sets it after cru_init, before cru_load */
  /* the placement: 32K, or for the 128K layout bank 0 while the catalogue fits v4, and WIDE beyond */
  if (layout == CRU_LAYOUT_128K) {
    place = CRU_PLACE_128K;
    if (c->items > CRU_COMPACT_ITEMS || c->recipes > CRU_COMPACT_RECIPES) place = CRU_PLACE_WIDE;
  }
  cri_set_place(c, place);
}
void cru_stir(crucible_core *c, uint8_t entropy) CORE_BANKED { c->rng = (uint16_t)(c->rng + entropy); }
void cru_set_name(crucible_core *c, const char *name) CORE_BANKED { set_name(c, name); }
void cru_get_name(const crucible_core *c, char *out) CORE_BANKED {
  uint8_t i, n = 0;
  char ch;
  for (i = 0; i < CRU_NAME; i++) {
    ch = c->name[i];
    if (!ch) ch = ' ';
    out[i] = ch;
    if (ch != ' ') n = (uint8_t)(i + 1u);
  }
  out[n] = 0;
}
