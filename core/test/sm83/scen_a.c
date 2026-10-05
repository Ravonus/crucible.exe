/* Sessions, part 1 (scenarios 0..5; part 2 in scen_c.c): the host tests' sessions on the real catalogue, by id. Literals stay in this file
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

static void s_fresh(void) {
  uint8_t e, i, found = 0;
  sc_begin(0);
  EQ(sc_boot(CRU_LAYOUT_32K, 5), CRU_LOAD_FRESH);
  EQ(C->found[0], 4);
  CHECK(cru_owned(C, CW_EARTH) && cru_owned(C, CW_WATER) && cru_owned(C, CW_FIRE) && cru_owned(C, CW_AIR));
  CHECK(!cru_owned(C, CW_STEAM));
  EQ(C->session, 1);
  EQ(C->focus, CW_EARTH);
  EQ(C->slot_a, CRU_NONE);
  cru_get_name(C, text);
  CHECK(streq(text, "YOU"));
  EQ(cru_play_value(C, CRU_PV_OWNED), 4);
  EQ(cru_play_value(C, CRU_PV_RECIPES), 0);
  EQ(C->total[0], CW_ITEMS);
  EQ(C->found[1], 4);
  EQ(C->total[1], 4);
  EQ(C->points, sc_feat_points());
  EQ(sc_boot(CRU_LAYOUT_32K, 5), CRU_LOAD_V4);
  EQ(C->session, 1); /* nothing saved this run */
  sc_mix(CW_FIRE, CW_WATER);
  EQ(sc_boot(CRU_LAYOUT_32K, 5), CRU_LOAD_V4);
  EQ(C->session, 2);
  /* a starter that rolled gilded earns GILDED I at power-on and puts the run on the board */
  for (e = 0; e < 64u && !found; e++) {
    plat_wipe();
    sc_boot(CRU_LAYOUT_32K, e);
    if (C->gilded) found = 1;
  }
  CHECK(found);
  i = cru_feat_tier(C, 9);
  CHECK(i >= 1u);
  CHECK(sc_has_toast((9u << 2) | 0u));
  EQ(C->points, sc_feat_points());
  EQ(cru_board_current(C), 0);
  sc_end(CRU_LAYOUT_32K);
}

static void s_outcomes(void) {
  uint16_t awarded = 0;
  sc_begin(1);
  boot_clean();
  EQ(sc_mix(CW_FIRE, CW_WATER), CRU_NEW);
  awarded += 10;
  EQ(C->mix.awarded, 10);
  CHECK(C->mix.fresh);
  CHECK(cru_owned(C, CW_STEAM));
  EQ(C->found[0], 5);
  EQ(C->points, awarded + sc_feat_points());
  EQ(sc_mix(CW_WATER, CW_FIRE), CRU_KNOWN);
  EQ(C->mix.awarded, 0);
  CHECK(!C->mix.fresh);
  EQ(sc_mix(CW_AIR, CW_AIR), CRU_NOTHING);
  CHECK(C->mix.fresh);
  EQ(C->fails, 1);
  EQ(C->streak, 0);
  EQ(sc_mix(CW_AIR, CW_AIR), CRU_NOTHING);
  CHECK(!C->mix.fresh);
  EQ(C->fails, 1);
  EQ(sc_mix(CW_FIRE, CW_FIRE), CRU_NEW);
  awarded += 10;
  EQ(sc_mix(CW_SUN, CW_WATER), CRU_ROUTE);
  awarded += 3;
  EQ(C->mix.awarded, 3);
  EQ(C->routes, 1);
  EQ(C->mix.result, CW_STEAM);
  EQ(sc_mix(CW_WATER, CW_SUN), CRU_KNOWN);
  EQ(C->points, awarded + sc_feat_points());
  EQ(C->made, 7);
  CHECK(cru_tried(C, CW_SUN, CW_WATER));
  CHECK(!cru_tried(C, CW_EARTH, CW_AIR));
  CHECK(cru_tried(C, CW_AIR, CW_AIR));
  EQ(cru_mix_begin(C, CW_DRAGON, CW_FIRE, 0), CRU_DENY);
  EQ(cru_mix_finish(C), 0);
  EQ(C->made, 7);
  EQ(cru_play_value(C, CRU_PV_RECIPES), 3);
  EQ(cru_play_value(C, CRU_PV_DUDS), 1);
  EQ(cru_play_value(C, CRU_PV_PAIRS), 4);
  EQ(cru_play_value(C, CRU_PV_OWNED), C->found[0]);
  EQ(cru_play_value(C, CRU_PV_MIRRORS), 1);
  EQ(cru_play_uses(C, CW_FIRE), 3);
  EQ(cru_play_uses(C, CW_WATER), 4);
  EQ(cru_recipe(C, CW_WATER, CW_FIRE), CW_STEAM);
  EQ(cru_recipe(C, CW_AIR, CW_AIR), CRU_NONE);
  CHECK(cru_recipe_row(C, CW_FIRE, CW_WATER) != CRU_NONE);
  CHECK(cru_is_starter(C, CW_AIR));
  CHECK(!cru_is_starter(C, CW_SUN));
  sc_end(CRU_LAYOUT_32K);
}

static void s_bench(void) {
  crucible_cells k;
  sc_begin(2);
  boot_clean();
  cru_bench_cells(C, &k);
  EQ(k.kind[CRU_CA], CRU_K_EMPTY);
  EQ(k.kind[CRU_CB], CRU_K_EMPTY);
  EQ(k.kind[CRU_CR], CRU_K_QUESTION);
  EQ(k.kind[CRU_CL], CRU_K_ITEM);
  EQ(k.id[CRU_CL], CW_AIR);
  EQ(k.id[CRU_CF], CW_EARTH);
  EQ(k.id[CRU_CN], CW_WATER);
  EQ(k.message, CRU_MSG_NAME);
  EQ(k.sign, CW_EARTH);
  EQ(cru_bench_move(C, CRU_RIGHT), 1);
  EQ(C->focus, CW_WATER);
  EQ(cru_bench_a(C, 1), CRU_PICKED);
  EQ(C->slot_a, CW_WATER);
  cru_bench_cells(C, &k);
  EQ(k.kind[CRU_CA], CRU_K_ITEM);
  EQ(k.id[CRU_CA], CW_WATER);
  EQ(k.id[CRU_CB], CW_WATER);
  EQ(k.kind[CRU_CR], CRU_K_QUESTION);
  cru_bench_move(C, CRU_RIGHT);
  EQ(cru_bench_a(C, 1), CRU_NEW);
  EQ(C->slot_b, CW_FIRE);
  EQ(cru_mix_finish(C), 10);
  EQ(C->slot_a, CW_WATER);
  cru_reveal_close(C);
  EQ(C->focus, CW_STEAM);
  EQ(C->slot_a, CRU_NONE);
  EQ(C->slot_b, CRU_NONE);
  cru_bench_move(C, CRU_LEFT);
  EQ(C->focus, CW_AIR);
  cru_bench_move(C, CRU_LEFT);
  cru_bench_move(C, CRU_LEFT);
  EQ(C->focus, CW_WATER);
  cru_bench_a(C, 2);
  cru_bench_move(C, CRU_RIGHT);
  cru_bench_cells(C, &k);
  EQ(k.kind[CRU_CR], CRU_K_ITEM);
  EQ(k.id[CRU_CR], CW_STEAM);
  EQ(cru_bench_a(C, 2), CRU_KNOWN);
  EQ(cru_mix_finish(C), 0);
  EQ(C->focus, CW_FIRE);
  EQ(C->slot_a, CW_WATER);
  EQ(C->slot_b, CRU_NONE);
  EQ(C->message, CRU_MSG_RESULT);
  cru_bench_cells(C, &k);
  EQ(k.message, CRU_MSG_RESULT);
  EQ(k.sign, CW_STEAM);
  EQ(cru_bench_b(C), 1);
  EQ(C->slot_a, CRU_NONE);
  EQ(cru_bench_b(C), 0);
  cru_bench_move(C, CRU_RIGHT);
  EQ(C->focus, CW_AIR);
  cru_bench_a(C, 3);
  EQ(cru_bench_a(C, 3), CRU_NOTHING);
  cru_mix_finish(C);
  EQ(C->focus, CW_AIR);
  EQ(C->slot_a, CW_AIR);
  EQ(C->message, CRU_MSG_NO_RESULT);
  cru_bench_cells(C, &k);
  EQ(k.kind[CRU_CR], CRU_K_TRIED);
  EQ(k.sign, CRU_NONE);
  cru_bench_move(C, CRU_RIGHT);
  EQ(C->message, CRU_MSG_NAME);
  sc_end(CRU_LAYOUT_32K);
}

static void s_shelf(void) {
  crucible_cells k;
  sc_begin(3);
  boot_clean();
  sc_mix(CW_FIRE, CW_WATER);
  sc_mix(CW_EARTH, CW_WATER);
  sc_mix(CW_AIR, CW_WATER);
  EQ(cru_shelf_step(C, CW_AIR, 1), CW_STEAM);
  EQ(cru_shelf_step(C, CW_MUD, 1), CW_RAIN);
  EQ(cru_shelf_step(C, CW_RAIN, 1), CW_EARTH);
  EQ(cru_shelf_step(C, CW_EARTH, -1), CW_RAIN);
  EQ(cru_shelf_group(C, CW_WATER, 1), CW_STEAM);
  EQ(cru_shelf_group(C, CW_STEAM, 1), CW_RAIN);
  EQ(cru_shelf_group(C, CW_RAIN, 1), CW_EARTH);
  EQ(cru_shelf_group(C, CW_MUD, -1), CW_EARTH);
  EQ(cru_shelf_group(C, CW_EARTH, -1), CW_RAIN);
  EQ(cru_shelf_group(C, CW_RAIN, -1), CW_STEAM);
  C->focus = CW_EARTH;
  EQ(cru_filter_found(C, 8), 2);
  EQ(cru_filter_apply(C, 8), 1);
  cru_filter_close(C);
  EQ(C->focus, CW_FIRE);
  cru_bench_cells(C, &k);
  EQ(k.id[CRU_CL], CW_STEAM);
  EQ(k.kind[CRU_CN], CRU_K_BLANK);
  EQ(cru_filter_found(C, 14), 1);
  cru_filter_apply(C, 14);
  cru_filter_close(C);
  cru_bench_cells(C, &k);
  EQ(C->focus, CW_FIRE);
  EQ(k.kind[CRU_CL], CRU_K_BLANK);
  EQ(k.kind[CRU_CN], CRU_K_BLANK);
  EQ(cru_bench_move(C, CRU_RIGHT), 0);
  EQ(cru_filter_apply(C, 0), 1);
  cru_filter_close(C);
  EQ(C->focus, CW_FIRE);
  sc_end(CRU_LAYOUT_32K);
}

static void check_counts(void) {
  uint8_t f;
  uint16_t i, found, total;
  for (f = 0; f < CRU_FILTERS; f++) {
    found = total = 0;
    for (i = 0; i < CW_ITEMS; i++)
      if (cru_filter_match(C, f, i)) {
        total++;
        if (cru_owned(C, i)) found++;
      }
    EQ(cru_filter_found(C, f), found);
    EQ(cru_filter_total(C, f), total);
  }
}
static void s_counts(void) {
  uint16_t sum = 0;
  uint8_t f, e, seq;
  sc_begin(4);
  e = boot_clean();
  check_counts();
  for (f = 1; f <= 7u; f++) sum = (uint16_t)(sum + cru_filter_total(C, f));
  EQ(sum, CW_ITEMS);
  sc_mix(CW_FIRE, CW_WATER);
  sc_mix(CW_EARTH, CW_FIRE);
  sc_mix(CW_LAVA, CW_WATER);
  sc_mix(CW_EARTH, CW_RAIN);
  sc_mix(CW_AIR, CW_WATER);
  sc_mix(CW_EARTH, CW_RAIN);
  check_counts();
  EQ(cru_filter_found(C, 9), 0);
  seq = (uint8_t)C->sequence;
  EQ(cru_filter_apply(C, 9), 0);
  EQ(C->filter, 0);
  EQ((uint8_t)C->sequence, seq);
  EQ(cru_filter_apply(C, 10), 1);
  EQ(C->filter, 10);
  EQ((uint8_t)C->sequence, (uint8_t)(seq + 1u));
  EQ(cru_filter_apply(C, 10), 1);
  EQ((uint8_t)C->sequence, (uint8_t)(seq + 1u));
  sc_boot(CRU_LAYOUT_32K, e);
  EQ(C->filter, 10);
  check_counts();
  sc_end(CRU_LAYOUT_32K);
}

static void s_feats(void) {
  uint16_t a, b, awarded = 0;
  uint8_t t, r, k, g, x, y, earned = 0, sum = 0;
  sc_begin(5);
  boot_clean();
  EQ(sc_mix(CW_FIRE, CW_WATER), CRU_NEW);
  EQ(sc_mix(CW_EARTH, CW_WATER), CRU_NEW);
  EQ(cru_feat_tier(C, 4), 0);
  sc_drain();
  EQ(sc_mix(CW_EARTH, CW_FIRE), CRU_NEW);
  awarded += 30;
  EQ(cru_feat_tier(C, 4), 1);
  CHECK(sc_has_toast((4u << 2) | 0u));
  EQ(cru_feat_tier(C, 20), 1);
  sc_drain();
  EQ(sc_mix(CW_AIR, CW_EARTH), CRU_NEW);
  awarded += 10;
  EQ(C->found[0], 8);
  EQ(cru_feat_tier(C, 0), 1);
  CHECK(sc_has_toast(0));
  CHECK(sc_has_toast(CRU_TOAST_TITLE | 28u));
  CHECK(sc_has_toast(CRU_TOAST_TITLE | 0u));
  EQ(sc_mix(CW_WATER, CW_WATER), CRU_NEW);
  awarded += 10;
  EQ(cru_feat_tier(C, 1), 1);
  EQ(cru_feat_tier(C, 8), 1);
  EQ(cru_feat_value(C, 8), 1);
  EQ(cru_feat_threshold(C, 8, 3), 4);
  EQ(cru_feat_threshold(C, 0, 3), CW_ITEMS);
  EQ(cru_feat_threshold(C, 1, 3), CW_RECIPES);
  EQ(sc_mix(CW_AIR, CW_AIR), CRU_NOTHING);
  EQ(C->streak, 0);
  EQ(C->nofail, 0);
  EQ(C->best_streak, 5);
  for (a = 0; a < CW_ITEMS && C->fails < 10u; a++)
    for (b = a; b < CW_ITEMS && C->fails < 10u; b++)
      if (cru_owned(C, a) && cru_owned(C, b) && cru_recipe(C, a, b) == CRU_NONE && !cru_tried(C, a, b)) {
        sc_drain();
        EQ(sc_mix(a, b), CRU_NOTHING);
      }
  EQ(C->fails, 10);
  EQ(cru_play_value(C, CRU_PV_PAIRS), 15);
  EQ(cru_feat_tier(C, 7), 1);
  EQ(cru_feat_tier(C, 6), 1);
  CHECK(sc_has_toast(6u << 2));
  CHECK(sc_has_toast(7u << 2));
  CHECK(sc_has_toast(CRU_TOAST_TITLE | 14u));
  CHECK(sc_has_toast(CRU_TOAST_TITLE | 15u));
  CHECK(sc_has_toast(CRU_TOAST_TITLE | 7u));
  CHECK(sc_has_toast(CRU_TOAST_TITLE | 8u));
  for (t = 0; t < CRU_TITLES; t++) {
    r = 0;
    k = t;
    while (k >= 7u) {
      k = (uint8_t)(k - 7u);
      r++;
    }
    x = cru_feat_tier(C, cru_title_feat(C, t, 0));
    y = cru_feat_tier(C, cru_title_feat(C, t, 1));
    g = x;
    if (y < g) g = y;
    EQ(cru_title_grade(C, t), g);
    EQ(cru_title_needs(C, t), (uint8_t)((x ? 0u : 1u) | (y ? 0u : 2u)));
    if (g) earned++;
    (void)r;
  }
  CHECK(earned >= 8u);
  EQ(cru_titles_earned(C), earned);
  EQ(cru_title_needs(C, 41), 3);
  EQ(cru_title_needs(C, 35), 1);
  EQ(cru_title_needs(C, 6), 2);
  EQ(cru_title_feat(C, 1, 0), 2);
  EQ(cru_title_feat(C, 1, 1), 11);
  EQ(cru_title_wear(C, 14), 1);
  EQ(C->title, 15);
  EQ(cru_title_wear(C, 7), 1);
  EQ(C->title, 8);
  EQ(cru_title_wear(C, 7), 2);
  EQ(C->title, 0);
  EQ(cru_title_wear(C, 41), 0);
  EQ(C->title, 0);
  EQ(C->points, awarded + sc_feat_points());
  for (t = 0; t < CRU_FEATS; t++) sum = (uint8_t)(sum + cru_feat_tier(C, t));
  EQ(cru_feats_total(C), sum);
  cru_title_name(14, text);
  CHECK(streq(text, "CURIOUS ADEPT"));
  cru_feat_name(6, text);
  CHECK(streq(text, "EXPLORER"));
  cru_feat_desc(19, text);
  CHECK(streq(text, "5 FINDS FROM START"));
  cru_tag_name(11, text);
  CHECK(streq(text, "MAGIC"));
  cru_title_part(1, 3, text);
  CHECK(streq(text, "SPARKSMITH"));
  cru_title_part(0, 5, text);
  CHECK(streq(text, "SOCIAL"));
  EQ(cru_feat_unit(C, 2), CRU_UNIT_FRAMES);
  EQ(cru_feat_unit(C, 12), CRU_UNIT_PERCENT);
  EQ(cru_feat_unit(C, 18), CRU_UNIT_MINUTES);
  EQ(cru_feat_unit(C, 19), CRU_UNIT_SECONDS);
  EQ(cru_feat_unit(C, 0), CRU_UNIT_COUNT);
  EQ(cru_feat_lower(C, 18), 1);
  EQ(cru_feat_lower(C, 0), 0);
  sc_end(CRU_LAYOUT_32K);
}

void scen_run_a(void) SCEN_FN {
  s_fresh();
  s_outcomes();
  s_bench();
  s_shelf();
  s_counts();
  s_feats();
}
