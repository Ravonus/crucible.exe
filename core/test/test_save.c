/* Save v4 through the byte store: round trip, a power cut at every single write of a mix (and of a dud filter
 * rotation, and of a migration), migration from v3/v2/v1 records, a catalogue update and a corrupted area. */
#include <string.h>
#include "harness.h"

static mem_store pre, post, work;

/* everything a player could observe after a power-on */
typedef struct {
  uint8_t b[4096];
  unsigned n;
} snap_t;
static void p8(snap_t *s, unsigned v) {
  if (s->n < sizeof s->b) s->b[s->n++] = (uint8_t)v;
}
static void p16(snap_t *s, unsigned v) {
  p8(s, v & 255u);
  p8(s, (v >> 8) & 255u);
}
static void pn(snap_t *s, const void *p, unsigned n) {
  const uint8_t *q = (const uint8_t *)p;
  while (n--) p8(s, *q++);
}
static void snapshot(crucible_core *c, snap_t *s) {
  uint16_t a, b;
  uint8_t i;
  s->n = 0;
  p16(s, c->sequence);
  p16(s, c->points);
  p16(s, c->rng);
  p16(s, c->made);
  p16(s, c->session);
  p16(s, c->variant_seed);
  p16(s, c->minutes);
  p16(s, c->fails);
  p16(s, c->book_views);
  p16(s, c->complete);
  p16(s, c->best_gap);
  p16(s, c->best_first);
  p16(s, c->last_new);
  p8(s, c->title);
  p8(s, c->filter);
  p8(s, c->flags);
  p8(s, c->routes);
  p8(s, c->options);
  p8(s, c->seconds);
  p8(s, c->streak);
  p8(s, c->best_streak);
  p8(s, c->chain);
  p8(s, c->best_chain);
  p8(s, c->best_flurry);
  p8(s, c->nofail);
  p8(s, c->best_nofail);
  p8(s, c->best_swift);
  p8(s, c->links);
  pn(s, c->name, CRU_NAME);
  pn(s, c->feats, CRU_FEATS);
  pn(s, c->variants, 64);
  pn(s, c->board, sizeof c->board);
  pn(s, c->tails, sizeof c->tails);
  for (i = 0; i < CRU_FILTERS; i++) {
    p16(s, c->found[i]);
    p16(s, c->total[i]);
  }
  p16(s, c->gilded);
  for (i = 0; i <= CRU_PV_PAIRS; i++) p16(s, cru_play_value(c, i));
  for (a = 0; a < c->t->items; a++) {
    p8(s, cru_owned(c, a));
    p16(s, cru_play_uses(c, a));
  }
  for (a = 0; a < c->t->items; a++)
    for (b = a; b < c->t->items; b++) p8(s, cru_tried(c, a, b));
}
static int same(const snap_t *x, const snap_t *y) { return x->n == y->n && !memcmp(x->b, y->b, x->n); }
static void copy_store(mem_store *to, const mem_store *from) {
  memcpy(to->mem, from->mem, IMAGE);
  to->budget = -1;
  to->writes = 0;
}

static void round_trip(void) {
  crucible_core c, d;
  snap_t x, y;
  uint8_t e = boot_clean(&c, &pre, CRU_LAYOUT_32K);
  mix(&c, "FIRE", "WATER");
  mix(&c, "EARTH", "FIRE");
  mix(&c, "AIR", "AIR");
  mix(&c, "FIRE", "FIRE");
  mix(&c, "SUN", "WATER");
  cru_filter_apply(&c, 8);
  cru_title_wear(&c, 0);
  cru_set_name(&c, "MERLIN");
  c.options = 1u | CRU_OPT_SFX_OFF | (2u << 3);
  cru_book_open(&c);
  cru_book_view(&c);
  tick_frames(&c, 4000);
  cru_save(&c);
  snapshot(&c, &x);
  EQ(boot(&d, &pre, &world, CRU_LAYOUT_32K, e), CRU_LOAD_V4);
  EQ(d.session, c.session + 1u);
  d.session = c.session;
  snapshot(&d, &y);
  CHECK(same(&x, &y));
  EQ(d.options, 1u | CRU_OPT_SFX_OFF | (2u << 3));
  /* the v4 record layout, byte by byte (v3 offsets kept, v4 fields in v3's spare room) */
  {
    const uint8_t *r = pre.mem + ((c.sequence & 1u) ? 0x0700 : 0x0500);
    uint16_t crc = 0xffffu, i;
    unsigned k;
    EQ(r[0], 0xc1);
    EQ(r[1], 4);
    EQ(r[2] | (r[3] << 8), c.sequence);
    EQ(r[4] | (r[5] << 8), c.points);
    EQ(r[6], c.title);
    EQ(r[7], c.filter);
    EQ(r[8] | (r[9] << 8), c.rng);
    EQ(r[10] | (r[11] << 8), c.made);
    EQ(r[12], c.flags);
    EQ(r[13], c.routes);
    EQ(r[14] | (r[15] << 8), 57);
    for (i = 16; i < 48; i++) EQ(r[i], 0);
    CHECK(!memcmp(r + 48, c.variants, 64));
    CHECK(!memcmp(r + 112, pre.mem + 0x0f00, 72));
    CHECK(!memcmp(r + 192, "MERLIN  ", 8));
    EQ(r[200] | (r[201] << 8), c.last_new);
    EQ(r[242] | (r[243] << 8), 70);
    EQ(r[244] | (r[245] << 8), c.variant_seed);
    CHECK(!memcmp(r + 246, c.lost, CRU_LOST_BYTES));
    EQ(r[246], CRU_LOST_VERSION); /* lost pieces (none cooling here) */
    for (i = 248; i < 264; i++) EQ(r[i], 0);
    for (i = 264; i < 376; i++) EQ(r[i], 0);
    CHECK(!memcmp(r + 376, c.feats, 24));
    EQ(r[400] | (r[401] << 8), c.minutes);
    EQ(r[402], c.seconds);
    EQ(r[403] | (r[404] << 8), c.fails);
    EQ(r[405] | (r[406] << 8), c.book_views);
    EQ(r[409] | (r[410] << 8), c.best_gap);
    EQ(r[411] | (r[412] << 8), c.best_first);
    EQ(r[413], c.streak);
    EQ(r[414], c.best_streak);
    EQ(r[415], c.chain);
    EQ(r[416], c.best_chain);
    EQ(r[417], c.last_new);
    EQ(r[418], c.best_flurry);
    EQ(r[419], c.nofail);
    EQ(r[420], c.best_nofail);
    EQ(r[421], c.best_swift);
    EQ(r[422], c.links);
    EQ(r[423], c.options);
    CHECK(!memcmp(r + 424, "MER", 3));
    EQ(r[427] | (r[428] << 8), c.session);
    CHECK(!memcmp(r + 429, c.board, 80));
    EQ(r[509], 0);
    for (i = 0; i < 510; i++) {
      crc ^= (uint16_t)(r[i] << 8);
      for (k = 0; k < 8; k++) crc = (uint16_t)(crc & 0x8000u ? (crc << 1) ^ 0x1021u : crc << 1);
    }
    EQ(r[510] | (r[511] << 8), crc);
  }
  /* the record pair alternates; the newer one wins */
  EQ(pre.mem[0x0500] | pre.mem[0x0700], 0xc1);
  CHECK(pre.mem[0x0501] == 4 && pre.mem[0x0701] == 4);
}

/* A power cut after k writes of one mix, for every k: the next power-on is consistent and equals the state just
 * before or just after the mix; once the record is written the mix always survives. */
static void torn_case(uint8_t layout, uint16_t a, uint16_t b, uint8_t expect, const char *label) {
  crucible_core c;
  snap_t before, after, now;
  unsigned long w0, total, k, nb = 0, na = 0, flips = 0;
  int last_after = 0;
  copy_store(&work, &pre);
  boot(&c, &work, &world, layout, 0);
  snapshot(&c, &before);
  copy_store(&post, &pre);
  boot(&c, &post, &world, layout, 0);
  EQ(cru_mix_begin(&c, a, b, 9), expect);
  w0 = post.writes;
  cru_mix_finish(&c);
  total = post.writes - w0;
  copy_store(&work, &post);
  boot(&c, &work, &world, layout, 0);
  snapshot(&c, &after);
  CHECK(!same(&before, &after));
  for (k = 0; k <= total; k++) {
    copy_store(&work, &pre);
    boot(&c, &work, &world, layout, 0);
    cru_mix_begin(&c, a, b, 9);
    work.budget = (long)k;
    cru_mix_finish(&c);
    work.budget = -1;
    EQ(boot(&c, &work, &world, layout, 0), CRU_LOAD_V4);
    snapshot(&c, &now);
    if (same(&now, &before)) {
      nb++;
      if (last_after) flips++;
    } else if (same(&now, &after)) {
      na++;
      last_after = 1;
    } else {
      CHECK(!"torn state is neither before nor after");
      fprintf(stderr, "    %s: cut after %lu of %lu writes\n", label, k, total);
      break;
    }
  }
  EQ(flips, 0);
  CHECK(nb > 0 && na > 0);
  EQ(nb + na, total + 1u);
  printf("    %-28s %s: %4lu writes, %4lu cuts before / %4lu after\n", label, layout ? "128K" : " 32K", total, nb, na);
}
static void torn_writes(void) {
  crucible_core c;
  uint8_t layout;
  for (layout = 0; layout < 2u; layout++) {
    ms_reset(&pre);
    boot(&c, &pre, &world, layout, 2);
    mix(&c, "FIRE", "WATER");
    mix(&c, "EARTH", "WATER");
    mix(&c, "FIRE", "FIRE");
    mix(&c, "AIR", "AIR");
    torn_case(layout, id_of("EARTH"), id_of("FIRE"), CRU_NEW, "NEW (LAVA)");
    torn_case(layout, id_of("SUN"), id_of("WATER"), CRU_ROUTE, "ROUTE (STEAM)");
    torn_case(layout, id_of("WATER"), id_of("FIRE"), CRU_KNOWN, "KNOWN");
    torn_case(layout, id_of("STEAM"), id_of("MUD"), CRU_NOTHING, "NOTHING, fresh dud");
    torn_case(layout, id_of("AIR"), id_of("AIR"), CRU_NOTHING, "NOTHING, repeat");
  }
}
/* the 32K dud filter rotates every 341 duds: a cut inside the generation clear is finished at power-on */
static void torn_rotation(void) {
  crucible_core c;
  uint16_t i, a, b, progress = 1, gcount = 0;
  ms_reset(&pre);
  boot(&c, &pre, &world, CRU_LAYOUT_32K, 2);
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
  for (a = 0; a < c.t->items; a++)
    for (b = a; b < c.t->items; b++) {
      gcount = (uint16_t)(pre.mem[0x0f00 + 18] | (pre.mem[0x0f00 + 19] << 8));
      if (gcount >= 341u && cru_recipe(&c, a, b) == CRU_NONE && !cru_tried(&c, a, b)) goto found;
      if (cru_recipe(&c, a, b) == CRU_NONE && !cru_tried(&c, a, b)) cru_mix_begin(&c, a, b, 0), cru_mix_finish(&c);
    }
  CHECK(!"no rotation point");
  return;
found:
  EQ(pre.mem[0x0f00 + 17], 0); /* generation 0 is full; the next new dud rotates to 1 and clears it */
  torn_case(CRU_LAYOUT_32K, a, b, CRU_NOTHING, "NOTHING, rotation clear");
  copy_store(&work, &post);
  boot(&c, &work, &world, CRU_LAYOUT_32K, 0);
  EQ(work.mem[0x0f00 + 17], 1);
  CHECK(cru_tried(&c, a, b));
}

/* ---- a v3 cartridge save, as v3 cartridges wrote it (nibble-table CRC) ---- */
static uint16_t crc_v3(const uint8_t *p, unsigned n) {
  static const uint16_t nib[16] = {0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
                                   0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef};
  uint16_t crc = 0xffffu;
  while (n--) {
    uint8_t b = *p++;
    crc = (uint16_t)((crc << 4) ^ nib[((crc >> 12) ^ (b >> 4)) & 15u]);
    crc = (uint16_t)((crc << 4) ^ nib[((crc >> 12) ^ b) & 15u]);
  }
  return crc;
}
static void seal(uint8_t *rec, unsigned size) {
  uint16_t c = crc_v3(rec, size - 2u);
  rec[size - 2u] = (uint8_t)c;
  rec[size - 1u] = (uint8_t)(c >> 8);
}
static void w16(uint8_t *p, unsigned v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
static unsigned tri(unsigned b) { return b * (b + 1u) / 2u; }
static const char *const v3_owned[] = {"EARTH", "WATER", "FIRE", "AIR",   "STEAM",
                                       "MUD",   "LAVA",  "SUN",  "OCEAN", "STONE"};
static const char *const v3_tried[][2] = {{"FIRE", "WATER"},  {"EARTH", "WATER"}, {"EARTH", "FIRE"}, {"FIRE", "FIRE"},
                                          {"WATER", "WATER"}, {"LAVA", "WATER"},  {"SUN", "WATER"},  {"AIR", "AIR"},
                                          {"STEAM", "MUD"},   {"SUN", "LAVA"},    {"FIRE", "WATER"}};
static void make_v3(uint8_t *mem, uint16_t at, uint16_t seq, uint16_t points) {
  uint8_t r[512];
  unsigned i, a, b, q;
  memset(r, 0, sizeof r);
  r[0] = 0xc1;
  r[1] = 3;
  w16(r + 2, seq);
  w16(r + 4, points);
  r[6] = 15;
  r[7] = 9;
  w16(r + 8, 0x1234);
  w16(r + 10, 10);
  r[12] = 1;
  r[13] = 1;
  r[14] = 57;
  for (i = 0; i < sizeof v3_owned / sizeof *v3_owned; i++) {
    q = id_of(v3_owned[i]);
    r[16 + (q >> 3)] |= (uint8_t)(1u << (q & 7u));
  }
  for (i = 0; i < 64; i++) r[48 + i] = (uint8_t)(i * 37u);
  for (i = 0; i < sizeof v3_tried / sizeof *v3_tried; i++) {
    a = id_of(v3_tried[i][0]);
    b = id_of(v3_tried[i][1]);
    if (a > b) {
      q = a;
      a = b;
      b = q;
    }
    q = tri(b) + a;
    r[112 + (q >> 3)] |= (uint8_t)(1u << (q & 7u));
  }
  r[376] = 2;
  r[377] = 1;
  r[380] = 1;
  r[382] = 1; /* COLLECTOR II, RECIPES I, HOT STREAK I, EXPLORER I */
  w16(r + 400, 75);
  r[402] = 30;
  w16(r + 403, 3);
  w16(r + 405, 12);
  w16(r + 407, 0);
  w16(r + 409, 900);
  w16(r + 411, 0);
  r[413] = 2;
  r[414] = 6;
  r[415] = 1;
  r[416] = 3;
  r[417] = (uint8_t)id_of("SUN");
  r[418] = 3;
  r[419] = 1;
  r[420] = 5;
  r[421] = 7;
  r[422] = 0;
  r[423] = 1;
  r[424] = 'R';
  r[425] = 'A';
  r[426] = 'V';
  w16(r + 427, 7);
  r[429] = 'R';
  r[430] = 'A';
  r[431] = 'V';
  w16(r + 432, 60);
  r[434] = 6;
  w16(r + 435, 3);
  r[437] = 15;
  r[439] = 'B';
  r[440] = 'O';
  r[441] = 'B';
  w16(r + 442, 25);
  r[444] = 2;
  w16(r + 445, 0xffff);
  seal(r, 512);
  memcpy(mem + at, r, 512);
}
static long v3_points; /* points after a clean migration: 345 plus the catch-up tiers of the boot check */
static void check_v3_carried(crucible_core *c) {
  crucible_board_row r;
  char name[CRU_NAME + 1];
  unsigned i, j, k, lb, x, y, t = sizeof v3_tried / sizeof *v3_tried, mine = 0, rav = 0, bob = 0;
  /* the record's tiers (COLLECTOR II, RECIPES I, HOT STREAK I, EXPLORER I = 30 points) are never lowered; the boot
   * check adds the tiers the carried statistics already reach */
  EQ(c->feats[0], 2);
  EQ(c->feats[1], 1);
  EQ(c->feats[6], 1);
  EQ(c->feats[4], 2); /* best streak 6 */
  EQ(c->feats[2], 3); /* best gap 900 frames */
  EQ(c->points, 345 + feat_points(c) - 30);
  EQ(c->title, 15);
  EQ(c->filter, 9);
  EQ(c->made, 10);
  EQ(c->routes, 1);
  EQ(c->minutes, 75);
  EQ(c->fails, 3);
  EQ(c->book_views, 12);
  EQ(c->best_gap, 900);
  EQ(c->best_streak, 6);
  EQ(c->best_chain, 3);
  EQ(c->last_new, id_of("SUN"));
  EQ(c->best_flurry, 3);
  EQ(c->best_nofail, 5);
  EQ(c->best_swift, 7);
  EQ(c->options, 1);
  EQ(c->variants[5], (uint8_t)(5 * 37));
  cru_get_name(c, name);
  CHECK(!strcmp(name, "RAV"));
  for (i = 0; i < CRU_BOARD_ROWS; i++) {
    cru_board_row(c, (uint8_t)i, &r);
    if (r.session == 3u) {
      rav++;
      CHECK(!strcmp(r.name, "RAV"));
      EQ(r.points, 60);
      EQ(r.finds, 6);
      EQ(r.title, 15);
    }
    if (r.session == CRU_FRIEND) {
      bob++;
      CHECK(!strcmp(r.name, "BOB"));
      EQ(r.points, 25);
    }
    if (r.mine) mine++;
  }
  EQ(rav, 1);
  EQ(bob, 1);
  EQ(mine, c->points > 345 ? 1 : 0);
  EQ(c->found[0], sizeof v3_owned / sizeof *v3_owned);
  for (i = 0; i < sizeof v3_owned / sizeof *v3_owned; i++) CHECK(cru_owned(c, id_of(v3_owned[i])));
  /* tried pairs: recipes exactly, duds through the filter */
  for (i = 0; i < t; i++) CHECK(cru_tried(c, id_of(v3_tried[i][0]), id_of(v3_tried[i][1])));
  EQ(cru_play_value(c, CRU_PV_RECIPES), 7);
  EQ(cru_play_value(c, CRU_PV_DUDS), 3);
  EQ(cru_play_value(c, CRU_PV_MIRRORS), 2);
  CHECK(!cru_tried(c, id_of("EARTH"), id_of("AIR")));
  /* use counts start at the exact lower bound: one mix per distinct tried pair */
  for (k = 0; k < c->t->items; k++) {
    lb = 0;
    for (i = 0; i < t; i++) {
      unsigned dup = 0;
      x = id_of(v3_tried[i][0]);
      y = id_of(v3_tried[i][1]);
      for (j = 0; j < i; j++) {
        unsigned x2 = id_of(v3_tried[j][0]), y2 = id_of(v3_tried[j][1]);
        if ((x2 == x && y2 == y) || (x2 == y && y2 == x)) dup = 1;
      }
      if (!dup && (x == k || y == k)) lb++;
    }
    EQ(cru_play_uses(c, (uint16_t)k), lb);
  }
}
static void migrate_v3(void) {
  crucible_core c;
  uint8_t layout;
  for (layout = 0; layout < 2u; layout++) {
    ms_reset(&pre);
    make_v3(pre.mem, 0x0100, 41, 345);
    make_v3(pre.mem, 0x0300, 40, 100); /* the older slot */
    EQ(boot(&c, &pre, &world, layout, 0), CRU_LOAD_V3);
    EQ(c.session, 8);
    check_v3_carried(&c);
    v3_points = c.points;
    EQ(boot(&c, &pre, &world, layout, 0), CRU_LOAD_V4);
    EQ(c.session, 9);
    c.session = 8;
    check_v3_carried(&c);
    if (layout == CRU_LAYOUT_32K)
      CHECK(pre.mem[0x0100] != 0xc1 || pre.mem[0x0101] != 3); /* the filter took the v3 area */
    else
      CHECK(pre.mem[0x0100] == 0xc1 && pre.mem[0x0101] == 3); /* kept: downgrade-safe */
    /* and play continues */
    EQ(mix(&c, "STONE", "AIR"), CRU_NEW);
    EQ(mix(&c, "AIR", "AIR"), CRU_NOTHING);
    CHECK(!c.mix.fresh);
  }
}
/* a power cut anywhere inside the migration: the next power-on still carries the v3 progress */
static void migrate_v3_torn(void) {
  crucible_core c;
  unsigned long total, k, step;
  uint8_t layout, kind;
  for (layout = 0; layout < 2u; layout++) {
    ms_reset(&pre);
    make_v3(pre.mem, 0x0100, 41, 345);
    copy_store(&work, &pre);
    boot(&c, &work, &world, layout, 0);
    total = work.writes;
    step = total / 397u + 1u;
    for (k = 0; k < total; k += step) {
      copy_store(&work, &pre);
      work.budget = (long)k;
      boot(&c, &work, &world, layout, 0);
      work.budget = -1;
      kind = boot(&c, &work, &world, layout, 0);
      CHECK(kind == CRU_LOAD_V3 || kind == CRU_LOAD_V4);
      EQ(c.points, v3_points);
      EQ(c.found[0], 10);
      EQ(cru_play_value(&c, CRU_PV_RECIPES), 7);
      CHECK(cru_owned(&c, id_of("STONE")) && cru_tried(&c, id_of("LAVA"), id_of("WATER")));
    }
    printf("    v3 migration %s: %lu writes, cut every %lu\n", layout ? "128K" : " 32K", total, step);
  }
}
static void migrate_v2_v1(void) {
  crucible_core c;
  uint8_t r[64], i;
  /* v2: 64 bytes, ids 0..31 owned, points, rng, four geometric seeds per id (3 maps to 0) */
  ms_reset(&pre);
  memset(r, 0, sizeof r);
  r[0] = 0xc1;
  r[1] = 2;
  w16(r + 2, 9);
  r[4] = 0x9f; /* EARTH WATER FIRE AIR STEAM MUD */
  w16(r + 8, 77);
  w16(r + 10, 0x4321);
  for (i = 0; i < 26; i++) r[12 + i] = (uint8_t)(i & 3u);
  seal(r, 64);
  memcpy(pre.mem + 0x80, r, 64);
  EQ(boot(&c, &pre, &world, CRU_LAYOUT_32K, 0), CRU_LOAD_V2);
  EQ(c.points, 77);
  EQ(c.found[0], 6);
  CHECK(cru_owned(&c, id_of("MUD")) && !cru_owned(&c, id_of("SALT")));
  EQ(cru_variant(&c, 1), 1);
  EQ(cru_variant(&c, 2), 2);
  EQ(cru_variant(&c, 3), 0);
  EQ(cru_variant(&c, 7), 0);
  CHECK(c.flags & CRU_FLAG_MIGRATED);
  EQ(boot(&c, &pre, &world, CRU_LAYOUT_32K, 0), CRU_LOAD_V4);
  EQ(c.points, 77);
  /* v1: 16 bytes */
  ms_reset(&pre);
  memset(r, 0, sizeof r);
  r[0] = 0xc1;
  r[1] = 1;
  w16(r + 2, 3);
  r[4] = 0x1f;
  w16(r + 8, 42);
  seal(r, 16);
  memcpy(pre.mem + 0x20, r, 16);
  EQ(boot(&c, &pre, &world, CRU_LAYOUT_32K, 0), CRU_LOAD_V1);
  EQ(c.points, 42 + feat_points(&c));
  EQ(c.found[0], 5);
  CHECK(cru_owned(&c, id_of("STEAM")));
  EQ(mix(&c, "STEAM", "AIR"), CRU_NEW);
}
/* a catalogue update (another size in the record) recounts every counter from the areas */
static void catalogue_change(void) {
  crucible_core c;
  snap_t x, y;
  uint16_t at, crc, i;
  uint8_t e = boot_clean(&c, &pre, CRU_LAYOUT_32K);
  mix(&c, "FIRE", "WATER");
  mix(&c, "EARTH", "FIRE");
  mix(&c, "AIR", "AIR");
  snapshot(&c, &x);
  at = (c.sequence & 1u) ? 0x0700 : 0x0500;
  pre.mem[at + 14] = 56; /* written by an older catalogue of 56 ids */
  crc = 0xffffu;
  for (i = 0; i < 510; i++) {
    uint8_t b = pre.mem[at + i];
    unsigned k;
    crc ^= (uint16_t)(b << 8);
    for (k = 0; k < 8; k++) crc = (uint16_t)(crc & 0x8000u ? (crc << 1) ^ 0x1021u : crc << 1);
  }
  pre.mem[at + 510] = (uint8_t)crc;
  pre.mem[at + 511] = (uint8_t)(crc >> 8);
  EQ(boot(&c, &pre, &world, CRU_LAYOUT_32K, e), CRU_LOAD_RECOUNTED);
  c.session--;
  c.sequence--; /* the repaired record was written back */
  snapshot(&c, &y);
  CHECK(same(&x, &y));
}
/* a byte of an area changed behind the record's back: the sum check recounts the counters from the areas */
static void corrupted_area(void) {
  crucible_core c;
  uint8_t e = boot_clean(&c, &pre, CRU_LAYOUT_32K);
  mix(&c, "FIRE", "WATER");
  mix(&c, "EARTH", "FIRE");
  pre.mem[0x0900 + (id_of("SUN") >> 3)] |= (uint8_t)(1u << (id_of("SUN") & 7u)); /* SUN appears owned */
  EQ(boot(&c, &pre, &world, CRU_LAYOUT_32K, e), CRU_LOAD_RECOUNTED);
  CHECK(cru_owned(&c, id_of("SUN")));
  EQ(cru_play_value(&c, CRU_PV_OWNED), 7);
  EQ(c.found[0], 7);
  EQ(boot(&c, &pre, &world, CRU_LAYOUT_32K, e), CRU_LOAD_V4); /* the repaired counters were written back */
}
/* a torn record falls back to the other one */
static void torn_record_fallback(void) {
  crucible_core c;
  uint16_t seq, points;
  uint8_t e = boot_clean(&c, &pre, CRU_LAYOUT_32K);
  mix(&c, "FIRE", "WATER");
  seq = c.sequence;
  points = c.points;
  mix(&c, "EARTH", "FIRE");
  pre.mem[((c.sequence & 1u) ? 0x0700 : 0x0500) + 300] ^= 0x40; /* flip a bit in the newest record */
  /* the older record is loaded; the areas already hold the newer mix (committed), so the counters are recounted
   * from them and written back as the next record */
  EQ(boot(&c, &pre, &world, CRU_LAYOUT_32K, e), CRU_LOAD_RECOUNTED);
  EQ(c.sequence, seq + 1u);
  EQ(c.points, points);
  CHECK(cru_owned(&c, id_of("STEAM")));
}

void save_tests(void) {
  printf("save\n");
  RUN(round_trip);
  RUN(torn_writes);
  RUN(torn_rotation);
  RUN(migrate_v3);
  RUN(migrate_v3_torn);
  RUN(migrate_v2_v1);
  RUN(catalogue_change);
  RUN(corrupted_area);
  RUN(torn_record_fallback);
}
