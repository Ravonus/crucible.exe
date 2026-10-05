/* Co-op and race over the link (docs/fight-system.md 9.3-9.5): two games, each on its own save, sharing what
 * they make.
 *   - every success sends P_MADE (id, flags: 1 new to the sender; serial); one at a time, re-sent each second until
 *     the partner acks that serial, so a dropped byte never loses a make;
 *   - the receiver keeps the first 12 partner elements per session that are new to its own save (and 24 a chapter on
 *     a story slot): the counter in its record goes up and is saved first, then the element is granted (the existing
 *     LINK_EV_GIFT path), so a cut between the two can only leave the counter one high, never the cap exceeded;
 *     what is not kept is shown, not given (the borrowed shelf of 9.4 is not built: see the report);
 *   - a toast for each partner make, NEW when it is new to this save, at least 3 s apart, four queued, "+N MORE" when
 *     more came; in race the toast carries their score;
 *   - a partner's miss: a short descending noise (SFX_PMISS) at most once in 2 s, never queued; the sender sends at most
 *     one a second;
 *   - a session ends with 20 XP and 2 more for each partner make you were told of (40 at most).
 * Saves stay separate: nothing here writes the partner's save; only this cartridge's own counter and grants. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_data.h"
#include "crucible_state.h"
#include "crucible_link.h"
#include "crucible_player.h"
#include "crucible_storyrun.h"
#include "crucible_link_scene.h"
#define KEEP_SESSION 12u
#define KEEP_CHAPTER 24u
#define TOAST_GAP 180u
uint8_t cru_owned(const crucible_core *c, uint16_t id) CORE_BANKED;
/* the outbox (one in flight), the partner toasts, the counters (globals: the link harness reads them) */
/* the queues live in SRAM bank 2 scratch (0xB4E0, no save uses it): WRAM is tight; the counts stay in WRAM */
typedef struct {
  uint16_t out[4], toast[4];
  uint8_t out_fl[4], tfl[4];
} lc_q;
static lc_q *const Q = (lc_q *)0xB4E0u;
uint8_t lc_out_n, lc_serial, lc_in, lc_toast_n, lc_more, lc_notified, lc_kept;
static uint8_t send_t_, miss_t_, pmiss_t_, seed_n_, seed_t_;
uint8_t lc_seed_got;
int8_t lc_their[2];
static uint16_t their_seed_;
#define ON                                                                                                             \
  ENABLE_RAM;                                                                                                          \
  SWITCH_RAM(2)
#define OFF DISABLE_RAM
static uint16_t toast_at_;
void link_coop_begin(void) BANKED {
  lc_out_n = 0;
  lc_serial = 1;
  lc_in = 0;
  lc_toast_n = 0;
  lc_more = 0;
  lc_notified = 0;
  lc_kept = 0;
  send_t_ = 60u;
  miss_t_ = 0;
  pmiss_t_ = 0;
  toast_at_ = (uint16_t)(sys_time - TOAST_GAP);
  pl.kept_s = 0;
  pl.tint[0] = pl.tint[1] = 0;
  player_save();
  lc_seed_got = 0;
  seed_n_ = 0;
  seed_t_ = 120u; /* a new session: its keep count starts again (saved before anything is kept) */
}
/* this side made something (new: a discovery of its own) */
void link_made(uint16_t id, uint8_t is_new) BANKED {
  uint8_t i;
  if (!link_started || link_mode == LINK_FIGHT) return;
  ON;
  if (lc_out_n == 4u) {
    for (i = 0; i < 3u; i++) {
      Q->out[i] = Q->out[i + 1u];
      Q->out_fl[i] = Q->out_fl[i + 1u];
    }
    lc_out_n = 3;
  } /* the oldest unsent gives way */
  Q->out[lc_out_n] = id;
  Q->out_fl[lc_out_n] = is_new ? 1u : 0u;
  lc_out_n++;
  OFF;
}
void link_missed(void) BANKED {
  if (!link_started || link_mode == LINK_FIGHT || miss_t_) return;
  if (link_send(P_MISS, lc_serial, 0)) miss_t_ = 60u;
}
static void toast_add(uint16_t id, uint8_t fl) {
  if (lc_toast_n == 4u) {
    if (lc_more < 99u) lc_more++;
    return;
  } /* four wait; the rest are counted */
  ON;
  Q->toast[lc_toast_n] = id;
  Q->tfl[lc_toast_n] = fl;
  OFF;
  lc_toast_n++;
}
/* a packet for co-op or race: a LINK_EV_* */
uint8_t link_coop_packet(uint8_t type, uint16_t a, uint16_t b) BANKED {
  uint8_t serial = (uint8_t)(b >> 8), fl = (uint8_t)b;
  if (type >= P_CUE && type <= P_CURSOR)
    return link_scene_packet(type, a, b); /* a shared scene (crucible_link_scene.c) */
  if (type == P_MADE) {
    if (fl & 0x80u) { /* an ack: the make in flight arrived */
      if (lc_out_n && serial == lc_serial) {
        uint8_t i;
        ON;
        for (i = 0; i + 1u < lc_out_n; i++) {
          Q->out[i] = Q->out[i + 1u];
          Q->out_fl[i] = Q->out_fl[i + 1u];
        }
        OFF;
        lc_out_n--;
        lc_serial++;
        if (!lc_serial) lc_serial = 1;
        send_t_ = 60u;
      }
      return LINK_EV_NONE;
    }
    (void)link_send(P_MADE, 0, (uint16_t)(0x80u | ((uint16_t)serial << 8))); /* ack (again, if our last ack was lost) */
    if (serial == lc_in || a >= core.items) return LINK_EV_NONE; /* heard already */
    lc_in = serial;
    {
      uint8_t is_new = !cru_owned(&core, a);
      toast_add(a, (uint8_t)(is_new ? 1u : 0u));
      if (lc_notified < 255u) lc_notified++;
      if (is_new && pl.kept_s < KEEP_SESSION && (pl_slot == 0xffu || pl.kept_ch < KEEP_CHAPTER)) {
        pl.kept_s++;
        if (pl_slot != 0xffu) pl.kept_ch++;
        player_save(); /* the counter first, saved; then the grant */
        lc_kept++;
        pl.plast = a;
        link_inbox_put(a);
        return LINK_EV_GIFT;
      }
    }
    return LINK_EV_NONE;
  }
  if (type == P_SEED) {
    if (!lc_seed_got) link_seed_session(a, (int8_t)(uint8_t)b, (int8_t)(uint8_t)(b >> 8));
    return LINK_EV_NONE;
  }
  if (type == P_MISS) {
    if (!pmiss_t_) {
      sound_play(SFX_PMISS);
      pmiss_t_ = 120u;
    } /* at most once in 2 s; never queued */
    return LINK_EV_NONE;
  }
  return LINK_EV_NONE;
}
/* every frame of a co-op or race session */
void link_coop_tick(uint8_t dt) BANKED {
  link_scene_tick(dt);
  if (seed_n_ < 10u && (seed_t_ = (uint8_t)(seed_t_ + dt)) >= 120u &&
      link_idle()) { /* our seed and axes, every 2 s for the session's first 20 s */
    uint8_t *k = talk_saga()->flags + 16u;
    if (link_send(P_SEED, (uint16_t)talk_saga()->seed, (uint16_t)(k[14] | ((uint16_t)k[15] << 8)))) {
      seed_t_ = 0;
      seed_n_++;
    }
  }
  if (miss_t_) miss_t_ = miss_t_ > dt ? (uint8_t)(miss_t_ - dt) : 0u;
  if (pmiss_t_) pmiss_t_ = pmiss_t_ > dt ? (uint8_t)(pmiss_t_ - dt) : 0u;
  if (!lc_out_n) return;
  if (send_t_ < 255u - dt) send_t_ = (uint8_t)(send_t_ + dt);
  if (send_t_ >= 60u && link_idle()) {
    uint16_t id;
    uint8_t fl;
    ON;
    id = Q->out[0];
    fl = Q->out_fl[0];
    OFF;
    if (link_send(P_MADE, id, (uint16_t)(fl | ((uint16_t)lc_serial << 8)))) send_t_ = 0;
  }
}
/* the next partner toast, when one is due (crucible_feats.c, with the core's own queue empty): 1 and its id, flags and
 * how many more were merged into it */
uint8_t link_toast_take(uint16_t *id, uint8_t *fl, uint8_t *more) BANKED {
  uint8_t i;
  if (!lc_toast_n || (uint16_t)(sys_time - toast_at_) < TOAST_GAP) return 0;
  ON;
  *id = Q->toast[0];
  *fl = Q->tfl[0];
  for (i = 0; i + 1u < lc_toast_n; i++) {
    Q->toast[i] = Q->toast[i + 1u];
    Q->tfl[i] = Q->tfl[i + 1u];
  }
  OFF;
  *more = lc_toast_n == 1u ? lc_more : 0u;
  lc_toast_n--;
  if (!lc_toast_n) lc_more = 0;
  toast_at_ = sys_time;
  return 1;
}
/* a scene of the partner's that this side was too busy to join: "SOMETHING HAPPENED ELSEWHERE" (a toast) */
void link_coop_elsewhere(void) BANKED { toast_add(0, 0x80u); }
/* race: their find, with their score (a toast "THEY MADE X  12/25") */
void link_coop_race(uint16_t id, uint8_t score) BANKED {
  if (id < core.items) {
    toast_add(id, (uint8_t)(0x40u | (score & 0x3fu)));
    if (lc_notified < 255u) lc_notified++;
  }
}
/* ---- seeds grow together (9.4): bounded, lasting, written as they happen (a cut loses nothing) ---- */
static uint8_t cell_of(int8_t o, int8_t h) {
  return (uint8_t)((o >= 12 ? 0u : o <= -12 ? 6u : 3u) + (h >= 12 ? 0u : h <= -12 ? 2u : 1u));
}
/* the partner's seed and axes arrived: the drift (one story event), the linked memory, the session counted */
void link_seed_session(uint16_t seed, int8_t order, int8_t heart) BANKED {
  crucible_story *s = talk_saga();
  uint8_t g[6];
  lc_seed_got = 1;
  their_seed_ = seed;
  lc_their[0] = order;
  lc_their[1] = heart;
  cru_story_event(s, CRU_EV_LINK, seed, cell_of(order, heart));
  link_partner_genome(g);
  memcpy(pl.pgenome, g, 6);
  {
    char n[CRU_NAME + 1u];
    link_peer(n);
    memcpy(pl.pname, n, 8);
  }
  if (pl.sessions < 255u) pl.sessions++;
  if (pl.plast != CRU_NONE && pl.plast < core.items)
    cru_story_remember(s, pl.plast); /* their last make drifts into your dream */
  player_save();
  if (story_on) story_save();
}
/* a shared scene: each axis moves toward the partner's by (theirs - yours) / 8, at most 2, at most 6 a session */
static int8_t clamp_(int16_t v, int8_t m) { return (int8_t)(v > m ? m : v < -m ? -m : v); }
void link_seed_scene(void) BANKED {
  crucible_story *s = talk_saga();
  uint8_t *k = s->flags + 16u, a;
  int8_t d, mine;
  int16_t v;
  if (!lc_seed_got) return;
  for (a = 0; a < 2u; a++) {
    mine = (int8_t)k[14u + a];
    d = clamp_((int16_t)(((int16_t)lc_their[a] - mine) / 8), 2);
    d = clamp_((int16_t)pl.tint[a] + d, 6) - pl.tint[a]; /* the session's cap */
    pl.tint[a] = (int8_t)(pl.tint[a] + d);
    v = (int16_t)mine + d;
    k[14u + a] = (uint8_t)(int8_t)(v > 60 ? 60 : v < -60 ? -60 : v);
  }
  cru_story_event(s, CRU_EV_LINK, their_seed_, cell_of(lc_their[0], lc_their[1]));
  player_save();
  if (story_on) story_save();
}
/* the session's XP: 20, and 2 for each partner make you were told of (40 more at most) */
void link_coop_end(void) BANKED {
  uint8_t bonus = lc_notified > 20u ? 40u : (uint8_t)(lc_notified * 2u);
  (void)player_xp((uint16_t)(20u + bonus));
  player_save();
}
