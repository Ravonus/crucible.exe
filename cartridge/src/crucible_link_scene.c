/* Shared scenes over the link (docs/fight-system.md 9.4): a boss met in a co-op session plays on both screens.
 *   1 the owner (whose game reached the boss) sends P_CUE (scene, seed) and the scene's setup as P_BEAT chunks
 *     (0xC0 + k: the boss, the owner's kit and passives, its hand, HP and focus), again and again until the partner
 *     answers P_READY; it waits at most 10 s, then plays alone
 *   2 the partner is pulled in at its next safe point (its bench, idle: crucible_flow.c), opens the same boss from the
 *     owner's setup and answers P_READY(1, its own HP)
 *   3 both run the boss engine in lockstep on one shared side: the owner's hand (one engine, one state on both
 *     screens), HP the average of both players' plus 2. Each round's active player comes from the seed (never three
 *     in a row: the anti-streak); the active one answers, P_BEAT carries the picks and close calls, re-sent until acked
 *   4 the watcher: SELECT nudges (a ghost mark the active one sees, P_CURSOR), holding B a second vetoes once a phase
 *     (the round is the watcher's)
 *   5 three seconds without a packet: the local player takes every remaining round (no restart)
 *   Outcomes: the owner's save gets the story's effects; the watcher's only its XP (and the linked memory, step 22).
 * Deviation from 9.4: the shared side is the owner's hand, not "each beat from the active player's own hand", so the
 * two engines can never differ; the end choice is the owner's. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_data.h"
#include "crucible_state.h"
#include "crucible_link.h"
#include "crucible_link_scene.h"
#define WORDS ((uint16_t *)0xB4C0u) /* SRAM bank 2 scratch: the scene's setup (no save uses it) */
#define WAIT_READY 600u /* 10 s: a partner mid-mix finishes its merge and reveal first */
uint8_t ls_role, ls_state, ls_beat, ls_active, ls_veto, ls_got[2], ls_nudge, ls_their_hp, ls_quiet, ls_nudges;
uint16_t ls_seed, ls_t;
static uint8_t streak_, last_active_, beat_tx_, beat_acked_, beat_t_, in_beat_, in_p0_, in_p1_, in_close_, have_in_,
    setup_k_;
static uint8_t frng_(uint16_t *r) {
  *r ^= (uint16_t)(*r << 7);
  *r ^= (uint16_t)(*r >> 9);
  *r ^= (uint16_t)(*r << 8);
  return (uint8_t)*r;
}
static void words_put(uint8_t k, uint16_t w) {
  ENABLE_RAM;
  SWITCH_RAM(2);
  WORDS[k] = w;
  DISABLE_RAM;
}
uint16_t ls_word(uint8_t k) BANKED {
  uint16_t w;
  ENABLE_RAM;
  SWITCH_RAM(2);
  w = WORDS[k];
  DISABLE_RAM;
  return w;
}
void link_scene_reset(void) BANKED {
  ls_role = LS_NONE;
  ls_state = 0;
  ls_got[0] = ls_got[1] = 0;
  have_in_ = 0;
  ls_nudge = 0xffu;
}
/* the owner: a boss begins in a co-op session (the setup in words 0..LS_WORDS-1) */
void link_scene_owner(const uint16_t *w) BANKED {
  uint8_t k;
  if (!link_on || !link_started || link_mode != LINK_COOP || !link_linked || ls_role == LS_WATCHER) return;
  for (k = 0; k < LS_WORDS; k++) words_put(k, w[k]);
  ls_role = LS_OWNER;
  ls_state = LS_CUE;
  ls_seed = w[0];
  ls_t = 0;
  ls_beat = 0;
  ls_veto = 0;
  streak_ = 0;
  last_active_ = 0xffu;
  setup_k_ = 0;
  beat_tx_ = 0xffu;
  beat_acked_ = 1;
  have_in_ = 0;
  ls_nudge = 0xffu;
  ls_quiet = 0;
  (void)link_send(P_CUE, 1u, ls_seed);
}
/* the owner waits for the partner (at most 4 s): 1 while it waits */
uint8_t link_scene_wait(void) BANKED { return ls_role == LS_OWNER && ls_state == LS_CUE; }
/* who answers this round: from the seed and the round, never three in a row (beat: the round's number) */
static uint8_t active_for(uint8_t beat) {
  uint16_t r = (uint16_t)(ls_seed ^ ((uint16_t)beat * 0x9e37u));
  uint8_t a;
  if (!r) r = 0x5eedu;
  a = (uint8_t)(frng_(&r) & 1u); /* 0 the owner, 1 the watcher */
  if (beat && a == last_active_ && streak_ >= 2u) a ^= 1u;
  return a;
}
void link_scene_round(uint8_t beat) BANKED { /* a new round: who answers it */
  uint8_t a;
  if (ls_state != LS_SHARED) return;
  a = active_for(beat);
  streak_ = a == last_active_ ? (uint8_t)(streak_ + 1u) : 1u;
  last_active_ = a;
  ls_beat = beat;
  ls_active = a;
  ls_nudge = 0xffu;
  have_in_ = 0;
}
uint8_t link_scene_shared(void) BANKED { return ls_state == LS_SHARED; }
/* 1: this cartridge answers the round (the partner gone quiet for 3 s: always) */
uint8_t link_scene_mine(void) BANKED {
  if (ls_state != LS_SHARED || !link_linked || ls_quiet) return 1;
  return ls_active == (ls_role == LS_OWNER ? 0u : 1u);
}
/* the active side locked its answer: it crosses (re-sent until acked) */
void link_scene_send(uint8_t p0, uint8_t p1, uint8_t close) BANKED {
  if (ls_state != LS_SHARED) return;
  beat_tx_ = ls_beat;
  beat_acked_ = 0;
  beat_t_ = 0;
  in_p0_ = p0;
  in_p1_ = p1;
  in_close_ = close; /* (kept to re-send) */
  (void)link_send(P_BEAT, (uint16_t)(ls_beat | ((uint16_t)p0 << 8)), (uint16_t)(p1 | ((uint16_t)close << 8)));
}
/* the watcher's side: the active one's answer for this round, once */
uint8_t link_scene_take(uint8_t *p0, uint8_t *p1, uint8_t *close) BANKED {
  if (!have_in_) return 0;
  have_in_ = 0;
  *p0 = in_p0_;
  *p1 = in_p1_;
  *close = in_close_;
  return 1;
}
void link_scene_nudge(uint8_t cursor) BANKED {
  if (ls_state == LS_SHARED) (void)link_send(P_CURSOR, (uint16_t)(ls_beat | ((uint16_t)cursor << 8)), 0);
}
/* the watcher holds B: the round is its own (once a phase) */
uint8_t link_scene_veto(void) BANKED {
  if (ls_state != LS_SHARED || ls_veto || link_scene_mine()) return 0;
  ls_veto = 1;
  ls_active ^= 1u;
  (void)link_send(P_CURSOR, (uint16_t)(ls_beat | 0xff00u), 1u);
  return 1;
}
void link_scene_phase(void) BANKED { ls_veto = 0; }
void link_scene_end(void) BANKED {
  if (ls_state == LS_SHARED || ls_state == LS_CUE) (void)link_send(P_READY, 0x80u, 0);
  link_scene_reset();
}
/* packets: a LINK_EV_* */
uint8_t link_scene_packet(uint8_t type, uint16_t a, uint16_t b) BANKED {
  uint8_t lo = (uint8_t)a, hi = (uint8_t)(a >> 8);
  if (type == P_CUE) {
    if (lo == 1u && ls_role == LS_NONE) {
      ls_role = LS_WATCHER;
      ls_state = LS_COLLECT;
      ls_seed = b;
      ls_got[0] = ls_got[1] = 0;
      ls_t = 0;
    }
    return LINK_EV_NONE;
  }
  if (type == P_READY) {
    if (lo == 0x80u) { /* the scene ended (or was given up) on the other side */
      if (ls_role == LS_WATCHER && ls_state != LS_SHARED) {
        link_scene_reset();
        link_coop_elsewhere();
      } /* never pulled in: a notice, later */
      else if (ls_state != LS_NONE)
        ls_state = LS_SOLO;
      return LINK_EV_NONE;
    }
    if (ls_role == LS_OWNER && ls_state == LS_CUE) {
      if (b & 0x100u) {
        ls_their_hp = (uint8_t)b;
        ls_state = LS_SHARED;
        link_seed_scene();
      } else
        ls_state = LS_SOLO;
    }
    return LINK_EV_NONE;
  }
  if (type == P_BEAT) {
    if ((lo & 0xC0u) == 0xC0u) { /* a setup chunk */
      uint8_t k = (uint8_t)(lo & 0x1fu);
      if (ls_role == LS_WATCHER && ls_state == LS_COLLECT && k < LS_WORDS) {
        words_put(k, b);
        ls_got[k >> 3] |= (uint8_t)(1u << (k & 7u));
      }
      return LINK_EV_NONE;
    }
    if (hi == 0xffu && b == 0xffffu) {
      if (lo == beat_tx_) beat_acked_ = 1;
      return LINK_EV_NONE;
    } /* an ack */
    if (ls_state == LS_SHARED && lo == ls_beat && !link_scene_mine()) {
      in_p0_ = hi;
      in_p1_ = (uint8_t)b;
      in_close_ = (uint8_t)(b >> 8);
      have_in_ = 1;
    } else if (ls_state == LS_SHARED && (uint8_t)(lo - ls_beat) < 0x80u)
      return LINK_EV_NONE; /* a round still ahead of us: no ack, it comes again */
    (void)link_send(P_BEAT, (uint16_t)(lo | 0xff00u), 0xffffu); /* held (or old): ack */
    return LINK_EV_NONE;
  }
  if (type == P_CURSOR) {
    if (ls_state != LS_SHARED || lo != ls_beat) return LINK_EV_NONE;
    if (hi == 0xffu) {
      if (!ls_veto) {
        ls_veto = 1;
        ls_active ^= 1u;
      }
    } /* the partner vetoed: the round is theirs */
    else {
      ls_nudge = hi;
      ls_nudges++;
    }
    return LINK_EV_NONE;
  }
  return LINK_EV_NONE;
}
/* the watcher: every chunk arrived (then crucible_flow.c pulls it in at its bench) */
uint8_t link_scene_pull(void) BANKED {
  uint8_t k;
  if (ls_role != LS_WATCHER || ls_state != LS_COLLECT) return 0;
  for (k = 0; k < LS_WORDS; k++)
    if (!(ls_got[k >> 3] & (uint8_t)(1u << (k & 7u)))) return 0;
  ls_state = LS_PULL;
  return 1;
}
uint8_t link_scene_watching(void) BANKED { return ls_role == LS_WATCHER && ls_state == LS_PULL; }
void link_scene_joined(uint8_t my_hp) BANKED { /* the watcher's boss is open: tell the owner */
  ls_state = LS_SHARED;
  ls_beat = 0;
  ls_veto = 0;
  streak_ = 0;
  last_active_ = 0xffu;
  beat_tx_ = 0xffu;
  beat_acked_ = 1;
  ls_quiet = 0;
  link_seed_scene();
  (void)link_send(P_READY, 1u, (uint16_t)(0x100u | my_hp));
}
/* every frame of a session */
void link_scene_tick(uint8_t dt) BANKED {
  if (ls_state == LS_NONE) return;
  ls_t = (uint16_t)(ls_t + dt);
  if (ls_role == LS_OWNER && ls_state == LS_CUE) {
    if (ls_t >= WAIT_READY) {
      ls_state = LS_SOLO;
      (void)link_send(P_READY, 0x80u, 0);
      return;
    } /* 10 s: alone */
    if (link_idle()) {
      if (setup_k_ < LS_WORDS) {
        if (link_send(P_BEAT, (uint16_t)(0xC0u | setup_k_), ls_word(setup_k_))) setup_k_++;
      } else if (link_send(P_CUE, 1u, ls_seed))
        setup_k_ = 0;
    }
    return;
  }
  if (ls_role == LS_WATCHER && ls_state == LS_COLLECT && ls_t >= 600u) {
    link_scene_reset();
    return;
  } /* the setup never completed */
  if (ls_state == LS_SHARED) {
    ls_quiet = !link_linked || link_quiet() >= 180u; /* three seconds without a packet: the local player takes over */
    if (!beat_acked_ && (beat_t_ = (uint8_t)(beat_t_ + dt)) >= 30u && link_idle()) {
      beat_t_ = 0;
      (void)link_send(P_BEAT, (uint16_t)(beat_tx_ | ((uint16_t)in_p0_ << 8)),
                      (uint16_t)(in_p1_ | ((uint16_t)in_close_ << 8)));
    }
  }
}
