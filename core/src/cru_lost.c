/* Lost pieces (crucible_core.h, "lost pieces"): a piece that stops being owned cools down before it can form again.
 *
 * Every drop (cru_grant.c: a gift a faction keeps, a theft, a boss's hit, a miss that destroys, a split) of a piece some
 * recipe makes starts a cooldown of N mixes, N in CRU_LOST_MIN..CRU_LOST_MAX, hashed from the save's tint seed, the id
 * and the loss serial, so a save always deals the same N for the same loss. Until N other mixes have made something,
 * a pair that would make it opens as NOTHING with mix.lost set (cru_bench.c): the first attempt is CRU_LOST_FIRST (the
 * host plays the merge glitching and lets a character speak), later ones CRU_LOST_AGAIN (the bench's result cell is
 * CRU_K_GLITCH before the mix; the mix fails quietly). Such a mix makes, scores, tries and counts nothing. Getting the
 * piece back any other way (cru_grant) ends its cooldown.
 *
 * The table is c->lost, saved verbatim in the record at bytes 246..263 (cru_save.c), so it is CRC-covered, A/B, and
 * written by the same record write that carries a drop's journal. A save from before it has zeros there: read as empty.
 * Up to CRU_LOST_SLOTS entries, newest first; when full the oldest drops. Entries are packed (a free one, left 0, is
 * never followed by a used one). */
#include "cru_internal.h"

#define E_LEFT 2u
#define E_FLAGS 3u

static uint8_t *entry(crucible_core *c, uint8_t k) { return c->lost + CRU_LOST_HEAD + (uint8_t)(k << 2); }
static uint16_t eid(const uint8_t *e) {
  uint8_t lo = e[0], hi = e[1];
  return (uint16_t)(lo | ((uint16_t)hi << 8));
}
static void copy4(uint8_t *to, const uint8_t *from) {
  to[0] = from[0];
  to[1] = from[1];
  to[2] = from[2];
  to[3] = from[3];
}
/* entry k out: the later ones move up, the last is freed */
static void remove_at(crucible_core *c, uint8_t k) {
  uint8_t *e;
  for (; (uint8_t)(k + 1u) < CRU_LOST_SLOTS; k++) copy4(entry(c, k), entry(c, (uint8_t)(k + 1u)));
  e = entry(c, CRU_LOST_SLOTS - 1u);
  e[0] = e[1] = e[2] = e[3] = 0;
}
/* the entry cooling id, or CRU_LOST_SLOTS */
static uint8_t find(crucible_core *c, uint16_t id) {
  uint8_t k, *e;
  for (k = 0; k < CRU_LOST_SLOTS; k++) {
    e = entry(c, k);
    if (!e[E_LEFT]) break;
    if (eid(e) == id) return k;
  }
  return CRU_LOST_SLOTS;
}

/* N for a loss: shifts and xors only (no multiply or divide on the SM83), then 0..8 by subtraction */
static uint8_t span(uint16_t seed, uint16_t id, uint8_t serial) {
  uint16_t x = (uint16_t)(seed ^ 0x9e37u ^ (uint16_t)(id << 5) ^ (uint16_t)(id >> 3) ^
                          (uint16_t)((uint16_t)serial << 9) ^ serial);
  uint8_t h, i;
  for (i = 0; i < 2u; i++) {
    x ^= (uint16_t)(x << 7);
    x ^= (uint16_t)(x >> 9);
    x ^= (uint16_t)(x << 8);
  }
  h = (uint8_t)(x ^ (x >> 8));
  while (h >= (uint8_t)(CRU_LOST_MAX - CRU_LOST_MIN + 1u)) h = (uint8_t)(h - (CRU_LOST_MAX - CRU_LOST_MIN + 1u));
  return (uint8_t)(CRU_LOST_MIN + h);
}
uint8_t cru_lost_span(uint16_t seed, uint16_t id, uint8_t serial) CORE_BANKED { return span(seed, id, serial); }

/* A drop: id starts cooling (again, if it was), as the newest entry. Pieces nothing makes are not tracked (no recipe
 * could ever bring them back, so they would only push out a real one). The caller writes the record. */
void cri_lost_mark(crucible_core *c, uint16_t id) CORE_LOCAL {
  uint8_t k, serial, *e;
  if (cri_route_first(c, id) == cri_route_first(c, (uint16_t)(id + 1u))) return;
  k = find(c, id);
  if (k < CRU_LOST_SLOTS) remove_at(c, k);
  for (k = CRU_LOST_SLOTS - 1u; k; k--) copy4(entry(c, k), entry(c, (uint8_t)(k - 1u)));
  serial = (uint8_t)(c->lost[1] + 1u);
  c->lost[0] = CRU_LOST_VERSION;
  c->lost[1] = serial;
  e = entry(c, 0);
  e[0] = (uint8_t)id;
  e[1] = (uint8_t)(id >> 8);
  e[E_LEFT] = span(c->variant_seed, id, serial);
  e[E_FLAGS] = 0;
}
/* owned again some other way (a grant): its cooldown ends */
void cri_lost_regain(crucible_core *c, uint16_t id) CORE_LOCAL {
  uint8_t k = find(c, id);
  if (k < CRU_LOST_SLOTS) remove_at(c, k);
}
/* CRU_LOST_NONE, FIRST or AGAIN for a result */
uint8_t cri_lost_check(crucible_core *c, uint16_t id) CORE_LOCAL {
  uint8_t k, f;
  if (!c->lost[CRU_LOST_HEAD + E_LEFT]) return CRU_LOST_NONE; /* the common case: nothing cooling */
  k = find(c, id);
  if (k >= CRU_LOST_SLOTS) return CRU_LOST_NONE;
  f = entry(c, k)[E_FLAGS];
  if (f & CRU_LOST_WARNED) return CRU_LOST_AGAIN;
  return CRU_LOST_FIRST;
}
/* the first attempt has glitched: later ones are cued on the bench */
void cri_lost_warn(crucible_core *c, uint16_t id) CORE_LOCAL {
  uint8_t k = find(c, id), *e;
  if (k >= CRU_LOST_SLOTS) return;
  e = entry(c, k);
  e[E_FLAGS] = (uint8_t)(e[E_FLAGS] | CRU_LOST_WARNED);
}
/* a mix made something: every cooldown moves one on; a finished one frees its entry */
void cri_lost_combo(crucible_core *c) CORE_LOCAL {
  uint8_t k = 0, *e, left;
  while (k < CRU_LOST_SLOTS) {
    e = entry(c, k);
    left = e[E_LEFT];
    if (!left) break;
    left--;
    e[E_LEFT] = left;
    if (left)
      k++;
    else
      remove_at(c, k);
  }
}
/* After a record is read (cru_load): a save from before the table (version 0), an unknown version or a damaged entry
 * reads as nothing cooling; an entry for a piece owned now, out of the catalogue, or past CRU_LOST_MAX goes. */
void cri_lost_settle(crucible_core *c) CORE_LOCAL {
  uint8_t k, n = 0, *e, *to;
  if (c->lost[0] != CRU_LOST_VERSION)
    for (k = 0; k < CRU_LOST_BYTES; k++) c->lost[k] = 0;
  c->lost[0] = CRU_LOST_VERSION;
  for (k = 0; k < CRU_LOST_SLOTS; k++) { /* keep the sound entries, packed in order */
    e = entry(c, k);
    if (!e[E_LEFT] || e[E_LEFT] > CRU_LOST_MAX || eid(e) >= c->items) continue;
    if (cri_owned(c, eid(e))) continue;
    e[E_FLAGS] = (uint8_t)(e[E_FLAGS] & CRU_LOST_WARNED);
    to = entry(c, n);
    if (to != e) copy4(to, e);
    n++;
  }
  for (; n < CRU_LOST_SLOTS; n++) {
    e = entry(c, n);
    e[0] = e[1] = e[2] = e[3] = 0;
  }
}

uint8_t cru_lost_state(crucible_core *c, uint16_t id) CORE_BANKED { return cri_lost_check(c, id); }
uint8_t cru_lost_left(crucible_core *c, uint16_t id) CORE_BANKED {
  uint8_t k;
  if (!c->lost[CRU_LOST_HEAD + E_LEFT]) return 0;
  k = find(c, id);
  if (k >= CRU_LOST_SLOTS) return 0;
  return entry(c, k)[E_LEFT];
}
uint8_t cru_lost_cue(crucible_core *c, uint16_t a, uint16_t b) CORE_BANKED {
  uint16_t row;
  if (!c->lost[CRU_LOST_HEAD + E_LEFT] || a >= c->items || b >= c->items) return 0;
  row = cri_find(c, a, b);
  if (row == CRU_NONE) return 0;
  return (uint8_t)(cri_lost_check(c, cri_result(c, row)) == CRU_LOST_AGAIN);
}
