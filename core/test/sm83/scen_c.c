/* Sessions, part 2 (scenarios 6..10): the host tests' sessions on the real catalogue, by id. Literals stay in this file
 * (on the ROM a literal must not be read from another bank), so names are compared here and copied to RAM before
 * the core reads them. */
#ifdef __SDCC
#pragma bank 255
#endif
#include "scen.h"

#define C (&scen_core)
static uint8_t streq(const char *a, const char *b) {
  char x, y;
  for (;;) {
    x = *a++;
    y = *b++;
    if (x != y) return 0;
    if (!x) return 1;
  }
}
static void copy(char *out, const char *s) {
  while ((*out++ = *s++) != 0) {}
}
static uint16_t rows[64];
static char text[24];

/* a fresh save whose starters rolled no gilded tint (so title and toast expectations are exact) */
static uint8_t boot_clean(void) {
  uint8_t e;
  for (e = 0; e < 64u; e++) {
    plat_wipe();
    sc_boot(CRU_LAYOUT_32K, e);
    if (!C->gilded) return e;
  }
  return 0;
}

static void s_chain(void) {
  sc_begin(6);
  boot_clean();
  sc_mix(CW_FIRE, CW_WATER);
  sc_mix(CW_STEAM, CW_AIR);
  EQ(C->chain, 2);
  EQ(cru_feat_tier(C, 5), 1);
  sc_mix(CW_CLOUD, CW_WATER);
  EQ(C->chain, 3);
  sc_mix(CW_RAIN, CW_EARTH);
  EQ(C->chain, 4);
  sc_mix(CW_PLANT, CW_PLANT);
  EQ(C->chain, 5);
  sc_mix(CW_TREE, CW_FIRE);
  EQ(C->chain, 6);
  EQ(cru_feat_tier(C, 5), 4);
  EQ(C->best_chain, 6);
  sc_mix(CW_EARTH, CW_WATER);
  EQ(C->chain, 1);
  EQ(C->last_new, CW_MUD);
  EQ(C->found[3], 2);
  EQ(C->total[3], 4);
  EQ(cru_feat_value(C, 12), 50);
  EQ(cru_feat_tier(C, 12), 2);
  EQ(C->total[2], 11);
  EQ(cru_feat_value(C, 11), 27);
  EQ(cru_feat_tier(C, 11), 1);
  sc_mix(CW_EARTH, CW_FIRE);
  EQ(cru_feat_value(C, 11), 36);
  sc_mix(CW_AIR, CW_EARTH);
  sc_mix(CW_LAVA, CW_WATER);
  sc_mix(CW_STONE, CW_AIR);
  EQ(cru_feat_value(C, 11), 63);
  EQ(cru_feat_tier(C, 11), 2);
  sc_end(CRU_LAYOUT_32K);
}

static void s_clocks(void) {
  uint8_t i, seq;
  sc_begin(7);
  boot_clean();
  sc_tick(600);
  sc_mix(CW_FIRE, CW_WATER);
  sc_tick(540);
  sc_mix(CW_EARTH, CW_WATER);
  EQ(C->best_gap, 600);
  EQ(cru_feat_tier(C, 2), 3);
  sc_mix(CW_EARTH, CW_FIRE);
  sc_mix(CW_AIR, CW_EARTH);
  sc_mix(CW_AIR, CW_WATER);
  EQ(C->best_first, 24);
  EQ(cru_feat_tier(C, 19), 4);
  EQ(C->best_flurry, 5);
  EQ(cru_feat_tier(C, 3), 3);
  sc_tick(3700);
  sc_mix(CW_AIR, CW_FIRE);
  EQ(C->flurry_n, 1);
  EQ(C->best_swift, 5);
  sc_mix(CW_AIR, CW_FIRE);
  EQ(C->swift_n, 1);
  seq = (uint8_t)C->sequence;
  for (i = 0; i < 10u; i++) sc_tick(3600);
  CHECK(C->minutes >= 10u);
  EQ(cru_feat_tier(C, 17), 1);
  CHECK((uint8_t)C->sequence != seq);
  cru_book_open(C);
  for (i = 0; i < 9u; i++) cru_book_view(C);
  EQ(C->book_views, 10);
  CHECK(C->flags & CRU_FLAG_BOOK_SEEN);
  EQ(cru_feat_tier(C, 16), 0);
  sc_tick(3600);
  EQ(cru_feat_tier(C, 16), 1);
  cru_link(C);
  EQ(cru_feat_tier(C, 23), 1);
  sc_end(CRU_LAYOUT_32K);
}

static void s_gilded(void) {
  uint16_t r, x;
  sc_begin(8);
  boot_clean();
  for (r = 1; r; r++) {
    x = r;
    x ^= (uint16_t)(x << 7);
    x ^= (uint16_t)(x >> 9);
    x ^= (uint16_t)(x << 8);
    x = (uint16_t)(x + 1u);
    if (!(x & 15u)) break;
  }
  C->rng = r;
  EQ(cru_mix_begin(C, CW_FIRE, CW_WATER, 0), CRU_NEW);
  EQ(C->mix.variant, 3);
  cru_mix_finish(C);
  EQ(cru_variant(C, CW_STEAM), 3);
  EQ(C->gilded, 1);
  EQ(cru_feat_tier(C, 9), 1);
  CHECK(cru_variant(C, 300) <= 3u); /* ids past 256 derive their tint */
  sc_end(CRU_LAYOUT_32K);
}

static void s_complete(void) {
  crucible_stat s;
  uint16_t a, b, n, i, ab[2];
  uint8_t progress = 1, t, first;
  sc_begin(9);
  boot_clean();
  /* the stats page before anything */
  cru_stat(C, 4, &s);
  EQ(s.known, 0);
  cru_stat(C, 5, &s);
  EQ(s.known, 0);
  cru_stat(C, 12, &s);
  EQ(s.known, 0);
  sc_mix(CW_FIRE, CW_WATER);
  sc_tick(300);
  sc_mix(CW_EARTH, CW_WATER);
  sc_mix(CW_AIR, CW_AIR);
  cru_stat(C, 0, &s);
  EQ(s.value, 6);
  EQ(s.of, CW_ITEMS);
  EQ(s.unit, CRU_UNIT_OF);
  cru_stat(C, 1, &s);
  EQ(s.value, 2);
  EQ(s.of, CW_RECIPES);
  cru_stat(C, 2, &s);
  EQ(s.value, 3);
  cru_stat(C, 3, &s);
  EQ(s.value, C->points);
  cru_stat(C, 4, &s);
  EQ(s.known, 1);
  EQ(s.value, 360);
  EQ(s.unit, CRU_UNIT_FRAMES);
  cru_stat(C, 7, &s);
  EQ(s.value, 2);
  cru_stat(C, 11, &s);
  EQ(s.unit, CRU_UNIT_PLAYTIME);
  /* the toast queue: QUICK HAND I came first */
  first = cru_toast_peek(C);
  EQ(first, (2u << 2) | 0u);
  cru_toast_done(C);
  CHECK(cru_toast_peek(C) != first);
  /* the book */
  sc_mix(CW_EARTH, CW_FIRE);
  sc_mix(CW_LAVA, CW_WATER);
  n = cru_book_rows(C, rows, 64);
  EQ(n, CW_ITEMS);
  EQ(rows[0], CW_EARTH);
  EQ(rows[4], CW_STEAM);
  EQ(rows[CW_ITEMS - 1u], CW_GEYSER);
  EQ(cru_book_route(C, CW_STEAM, ab), CRU_BOOK_RECIPE);
  EQ(ab[0], CW_FIRE);
  EQ(ab[1], CW_WATER);
  EQ(cru_book_route(C, CW_STONE, ab), CRU_BOOK_RECIPE);
  EQ(ab[0], CW_LAVA);
  EQ(ab[1], CW_WATER);
  EQ(cru_book_route(C, CW_WATER, ab), CRU_BOOK_ELEMENT);
  EQ(cru_book_route(C, CW_DRAGON, ab), CRU_BOOK_UNKNOWN);
  EQ(cru_book_use(C, CW_DRAGON), 0);
  cru_bench_a(C, 0);
  EQ(cru_book_use(C, CW_STONE), 1);
  EQ(C->focus, CW_STONE);
  EQ(C->slot_a, CRU_NONE);
  sc_mix(CW_AIR, CW_WATER);
  cru_filter_apply(C, 3);
  n = cru_book_rows(C, rows, 2);
  EQ(n, 4);
  EQ(rows[0], CW_RAIN);
  EQ(rows[1], CW_SNOW);
  cru_filter_apply(C, 0);
  /* everything: every recipe until nothing new */
  while (progress) {
    progress = 0;
    for (a = 0; a < CW_ITEMS; a++)
      for (b = a; b < CW_ITEMS; b++)
        if (cru_recipe(C, a, b) != CRU_NONE && cru_owned(C, a) && cru_owned(C, b) && !cru_tried(C, a, b)) {
          sc_mix(a, b);
          progress = 1;
        }
  }
  EQ(C->found[0], CW_ITEMS);
  EQ(cru_play_value(C, CRU_PV_RECIPES), CW_RECIPES);
  EQ(cru_play_value(C, CRU_PV_ROUTES), CW_ROUTED);
  EQ(cru_play_value(C, CRU_PV_DEEP), CW_DEEPEST);
  EQ(cru_play_value(C, CRU_PV_MIRRORS), CW_MIRRORS);
  EQ(C->mirror_total, CW_MIRRORS);
  EQ(cru_feat_tier(C, 0), 4);
  EQ(cru_feat_tier(C, 1), 4);
  EQ(cru_feat_tier(C, 8), 4);
  for (t = 10; t <= 15u; t++) EQ(cru_feat_tier(C, t), 4);
  CHECK(C->complete >= 1u);
  EQ(cru_feat_tier(C, 18), 4);
  EQ(C->toast_n, 8);
  for (i = 0; i < CW_ITEMS; i++) CHECK(cru_owned(C, i));
  sc_end(CRU_LAYOUT_32K);
}

static void s_board(void) {
  crucible_board_row r;
  uint8_t e, i, k, rank;
  uint16_t p1, p2, prev, eighth;
  static const uint8_t finds[10][2] = {
      {CW_FIRE, CW_FIRE},   {CW_WATER, CW_WATER}, {CW_AIR, CW_FIRE},    {CW_EARTH, CW_EARTH}, {CW_STEAM, CW_AIR},
      {CW_PLANT, CW_PLANT}, {CW_OCEAN, CW_FIRE},  {CW_PLANT, CW_EARTH}, {CW_STONE, CW_AIR},   {CW_STONE, CW_FIRE}};
  sc_begin(10);
  e = boot_clean();
  sc_mix(CW_FIRE, CW_WATER);
  sc_mix(CW_EARTH, CW_WATER);
  p1 = C->points;
  EQ(cru_board_current(C), 0);
  EQ(cru_board_run_points(C), p1);
  cru_board_row(C, 0, &r);
  CHECK(streq(r.name, "YOU"));
  EQ(r.points, p1);
  EQ(r.finds, 2);
  EQ(r.session, 1);
  CHECK(r.mine);
  sc_boot(CRU_LAYOUT_32K, e);
  EQ(C->session, 2);
  cru_board_row(C, 0, &r);
  CHECK(!r.mine);
  copy(text, "ALCHEMISTS"); /* into RAM: the core reads it from its own bank */
  cru_set_name(C, text);
  cru_get_name(C, text);
  CHECK(streq(text, "ALCHEMIS"));
  sc_mix(CW_EARTH, CW_FIRE);
  sc_mix(CW_AIR, CW_EARTH);
  sc_mix(CW_LAVA, CW_WATER);
  sc_mix(CW_AIR, CW_WATER);
  sc_mix(CW_EARTH, CW_RAIN);
  EQ(cru_title_wear(C, 0), 1);
  p2 = cru_board_run_points(C);
  CHECK(p2 > p1);
  EQ(cru_board_current(C), 0);
  cru_board_row(C, 0, &r);
  CHECK(streq(r.name, "ALCHEMIS"));
  EQ(r.points, p2);
  EQ(r.session, 2);
  EQ(r.title, 1);
  EQ(r.finds, 5);
  cru_board_row(C, 1, &r);
  CHECK(streq(r.name, "YOU"));
  EQ(r.points, p1);
  for (k = 0; k < 10u; k++) {
    sc_boot(CRU_LAYOUT_32K, e);
    EQ(sc_mix(finds[k][0], finds[k][1]), CRU_NEW);
    rank = cru_board_current(C);
    CHECK(rank != CRU_NONE8 || k >= 6u);
  }
  prev = 0xffffu;
  for (i = 0; i < CRU_BOARD_ROWS; i++) {
    cru_board_row(C, i, &r);
    CHECK(r.points <= prev && r.points > 0);
    prev = r.points;
  }
  eighth = prev;
  CHECK(eighth >= 10u);
  sc_boot(CRU_LAYOUT_32K, e);
  EQ(sc_mix(CW_SUN, CW_WATER), CRU_ROUTE);
  EQ(cru_board_run_points(C), 3);
  EQ(cru_board_current(C), CRU_NONE8);
  cru_board_row(C, 7, &r);
  EQ(r.points, eighth);
  sc_end(CRU_LAYOUT_32K);
}

void scen_run_c(void) SCEN_FN {
  s_chain();
  s_clocks();
  s_gilded();
  s_complete();
  s_board();
}
