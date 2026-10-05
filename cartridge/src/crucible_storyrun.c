/* Story runs on the cartridge: three slots (core cru_story.c: SRAM banks 15, 1, 3). A run is the same crucible_core
 * the bench plays, opened on the slot's mapped store; its saga (factions, dream, truths) is the TALK screen's
 * crucible_story. Free play is the Classic save, reloaded when the run is left. After every mix the run reacts: a
 * miss may destroy one of the two (the chain grows), a make may bring a visitor, and running out ends it for good. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_data.h"
#include "crucible_state.h"
#include "crucible_codec.h"
#include "crucible_storyrun.h"
#include "crucible_ui.h"
#include "crucible_time.h"
#include "crucible_player.h"
#include "crucible_link.h"
#include "crucible_avatar.h"
extern const crucible_tables cru_world_tables;
uint8_t story_on, story_slot_at;
static crucible_store base_, run_store_;
static crucible_story_link link_;
static uint16_t lost_;
static uint8_t since_talk_;

/* a slot to zero in one go (its whole SRAM bank up to the record) */
static void wipe(uint8_t slot) {
  ENABLE_RAM;
  SWITCH_RAM(slot == 1u ? 1u : slot == 2u ? 3u : 15u);
  memset((void *)0xA000u, 0, 0x1D80u);
  SWITCH_RAM(0);
  DISABLE_RAM;
}
static void base(void) {
  base_.read = crucible_sram_read;
  base_.write = crucible_sram_write;
  base_.ctx = 0;
}
uint8_t story_peek(uint8_t slot, crucible_story *out) BANKED {
  base();
  return cru_story_peek(&base_, slot, out);
}
static uint8_t open_(uint8_t slot, uint8_t entropy, uint8_t scale) {
  crucible_story *s = talk_saga();
  base();
  link_.base = &base_;
  link_.slot = slot;
  story_slot_at = slot;
  run_store_.read = crucible_story_read;
  run_store_.write = crucible_story_write;
  run_store_.ctx = &link_; /* bank 0 */
  story_on = 1;
  since_talk_ = 0;
  return cru_story_open(&core, s, &cru_world_tables, &link_, &run_store_, entropy, scale);
}
/* A new run from the wake-up scene: its name, how hard it holds, and everything pressed while naming (hash). */
void story_begin(uint8_t slot, uint8_t scale, uint32_t hash, const char *name) BANKED {
  crucible_story *s;
  wipe(slot);
  open_(slot, (uint8_t)(hash ^ (hash >> 8) ^ DIV_REG), scale);
  s = talk_saga();
  cru_story_event(s, CRU_EV_CHOICE, (uint16_t)hash, (uint16_t)(hash >> 16)); /* the naming seeds the dream */
  cru_set_name(&core, name);
  cru_save(&core);
  memset(s->name, ' ', sizeof s->name);
  memcpy(s->name, name, strlen(name) < sizeof s->name ? strlen(name) : sizeof s->name);
  talk_saga_ready();
  cru_story_save(&link_, s);
  player_story_new(slot, av_genome_set ? av_genome : 0);
  av_genome_set = 0; /* who you are in this run (fight spec 10.4) */
  time_story_opened(slot, 0);
}
uint8_t story_resume(uint8_t slot) BANKED {
  crucible_story peek;
  char n[CRU_NAME + 1u];
  if (!story_peek(slot, &peek)) return 0;
  open_(slot, DIV_REG, CRU_STORY_NORMAL);
  talk_saga_ready();
  player_story_load(slot,
                    talk_saga()->seed); /* an older run has no player record yet: defaults, its face from its seed */
  avatar_cache(pl.genome);
  memcpy(n, talk_saga()->name, CRU_NAME);
  n[CRU_NAME] = 0;
  cru_set_name(&core, n); /* the slot's name (it may have been renamed) */
  time_story_opened(slot, 1); /* the time away reaches the run once */
  return 1;
}
/* A slot's name, changed on the slot list: the name lives with the run (name: CRU_NAME letters in RAM, blank-padded). */
void story_rename(uint8_t slot, const char *name) BANKED {
  crucible_story st;
  crucible_story_link l;
  char n[CRU_NAME + 1u];
  memcpy(n, name, CRU_NAME);
  n[CRU_NAME] = 0;
  /* the open run: its saga in RAM is newer than the slot's record */
  if (story_on && story_slot_at == slot) {
    memcpy(talk_saga()->name, n, CRU_NAME);
    story_save();
    cru_set_name(&core, n);
    cru_save(&core);
    return;
  }
  base();
  if (!cru_story_peek(&base_, slot, &st)) return;
  memcpy(st.name, n, CRU_NAME);
  l.base = &base_;
  l.slot = slot;
  cru_story_save(&l, &st);
}
void story_save(void) BANKED {
  if (story_on) cru_story_save(&link_, talk_saga());
}
/* Back to free play: the Classic save again (the run is saved where it stands). */
void story_leave(void) BANKED {
  if (!story_on) return;
  story_save();
  story_on = 0;
  talk_saga_reset();
  time_free_play();
  save_boot();
}
/* The run lost for good: its slot is wiped, and free play returns. */
void story_end(void) BANKED {
  if (talk_saga()->lucid == 0u && pl_slot != 0xffu) {
    pl.flags |= PF_GLITCH;
    player_unlock(47);
    player_save();
  } /* lost in the dream: the glitch mark, kept by the slot's next run (8.4) */
  story_on = 0;
  wipe(story_slot_at);
  talk_saga_reset();
  time_free_play();
  save_boot();
}
uint16_t story_lost(void) BANKED { return lost_; }
/* After a mix (made: something formed). STORY_* for the cartridge to act on. */
/* XP from making (fight spec 8.3): a new discovery 5, a known recipe remade 1 (at most 20 a chapter) */
static void make_xp(uint8_t made) {
  uint8_t o = core.mix.outcome;
  if (!made) return;
  if (o == CRU_NEW)
    (void)player_xp(5);
  else if ((o == CRU_ROUTE || o == CRU_KNOWN) && pl.remake < 20u) {
    pl.remake++;
    (void)player_xp(1);
  } else
    return;
  if (story_on) player_save(); /* free play: the Classic record carries it with the mix's own save */
}
uint8_t story_after_mix(uint16_t a, uint16_t b, uint8_t made) BANKED {
  crucible_story *s = talk_saga();
  uint8_t st;
  if (story_on) { /* the chapter's alignment cell (kdc order and heart, a third each: 0 LG .. 4 TN .. 8 CE), for the styles a whole chapter in one cell opens */
    int8_t o = (int8_t)s->flags[30], h = (int8_t)s->flags[31];
    player_chapter(s->chapter, (uint8_t)((o >= 12 ? 0u : o <= -12 ? 6u : 3u) + (h >= 12 ? 0u : h <= -12 ? 2u : 1u)));
  }
  make_xp(made);
  if (link_started) {
    if (!made)
      link_missed();
    else if (core.mix.outcome != CRU_NEW)
      link_made(a, 0);
  } /* the partner hears every make and miss (a discovery goes by link_found) */
  /* a lost piece that would not form (the core's cru_lost.c): no miss to pay, no visitor; the first time, the machine
   * glitches in with a word about it (talk_lost_arm, through the loss scene's path), later times nothing at all */
  if (core.mix.lost) {
    if (core.mix.lost != CRU_LOST_FIRST) return STORY_NONE;
    talk_lost_arm(core.mix.result);
    return STORY_LOSS;
  }
  if (!story_on) { /* free play: now and then someone wanders in too, more rarely */
    if (made && ++since_talk_ >= 6u && !(DIV_REG & 3u)) {
      since_talk_ = 0;
      return STORY_VISIT;
    }
    return STORY_NONE;
  }
  lost_ = CRU_NONE;
  if (!made && cru_story_fail(&core, s, a, b, &lost_)) {
    st = cru_story_state(&core, s);
    story_save();
    return st == CRU_RUN_ON ? STORY_LOSS : STORY_OVER;
  }
  st = cru_story_state(&core, s);
  story_save();
  if (st != CRU_RUN_ON && st != CRU_RUN_END) return STORY_OVER;
  /* a faction that hates you sends its champion now and then */
  if (made) {
    uint8_t f;
    for (f = 0; f < CRU_FACTIONS; f++)
      if (s->stand[f] <= -50) break;
    if (f < CRU_FACTIONS && !(DIV_REG & 3u)) return STORY_BOSS;
  }
  /* now and then someone steps in while you work (more often the deeper it goes) */
  if (made && ++since_talk_ >= 3u && (DIV_REG & 3u) <= (uint8_t)(s->chapter >> 2)) {
    since_talk_ = 0;
    return STORY_VISIT;
  }
  return STORY_NONE;
}

/* ---- the story HUD on the bench's top row: what you hold, what a miss risks, what just changed ---- */
#define T_CREAM 7u
#define T_BRASS 15u
static uint16_t hud_owned_ = 0xffffu;
static uint8_t hud_delta_t_, hud_t_, hud_up_, hud_blink_, hud_slow_;
static void hud_text(uint8_t x, const char *s, uint8_t w, uint8_t attr) {
  uint8_t tiles[8], attrs[8], i = 0, c;
  memset(attrs, attr, w);
  while (i < w) {
    c = *s;
    if (c) s++;
    tiles[i++] = GLYPH(c);
  }
  VBK_REG = 1;
  set_bkg_tiles(x, 0, w, 1, attrs);
  VBK_REG = 0;
  set_bkg_tiles(x, 0, w, 1, tiles);
}
static void hud_num(char *o, uint16_t n, uint8_t w) {
  o[w] = 0;
  while (w) {
    o[--w] = (char)('0' + n % 10u);
    n /= 10u;
  }
}
/* Every frame on the bench during a run. force: the header was just redrawn. */
void story_hud(uint8_t dt, uint8_t force) BANKED {
  crucible_story *s = talk_saga();
  uint16_t owned = core.found[0];
  char b[8];
  uint8_t odds, redraw = force;
  if (!story_on) {
    hud_owned_ = 0xffffu;
    return;
  }
  if (hud_owned_ != 0xffffu && owned != hud_owned_) {
    hud_up_ = owned > hud_owned_;
    hud_delta_t_ = 90u;
    redraw = 1;
    sound_play(hud_up_ ? SFX_NEW : SFX_DENY);
  }
  hud_owned_ = owned;
  hud_t_ = (uint8_t)(hud_t_ + dt);
  if (hud_t_ >= 16u) {
    hud_t_ = 0;
    redraw = 1;
    hud_blink_ ^= 1u;
    hud_slow_++;
  }
  if (hud_delta_t_) hud_delta_t_ = hud_delta_t_ > dt ? (uint8_t)(hud_delta_t_ - dt) : 0u;
  if (!redraw) return;
  /* the count: blinks when four or fewer are left (running out ends the run) */
  hud_num(b, owned > 9999u ? 9999u : owned, 4);
  if (owned <= 4u && hud_blink_) memset(b, ' ', 4);
  hud_text(2, b, 4, owned <= 4u ? T_BRASS : T_CREAM);
  /* between the counters: a change just now, else what the next miss risks (taking turns with the filter in use) */
  if (hud_delta_t_) {
    hud_text(7, hud_up_ ? "+1 GOT " : "-1 LOST", 7, hud_up_ ? T_CREAM : T_BRASS);
    return;
  }
  if (core.filter && (hud_slow_ & 4u)) {
    char l[10];
    filter_label(core.filter, l);
    hud_text(7, l, 7, T_BRASS);
    return;
  } /* a category name is written 10 bytes wide */
  odds = cru_story_fail_odds(s);
  strcpy(b, "MISS");
  hud_num(b + 4, (uint16_t)(((uint16_t)odds * 100u) >> 8) + 1u, 2);
  b[6] = '%';
  b[7] = 0;
  hud_text(7, b, 7, s->fails ? T_BRASS : T_CREAM);
}
