/* Host test harness for core: a tiny check runner, a memory byte store with power-cut injection (writes
 * after the budget are lost), the real world catalogue and session helpers. */
#ifndef CRU_HARNESS_H
#define CRU_HARNESS_H
#include <stdint.h>
#include <stdio.h>
#include "crucible_core.h"

extern int t_checks, t_fails;
#define CHECK(cond)                                                                                                    \
  do {                                                                                                                 \
    t_checks++;                                                                                                        \
    if (!(cond)) {                                                                                                     \
      t_fails++;                                                                                                       \
      fprintf(stderr, "  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);                                       \
    }                                                                                                                  \
  } while (0)
#define EQ(a, b)                                                                                                       \
  do {                                                                                                                 \
    long a_ = (long)(a), b_ = (long)(b);                                                                               \
    t_checks++;                                                                                                        \
    if (a_ != b_) {                                                                                                    \
      t_fails++;                                                                                                       \
      fprintf(stderr, "  %s:%d: %s = %ld, expected %s = %ld\n", __FILE__, __LINE__, #a, a_, #b, b_);                   \
    }                                                                                                                  \
  } while (0)
#define RUN(fn)                                                                                                        \
  do {                                                                                                                 \
    int f0_ = t_fails;                                                                                                 \
    fn();                                                                                                              \
    printf("  %-46s %s\n", #fn, t_fails == f0_ ? "ok" : "FAIL");                                                       \
  } while (0)

#define IMAGE (16u * 8192u) /* a 128 KB cartridge RAM image */
typedef struct {
  uint8_t mem[IMAGE];
  long budget; /* < 0: unlimited; otherwise writes left before the power cut */
  unsigned long writes; /* writes attempted */
} mem_store;
void ms_reset(mem_store *m);
crucible_store ms_store(mem_store *m);

extern const crucible_tables world; /* the bundled world (generated/crucible_tables_world.h) */
extern const crucible_tables
    world_rows; /* the same with recipe_bit NULL (tried bit = sorted row, as crucible_play.c) */
uint16_t id_of(const char *name);
const char *name_of(uint16_t id);

uint8_t boot(crucible_core *c, mem_store *m, const crucible_tables *t, uint8_t layout, uint8_t entropy);
uint8_t boot_clean(crucible_core *c, mem_store *m, uint8_t layout); /* fresh, no gilded starter */
uint8_t mixi(crucible_core *c, uint16_t a, uint16_t b);
uint8_t mix(crucible_core *c, const char *a, const char *b);
void tick_frames(crucible_core *c, unsigned frames);
void drain_toasts(crucible_core *c);
uint8_t has_toast(const crucible_core *c, uint8_t code);
/* points = sum of mix awards + the tier points of every feat tier reached */
long feat_points(const crucible_core *c);
#endif
