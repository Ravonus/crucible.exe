/* The bench: shelf moves, slots, the result preview, the name sign, mixing and its scoring; the filter screen's rules
 * and the recipe book's. */
#include "cru_internal.h"

CRI_SAT

/* Up/down: the first owned item of the next or previous category group (wraps). */
static uint16_t group(crucible_core *c, uint16_t id, int8_t d) {
  uint8_t cat = cri_category(c, id);
  uint16_t n = id, p, i;
  for (i = 0; i < c->items; i++) {
    n = cri_step(c, n, d);
    if (cri_category(c, n) != cat || n == id) break;
  }
  if (d < 0 && n != id) { /* landed on the previous group's last item: walk back to its first */
    cat = cri_category(c, n);
    for (i = 0; i < c->items; i++) {
      p = cri_step(c, n, -1);
      if (cri_category(c, p) != cat || p == n) break;
      n = p;
    }
  }
  return n;
}
uint16_t cru_shelf_group(crucible_core *c, uint16_t id, int8_t d) CORE_BANKED { return group(c, id, d); }

/* ---- filter ---- */
uint16_t cru_filter_found(const crucible_core *c, uint8_t f) CORE_BANKED {
  uint16_t v;
  if (f >= CRU_FILTERS) return 0;
  v = c->found[f];
  return v;
}
uint16_t cru_filter_total(const crucible_core *c, uint8_t f) CORE_BANKED {
  uint16_t v;
  if (f >= CRU_FILTERS) return 0;
  v = c->total[f];
  return v;
}
/* A tag with nothing found yet cannot be shown (DENY); a change is saved. */
uint8_t cru_filter_apply(crucible_core *c, uint8_t f) CORE_BANKED {
  uint16_t n;
  if (f >= CRU_FILTERS) return 0;
  n = c->found[f];
  if (!n) return 0;
  if (c->filter != f) {
    c->filter = f;
    cri_save(c);
  }
  return 1;
}
/* Closing the filter screen (applied or not): a focus the filter hides moves to the next owned item it shows. */
void cru_filter_close(crucible_core *c) CORE_BANKED {
  uint16_t focus = c->focus;
  uint8_t f = c->filter;
  if (cri_match(c, f, focus) && cri_owned(c, focus)) return;
  c->focus = cri_step(c, focus, 1);
  c->message = CRU_MSG_NAME; /* the sign follows the new focus ("=RESULT" belonged to the old one) */
}

/* ---- mixing ---- */
/* Decides the outcome when the merge starts (so the animation can differ): NOTHING without a recipe, NEW if the
 * result is not owned, ROUTE if it is but this pair was never tried, KNOWN otherwise. A NEW result's tint is
 * rolled here. */
static uint8_t begin(crucible_core *c, uint16_t a, uint16_t b, uint8_t entropy) {
  crucible_mix *m = &c->mix;
  m->a = a;
  m->b = b;
  m->awarded = 0;
  m->fresh = 0;
  m->flags = 0;
  m->toasts = 0;
  m->variant = 0;
  m->row = CRU_NONE;
  m->result = CRU_NONE;
  m->open = 0;
  m->lost = CRU_LOST_NONE;
  m->outcome = CRU_DENY;
  if (a >= c->items || b >= c->items) return CRU_DENY;
  if (!cri_owned(c, a) || !cri_owned(c, b)) return CRU_DENY;
  m->row = cri_find(c, a, b);
  if (m->row == CRU_NONE)
    m->outcome = CRU_NOTHING;
  else {
    m->result = cri_result(c, m->row);
    if (!cri_owned(c, m->result)) {
      m->outcome = CRU_NEW;
      m->lost = cri_lost_check(c, m->result);
      if (m->lost) m->outcome = CRU_NOTHING; /* a lost piece still cooling: it will not form (cru_lost.c) */
    } else if (!cri_tried(c, a, b, m->row))
      m->outcome = CRU_ROUTE;
    else
      m->outcome = CRU_KNOWN;
    if (m->outcome == CRU_NEW) cri_set_variant(c, m->result, cri_roll(c, entropy));
    m->variant = cri_variant(c, m->result);
  }
  m->open = 1;
  return m->outcome;
}
uint8_t cru_mix_begin(crucible_core *c, uint16_t a, uint16_t b, uint8_t entropy) CORE_BANKED {
  cri_save_flush(c);
  return begin(c, a, b, entropy);
}

static uint8_t finish(crucible_core *c, uint8_t later) {
  crucible_mix *m = &c->mix;
  uint8_t is_new, before;
  cri_save_flush(c);
  if (!m->open) return 0;
  m->open = 0;
  if (m->lost) {
    /* a lost piece that would not form: nothing is made, scored, tried or counted; the first attempt is remembered
     * (and saved), so later ones are cued on the bench. It lands as a miss does, without the NO RESULT sign. */
    if (m->lost == CRU_LOST_FIRST) {
      cri_lost_warn(c, m->result);
      cri_save(c);
    }
    if (c->slot_b != CRU_NONE) {
      c->focus = c->slot_b;
      c->slot_b = CRU_NONE;
      c->message = CRU_MSG_NAME;
    }
    return 0;
  }
  /* a pair tried before is a repeat: it never feeds streaks, fails or pace */
  m->fresh = 1;
  if (cri_tried(c, m->a, m->b, m->row)) m->fresh = 0;
  m->flags = cri_play_mix(c, m->a, m->b, m->row, m->result);
  if (c->made < 0xffffu) c->made++;
  is_new = m->outcome == CRU_NEW;
  m->awarded = cri_points(c, m->outcome);
  if (is_new)
    cri_count_new(c, m->result);
  else if (m->outcome == CRU_ROUTE && c->routes < 255u)
    c->routes++;
  c->points = cri_sat_add(c->points, m->awarded);
  before = c->toast_n;
  cri_feats_mix(c, m->a, m->b, m->result, is_new, m->fresh);
  m->toasts = (uint8_t)(c->toast_n - before);
  if (m->outcome != CRU_NOTHING) cri_lost_combo(c); /* something was made: lost pieces cool one mix more */
  if (later)
    cri_save_begin(c, 1u); /* the record over frames; the areas change once it is whole */
  else {
    cri_save(c); /* the record carries the staged journal */
    cri_play_commit(c);
  } /* then the areas change */
  /* on the bench a known or empty pair resolves where it stands: slot A stays, the second pick takes focus */
  if (c->slot_b != CRU_NONE && (m->outcome == CRU_KNOWN || m->outcome == CRU_NOTHING)) {
    c->focus = c->slot_b;
    c->slot_b = CRU_NONE;
    if (m->outcome == CRU_KNOWN)
      c->message = CRU_MSG_RESULT;
    else
      c->message = CRU_MSG_NO_RESULT;
  }
  return m->awarded;
}
uint8_t cru_mix_finish(crucible_core *c) CORE_BANKED { return finish(c, 0); }
uint8_t cru_mix_finish_later(crucible_core *c) CORE_BANKED { return finish(c, 1u); }

/* ---- the bench ---- */
uint8_t cru_bench_move(crucible_core *c, uint8_t dir) CORE_BANKED {
  uint16_t old = c->focus, now = old;
  if (dir == CRU_LEFT)
    now = cri_step(c, old, -1);
  else if (dir == CRU_RIGHT)
    now = cri_step(c, old, 1);
  else if (dir == CRU_UP)
    now = group(c, old, -1);
  else if (dir == CRU_DOWN)
    now = group(c, old, 1);
  c->focus = now;
  if (now == old) return 0;
  c->message = CRU_MSG_NAME;
  return 1;
}
/* A: slot A empty -> it takes the focus (the focus stays); set -> slot B = focus and the merge begins. */
uint8_t cru_bench_a(crucible_core *c, uint8_t entropy) CORE_BANKED {
  uint8_t o;
  c->message = CRU_MSG_NAME;
  if (c->slot_a == CRU_NONE) {
    c->slot_a = c->focus;
    return CRU_PICKED;
  }
  c->slot_b = c->focus;
  o = begin(c, c->slot_a, c->slot_b, entropy);
  if (o == CRU_DENY) c->slot_b = CRU_NONE;
  return o;
}
uint8_t cru_bench_b(crucible_core *c) CORE_BANKED {
  if (c->slot_a == CRU_NONE) return 0;
  c->slot_a = CRU_NONE;
  c->message = CRU_MSG_NAME;
  return 1;
}
/* After a NEW or ROUTE reveal: the result takes focus, both slots clear. */
void cru_reveal_close(crucible_core *c) CORE_BANKED {
  uint16_t r = c->mix.result;
  if (r != CRU_NONE) c->focus = r;
  c->slot_a = c->slot_b = CRU_NONE;
  c->message = CRU_MSG_NAME;
}
static void cell(crucible_cells *o, uint8_t i, uint8_t kind, uint16_t id) {
  o->kind[i] = kind;
  o->id[i] = id;
}
void cru_bench_cells(crucible_core *c, crucible_cells *o) CORE_BANKED {
  uint16_t row, l, n, a = c->slot_a, focus = c->focus;
  uint8_t msg = c->message;
  if (a == CRU_NONE) {
    cell(o, CRU_CA, CRU_K_EMPTY, CRU_NONE);
    cell(o, CRU_CB, CRU_K_EMPTY, CRU_NONE);
    cell(o, CRU_CR, CRU_K_QUESTION, CRU_NONE);
  } else {
    cell(o, CRU_CA, CRU_K_ITEM, a);
    cell(o, CRU_CB, CRU_K_ITEM, focus);
    /* the result preview: tried with a recipe shows it, tried without shows x, untried shows ? */
    row = cri_find(c, a, focus);
    if (row != CRU_NONE && cri_lost_check(c, cri_result(c, row)) == CRU_LOST_AGAIN)
      cell(o, CRU_CR, CRU_K_GLITCH, CRU_NONE);
    else if (!cri_tried(c, a, focus, row))
      cell(o, CRU_CR, CRU_K_QUESTION, CRU_NONE);
    else if (row == CRU_NONE)
      cell(o, CRU_CR, CRU_K_TRIED, CRU_NONE);
    else
      cell(o, CRU_CR, CRU_K_ITEM, cri_result(c, row));
  }
  /* neighbours: blank when equal to the focus, or (the next one) to the previous one */
  l = cri_step(c, focus, -1);
  n = cri_step(c, focus, 1);
  if (l == focus)
    cell(o, CRU_CL, CRU_K_BLANK, CRU_NONE);
  else
    cell(o, CRU_CL, CRU_K_ITEM, l);
  cell(o, CRU_CF, CRU_K_ITEM, focus);
  if (n == focus || n == l)
    cell(o, CRU_CN, CRU_K_BLANK, CRU_NONE);
  else
    cell(o, CRU_CN, CRU_K_ITEM, n);
  o->message = msg;
  o->sign = focus;
  if (msg == CRU_MSG_RESULT) {
    /* "=" + the result of slot A and the focus; when that pair makes nothing (the focus moved), the plain name */
    row = CRU_NONE;
    if (a != CRU_NONE) row = cri_find(c, a, focus);
    if (row != CRU_NONE)
      o->sign = cri_result(c, row);
    else
      o->message = CRU_MSG_NAME;
  } else if (msg == CRU_MSG_NO_RESULT)
    o->sign = CRU_NONE;
}

/* ---- recipe book ---- */
/* The rows: the whole catalogue in shelf order, narrowed by the filter (owned and unknown alike). */
/* The caption's recipe: the first tried recipe (authored order) that makes it, ingredients as authored. */
uint8_t cru_book_route(crucible_core *c, uint16_t id, uint16_t *ab) CORE_BANKED {
  uint16_t i, e, row, x;
  if (id >= c->items || !cri_owned(c, id)) return CRU_BOOK_UNKNOWN;
  e = cri_route_first(c, (uint16_t)(id + 1u));
  for (i = cri_route_first(c, id); i < e; i++) {
    row = cri_route(c, i);
    if (!cri_tried(c, 0, 0, row)) continue;
    ab[0] = cri_recipe_a(c, row);
    ab[1] = cri_recipe_b(c, row);
    if (cri_swapped(c, row)) {
      x = ab[0];
      ab[0] = ab[1];
      ab[1] = x;
    }
    return CRU_BOOK_RECIPE;
  }
  if (cri_starter(c, id)) return CRU_BOOK_ELEMENT;
  return CRU_BOOK_SHARED;
}
static void view(crucible_core *c) {
  uint16_t v = c->book_views;
  if (v < 0xffffu) c->book_views = (uint16_t)(v + 1u);
}
void cru_book_view(crucible_core *c) CORE_BANKED { view(c); }
void cru_book_open(crucible_core *c) CORE_BANKED {
  uint8_t f = c->flags;
  f |= CRU_FLAG_BOOK_SEEN;
  c->flags = f;
  view(c);
}
/* A on a row: an owned item takes the bench focus (slot A clears); an unknown one is denied. */
uint8_t cru_book_use(crucible_core *c, uint16_t id) CORE_BANKED {
  uint8_t f;
  if (id >= c->items || !cri_owned(c, id)) return 0;
  c->focus = id;
  f = c->filter;
  if (!cri_match(c, f, id)) c->filter = 0;
  c->slot_a = CRU_NONE;
  c->message = CRU_MSG_NAME;
  return 1;
}
