/* RESET GAME (cru_reset in crucible_core.h). Every bit of progress goes --
 * owned items, tried pairs and duds, use counts, points, feats, titles, stats, the leaderboard -- and the save starts over
 * as a fresh one would (the starters owned with new tints), keeping only the options byte, the player's name and the
 * record sequence. Then the bench opens as at power-on.
 *
 * A reset is not torn-write safe: a power cut in the middle loads as a recounted save (the areas
 * cleared, the last record's counters). It runs only when the player holds A on RESET GAME. */
#include "cru_internal.h"

void cru_reset(crucible_core *c, uint8_t entropy) CORE_BANKED {
  uint8_t options = c->options, i, *p, *e;
  uint16_t sequence = c->sequence, id;
  char name[CRU_NAME];
  for (i = 0; i < CRU_NAME; i++) name[i] = c->name[i];
  /* the state from the first saved field on (cru_save.c clear_state); the catalogue, store and layout stay */
  p = (uint8_t *)&c->sequence;
  e = (uint8_t *)(c + 1);
  while (p < e) *p++ = 0;
  c->last_new = c->focus = c->slot_a = c->slot_b = CRU_NONE;
  c->options = options;
  c->sequence = sequence;
  for (i = 0; i < CRU_NAME; i++) c->name[i] = name[i];
  /* a fresh play memory owning the starters, each with a new tint (cru_load's fresh path) */
  c->rng = (uint16_t)(0x93d1u ^ entropy);
  cri_play_init(c, (const uint8_t *)0);
  for (id = 0; id < c->items; id++)
    if (cri_starter(c, id)) cri_set_variant(c, id, cri_roll(c, entropy));
  c->variant_seed = c->rng;
  cri_write(c); /* the fresh record */
  cri_play_commit(c); /* clears both dud generations, as a fresh save's power-on */
  cri_write(c);
  /* the bench opens as at power-on (cru_load) */
  cri_derive(c);
  c->boot_points = c->points;
  c->boot_found = c->found[0];
  c->focus = cri_shelf(c, 0);
  if (!cri_owned(c, c->focus)) c->focus = cri_step(c, c->focus, 1);
  c->message = CRU_MSG_NAME;
  cri_feats_reset(c);
  if (cri_check(c)) cri_save(c); /* a starter that rolled gilded earns GILDED I at once */
}
