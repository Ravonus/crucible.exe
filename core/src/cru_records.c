/* Records: the leaderboard (the eight best runs, points gained per power-on) and the STATS rows. */
#include "cru_internal.h"

static uint8_t *row(crucible_core *c, uint8_t i) { return c->board + (uint8_t)((i << 3) + (i << 1)); }
static const uint8_t *crow(const crucible_core *c, uint8_t i) { return c->board + (uint8_t)((i << 3) + (i << 1)); }
static uint8_t *tail(crucible_core *c, uint8_t i) { return c->tails + (uint8_t)((i << 2) + i); }
static uint16_t le16(const uint8_t *p) {
  uint8_t lo = p[0], hi = p[1];
  return (uint16_t)(lo | ((uint16_t)hi << 8));
}
static uint16_t row_points(const crucible_core *c, uint8_t i) {
  const uint8_t *r = crow(c, i);
  return le16(r + 3);
}
static uint16_t row_session(const crucible_core *c, uint8_t i) {
  const uint8_t *r = crow(c, i);
  return le16(r + 6);
}
static void swap(uint8_t *a, uint8_t *b, uint8_t n) {
  uint8_t x, y;
  while (n--) {
    x = *a;
    y = *b;
    *a++ = y;
    *b++ = x;
  }
}

static uint8_t current(const crucible_core *c) {
  uint8_t i;
  uint16_t s = c->session, p, q;
  for (i = 0; i < CRU_BOARD_ROWS; i++) {
    p = row_points(c, i);
    q = row_session(c, i);
    if (p && q == s) return i;
  }
  return CRU_NONE8;
}
uint8_t cru_board_current(const crucible_core *c) CORE_BANKED { return current(c); }
uint16_t cru_board_run_points(const crucible_core *c) CORE_BANKED { return (uint16_t)(c->points - c->boot_points); }

/* On every save: this run's row (initials, points gained, new finds, session, worn title) moves up past every row
 * with fewer points; a run that does not beat the eighth row stays off the board. */
void cri_board_update(crucible_core *c) CORE_LOCAL {
  uint16_t pts = (uint16_t)(c->points - c->boot_points), found = (uint16_t)(c->found[0] - c->boot_found);
  uint8_t i, *r, *n, k;
  uint16_t below;
  if (!pts) return;
  i = current(c);
  if (i == CRU_NONE8) {
    i = CRU_BOARD_ROWS - 1u;
    below = row_points(c, i);
    if (below >= pts) return;
  }
  r = row(c, i);
  r[0] = (uint8_t)c->name[0];
  r[1] = (uint8_t)c->name[1];
  r[2] = (uint8_t)c->name[2];
  r[3] = (uint8_t)pts;
  r[4] = (uint8_t)(pts >> 8);
  if (found > 255u) found = 255u;
  r[5] = (uint8_t)found;
  r[6] = (uint8_t)c->session;
  r[7] = (uint8_t)(c->session >> 8);
  r[8] = c->title;
  r[9] = 0;
  n = tail(c, i);
  for (r = (uint8_t *)c->name + 3; r < (uint8_t *)c->name + CRU_NAME; r++) {
    k = *r;
    *n++ = k;
  }
  while (i) {
    below = row_points(c, (uint8_t)(i - 1u));
    if (below >= pts) break;
    swap(row(c, (uint8_t)(i - 1u)), row(c, i), CRU_BOARD_ROW);
    swap(tail(c, (uint8_t)(i - 1u)), tail(c, i), 5);
    i--;
  }
}

void cru_board_row(const crucible_core *c, uint8_t i, crucible_board_row *out) CORE_BANKED {
  const uint8_t *r, *t;
  uint8_t k, n = 0;
  char ch;
  if (i >= CRU_BOARD_ROWS) i = CRU_BOARD_ROWS - 1u;
  r = crow(c, i);
  t = c->tails + (uint8_t)((i << 2) + i);
  for (k = 0; k < CRU_NAME; k++) {
    if (k < 3u)
      ch = (char)r[k];
    else
      ch = (char)t[k - 3u];
    if (!ch) ch = ' ';
    out->name[k] = ch;
    if (ch != ' ') n = (uint8_t)(k + 1u);
  }
  out->name[n] = 0;
  out->points = row_points(c, i);
  k = r[5];
  out->finds = k;
  out->session = row_session(c, i);
  k = r[8];
  out->title = k;
  out->mine = 0;
  if (out->points && out->session == c->session) out->mine = 1;
}

/* STATS rows: FOUND, RECIPES, PAIRS TRIED, POINTS, FASTEST PAIR, FIVE FINDS, BEST FLURRY, BEST STREAK,
 * LONGEST CHAIN, CLEAN RUN, PAIRS/MINUTE, PLAY TIME, COMPLETED IN. */
void cru_stat(crucible_core *c, uint8_t i, crucible_stat *o) CORE_BANKED {
  o->of = 0;
  o->unit = CRU_UNIT_COUNT;
  o->known = 1;
  switch (i) {
  case 0:
    o->value = c->found[0];
    o->of = c->items;
    o->unit = CRU_UNIT_OF;
    break;
  case 1:
    o->value = cri_value(c, CRU_PV_RECIPES);
    o->of = c->recipes;
    o->unit = CRU_UNIT_OF;
    break;
  case 2: o->value = cri_value(c, CRU_PV_PAIRS); break;
  case 3: o->value = c->points; break;
  case 4:
    o->value = c->best_gap;
    o->unit = CRU_UNIT_FRAMES;
    o->known = c->best_gap != 0;
    break;
  case 5:
    o->value = c->best_first;
    o->unit = CRU_UNIT_SECONDS;
    o->known = c->best_first != 0;
    break;
  case 6: o->value = c->best_flurry; break;
  case 7: o->value = c->best_streak; break;
  case 8: o->value = c->best_chain; break;
  case 9: o->value = c->best_nofail; break;
  case 10: o->value = c->best_swift; break;
  case 11:
    o->value = c->minutes;
    o->of = c->seconds;
    o->unit = CRU_UNIT_PLAYTIME;
    break;
  default:
    o->value = c->complete;
    o->unit = CRU_UNIT_MINUTES;
    o->known = c->complete != 0;
    break;
  }
}
