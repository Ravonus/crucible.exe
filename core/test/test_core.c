/* Realistic sessions over the real catalogue: outcomes and scoring, the bench and shelf rules, filters, feats
 * reaching tiers, titles, clocks, the book, the leaderboard and the stats page. */
#include <string.h>
#include "harness.h"

static mem_store m, m2;

static void fresh_save(void) {
  crucible_core c, d;
  char name[CRU_NAME + 1];
  uint8_t e = boot_clean(&c, &m, CRU_LAYOUT_32K), i, gilded_e = 0;
  EQ(c.found[0], 4);
  CHECK(cru_owned(&c, id_of("EARTH")) && cru_owned(&c, id_of("WATER")) && cru_owned(&c, id_of("FIRE")) &&
        cru_owned(&c, id_of("AIR")));
  CHECK(!cru_owned(&c, id_of("STEAM")));
  EQ(c.session, 1);
  EQ(c.points, 0);
  EQ(c.focus, id_of("EARTH"));
  EQ(c.slot_a, CRU_NONE);
  cru_get_name(&c, name);
  CHECK(!strcmp(name, "YOU"));
  EQ(cru_play_value(&c, CRU_PV_OWNED), 4);
  EQ(cru_play_value(&c, CRU_PV_RECIPES), 0);
  EQ(c.total[0], 57);
  EQ(c.found[1], 4);
  EQ(c.total[1], 4);
  EQ(cru_toast_peek(&c), CRU_TOAST_NONE);
  EQ(boot(&d, &m, &world, CRU_LAYOUT_32K, e), CRU_LOAD_V4);
  EQ(d.session, 1); /* a run is kept only once it saves (as the cartridge): nothing was saved yet */
  mix(&d, "FIRE", "WATER");
  EQ(boot(&d, &m, &world, CRU_LAYOUT_32K, e), CRU_LOAD_V4);
  EQ(d.session, 2);
  /* a starter that rolled gilded earns GILDED I at boot (+5) and puts this run on the board */
  for (i = 0; i < 255u; i++) {
    ms_reset(&m2);
    boot(&d, &m2, &world, CRU_LAYOUT_32K, i);
    if (d.gilded) {
      gilded_e = 1;
      break;
    }
  }
  CHECK(gilded_e);
  EQ(cru_feat_tier(&d, 9), d.gilded >= 2 ? 2 : 1);
  CHECK(has_toast(&d, (uint8_t)((9u << 2) | 0u)));
  EQ(d.points, feat_points(&d));
  EQ(cru_board_current(&d), 0);
  EQ(cru_board_run_points(&d), d.points);
}

static void outcomes_and_scoring(void) {
  crucible_core c;
  long awarded = 0;
  boot_clean(&c, &m, CRU_LAYOUT_32K);
  EQ(mix(&c, "FIRE", "WATER"), CRU_NEW);
  awarded += 10;
  EQ(c.mix.awarded, 10);
  CHECK(c.mix.fresh);
  CHECK(cru_owned(&c, id_of("STEAM")));
  EQ(c.found[0], 5);
  EQ(c.points, awarded + feat_points(&c));
  EQ(mix(&c, "WATER", "FIRE"), CRU_KNOWN); /* unordered, already tried: resolves on the bench, scores 0 */
  EQ(c.mix.awarded, 0);
  CHECK(!c.mix.fresh);
  EQ(mix(&c, "AIR", "AIR"), CRU_NOTHING);
  EQ(c.mix.awarded, 0);
  CHECK(c.mix.fresh);
  EQ(c.fails, 1);
  EQ(c.streak, 0);
  EQ(mix(&c, "AIR", "AIR"), CRU_NOTHING); /* a repeat: never a fail again */
  CHECK(!c.mix.fresh);
  EQ(c.fails, 1);
  EQ(mix(&c, "FIRE", "FIRE"), CRU_NEW);
  awarded += 10;
  EQ(mix(&c, "SUN", "WATER"), CRU_ROUTE);
  awarded += 3; /* a new recipe to an owned item */
  EQ(c.mix.awarded, 3);
  EQ(c.routes, 1);
  EQ(c.mix.result, id_of("STEAM"));
  EQ(mix(&c, "WATER", "SUN"), CRU_KNOWN);
  EQ(c.points, awarded + feat_points(&c));
  EQ(c.made, 7);
  CHECK(cru_tried(&c, id_of("SUN"), id_of("WATER")));
  CHECK(!cru_tried(&c, id_of("EARTH"), id_of("AIR")));
  CHECK(cru_tried(&c, id_of("AIR"), id_of("AIR")));
  /* an ingredient that is not owned is denied and changes nothing */
  EQ(cru_mix_begin(&c, id_of("DRAGON"), id_of("FIRE"), 0), CRU_DENY);
  EQ(cru_mix_finish(&c), 0);
  EQ(c.made, 7);
  EQ(cru_play_value(&c, CRU_PV_RECIPES), 3);
  EQ(cru_play_value(&c, CRU_PV_DUDS), 1);
  EQ(cru_play_value(&c, CRU_PV_PAIRS), 4);
  EQ(cru_play_value(&c, CRU_PV_OWNED), c.found[0]);
  EQ(cru_play_value(&c, CRU_PV_MIRRORS), 1); /* FIRE+FIRE */
  EQ(cru_play_uses(&c, id_of("FIRE")), 3); /* FIRE+WATER twice, FIRE+FIRE once (one use per distinct item) */
  EQ(cru_play_uses(&c, id_of("WATER")), 4);
}

static void bench_flow(void) {
  crucible_core c;
  crucible_cells k;
  uint16_t E = id_of("EARTH"), W = id_of("WATER"), F = id_of("FIRE"), A = id_of("AIR"), S = id_of("STEAM");
  boot_clean(&c, &m, CRU_LAYOUT_32K);
  cru_bench_cells(&c, &k);
  EQ(k.kind[CRU_CA], CRU_K_EMPTY);
  EQ(k.kind[CRU_CB], CRU_K_EMPTY);
  EQ(k.kind[CRU_CR], CRU_K_QUESTION);
  EQ(k.kind[CRU_CL], CRU_K_ITEM);
  EQ(k.id[CRU_CL], A); /* wraps */
  EQ(k.id[CRU_CF], E);
  EQ(k.id[CRU_CN], W);
  EQ(k.message, CRU_MSG_NAME);
  EQ(k.sign, E);
  EQ(cru_bench_move(&c, CRU_RIGHT), 1);
  EQ(c.focus, W);
  EQ(cru_bench_a(&c, 1), CRU_PICKED);
  EQ(c.slot_a, W);
  EQ(c.focus, W);
  cru_bench_cells(&c, &k);
  EQ(k.kind[CRU_CA], CRU_K_ITEM);
  EQ(k.id[CRU_CA], W);
  EQ(k.id[CRU_CB], W);
  EQ(k.kind[CRU_CR], CRU_K_QUESTION);
  cru_bench_move(&c, CRU_RIGHT);
  EQ(cru_bench_a(&c, 1), CRU_NEW);
  EQ(c.slot_b, F);
  EQ(cru_mix_finish(&c), 10);
  EQ(c.slot_a, W); /* the reveal is on screen */
  cru_reveal_close(&c);
  EQ(c.focus, S);
  EQ(c.slot_a, CRU_NONE);
  EQ(c.slot_b, CRU_NONE);
  /* KNOWN: WATER then FIRE again resolves on the bench */
  cru_bench_move(&c, CRU_LEFT);
  EQ(c.focus, A);
  cru_bench_move(&c, CRU_LEFT);
  cru_bench_move(&c, CRU_LEFT);
  EQ(c.focus, W);
  cru_bench_a(&c, 2);
  cru_bench_move(&c, CRU_RIGHT);
  cru_bench_cells(&c, &k);
  EQ(k.kind[CRU_CR], CRU_K_ITEM);
  EQ(k.id[CRU_CR], S); /* tried with a recipe: preview of the result */
  EQ(cru_bench_a(&c, 2), CRU_KNOWN);
  EQ(cru_mix_finish(&c), 0);
  EQ(c.focus, F);
  EQ(c.slot_a, W);
  EQ(c.slot_b, CRU_NONE);
  EQ(c.message, CRU_MSG_RESULT);
  cru_bench_cells(&c, &k);
  EQ(k.message, CRU_MSG_RESULT);
  EQ(k.sign, S);
  /* NOTHING: AIR + AIR; slot A stays, x on the result */
  EQ(cru_bench_b(&c), 1);
  EQ(c.slot_a, CRU_NONE);
  EQ(cru_bench_b(&c), 0);
  cru_bench_move(&c, CRU_RIGHT);
  EQ(c.focus, A);
  cru_bench_a(&c, 3);
  EQ(cru_bench_a(&c, 3), CRU_NOTHING);
  cru_mix_finish(&c);
  EQ(c.focus, A);
  EQ(c.slot_a, A);
  EQ(c.message, CRU_MSG_NO_RESULT);
  cru_bench_cells(&c, &k);
  EQ(k.kind[CRU_CR], CRU_K_TRIED);
  EQ(k.sign, CRU_NONE);
  cru_bench_move(&c, CRU_RIGHT); /* any focus change resets the sign */
  EQ(c.message, CRU_MSG_NAME);
}

static void shelf_navigation(void) {
  crucible_core c;
  crucible_cells k;
  uint16_t E = id_of("EARTH"), W = id_of("WATER"), F = id_of("FIRE"), A = id_of("AIR"), S = id_of("STEAM"),
           M = id_of("MUD"), R = id_of("RAIN");
  boot_clean(&c, &m, CRU_LAYOUT_32K);
  mix(&c, "FIRE", "WATER");
  mix(&c, "EARTH", "WATER");
  mix(&c, "AIR", "WATER");
  EQ(cru_shelf_step(&c, A, 1), S);
  EQ(cru_shelf_step(&c, M, 1), R);
  EQ(cru_shelf_step(&c, R, 1), E); /* wraps */
  EQ(cru_shelf_step(&c, E, -1), R);
  EQ(cru_shelf_group(&c, W, 1), S); /* first owned of the next group */
  EQ(cru_shelf_group(&c, S, 1), R);
  EQ(cru_shelf_group(&c, R, 1), E);
  EQ(cru_shelf_group(&c, M, -1), E); /* first owned of the previous group */
  EQ(cru_shelf_group(&c, E, -1), R);
  EQ(cru_shelf_group(&c, R, -1), S);
  /* a filter narrows the shelf; closing the filter refocuses a hidden focus */
  c.focus = E;
  EQ(cru_filter_found(&c, 8), 2); /* HOT: FIRE, STEAM */
  EQ(cru_filter_apply(&c, 8), 1);
  cru_filter_close(&c);
  EQ(c.focus, F);
  cru_bench_cells(&c, &k);
  EQ(k.id[CRU_CL], S);
  EQ(k.kind[CRU_CN], CRU_K_BLANK); /* the next one equals the previous one */
  EQ(cru_filter_found(&c, 14), 1); /* GLOWS: FIRE only */
  cru_filter_apply(&c, 14);
  cru_filter_close(&c);
  cru_bench_cells(&c, &k);
  EQ(c.focus, F);
  EQ(k.kind[CRU_CL], CRU_K_BLANK);
  EQ(k.kind[CRU_CN], CRU_K_BLANK);
  EQ(cru_bench_move(&c, CRU_RIGHT), 0);
  EQ(cru_filter_apply(&c, 0), 1);
  cru_filter_close(&c);
  EQ(c.focus, F);
}

/* a filter change after "=RESULT" moves the focus and must reset the sign; a stale "=RESULT" whose pair makes nothing
 * shows the plain name (never the name of id 0xFFFF) */
static void sign_after_filter(void) {
  crucible_core c;
  crucible_cells k;
  uint16_t i, odd = CRU_NONE;
  boot_clean(&c, &m, CRU_LAYOUT_32K);
  mix(&c, "FIRE", "WATER");
  mix(&c, "EARTH", "WATER");
  c.focus = id_of("WATER");
  cru_bench_a(&c, 1);
  cru_bench_move(&c, CRU_RIGHT);
  EQ(c.focus, id_of("FIRE"));
  EQ(cru_bench_a(&c, 1), CRU_KNOWN);
  cru_mix_finish(&c);
  EQ(c.message, CRU_MSG_RESULT);
  EQ(c.slot_a, id_of("WATER"));
  EQ(cru_filter_apply(&c, 10), 1); /* WET hides FIRE */
  cru_filter_close(&c);
  CHECK(c.focus != id_of("FIRE"));
  EQ(c.message, CRU_MSG_NAME);
  cru_bench_cells(&c, &k);
  EQ(k.message, CRU_MSG_NAME);
  EQ(k.sign, c.focus);
  /* a RESULT message left on a focus whose pair with slot A makes nothing */
  for (i = 0; i < c.t->items && odd == CRU_NONE; i++)
    if (cru_owned(&c, i) && cru_filter_match(&c, c.filter, i) && cru_recipe(&c, c.slot_a, i) == CRU_NONE) odd = i;
  CHECK(odd != CRU_NONE);
  c.focus = odd;
  c.message = CRU_MSG_RESULT;
  cru_bench_cells(&c, &k);
  EQ(k.message, CRU_MSG_NAME);
  EQ(k.sign, odd);
}

/* found/total per filter equal a brute-force count, after play and after a reboot */
static void check_counts(crucible_core *c) {
  uint8_t f;
  uint16_t i, found, total;
  for (f = 0; f < CRU_FILTERS; f++) {
    found = total = 0;
    for (i = 0; i < c->t->items; i++)
      if (cru_filter_match(c, f, i)) {
        total++;
        if (cru_owned(c, i)) found++;
      }
    EQ(cru_filter_found(c, f), found);
    EQ(cru_filter_total(c, f), total);
  }
}
static void filter_counts(void) {
  crucible_core c, d;
  uint16_t sum = 0;
  uint8_t f, e = boot_clean(&c, &m, CRU_LAYOUT_32K), seq;
  check_counts(&c);
  for (f = 1; f <= 7u; f++) sum = (uint16_t)(sum + cru_filter_total(&c, f));
  EQ(sum, 57);
  mix(&c, "FIRE", "WATER");
  mix(&c, "EARTH", "FIRE");
  mix(&c, "LAVA", "WATER");
  mix(&c, "EARTH", "RAIN");
  mix(&c, "AIR", "WATER");
  mix(&c, "EARTH", "RAIN");
  check_counts(&c);
  EQ(cru_filter_found(&c, 9), 0); /* COLD: nothing yet */
  seq = (uint8_t)c.sequence;
  EQ(cru_filter_apply(&c, 9), 0); /* denied, unchanged, not saved */
  EQ(c.filter, 0);
  EQ((uint8_t)c.sequence, seq);
  EQ(cru_filter_apply(&c, 10), 1); /* WET */
  EQ(c.filter, 10);
  EQ((uint8_t)c.sequence, (uint8_t)(seq + 1u));
  EQ(cru_filter_apply(&c, 10), 1);
  EQ((uint8_t)c.sequence, (uint8_t)(seq + 1u)); /* unchanged: no save */
  boot(&d, &m, &world, CRU_LAYOUT_32K, e);
  EQ(d.filter, 10);
  check_counts(&d);
}

static void feats_and_titles(void) {
  crucible_core c;
  uint16_t a, b;
  uint8_t t, r, k, g, earned = 0, sum = 0;
  long awarded = 0;
  char s[20];
  boot_clean(&c, &m, CRU_LAYOUT_32K);
  /* HOT STREAK I: three fresh pairs that make something, no miss */
  EQ(mix(&c, "FIRE", "WATER"), CRU_NEW);
  EQ(mix(&c, "EARTH", "WATER"), CRU_NEW);
  EQ(cru_feat_tier(&c, 4), 0);
  drain_toasts(&c);
  EQ(mix(&c, "EARTH", "FIRE"), CRU_NEW);
  awarded += 30;
  EQ(cru_feat_tier(&c, 4), 1);
  CHECK(has_toast(&c, (4u << 2) | 0u));
  EQ(cru_feat_tier(&c, 20), 1); /* CLEAN RUN I: three new finds, no fresh fail */
  /* COLLECTOR I at 8 found; it opens the ADEPT column for every trait feat already earned */
  drain_toasts(&c);
  EQ(mix(&c, "AIR", "EARTH"), CRU_NEW);
  awarded += 10;
  EQ(c.found[0], 8);
  EQ(cru_feat_tier(&c, 0), 1);
  CHECK(has_toast(&c, 0));
  CHECK(has_toast(&c, CRU_TOAST_TITLE | 28u)); /* FIERCE ADEPT (HOT STREAK) */
  CHECK(has_toast(&c, CRU_TOAST_TITLE | 0u)); /* SWIFT ADEPT (QUICK HAND: finds a second apart) */
  /* RECIPES I at 5 distinct recipes, MIRROR I with a same+same recipe */
  EQ(mix(&c, "WATER", "WATER"), CRU_NEW);
  awarded += 10;
  EQ(cru_feat_tier(&c, 1), 1);
  EQ(cru_feat_tier(&c, 8), 1);
  EQ(cru_feat_value(&c, 8), 1);
  EQ(cru_feat_threshold(&c, 8, 3), 4);
  EQ(cru_feat_threshold(&c, 0, 3), 57); /* ALL clamps to the catalogue */
  EQ(cru_feat_threshold(&c, 1, 3), 70);
  /* the streak breaks on a fresh fail */
  EQ(mix(&c, "AIR", "AIR"), CRU_NOTHING);
  EQ(c.streak, 0);
  EQ(c.nofail, 0);
  EQ(c.best_streak, 5);
  /* STUBBORN I (ten fresh fails) and EXPLORER I (fifteen different pairs) land on the same mix */
  for (a = 0; a < c.t->items && c.fails < 10u; a++)
    for (b = a; b < c.t->items && c.fails < 10u; b++)
      if (cru_owned(&c, a) && cru_owned(&c, b) && cru_recipe(&c, a, b) == CRU_NONE && !cru_tried(&c, a, b)) {
        drain_toasts(&c);
        EQ(mixi(&c, a, b), CRU_NOTHING);
      }
  EQ(c.fails, 10);
  EQ(cru_play_value(&c, CRU_PV_PAIRS), 15);
  EQ(cru_feat_tier(&c, 7), 1);
  EQ(cru_feat_tier(&c, 6), 1);
  CHECK(has_toast(&c, 6u << 2));
  CHECK(has_toast(&c, 7u << 2));
  CHECK(has_toast(&c, CRU_TOAST_TITLE | 14u)); /* CURIOUS ADEPT */
  CHECK(has_toast(&c, CRU_TOAST_TITLE | 15u)); /* CURIOUS MASON (GEOLOGIST: 4 of 11 MATTER = 36 %) */
  CHECK(has_toast(&c, CRU_TOAST_TITLE | 7u)); /* DOGGED ADEPT */
  CHECK(has_toast(&c, CRU_TOAST_TITLE | 8u)); /* DOGGED MASON */
  CHECK(c.toast_n >= 6u); /* (and HOARDER II on the same mix) */
  /* grades and needs over the whole matrix */
  for (t = 0; t < CRU_TITLES; t++) {
    r = (uint8_t)(t / 7u);
    k = (uint8_t)(t % 7u);
    a = c.feats[c.t->rules->trait_feat[r]];
    b = c.feats[c.t->rules->domain_feat[k]];
    g = (uint8_t)(a < b ? a : b);
    EQ(cru_title_grade(&c, t), g);
    EQ(cru_title_needs(&c, t), (a ? 0 : 1) | (b ? 0 : 2));
    EQ(cru_title_feat(&c, t, 0), c.t->rules->trait_feat[r]);
    EQ(cru_title_feat(&c, t, 1), c.t->rules->domain_feat[k]);
    if (g) earned++;
  }
  CHECK(earned >= 8u); /* SWIFT DOGGED CURIOUS FIERCE (GILDED if OCEAN rolled gilded) x ADEPT MASON */
  EQ(cru_titles_earned(&c), earned);
  EQ(cru_title_needs(&c, 41), 3); /* SOCIAL VOYAGER: NEEDS TWO AWARDS */
  EQ(cru_title_needs(&c, 35), 1); /* SOCIAL ADEPT: NEEDS LINKED */
  EQ(cru_title_needs(&c, 6), 2); /* SWIFT VOYAGER: NEEDS VOYAGER */
  /* wear / take off / deny */
  EQ(cru_title_wear(&c, 14), 1);
  EQ(c.title, 15);
  EQ(cru_title_wear(&c, 7), 1);
  EQ(c.title, 8);
  EQ(cru_title_wear(&c, 7), 2);
  EQ(c.title, 0);
  EQ(cru_title_wear(&c, 41), 0);
  EQ(c.title, 0);
  EQ(c.points, awarded + feat_points(&c));
  for (t = 0; t < CRU_FEATS; t++) sum = (uint8_t)(sum + c.feats[t]);
  EQ(cru_feats_total(&c), sum);
  cru_title_name(14, s);
  CHECK(!strcmp(s, "CURIOUS ADEPT"));
  cru_feat_name(6, s);
  CHECK(!strcmp(s, "EXPLORER"));
  cru_feat_desc(19, s);
  CHECK(!strcmp(s, "5 FINDS FROM START"));
  cru_tag_name(11, s);
  CHECK(!strcmp(s, "MAGIC"));
  cru_title_part(1, 3, s);
  CHECK(!strcmp(s, "SPARKSMITH"));
  cru_title_part(0, 5, s);
  CHECK(!strcmp(s, "SOCIAL"));
}

static void chain_and_categories(void) {
  crucible_core c;
  boot_clean(&c, &m, CRU_LAYOUT_32K);
  /* CHAIN: each find built on the newest one */
  mix(&c, "FIRE", "WATER"); /* STEAM, chain 1 */
  mix(&c, "STEAM", "AIR");
  EQ(c.chain, 2); /* CLOUD */
  EQ(cru_feat_tier(&c, 5), 1);
  mix(&c, "CLOUD", "WATER");
  EQ(c.chain, 3); /* RAIN */
  mix(&c, "RAIN", "EARTH");
  EQ(c.chain, 4); /* PLANT */
  mix(&c, "PLANT", "PLANT");
  EQ(c.chain, 5); /* TREE */
  mix(&c, "TREE", "FIRE");
  EQ(c.chain, 6); /* CHARCOAL */
  EQ(cru_feat_tier(&c, 5), 4);
  EQ(c.best_chain, 6);
  mix(&c, "EARTH", "WATER");
  EQ(c.chain, 1); /* MUD: not built on CHARCOAL */
  EQ(c.last_new, id_of("MUD"));
  /* category feats: percent of a category found, floor(100 f / n) */
  EQ(c.found[3], 2);
  EQ(c.total[3], 4); /* WEATHER: CLOUD, RAIN */
  EQ(cru_feat_value(&c, 12), 50); /* STORMCALL */
  EQ(cru_feat_tier(&c, 12), 2);
  EQ(cru_feat_unit(&c, 12), CRU_UNIT_PERCENT);
  EQ(c.total[2], 11);
  EQ(cru_feat_value(&c, 11), 27); /* GEOLOGIST: MATTER 3 of 11 (STEAM, CHARCOAL, MUD) -> 27 % */
  EQ(cru_feat_tier(&c, 11), 1);
  mix(&c, "EARTH", "FIRE"); /* LAVA: 4/11 = 36 % */
  EQ(cru_feat_value(&c, 11), 36);
  mix(&c, "AIR", "EARTH");
  mix(&c, "LAVA", "WATER");
  mix(&c, "STONE", "AIR"); /* DUST STONE SAND: 7/11 = 63 % */
  EQ(cru_feat_value(&c, 11), 63);
  EQ(cru_feat_tier(&c, 11), 2); /* 50 <= 63 < 75 */
}

static void clock_feats(void) {
  crucible_core c;
  uint8_t i, seq;
  boot_clean(&c, &m, CRU_LAYOUT_32K);
  /* OPENING counts seconds from power-on to the fifth find */
  tick_frames(&c, 600); /* 10 s on the title card */
  mix(&c, "FIRE", "WATER");
  tick_frames(&c, 540); /* QUICK HAND: 540 + the merge's 60 = 600 frames between finds */
  mix(&c, "EARTH", "WATER");
  EQ(c.best_gap, 600);
  EQ(cru_feat_tier(&c, 2), 3); /* <= 60, 30, 15 s */
  EQ(cru_feat_unit(&c, 2), CRU_UNIT_FRAMES);
  EQ(cru_feat_lower(&c, 2), 1);
  mix(&c, "EARTH", "FIRE");
  mix(&c, "AIR", "EARTH");
  mix(&c, "AIR", "WATER");
  EQ(c.best_first, 10 + 1 + 9 + 1 + 1 + 1 + 1); /* 10 s, then 60+540+60+60+60 frames */
  EQ(cru_feat_tier(&c, 19), 4); /* <= 90 s */
  /* FLURRY: five finds within 60 s (780 frames from the first to the fifth) */
  EQ(c.best_flurry, 5);
  EQ(cru_feat_tier(&c, 3), 3);
  tick_frames(&c, 3700); /* the window empties */
  mix(&c, "AIR", "FIRE");
  EQ(c.flurry_n, 1);
  /* SWIFT: fresh pairs in 60 s; a repeat does not count */
  EQ(c.best_swift, 5);
  mix(&c, "AIR", "FIRE");
  EQ(c.swift_n, 1);
  /* DEVOTED: minutes at the bench; a tier reached at a minute tick saves by itself */
  seq = (uint8_t)c.sequence;
  for (i = 0; i < 10u; i++) tick_frames(&c, 3600);
  CHECK(c.minutes >= 10u);
  EQ(cru_feat_tier(&c, 17), 1);
  CHECK((uint8_t)c.sequence != seq);
  /* SCHOLAR: book views count, checked at the next minute */
  cru_book_open(&c);
  for (i = 0; i < 9u; i++) cru_book_view(&c);
  EQ(c.book_views, 10);
  CHECK(c.flags & CRU_FLAG_BOOK_SEEN);
  EQ(cru_feat_tier(&c, 16), 0);
  tick_frames(&c, 3600);
  EQ(cru_feat_tier(&c, 16), 1);
  /* LINKED */
  cru_link(&c);
  EQ(cru_feat_tier(&c, 23), 1);
}

static void gilded_roll(void) {
  crucible_core c;
  uint16_t r, x;
  boot_clean(&c, &m, CRU_LAYOUT_32K);
  /* find an rng state whose next roll (entropy 0) is gilded, as the timer would one time in sixteen */
  for (r = 1; r; r++) {
    x = r;
    x ^= (uint16_t)(x << 7);
    x ^= (uint16_t)(x >> 9);
    x ^= (uint16_t)(x << 8);
    x = (uint16_t)(x + 1u);
    if (!(x & 15u)) break;
  }
  c.rng = r;
  EQ(cru_mix_begin(&c, id_of("FIRE"), id_of("WATER"), 0), CRU_NEW);
  EQ(c.mix.variant, 3);
  cru_mix_finish(&c);
  EQ(cru_variant(&c, id_of("STEAM")), 3);
  EQ(c.gilded, 1);
  EQ(cru_feat_tier(&c, 9), 1);
}

static void complete_run(void) {
  crucible_core c;
  uint16_t i, a, b, routes = 0, progress = 1;
  uint8_t t;
  boot_clean(&c, &m, CRU_LAYOUT_32K);
  while (progress) {
    progress = 0;
    for (i = 0; i < c.t->recipes; i++) {
      a = c.t->recipe_a[i];
      b = c.t->recipe_b[i];
      if (cru_owned(&c, a) && cru_owned(&c, b) && !cru_tried(&c, a, b)) {
        mixi(&c, a, b);
        progress = 1;
      }
    }
  }
  EQ(c.found[0], 57);
  EQ(cru_play_value(&c, CRU_PV_RECIPES), 70);
  for (i = 0; i < c.t->items; i++)
    if (c.t->route_first[i + 1u] > c.t->route_first[i]) routes++;
  EQ(cru_play_value(&c, CRU_PV_ROUTES), routes);
  EQ(cru_play_value(&c, CRU_PV_DEEP), 7);
  EQ(cru_play_value(&c, CRU_PV_MIRRORS), 6);
  EQ(c.mirror_total, 6);
  EQ(cru_feat_tier(&c, 0), 4);
  EQ(cru_feat_tier(&c, 1), 4);
  EQ(cru_feat_tier(&c, 8), 4);
  for (t = 10; t <= 15u; t++) EQ(cru_feat_tier(&c, t), 4); /* every category at 100 % */
  CHECK(c.complete >= 1u);
  EQ(cru_feat_tier(&c, 18), 4); /* SPEEDRUN: all found within 15 minutes */
  EQ(c.toast_n, 8); /* the toast queue holds eight */
  EQ(cru_title_grade(&c, 0),
     cru_feat_tier(&c, 2) < 4 ? cru_feat_tier(&c, 2) : 4); /* SWIFT ADEPT = min(QUICK HAND, 4) */
}

static void toast_queue(void) {
  crucible_core c;
  uint8_t first;
  boot_clean(&c, &m, CRU_LAYOUT_32K);
  mix(&c, "FIRE", "WATER");
  mix(&c, "EARTH", "WATER");
  mix(&c, "EARTH", "FIRE");
  CHECK(c.toast_n >= 2u);
  first = cru_toast_peek(&c);
  EQ(first, (2u << 2) | 0u); /* QUICK HAND I (two finds a second apart) is checked before FLURRY I */
  CHECK(has_toast(&c, (3u << 2) | 0u));
  cru_toast_done(&c);
  CHECK(cru_toast_peek(&c) != first);
  drain_toasts(&c);
  EQ(cru_toast_peek(&c), CRU_TOAST_NONE);
}

static void book(void) {
  crucible_core c;
  uint16_t rows[64], ab[2], n, i;
  boot_clean(&c, &m, CRU_LAYOUT_32K);
  mix(&c, "FIRE", "WATER");
  mix(&c, "EARTH", "FIRE");
  mix(&c, "LAVA", "WATER");
  n = cru_book_rows(&c, rows, 64);
  EQ(n, 57);
  for (i = 0; i < n; i++) EQ(rows[i], c.t->shelf[i]);
  EQ(cru_book_route(&c, id_of("STEAM"), ab), CRU_BOOK_RECIPE);
  EQ(ab[0], id_of("FIRE"));
  EQ(ab[1], id_of("WATER")); /* as authored: fire + water */
  EQ(cru_book_route(&c, id_of("STONE"), ab), CRU_BOOK_RECIPE);
  EQ(ab[0], id_of("LAVA"));
  EQ(ab[1], id_of("WATER")); /* authored lava + water (row (1,8)) */
  EQ(cru_book_route(&c, id_of("WATER"), ab), CRU_BOOK_ELEMENT);
  EQ(cru_book_route(&c, id_of("DRAGON"), ab), CRU_BOOK_UNKNOWN);
  EQ(cru_book_use(&c, id_of("DRAGON")), 0);
  cru_bench_a(&c, 0);
  EQ(cru_book_use(&c, id_of("STONE")), 1);
  EQ(c.focus, id_of("STONE"));
  EQ(c.slot_a, CRU_NONE);
  mix(&c, "AIR", "WATER");
  cru_filter_apply(&c, 3); /* WEATHER */
  n = cru_book_rows(&c, rows, 2);
  EQ(n, 4);
  EQ(rows[0], id_of("RAIN"));
  EQ(rows[1], id_of("SNOW"));
}

static void leaderboard(void) {
  crucible_core c;
  crucible_board_row r;
  uint8_t e = boot_clean(&c, &m, CRU_LAYOUT_32K), i, k;
  uint16_t p1, p2, prev, eighth;
  char name[CRU_NAME + 1];
  static const char *const finds[][2] = {{"FIRE", "FIRE"}, {"WATER", "WATER"}, {"AIR", "FIRE"},   {"EARTH", "EARTH"},
                                         {"STEAM", "AIR"}, {"PLANT", "PLANT"}, {"OCEAN", "FIRE"}, {"PLANT", "EARTH"},
                                         {"STONE", "AIR"}, {"STONE", "FIRE"}};
  mix(&c, "FIRE", "WATER");
  mix(&c, "EARTH", "WATER");
  p1 = c.points;
  EQ(cru_board_current(&c), 0);
  EQ(cru_board_run_points(&c), p1);
  cru_board_row(&c, 0, &r);
  CHECK(!strcmp(r.name, "YOU"));
  EQ(r.points, p1);
  EQ(r.finds, 2);
  EQ(r.session, 1);
  CHECK(r.mine);
  /* run 2: a longer name, more points, a title worn: it goes first */
  boot(&c, &m, &world, CRU_LAYOUT_32K, e);
  EQ(c.session, 2);
  cru_board_row(&c, 0, &r);
  CHECK(!r.mine);
  cru_set_name(&c, "ALCHEMISTS");
  cru_get_name(&c, name);
  CHECK(!strcmp(name, "ALCHEMIS"));
  mix(&c, "EARTH", "FIRE");
  mix(&c, "AIR", "EARTH");
  mix(&c, "LAVA", "WATER");
  mix(&c, "AIR", "WATER");
  mix(&c, "EARTH", "RAIN");
  EQ(cru_title_wear(&c, 0), 1);
  p2 = cru_board_run_points(&c);
  CHECK(p2 > p1);
  EQ(cru_board_current(&c), 0);
  cru_board_row(&c, 0, &r);
  CHECK(!strcmp(r.name, "ALCHEMIS"));
  EQ(r.points, p2);
  EQ(r.session, 2);
  EQ(r.title, 1);
  EQ(r.finds, 5);
  cru_board_row(&c, 1, &r);
  CHECK(!strcmp(r.name, "YOU"));
  EQ(r.points, p1);
  /* ten more runs of one find each: the board keeps the eight best, in order */
  for (k = 0; k < 10u; k++) {
    boot(&c, &m, &world, CRU_LAYOUT_32K, e);
    EQ(mix(&c, finds[k][0], finds[k][1]), CRU_NEW);
    i = cru_board_current(&c);
    CHECK(i != CRU_NONE8 || k >= 6u);
  }
  prev = 0xffffu;
  for (i = 0; i < CRU_BOARD_ROWS; i++) {
    cru_board_row(&c, i, &r);
    CHECK(r.points <= prev && r.points > 0);
    prev = r.points;
  }
  eighth = prev;
  CHECK(eighth >= 10u);
  /* a run that does not beat the eighth row stays off (a +3 route) */
  boot(&c, &m, &world, CRU_LAYOUT_32K, e);
  EQ(mix(&c, "SUN", "WATER"), CRU_ROUTE);
  EQ(cru_board_run_points(&c), 3);
  EQ(cru_board_current(&c), CRU_NONE8);
  cru_board_row(&c, 7, &r);
  EQ(r.points, eighth);
}

static void stats_rows(void) {
  crucible_core c;
  crucible_stat s;
  boot_clean(&c, &m, CRU_LAYOUT_32K);
  cru_stat(&c, 4, &s);
  EQ(s.known, 0); /* FASTEST PAIR: -- */
  cru_stat(&c, 5, &s);
  EQ(s.known, 0); /* FIVE FINDS: -- */
  cru_stat(&c, 12, &s);
  EQ(s.known, 0); /* COMPLETED IN: -- */
  mix(&c, "FIRE", "WATER");
  tick_frames(&c, 300);
  mix(&c, "EARTH", "WATER");
  mix(&c, "AIR", "AIR");
  cru_stat(&c, 0, &s);
  EQ(s.value, 6);
  EQ(s.of, 57);
  EQ(s.unit, CRU_UNIT_OF);
  cru_stat(&c, 1, &s);
  EQ(s.value, 2);
  EQ(s.of, 70);
  cru_stat(&c, 2, &s);
  EQ(s.value, 3);
  cru_stat(&c, 3, &s);
  EQ(s.value, c.points);
  cru_stat(&c, 4, &s);
  EQ(s.known, 1);
  EQ(s.value, 360);
  EQ(s.unit, CRU_UNIT_FRAMES);
  cru_stat(&c, 7, &s);
  EQ(s.value, 2);
  cru_stat(&c, 11, &s);
  EQ(s.unit, CRU_UNIT_PLAYTIME);
  EQ(s.value * 60u + s.of, (60u * 3u + 300u) / 60u);
}

void core_tests(void) {
  printf("core\n");
  RUN(fresh_save);
  RUN(outcomes_and_scoring);
  RUN(bench_flow);
  RUN(shelf_navigation);
  RUN(filter_counts);
  RUN(sign_after_filter);
  RUN(feats_and_titles);
  RUN(chain_and_categories);
  RUN(clock_feats);
  RUN(gilded_roll);
  RUN(complete_run);
  RUN(toast_queue);
  RUN(book);
  RUN(leaderboard);
  RUN(stats_rows);
}
