/* cru_grant (src/cru_grant.c): an item owned without a mix here; cru_reset (src/cru_reset.c): RESET GAME. Their own binary
 * (tools/core-tests.sh test): grant sessions over the real catalogue, a power cut after every single write of a grant in both layouts,
 * and a reset after a session. */
#include <string.h>
#include "harness.h"

static mem_store m, pre, post, work;

/* found/total of every filter against a brute-force count over the catalogue */
static void check_counts(crucible_core *c) {
  uint16_t f, id, found, total, gilded = 0;
  for (f = 0; f < CRU_FILTERS; f++) {
    found = total = 0;
    for (id = 0; id < c->t->items; id++)
      if (cru_filter_match(c, (uint8_t)f, id)) {
        total++;
        if (cru_owned(c, id)) found++;
      }
    EQ(cru_filter_found(c, (uint8_t)f), found);
    EQ(cru_filter_total(c, (uint8_t)f), total);
  }
  for (id = 0; id < c->t->items; id++)
    if (cru_owned(c, id) && cru_variant(c, id) == 3u) gilded++;
  EQ(c->gilded, gilded);
  EQ(cru_play_value(c, CRU_PV_OWNED), c->found[0]);
}

static void sessions(void) {
  crucible_core c, d;
  crucible_board_row r;
  uint16_t id, n = 0, points, made, ab[2];
  uint8_t e = boot_clean(&c, &m, CRU_LAYOUT_32K), toast_seen = 0;
  EQ(cru_grant(&c, id_of("EARTH"), CRU_GRANT_SYNC), 0); /* owned already */
  EQ(cru_grant(&c, 57, CRU_GRANT_SYNC), 0); /* not in the catalogue */
  EQ(cru_grant(&c, CRU_NONE, CRU_GRANT_SYNC), 0);
  points = c.points;
  made = c.made;
  EQ(cru_grant(&c, id_of("STEAM"), CRU_GRANT_SYNC), 1);
  CHECK(cru_owned(&c, id_of("STEAM")));
  EQ(c.points, points);
  EQ(c.made, made); /* no mix, no mix points */
  EQ(c.found[0], 5);
  EQ(cru_play_value(&c, CRU_PV_RECIPES), 0); /* nothing was tried */
  EQ(c.streak, 0);
  EQ(c.chain, 0);
  EQ(c.best_gap, 0);
  EQ(c.flurry_n, 0);
  EQ(c.last_new, CRU_NONE); /* no pace */
  check_counts(&c);
  EQ(cru_grant(&c, id_of("STEAM"), CRU_GRANT_SYNC), 0);
  /* the pair that makes it here is a new recipe, not a new find */
  EQ(mix(&c, "FIRE", "WATER"), CRU_ROUTE);
  EQ(c.mix.awarded, 3);
  /* the shelf and the book see it */
  CHECK(cru_shelf_step(&c, id_of("STEAM"), 1) != id_of("STEAM"));
  EQ(cru_book_route(&c, id_of("STEAM"), ab), CRU_BOOK_RECIPE);
  /* it survives a power-on, consistently */
  boot(&d, &m, &world, CRU_LAYOUT_32K, e);
  CHECK(cru_owned(&d, id_of("STEAM")));
  EQ(d.found[0], 5);
  check_counts(&d);
  /* the feats that depend on what is owned follow (COLLECTOR, the category shares), with their points and toasts */
  drain_toasts(&d);
  for (id = 0; id < 57u && n < 20u; id++) {
    if (cru_grant(&d, id, CRU_GRANT_IMPORT)) n++;
    if (has_toast(&d, (uint8_t)((0u << 2) | 2u))) toast_seen = 1; /* COLLECTOR III */
    drain_toasts(&d);
  }
  EQ(d.found[0], 25);
  CHECK(cru_feat_tier(&d, 0) >= 3u);
  CHECK(toast_seen);
  EQ(d.points, 3 + feat_points(&d)); /* the route's 3, then tier points only */
  check_counts(&d);
  /* not this run's finds: the run's board row has none */
  EQ(cru_board_current(&d), 0);
  cru_board_row(&d, 0, &r);
  EQ(r.finds, 0);
  /* a grant waits while a mix is open */
  EQ(cru_mix_begin(&d, id_of("FIRE"), id_of("FIRE"), 1), CRU_NEW);
  EQ(cru_grant(&d, id_of("ENERGY"), CRU_GRANT_SYNC), 0);
  cru_mix_finish(&d);
  /* everything: complete, COLLECTOR IV, every category share at 100 */
  for (id = 0; id < 57u; id++) cru_grant(&d, id, CRU_GRANT_SYNC);
  EQ(d.found[0], 57);
  CHECK(d.complete > 0);
  EQ(cru_feat_tier(&d, 0), 4);
  for (id = 10; id < 16u; id++) EQ(cru_feat_tier(&d, (uint8_t)id), 4);
  check_counts(&d);
  boot(&c, &m, &world, CRU_LAYOUT_32K, e);
  EQ(c.found[0], 57);
  check_counts(&c);
}

/* everything a player could observe after a power-on */
typedef struct {
  uint8_t b[2048];
  unsigned n;
} snap_t;
static void p8(snap_t *s, unsigned v) {
  if (s->n < sizeof s->b) s->b[s->n++] = (uint8_t)v;
}
static void p16(snap_t *s, unsigned v) {
  p8(s, v & 255u);
  p8(s, (v >> 8) & 255u);
}
static void snapshot(crucible_core *c, snap_t *s) {
  uint16_t a;
  uint8_t i;
  s->n = 0;
  p16(s, c->sequence);
  p16(s, c->points);
  p16(s, c->rng);
  p16(s, c->made);
  p16(s, c->complete);
  for (i = 0; i < CRU_FEATS; i++) p8(s, c->feats[i]);
  for (i = 0; i < 64u; i++) p8(s, c->variants[i]);
  for (i = 0; i < sizeof c->board; i++) p8(s, c->board[i]);
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
}
static int same(const snap_t *x, const snap_t *y) { return x->n == y->n && !memcmp(x->b, y->b, x->n); }
static void copy_store(mem_store *to, const mem_store *from) {
  memcpy(to->mem, from->mem, IMAGE);
  to->budget = -1;
  to->writes = 0;
}

/* A power cut after k writes of one grant, for every k: the next power-on is consistent and equals the state just before
 * or just after it; once the record is written the grant always survives. */
static void torn(void) {
  crucible_core c;
  snap_t before, after, now;
  unsigned long w0, total, k, nb, na, flips;
  int last_after;
  uint8_t layout;
  for (layout = 0; layout < 2u; layout++) {
    ms_reset(&pre);
    boot(&c, &pre, &world, layout, 2);
    mix(&c, "FIRE", "WATER");
    mix(&c, "AIR", "AIR");
    copy_store(&work, &pre);
    boot(&c, &work, &world, layout, 0);
    snapshot(&c, &before);
    copy_store(&post, &pre);
    boot(&c, &post, &world, layout, 0);
    w0 = post.writes;
    EQ(cru_grant(&c, id_of("DRAGON"), CRU_GRANT_SYNC), 1);
    total = post.writes - w0;
    copy_store(&work, &post);
    boot(&c, &work, &world, layout, 0);
    snapshot(&c, &after);
    CHECK(!same(&before, &after));
    nb = na = flips = 0;
    last_after = 0;
    for (k = 0; k <= total; k++) {
      copy_store(&work, &pre);
      boot(&c, &work, &world, layout, 0);
      work.budget = (long)k;
      cru_grant(&c, id_of("DRAGON"), CRU_GRANT_SYNC);
      work.budget = -1;
      EQ(boot(&c, &work, &world, layout, 0), CRU_LOAD_V4);
      check_counts(&c);
      snapshot(&c, &now);
      if (same(&now, &before)) {
        nb++;
        if (last_after) flips++;
      } else if (same(&now, &after)) {
        na++;
        last_after = 1;
      } else {
        CHECK(!"a torn grant is neither before nor after");
        fprintf(stderr, "    cut after %lu of %lu writes\n", k, total);
        break;
      }
    }
    EQ(flips, 0);
    CHECK(nb > 0 && na > 0);
    EQ(nb + na, total + 1u);
    printf("    GRANT (DRAGON) %s: %4lu writes, %4lu cuts before / %4lu after\n", layout ? "128K" : " 32K", total, nb,
           na);
  }
}

/* RESET GAME: as fresh as a new save, but the options, the name and the record sequence stay */
static void reset(void) {
  crucible_core c, d, f;
  crucible_board_row r;
  char name[CRU_NAME + 1];
  uint16_t id, seq;
  uint8_t layout, e;
  for (layout = 0; layout < 2u; layout++) {
    e = boot_clean(&c, &m, layout);
    mix(&c, "FIRE", "WATER");
    mix(&c, "EARTH", "FIRE");
    mix(&c, "AIR", "AIR");
    mix(&c, "FIRE", "FIRE");
    cru_grant(&c, id_of("DRAGON"), CRU_GRANT_SYNC);
    cru_filter_apply(&c, 1);
    cru_set_name(&c, "MERLIN");
    c.options = 1u | CRU_OPT_SFX_OFF | (2u << 3);
    cru_book_open(&c);
    tick_frames(&c, 7300);
    cru_save(&c);
    CHECK(c.points > 0);
    CHECK(cru_feats_total(&c) > 0);
    CHECK(cru_tried(&c, id_of("AIR"), id_of("AIR")));
    seq = c.sequence;
    cru_reset(&c, 7);
    EQ(c.found[0], 4);
    EQ(c.made, 0);
    EQ(c.points, feat_points(&c));
    EQ(c.filter, 0);
    EQ(c.title, 0);
    EQ(c.minutes, 0);
    EQ(c.book_views, 0);
    for (id = 0; id < 57u; id++) EQ(cru_owned(&c, id), cru_is_starter(&c, id));
    CHECK(!cru_tried(&c, id_of("AIR"), id_of("AIR")));
    CHECK(!cru_tried(&c, id_of("FIRE"), id_of("WATER")));
    EQ(cru_play_value(&c, CRU_PV_PAIRS), 0);
    EQ(c.options, 1u | CRU_OPT_SFX_OFF | (2u << 3));
    cru_get_name(&c, name);
    CHECK(!strcmp(name, "MERLIN"));
    CHECK((uint16_t)(c.sequence - seq) >= 2u);
    EQ(c.focus, id_of("EARTH"));
    EQ(c.slot_a, CRU_NONE);
    cru_board_row(&c, 0, &r);
    EQ(r.points, 0); /* the board starts over */
    check_counts(&c);
    /* it is what a power-on finds, and it plays as a new save */
    boot(&d, &m, &world, layout, e);
    EQ(d.found[0], 4);
    EQ(d.points, c.points);
    EQ(d.options, c.options);
    CHECK(!cru_tried(&d, id_of("AIR"), id_of("AIR")));
    cru_get_name(&d, name);
    CHECK(!strcmp(name, "MERLIN"));
    check_counts(&d);
    EQ(mix(&d, "FIRE", "WATER"), CRU_NEW);
    EQ(mix(&d, "AIR", "AIR"), CRU_NOTHING);
    CHECK(d.mix.fresh);
    /* the same as a fresh save given the same name and options, but for the tints and the sequence */
    ms_reset(&pre);
    boot(&f, &pre, &world, layout, e);
    EQ(f.found[0], 4);
    EQ(f.total[0], c.total[0]);
  }
}

int main(void) {
  printf("grant\n");
  RUN(sessions);
  RUN(torn);
  RUN(reset);
  printf("%d checks, %d failed\n", t_checks, t_fails);
  return t_fails ? 1 : 0;
}
