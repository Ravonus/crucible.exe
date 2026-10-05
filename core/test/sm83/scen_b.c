/* Save paths (scenarios 11..17): round trip, power cuts during mixes (sampled write counts, every count near the
 * record's end and the commit), a cut inside a dud-filter rotation, v3 migration in both layouts with cuts inside it,
 * v2/v1, a catalogue update, a corrupted area and record, RESET GAME, the 128 KB layout, and a grant. */
#ifdef __SDCC
#pragma bank 255
#endif
#include "scen.h"

#define C (&scen_core)
#define LIVE 0x0f00u

static uint16_t live16(uint8_t o) {
  uint8_t lo = plat_peek(LIVE + o), hi = plat_peek(LIVE + o + 1u);
  return (uint16_t)(lo | ((uint16_t)hi << 8));
}
static uint16_t newest_record(void) {
  uint16_t at = 0x0500u;
  if (C->sequence & 1u) at = 0x0700u;
  return at;
}

static void s_round_trip(void) {
  uint32_t x, y;
  uint16_t at, i;
  uint8_t e = 7;
  char name[8];
  sc_begin(11);
  sc_boot(CRU_LAYOUT_32K, e);
  sc_mix(CW_FIRE, CW_WATER);
  sc_mix(CW_EARTH, CW_FIRE);
  sc_mix(CW_AIR, CW_AIR);
  sc_mix(CW_FIRE, CW_FIRE);
  sc_mix(CW_SUN, CW_WATER);
  cru_filter_apply(C, 8);
  cru_title_wear(C, 0);
  name[0] = 'M';
  name[1] = 'E';
  name[2] = 'R';
  name[3] = 'L';
  name[4] = 'I';
  name[5] = 'N';
  name[6] = 0;
  cru_set_name(C, name);
  C->options = (uint8_t)(1u | CRU_OPT_SFX_OFF | (2u << 3));
  cru_book_open(C);
  cru_book_view(C);
  sc_tick(4000);
  cru_stir(C, 0x55);
  cru_save(C);
  x = sc_state(1);
  /* the record, byte by byte where the layout matters */
  at = newest_record();
  EQ(plat_peek(at), 0xc1);
  EQ(plat_peek(at + 1u), 4);
  EQ(plat_peek(at + 4u) | (plat_peek(at + 5u) << 8), C->points);
  EQ(plat_peek(at + 14u), CW_ITEMS);
  for (i = 0; i < 64u; i++) EQ(plat_peek(at + 48u + i), C->variants[i]);
  for (i = 0; i < 72u; i++) EQ(plat_peek(at + 112u + i), plat_peek(LIVE + i));
  EQ(plat_peek(at + 192u), 'M');
  EQ(plat_peek(at + 197u), 'N');
  EQ(plat_peek(at + 198u), ' ');
  EQ(plat_peek(at + 242u), CW_RECIPES);
  for (i = 0; i < 24u; i++) EQ(plat_peek(at + 376u + i), C->feats[i]);
  EQ(plat_peek(at + 423u), C->options);
  EQ(plat_peek(at + 424u), 'M');
  for (i = 0; i < 80u; i++) EQ(plat_peek(at + 429u + i), C->board[i]);
  EQ(sc_boot(CRU_LAYOUT_32K, e), CRU_LOAD_V4);
  C->session--;
  y = sc_state(1);
  EQ(x, y);
  EQ(C->options, 1u | CRU_OPT_SFX_OFF | (2u << 3));
  sc_end(CRU_LAYOUT_32K);
}

/* power cuts: the image before the mix sits in the backup bank */
static uint16_t cut_step(uint16_t k, uint16_t total, uint16_t stride) {
  if (k < 8u) return 1;
  if ((uint16_t)(k + 48u) >= total) return 1; /* every cut through the record's last bytes and the commit */
  return stride;
}
static void torn_case(uint8_t idx, uint16_t a, uint16_t b, uint8_t expect, uint16_t stride) {
  uint32_t before, after, now, w0;
  uint16_t total, k, nb = 0, na = 0, flips = 0, odd = 0;
  uint8_t last_after = 0;
  plat_restore();
  sc_boot(CRU_LAYOUT_32K, 0);
  before = sc_state(0);
  plat_restore();
  sc_boot(CRU_LAYOUT_32K, 0);
  EQ(cru_mix_begin(C, a, b, 9), expect);
  w0 = plat_writes();
  cru_mix_finish(C);
  total = (uint16_t)(plat_writes() - w0);
  sc_boot(CRU_LAYOUT_32K, 0);
  after = sc_state(0);
  CHECK(before != after);
  for (k = 0; k <= total; k = (uint16_t)(k + cut_step(k, total, stride))) {
    plat_restore();
    sc_boot(CRU_LAYOUT_32K, 0);
    cru_mix_begin(C, a, b, 9);
    plat_budget(k, 1);
    cru_mix_finish(C);
    plat_budget(0, 0);
    EQ(sc_boot(CRU_LAYOUT_32K, 0), CRU_LOAD_V4);
    now = sc_state(0);
    if (now == before) {
      nb++;
      if (last_after) flips++;
    } else if (now == after) {
      na++;
      last_after = 1;
    } else
      odd++;
  }
  EQ(odd, 0);
  EQ(flips, 0);
  CHECK(nb > 0u && na > 0u);
  sc_torn(idx, nb, na);
}
static void s_torn(void) {
  sc_begin(12);
  sc_boot(CRU_LAYOUT_32K, 2);
  sc_mix(CW_FIRE, CW_WATER);
  sc_mix(CW_EARTH, CW_WATER);
  sc_mix(CW_FIRE, CW_FIRE);
  sc_mix(CW_AIR, CW_AIR);
  plat_backup();
  torn_case(0, CW_EARTH, CW_FIRE, CRU_NEW, 29);
  torn_case(1, CW_SUN, CW_WATER, CRU_ROUTE, 29);
  torn_case(2, CW_WATER, CW_FIRE, CRU_KNOWN, 29);
  torn_case(3, CW_STEAM, CW_MUD, CRU_NOTHING, 29);
  torn_case(4, CW_AIR, CW_AIR, CRU_NOTHING, 29);
  plat_restore();
  sc_boot(CRU_LAYOUT_32K, 0);
  sc_end(CRU_LAYOUT_32K);
}
/* the 32K dud filter rotates every 341 duds; a cut inside the generation clear is finished at power-on */
static void s_rotation(void) {
  uint16_t a, b;
  uint8_t progress = 1;
  sc_begin(13);
  sc_boot(CRU_LAYOUT_32K, 2);
  while (progress) {
    progress = 0;
    for (a = 0; a < CW_ITEMS; a++)
      for (b = a; b < CW_ITEMS; b++)
        if (cru_recipe(C, a, b) != CRU_NONE && cru_owned(C, a) && cru_owned(C, b) && !cru_tried(C, a, b)) {
          cru_mix_begin(C, a, b, 0);
          cru_mix_finish(C);
          progress = 1;
        }
  }
  for (a = 0; a < CW_ITEMS; a++)
    for (b = a; b < CW_ITEMS; b++) {
      if (cru_recipe(C, a, b) != CRU_NONE || cru_tried(C, a, b)) continue;
      if (live16(18) >= 341u) goto found;
      cru_mix_begin(C, a, b, 0);
      cru_mix_finish(C);
    }
  sc_fail((uint16_t)__LINE__); /* the catalogue always reaches a rotation */
found:
  EQ(plat_peek(LIVE + 17u), 0);
  plat_backup();
  torn_case(5, a, b, CRU_NOTHING, 61);
  sc_boot(CRU_LAYOUT_32K, 0); /* the last cut was the whole mix */
  EQ(plat_peek(LIVE + 17u), 1);
  CHECK(cru_tried(C, a, b));
  EQ(cru_play_value(C, CRU_PV_RECIPES), CW_RECIPES);
  sc_end(CRU_LAYOUT_32K);
}

/* ---- v3 migration ---- */
static uint16_t v3_points;
static const uint8_t tried[11][2] = {{CW_FIRE, CW_WATER}, {CW_EARTH, CW_WATER}, {CW_EARTH, CW_FIRE},
                                     {CW_FIRE, CW_FIRE},  {CW_WATER, CW_WATER}, {CW_LAVA, CW_WATER},
                                     {CW_SUN, CW_WATER},  {CW_AIR, CW_AIR},     {CW_STEAM, CW_MUD},
                                     {CW_SUN, CW_LAVA},   {CW_FIRE, CW_WATER}};
static void check_v3(void) {
  crucible_board_row r;
  uint8_t i, j, rav = 0, bob = 0, dup;
  uint16_t k, lb;
  EQ(C->feats[0], 2);
  EQ(C->feats[1], 1);
  EQ(C->feats[6], 1);
  EQ(C->feats[4], 2);
  EQ(C->feats[2], 3);
  EQ(C->points, 345u + sc_feat_points() - 30u);
  EQ(C->title, 15);
  EQ(C->filter, 9);
  EQ(C->made, 10);
  EQ(C->routes, 1);
  EQ(C->minutes, 75);
  EQ(C->fails, 3);
  EQ(C->book_views, 12);
  EQ(C->best_gap, 900);
  EQ(C->best_streak, 6);
  EQ(C->best_chain, 3);
  EQ(C->last_new, CW_SUN);
  EQ(C->best_flurry, 3);
  EQ(C->best_nofail, 5);
  EQ(C->best_swift, 7);
  EQ(C->options, 1);
  EQ(C->variants[5], (uint8_t)(5u * 37u));
  EQ(C->name[0], 'R');
  EQ(C->name[2], 'V');
  EQ(C->name[3], ' ');
  for (i = 0; i < CRU_BOARD_ROWS; i++) {
    cru_board_row(C, i, &r);
    if (r.session == 3u) {
      rav++;
      EQ(r.name[0], 'R');
      EQ(r.name[3], 0);
      EQ(r.points, 60);
      EQ(r.finds, 6);
      EQ(r.title, 15);
    }
    if (r.session == CRU_FRIEND) {
      bob++;
      EQ(r.name[0], 'B');
      EQ(r.points, 25);
    }
  }
  EQ(rav, 1);
  EQ(bob, 1);
  EQ(C->found[0], 10);
  for (i = 0; i < 11u; i++) CHECK(cru_tried(C, tried[i][0], tried[i][1]));
  EQ(cru_play_value(C, CRU_PV_RECIPES), 7);
  EQ(cru_play_value(C, CRU_PV_DUDS), 3);
  EQ(cru_play_value(C, CRU_PV_MIRRORS), 2);
  CHECK(!cru_tried(C, CW_EARTH, CW_AIR));
  for (k = 0; k < CW_ITEMS; k++) {
    lb = 0;
    for (i = 0; i < 11u; i++) {
      dup = 0;
      for (j = 0; j < i; j++)
        if ((tried[j][0] == tried[i][0] && tried[j][1] == tried[i][1]) ||
            (tried[j][0] == tried[i][1] && tried[j][1] == tried[i][0]))
          dup = 1;
      if (!dup && (tried[i][0] == k || tried[i][1] == k)) lb++;
    }
    EQ(cru_play_uses(C, k), lb);
  }
}
static void s_migrate(void) {
  uint8_t layout, kind;
  uint32_t total, k, w0;
  uint16_t cuts = 0;
  sc_begin(14);
  for (layout = 0; layout < 2u; layout++) {
    plat_wipe();
    sc_make_v3(0x0100, 41, 345);
    sc_make_v3(0x0300, 40, 100);
    EQ(sc_boot(layout, 0), CRU_LOAD_V3);
    EQ(C->session, 8);
    check_v3();
    v3_points = C->points;
    EQ(sc_boot(layout, 0), CRU_LOAD_V4);
    EQ(C->session, 9);
    check_v3();
    if (!layout)
      CHECK(plat_peek(0x0100) != 0xc1 || plat_peek(0x0101) != 3);
    else
      CHECK(plat_peek(0x0100) == 0xc1 && plat_peek(0x0101) == 3);
    EQ(sc_mix(CW_STONE, CW_AIR), CRU_NEW);
    EQ(sc_mix(CW_AIR, CW_AIR), CRU_NOTHING);
    CHECK(!C->mix.fresh);
  }
  /* power cuts inside the 32K migration */
  plat_wipe();
  sc_make_v3(0x0100, 41, 345);
  plat_backup();
  w0 = plat_writes();
  sc_boot(CRU_LAYOUT_32K, 0);
  total = plat_writes() - w0;
  for (k = 0; k < total; k += 997u) {
    plat_restore();
    plat_budget((uint16_t)k, 1);
    sc_boot(CRU_LAYOUT_32K, 0);
    plat_budget(0, 0);
    kind = sc_boot(CRU_LAYOUT_32K, 0);
    CHECK(kind == CRU_LOAD_V3 || kind == CRU_LOAD_V4);
    EQ(C->points, v3_points);
    EQ(C->found[0], 10);
    EQ(cru_play_value(C, CRU_PV_RECIPES), 7);
    CHECK(cru_owned(C, CW_STONE) && cru_tried(C, CW_LAVA, CW_WATER));
    cuts++;
  }
  sc_torn(6, cuts, (uint16_t)total);
  sc_end(CRU_LAYOUT_32K);
}

static void seal(uint16_t at, uint16_t size) {
  /* the v1/v2 CRC: CRC-16/CCITT over size-2 bytes (bitwise here) */
  uint16_t crc = 0xffffu, i;
  uint8_t k, b;
  for (i = 0; i < (uint16_t)(size - 2u); i++) {
    b = plat_peek(at + i);
    crc ^= (uint16_t)((uint16_t)b << 8);
    for (k = 0; k < 8u; k++) {
      if (crc & 0x8000u)
        crc = (uint16_t)((crc << 1) ^ 0x1021u);
      else
        crc = (uint16_t)(crc << 1);
    }
  }
  plat_poke(at + size - 2u, (uint8_t)crc);
  plat_poke(at + size - 1u, (uint8_t)(crc >> 8));
}
static void s_v2_v1(void) {
  uint8_t i;
  sc_begin(15);
  plat_poke(0x80, 0xc1);
  plat_poke(0x81, 2);
  plat_poke(0x82, 9);
  plat_poke(0x84, 0x9f);
  plat_poke(0x88, 77);
  plat_poke(0x8a, 0x21);
  plat_poke(0x8b, 0x43);
  for (i = 0; i < 26u; i++) plat_poke(0x8cu + i, (uint8_t)(i & 3u));
  seal(0x80, 64);
  EQ(sc_boot(CRU_LAYOUT_32K, 0), CRU_LOAD_V2);
  EQ(C->points, 77);
  EQ(C->found[0], 6);
  CHECK(cru_owned(C, CW_MUD) && !cru_owned(C, CW_SALT));
  EQ(cru_variant(C, 1), 1);
  EQ(cru_variant(C, 2), 2);
  EQ(cru_variant(C, 3), 0);
  EQ(cru_variant(C, 7), 0);
  CHECK(C->flags & CRU_FLAG_MIGRATED);
  EQ(sc_boot(CRU_LAYOUT_32K, 0), CRU_LOAD_V4);
  EQ(C->points, 77);
  plat_wipe();
  plat_poke(0x20, 0xc1);
  plat_poke(0x21, 1);
  plat_poke(0x22, 3);
  plat_poke(0x24, 0x1f);
  plat_poke(0x28, 42);
  seal(0x20, 16);
  EQ(sc_boot(CRU_LAYOUT_32K, 0), CRU_LOAD_V1);
  EQ(C->points, 42u + sc_feat_points());
  EQ(C->found[0], 5);
  CHECK(cru_owned(C, CW_STEAM));
  EQ(sc_mix(CW_STEAM, CW_AIR), CRU_NEW);
  sc_end(CRU_LAYOUT_32K);
}

static void s_repairs(void) {
  uint32_t x, y;
  uint16_t at, seq, points, tiers;
  uint8_t v;
  char name[4];
  sc_begin(16);
  /* a catalogue update: another size in the record recounts and writes back */
  sc_boot(CRU_LAYOUT_32K, 3);
  sc_mix(CW_FIRE, CW_WATER);
  sc_mix(CW_EARTH, CW_FIRE);
  sc_mix(CW_AIR, CW_AIR);
  x = sc_state(1);
  at = newest_record();
  plat_poke(at + 14u, 56);
  sc_crc_fix(at);
  EQ(sc_boot(CRU_LAYOUT_32K, 3), CRU_LOAD_RECOUNTED);
  C->session--;
  C->sequence--;
  y = sc_state(1);
  EQ(x, y);
  /* a corrupted area: the sum check recounts */
  v = plat_peek(0x0900u + (CW_SUN >> 3));
  v |= (uint8_t)(1u << (CW_SUN & 7u));
  plat_poke(0x0900u + (CW_SUN >> 3), v);
  EQ(sc_boot(CRU_LAYOUT_32K, 3), CRU_LOAD_RECOUNTED);
  CHECK(cru_owned(C, CW_SUN));
  EQ(cru_play_value(C, CRU_PV_OWNED), 7);
  EQ(C->found[0], 7);
  EQ(sc_boot(CRU_LAYOUT_32K, 3), CRU_LOAD_V4);
  /* a corrupted newest record: the older one loads */
  sc_mix(CW_SUN, CW_WATER);
  seq = C->sequence;
  points = C->points;
  tiers = sc_feat_points();
  sc_mix(CW_EARTH, CW_WATER);
  at = newest_record();
  v = plat_peek(at + 300u);
  plat_poke(at + 300u, (uint8_t)(v ^ 0x40u));
  EQ(sc_boot(CRU_LAYOUT_32K, 3), CRU_LOAD_RECOUNTED);
  /* the older record's points, plus any tier the recounted areas complete at power-on (it saves once more) */
  EQ(C->points, points + (uint16_t)(sc_feat_points() - tiers));
  if (C->points == points)
    EQ(C->sequence, seq + 1u);
  else
    EQ(C->sequence, seq + 2u);
  /* RESET GAME: every bit of progress goes; the options, the name and the record sequence stay */
  name[0] = 'Z';
  name[1] = 'E';
  name[2] = 'D';
  name[3] = 0;
  cru_set_name(C, name);
  C->options = 9;
  seq = C->sequence;
  cru_reset(C, 5);
  EQ(C->found[0], 4);
  EQ(C->made, 0);
  EQ(C->fails, 0);
  EQ(C->points, sc_feat_points());
  EQ(C->options, 9);
  EQ(C->name[0], 'Z');
  EQ(C->name[2], 'D');
  CHECK(C->sequence >= seq + 2u);
  EQ(C->focus, CW_EARTH);
  CHECK(!cru_owned(C, CW_STEAM));
  CHECK(!cru_tried(C, CW_FIRE, CW_WATER));
  CHECK(!cru_tried(C, CW_AIR, CW_AIR));
  EQ(cru_play_value(C, CRU_PV_RECIPES), 0);
  EQ(cru_play_value(C, CRU_PV_DUDS), 0);
  EQ(cru_play_uses(C, CW_FIRE), 0);
  EQ(sc_boot(CRU_LAYOUT_32K, 3), CRU_LOAD_V4);
  EQ(C->found[0], 4);
  EQ(C->options, 9);
  EQ(C->name[0], 'Z');
  EQ(sc_mix(CW_FIRE, CW_WATER), CRU_NEW);
  sc_end(CRU_LAYOUT_32K);
}

/* the 128 KB layout (32-bit filter addresses, 32 KB generation clears) and a grant */
static void s_big(void) {
  uint16_t found, points, tiers;
  sc_begin(17);
  EQ(sc_boot(CRU_LAYOUT_128K, 4), CRU_LOAD_FRESH);
  sc_mix(CW_FIRE, CW_WATER);
  sc_mix(CW_AIR, CW_AIR);
  sc_mix(CW_STEAM, CW_EARTH);
  sc_mix(CW_EARTH, CW_WATER);
  sc_mix(CW_STEAM, CW_MUD);
  CHECK(cru_tried(C, CW_AIR, CW_AIR));
  CHECK(cru_tried(C, CW_STEAM, CW_MUD));
  CHECK(!cru_tried(C, CW_MUD, CW_MUD));
  EQ(cru_play_value(C, CRU_PV_DUDS), 2);
  found = C->found[0];
  points = C->points;
  tiers = sc_feat_points();
  EQ(cru_grant(C, CW_DRAGON, CRU_GRANT_SYNC), 1);
  EQ(cru_grant(C, CW_DRAGON, CRU_GRANT_SYNC), 0);
  CHECK(cru_owned(C, CW_DRAGON));
  EQ(C->found[0], found + 1u);
  EQ(C->points, points + (uint16_t)(sc_feat_points() - tiers)); /* no mix points: only tiers the grant completes */
  EQ(sc_boot(CRU_LAYOUT_128K, 4), CRU_LOAD_V4);
  CHECK(cru_owned(C, CW_DRAGON));
  CHECK(cru_tried(C, CW_STEAM, CW_MUD));
  EQ(cru_play_value(C, CRU_PV_OWNED), C->found[0]);
  /* a lost piece (cru_lost.c): STEAM dropped cools for N in 2..10 mixes that make something */
  {
    crucible_cells v;
    uint8_t n, k;
    EQ(cru_drop(C, CW_STEAM), 1);
    n = cru_lost_left(C, CW_STEAM);
    CHECK(n >= CRU_LOST_MIN && n <= CRU_LOST_MAX);
    EQ(n, cru_lost_span(C->variant_seed, CW_STEAM, 1));
    points = C->points;
    EQ(sc_mix(CW_FIRE, CW_WATER), CRU_NOTHING);
    EQ(C->mix.lost, CRU_LOST_FIRST); /* the first attempt glitches */
    CHECK(!cru_owned(C, CW_STEAM));
    EQ(C->points, points);
    EQ(cru_lost_left(C, CW_STEAM), n);
    C->slot_a = CW_WATER;
    C->focus = CW_FIRE;
    cru_bench_cells(C, &v);
    EQ(v.kind[CRU_CR], CRU_K_GLITCH); /* later ones are cued */
    EQ(sc_mix(CW_WATER, CW_FIRE), CRU_NOTHING);
    EQ(C->mix.lost, CRU_LOST_AGAIN);
    EQ(cru_lost_left(C, CW_STEAM), n);
    EQ(sc_boot(CRU_LAYOUT_128K, 4), CRU_LOAD_V4); /* saved */
    EQ(cru_lost_left(C, CW_STEAM), n);
    EQ(cru_lost_state(C, CW_STEAM), CRU_LOST_AGAIN);
    for (k = 0; k < n; k++) EQ(sc_mix(CW_EARTH, CW_WATER), CRU_KNOWN);
    EQ(cru_lost_state(C, CW_STEAM), CRU_LOST_NONE);
    EQ(sc_mix(CW_FIRE, CW_WATER), CRU_NEW);
    CHECK(cru_owned(C, CW_STEAM)); /* normal again */
  }
  sc_end(CRU_LAYOUT_128K);
}

void scen_run_b(void) SCEN_FN {
  s_round_trip();
  s_torn();
  s_rotation();
  s_migrate();
  s_v2_v1();
  s_repairs();
  s_big();
}
