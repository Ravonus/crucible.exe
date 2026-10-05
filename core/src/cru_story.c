/* Story runs (CRUCIBLE.EXE's dreamscape mode): a second game beside Classic, on the same rules, in its own save.
 *
 * A story run is an ordinary crucible_core whose byte store is cru_story_store: it maps the parts of the WIDE save
 * layout a run needs (the two records, the play block and journal, OWNED and RBITS) into SRAM bank 15, which the
 * Classic save never touches, and drops the rest (use counts and the dud filter read as empty, writes are ignored).
 * So the bench, the shelf, mixing, the book, feats and the journal all work unchanged, and the Classic save is never
 * written while a run is open. What the run adds lives in a small record at the end of bank 15 (crucible_story):
 * the run seed (re-hashed with every event), the loss scale, story flags, character moods, locked elements, a clock.
 *
 *   bank 15 (0x1E000):  0x0000 bank 0's 0x0500-0x0FFF (records, play block; a small catalogue's areas)
 *                       0x0B00 OWNED   0x1000 RBITS (a wide catalogue's)   0x1D00 the story record */
#include "cru_internal.h"

/* three run slots: SRAM banks 15, 1 and 3 (none of them written by the Classic save). The slot's place travels in
 * the caller's crucible_story_link (the story store's context), so the core keeps no state of its own. */
static const uint32_t SLOT_BASE[CRU_STORY_SLOTS] = {0x1E000ul, 0x02000ul, 0x06000ul};
static uint32_t slot_base(uint8_t slot) { return SLOT_BASE[slot < CRU_STORY_SLOTS ? slot : 0u]; }
#define S_REC 0x1D00u
#define S_MAGIC0 'D'
#define S_MAGIC1 'R'
#define pick cri_story_pick

static uint32_t map(uint32_t sb, uint32_t at) {
  /* bank 0 from the records to the play block: the records, and a small catalogue's OWNED / RBITS areas */
  if (at >= P_V4_A && at < 0x1000u) return sb + (at - P_V4_A);
  /* a wide catalogue's areas: as much of each slot as 10,240 ids and 22,528 recipes need (the rest reads as zero) */
  if (at >= 0x10000ul + W_OWNED && at < 0x10000ul + W_OWNED + 0x500u) return sb + 0xB00u + (at - (0x10000ul + W_OWNED));
  if (at >= 0x10000ul + W_RBITS && at < 0x10000ul + W_RBITS + 0xB00u)
    return sb + 0x1000u + (at - (0x10000ul + W_RBITS));
  return 0xfffffffful;
}
static uint8_t s_read(void *ctx, uint32_t at) {
  const crucible_story_link *l = (const crucible_story_link *)ctx;
  uint32_t m = map(slot_base(l->slot), at);
  if (m != 0xfffffffful) return l->base->read(l->base->ctx, m);
  if ((at >= 0x10000ul + W_SPILL && at < 0x10000ul + W_END) || (at >= P_SPILL && at < P_USES))
    return 0xffu; /* empty spill tables */
  return 0;
}
static void s_write(void *ctx, uint32_t at, uint8_t v) {
  const crucible_story_link *l = (const crucible_story_link *)ctx;
  uint32_t m = map(slot_base(l->slot), at);
  if (m != 0xfffffffful) l->base->write(l->base->ctx, m, v);
}
void cru_story_store(crucible_story_link *link, crucible_store *out) CORE_BANKED {
  out->read = s_read;
  out->write = s_write;
  out->ctx = (void *)link;
}

/* ---- the story record ---- */
static uint16_t crc(const uint8_t *p, uint8_t n) {
  uint16_t h = 0xffffu;
  uint8_t i;
  while (n--) {
    h ^= *p++;
    for (i = 0; i < 8u; i++) h = (h & 1u) ? (uint16_t)((h >> 1) ^ 0xa001u) : (uint16_t)(h >> 1);
  }
  return h;
}
static void rec_write(const crucible_store *b, uint32_t r, const crucible_story *s) {
  const uint8_t *p = (const uint8_t *)s;
  uint8_t i;
  uint16_t h = crc(p, (uint8_t)sizeof *s);
  b->write(b->ctx, r, S_MAGIC0);
  b->write(b->ctx, r + 1u, S_MAGIC1);
  for (i = 0; i < (uint8_t)sizeof *s; i++) b->write(b->ctx, r + 2u + i, p[i]);
  b->write(b->ctx, r + 2u + sizeof *s, (uint8_t)h);
  b->write(b->ctx, r + 3u + sizeof *s, (uint8_t)(h >> 8));
}
static uint8_t rec_read(const crucible_store *b, uint32_t r, crucible_story *s) {
  uint8_t *p = (uint8_t *)s, i;
  uint16_t h;
  if (b->read(b->ctx, r) != S_MAGIC0 || b->read(b->ctx, r + 1u) != S_MAGIC1) return 0;
  for (i = 0; i < (uint8_t)sizeof *s; i++) p[i] = b->read(b->ctx, r + 2u + i);
  h = (uint16_t)(b->read(b->ctx, r + 2u + sizeof *s) | ((uint16_t)b->read(b->ctx, r + 3u + sizeof *s) << 8));
  return h == crc(p, (uint8_t)sizeof *s) && s->version == CRU_STORY_VERSION;
}
void cru_story_save(const crucible_story_link *link, const crucible_story *s) CORE_BANKED {
  rec_write(link->base, slot_base(link->slot) + S_REC, s);
}
uint8_t cru_story_peek(const crucible_store *base, uint8_t slot, crucible_story *out) CORE_BANKED {
  return rec_read(base, slot_base(slot) + S_REC, out);
}
/* A run lost for good (or abandoned): its record and its save headers go, so the slot opens fresh next time. */
void cru_story_wipe(const crucible_store *base, uint8_t slot) CORE_BANKED {
  uint32_t sb = slot_base(slot);
  uint16_t i;
  for (i = 0; i < 0x1B00u; i++)
    base->write(base->ctx, sb + i, 0); /* records, play block, OWNED and RBITS: a run starts from zero */
  base->write(base->ctx, sb + S_REC, 0);
  base->write(base->ctx, sb + S_REC + 1u, 0);
}

/* ---- the run seed: xorshift32 stirred with each event (shifts only) ---- */
static uint32_t stir(uint32_t x, uint16_t v) {
  x ^= v;
  x ^= (uint32_t)v << 16;
  if (!x) x = 0x9e3779b9ul;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return x;
}
void cru_story_event(crucible_story *s, uint8_t kind, uint16_t a, uint16_t b) CORE_BANKED {
  s->seed = stir(stir(s->seed, (uint16_t)((uint16_t)kind << 8 | (uint8_t)a)), (uint16_t)(a ^ (b << 3)));
}
uint16_t cri_story_pick(crucible_story *s,
                        uint16_t n) CORE_LOCAL { /* 0..n-1 (n >= 1): a draw under the next power of two, retried */
  uint16_t m = 1, r;
  while (m < n) m = (uint16_t)(m << 1);
  m = (uint16_t)(m - 1u);
  do {
    s->seed = stir(s->seed, 0x5a5au);
    r = (uint16_t)s->seed & m;
  } while (r >= n);
  return r;
}

/* ---- a run ---- */
static uint8_t locked(const crucible_story *s, uint16_t id) {
  uint8_t i;
  for (i = 0; i < CRU_STORY_LOCKS; i++)
    if (s->locks[i] == id) return 1;
  return 0;
}
uint8_t cru_story_open(crucible_core *c, crucible_story *s, const crucible_tables *t, crucible_story_link *link,
                       crucible_store *story_store, uint8_t entropy, uint8_t scale) CORE_BANKED {
  uint8_t fresh, i, got;
  uint16_t id, tries;
  if (!story_store->read) cru_story_store(link, story_store); /* a banked cartridge passes its own bank-0 store */
  cru_init(c, t, story_store, CRU_LAYOUT_128K);
  c->lean = 1;
  fresh = cru_load(c, entropy) == CRU_LOAD_FRESH;
  if (!fresh && rec_read(link->base, slot_base(link->slot) + S_REC, s)) return 0;
  /* a new run: the four classics (the catalogue's starters) and two seeded extras made straight from them */
  for (i = 0; i < (uint8_t)sizeof *s; i++) ((uint8_t *)s)[i] = 0;
  s->version = CRU_STORY_VERSION;
  s->scale = scale > CRU_STORY_HARSH ? CRU_STORY_NORMAL : scale;
  s->seed = stir(stir(0x2545f491ul, entropy), c->variant_seed);
  for (i = 0; i < CRU_STORY_LOCKS; i++) s->locks[i] = CRU_NONE;
  for (i = 0; i < CRU_STORY_MEMORY; i++) s->memory[i] = CRU_NONE;
  s->lucid = 220u;
  s->nemesis.next = s->nemesis.stolen = CRU_NONE;
  {
    uint8_t a, v;
    for (a = 0; a < 3u; a++)
      for (v = 0; v < 8u; v++) s->lean[a][v] = (int16_t)pick(s, 4u);
  } /* the seed's faint tilt */
  /* each from a seeded start, the next depth-1 element along the catalogue (a scan always finds one) */
  for (got = 0; got < 2u; got++)
    for (id = pick(s, c->items), tries = 0; tries < c->items;
         tries++, id = (uint16_t)(id + 1u >= c->items ? 0u : id + 1u))
      if (cri_depth(c, id) == 1u && !cru_owned(c, id)) {
        cru_grant(c, id, (uint8_t)s->seed);
        break;
      }
  rec_write(link->base, slot_base(link->slot) + S_REC, s);
  return 1;
}
/* An element back into the two it is made of: the first recipe that makes it, its ingredients granted, it dropped
 * (a starter stays). 1: split. */
uint8_t cru_story_split(crucible_core *c, crucible_story *s, uint16_t id, uint16_t *ab) CORE_BANKED {
  uint16_t first, row, a, b;
  if (id >= c->items || !cru_owned(c, id)) return 0;
  first = cri_route_first(c, id);
  if (first == cri_route_first(c, (uint16_t)(id + 1u))) return 0; /* nothing makes it */
  row = cri_route(c, first);
  a = cri_recipe_a(c, row);
  b = cri_recipe_b(c, row);
  if (!locked(s, a)) cru_grant(c, a, (uint8_t)s->seed);
  if (!locked(s, b)) cru_grant(c, b, (uint8_t)s->seed);
  cru_drop(c, id);
  if (ab) {
    ab[0] = a;
    ab[1] = b;
  }
  s->clock = (uint16_t)(s->clock + CRU_STORY_SPLIT_COST);
  cru_story_event(s, CRU_EV_SPLIT, id, a);
  cru_story_act(s, CRU_ACT_SPLIT);
  return 1;
}
/* A mistake (a failed puzzle, a wrong answer, a glitch), by the run's loss scale: Gentle costs clock only; Normal one
 * element; Harsh one to three, the last locked away for the rest of the run. Starters are never lost. Returns how many
 * elements went; lost[] receives them. */
uint8_t cru_story_loss(crucible_core *c, crucible_story *s, uint16_t *lost, uint8_t max) CORE_BANKED {
  uint8_t n, k, gone = 0;
  uint16_t at, start, id = CRU_NONE, tries;
  s->clock = (uint16_t)(s->clock + CRU_STORY_MISTAKE_COST);
  cru_story_event(s, CRU_EV_LOSS, s->scale, 0);
  cru_story_act(s, CRU_ACT_LOSS);
  if (s->scale == CRU_STORY_GENTLE) return 0;
  n = s->scale == CRU_STORY_HARSH ? (uint8_t)(1u + pick(s, 3u)) : 1u;
  if (n > max) n = max;
  for (k = 0; k < n; k++) {
    start = pick(s, c->items);
    for (tries = 0, at = start; tries < c->items; tries++) {
      id = cri_shelf(c, at);
      if (cru_owned(c, id) && !cri_starter(c, id)) break;
      if (++at >= c->items) at = 0;
    }
    if (tries >= c->items || !cru_drop(c, id)) break;
    if (lost) lost[gone] = id;
    gone++;
    if (s->scale == CRU_STORY_HARSH && k + 1u == n) {
      uint8_t i;
      for (i = 0; i < CRU_STORY_LOCKS; i++)
        if (s->locks[i] == CRU_NONE) {
          s->locks[i] = id;
          break;
        }
    }
  }
  return gone;
}
/* A reward: id owned (unless locked this run). */
uint8_t cru_story_gain(crucible_core *c, crucible_story *s, uint16_t id) CORE_BANKED {
  if (locked(s, id)) return 0;
  cru_story_event(s, CRU_EV_GAIN, id, 0);
  return cru_grant(c, id, (uint8_t)s->seed);
}
uint8_t cru_story_flag(const crucible_story *s, uint8_t f) CORE_BANKED {
  return (uint8_t)((s->flags[f >> 3] >> (f & 7u)) & 1u);
}
void cru_story_set_flag(crucible_story *s, uint8_t f) CORE_BANKED {
  s->flags[f >> 3] |= (uint8_t)(1u << (f & 7u));
  cru_story_event(s, CRU_EV_FLAG, f, 0);
}
