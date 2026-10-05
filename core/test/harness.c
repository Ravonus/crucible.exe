#include <string.h>
#include "harness.h"
#include "../generated/crucible_tables_world.h"

int t_checks, t_fails;

static uint8_t ms_read(void *ctx, uint32_t at) {
  mem_store *m = (mem_store *)ctx;
  if (at >= IMAGE) {
    fprintf(stderr, "read out of the image at 0x%x\n", (unsigned)at);
    t_fails++;
    return 0;
  }
  return m->mem[at];
}
static void ms_write(void *ctx, uint32_t at, uint8_t v) {
  mem_store *m = (mem_store *)ctx;
  if (at >= IMAGE) {
    fprintf(stderr, "write out of the image at 0x%x\n", (unsigned)at);
    t_fails++;
    return;
  }
  m->writes++;
  if (m->budget == 0) return; /* the power is off */
  if (m->budget > 0) m->budget--;
  m->mem[at] = v;
}
void ms_reset(mem_store *m) {
  memset(m->mem, 0, IMAGE);
  m->budget = -1;
  m->writes = 0;
}
crucible_store ms_store(mem_store *m) {
  crucible_store s;
  s.read = ms_read;
  s.write = ms_write;
  s.ctx = m;
  return s;
}

const crucible_tables world = CRUCIBLE_WORLD_TABLES(&cru_rules_world);
/* the same tables with tried bit = sorted row (crucible_play.c's indexing), for the agreement test */
static const uint16_t row_ids[CRUCIBLE_WORLD_RECIPES] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69};
const crucible_tables world_rows = {
    CRUCIBLE_WORLD_ITEMS,       CRUCIBLE_WORLD_RECIPES,     CRUCIBLE_WORLD_CATEGORIES,  crucible_world_category,
    crucible_world_traits,      crucible_world_starters,    crucible_world_shelf,       crucible_world_shelf_pos,
    crucible_world_recipe_a,    crucible_world_recipe_b,    crucible_world_recipe_r,    row_ids,
    crucible_world_recipe_swap, crucible_world_depth,       crucible_world_route_first, crucible_world_route_list,
    &cru_rules_world,           crucible_world_trait_shelf, crucible_world_trait_rank,  CRUCIBLE_WORLD_FACTS};

uint16_t id_of(const char *name) {
  uint16_t i;
  for (i = 0; i < CRUCIBLE_WORLD_ITEMS; i++)
    if (!strcmp(crucible_world_names[i], name)) return i;
  fprintf(stderr, "unknown item %s\n", name);
  t_fails++;
  return 0;
}
const char *name_of(uint16_t id) { return id < CRUCIBLE_WORLD_ITEMS ? crucible_world_names[id] : "-"; }

uint8_t boot(crucible_core *c, mem_store *m, const crucible_tables *t, uint8_t layout, uint8_t entropy) {
  crucible_store s = ms_store(m);
  memset(c, 0xa5, sizeof *c); /* the core must not depend on the caller clearing it */
  cru_init(c, t, &s, layout);
  return cru_load(c, entropy);
}
uint8_t boot_clean(crucible_core *c, mem_store *m, uint8_t layout) {
  uint8_t e;
  for (e = 0; e < 255u; e++) {
    ms_reset(m);
    boot(c, m, &world, layout, e);
    if (!c->gilded) return e;
  }
  return 0;
}
uint8_t mixi(crucible_core *c, uint16_t a, uint16_t b) {
  uint8_t o = cru_mix_begin(c, a, b, 0x3c);
  tick_frames(c, 60); /* a merge animation takes about a second */
  cru_mix_finish(c);
  return o;
}
uint8_t mix(crucible_core *c, const char *a, const char *b) { return mixi(c, id_of(a), id_of(b)); }
void tick_frames(crucible_core *c, unsigned frames) {
  while (frames >= 6u) {
    cru_tick(c, 6);
    frames -= 6u;
  }
  if (frames) cru_tick(c, (uint8_t)frames);
}
void drain_toasts(crucible_core *c) {
  while (cru_toast_peek(c) != CRU_TOAST_NONE) cru_toast_done(c);
}
uint8_t has_toast(const crucible_core *c, uint8_t code) {
  uint8_t i;
  for (i = 0; i < c->toast_n; i++)
    if (c->toast[(uint8_t)(c->toast_first + i) & 7u] == code) return 1;
  return 0;
}
long feat_points(const crucible_core *c) {
  long p = 0;
  uint8_t f, k;
  for (f = 0; f < CRU_FEATS; f++)
    for (k = 0; k < c->feats[f]; k++) p += c->t->rules->tier_points[k];
  return p;
}
