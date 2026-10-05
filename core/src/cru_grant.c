/* Grants: an item owned without a mix here (cru_grant in crucible_core.h). A discovery synced from another device of the
 * same account, or one carried over from an older save format, becomes owned with no mix points and no pace, while the
 * found counts, the tints, the feats and the save stay as consistent as after any mix.
 *
 * The one area byte a grant changes (its OWNED bit) is staged in the same write-ahead journal as a mix's (by
 * cri_play_grant in cru_play.c, which knows the placement), so a power cut leaves the grant either whole or absent:
 *   1. finish any journal an interrupted commit left (cri_play_commit), so the area bytes read here are current;
 *   2. stage the OWNED byte and keep the play block's counters and byte sum in step;
 *   3. write the record (it carries the journal), then commit (the area changes). */
#include "cru_internal.h"

CRI_READ

static uint8_t lv(crucible_core *c, uint8_t o) { return cri_rd(c, (uint16_t)(P_LIVE + o)); }

uint8_t cru_grant(crucible_core *c, uint16_t id, uint8_t source) CORE_BANKED {
  uint16_t minutes;
  if (id >= c->items || c->mix.open) return 0;
  if (lv(c, L_JN) || lv(c, L_JDUD) || lv(c, L_CLEARING)) cri_play_commit(c);
  if (cri_owned(c, id)) return 0;
  /* the OWNED bit (by id in v4, by shelf position in v5) staged in the journal, with the play block's counters and
   * byte sum (cru_play.c) */
  cri_play_grant(c, id);
  /* its tint, the per-filter found counts, gilded and the shelf summary, as a discovery has them */
  cri_set_variant(c, id, cri_roll(c, source));
  cri_count_new(c, id);
  cri_lost_regain(c, id); /* back some other way: no cooldown */
  c->boot_found++; /* not a find of this run: the leaderboard row counts the run's own finds */
  if (!c->complete && c->found[0] >= c->items) {
    minutes = c->minutes;
    if (!minutes) minutes = 1;
    c->complete = minutes;
  }
  cri_check(c); /* a tier it completes lands now, with its points and toast (as at power-on) */
  cri_save(c); /* the record carries the staged byte */
  cri_play_commit(c); /* then the area changes */
  return 1;
}

/* A drop: an owned item stops being owned (a split, or a loss in a story run). Its discovery history stays (the recipes
 * tried, its use count); the found counts are derived again (O(items), a rare event). Journalled like a grant. 1:
 * dropped; 0: not owned, a starter, out of range, or a mix is open. */
static uint8_t drop(crucible_core *c, uint16_t id, uint8_t starters) {
  if (id >= c->items || c->mix.open) return 0;
  if (lv(c, L_JN) || lv(c, L_JDUD) || lv(c, L_CLEARING)) cri_play_commit(c);
  if (!cri_owned(c, id) || (!starters && cri_starter(c, id))) return 0;
  cri_play_drop(c, id);
  cri_lost_mark(c, id); /* it cools down before a mix can make it again (cru_lost.c): saved with the drop */
  cri_save(c);
  cri_play_commit(c);
  cri_derive(c);
  if (c->focus == id || c->slot_a == id) {
    c->slot_a = CRU_NONE;
    c->focus = cru_shelf_step(c, id, 1);
  }
  return 1;
}
uint8_t cru_drop(crucible_core *c, uint16_t id) CORE_BANKED { return drop(c, id, 0); }
uint8_t cru_drop_any(crucible_core *c, uint16_t id) CORE_BANKED { return drop(c, id, 1); }
