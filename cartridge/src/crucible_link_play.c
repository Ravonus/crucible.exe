/* Link play over crucible_link.c's packets: THE CRUCIBLE in lockstep (docs/fight-system.md). Saves stay
 * separate; only each player's setup and the action words cross the cable. Every protocol here survives per-byte
 * delays (a cable bridged over the network moves a byte every 100-200 ms at worst): what matters is re-sent until the
 * partner says it arrived, nothing assumes timing, and silence ends a session only after 8 s (crucible_link.c).
 *
 * Setup: each side sends its bag (P_DECK x4: two ids each), its face and level (P_AVATAR x2), its catalogue size
 * (P_CAT) and its seed, cell and bag size (P_PAS), again every 1.5 s until it holds the partner's whole setup and the
 * partner's P_PAS says it holds ours. The session seed mixes both seeds and both alignment cells, symmetric because
 * host and guest are fixed. Turns alternate: the side to move sends P_FTURN (its word, the turn and the round, the
 * state hash before the move), re-sent every half second until the partner's ack for that turn comes back; the partner
 * acks every copy it gets. A hash that differs: the host's state wins (P_FSYNC chunks), and the turn starts again from
 * it. Both carts run the engine in the host's side order for the hash; the guest's screen swaps to see itself. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_data.h"
#include "crucible_state.h"
#include "crucible_link.h"
#include "crucible_fight.h"
#include "crucible_fight_int.h"
#include "crucible_player.h"
#include "crucible_storyrun.h"
#define RESEND_SETUP 90u
#define SETUP_GAP 6u
#define RESEND_TURN 30u
uint8_t link_send(uint8_t type, uint16_t a, uint16_t b) BANKED;
uint8_t cri_depth(crucible_core *c, uint16_t id) BANKED;

uint8_t lp_ready, lp_have, lp_they_have, lp_sync_n, lp_desyncs, lp_resends, lp_wins[2], lp_bout, lp_synced, lp_match,
    lp_result, lp_again[2], lp_left;
static uint8_t again_t_;
static uint16_t their_bag_[8], my_bag_[8], their_seed_, my_seed_, setup_t_;
static uint8_t their_g_[6], their_n_, my_n_, their_level_, their_attrs_, their_cell_, cat_ok_ = 1, setup_k_;
static uint16_t my_word_, their_word_[2];
static uint8_t my_key_ = 0xffu, my_hash_, acked_, turn_t_, their_key_[2], their_hash_[2], in_;
#define SETUP_ALL 0xffu
static const uint8_t BIT8[8] = {1, 2, 4, 8, 16, 32, 64, 128};
static uint8_t key_(uint8_t turn) {
  return (uint8_t)((turn & 63u) | (((fight_round + lp_match) & 3u) << 6));
} /* (a rematch shifts the rounds' tag: a stale copy of the last match's move is never this one) */
static uint8_t my_cell(void) {
  if (!story_on) return 4u;
  {
    uint8_t *k = talk_saga()->flags + 16u;
    int8_t o = (int8_t)k[14], h = (int8_t)k[15];
    return (uint8_t)((o >= 12 ? 0u : o <= -12 ? 2u : 1u) * 3u + (h >= 12 ? 0u : h <= -12 ? 2u : 1u));
  }
}
/* the bag this side brings: its own (pinned and auto), or RANDOM: eight of two stars or less from a seeded place on
 * its own shelf */
static void bag_now(void) {
  if (link_rule(3) & 8u) {
    uint16_t id = (uint16_t)((my_seed_ ^ link_seed) % core.items), n;
    my_n_ = 0;
    for (n = 0; n < core.items && my_n_ < 8u; n++) {
      id = player_owned_next(id);
      if (id >= 4u && cri_depth(&core, id) <= 3u) my_bag_[my_n_++] = id;
    }
  } else
    my_n_ = fs_bag(my_bag_);
}
static uint8_t send_setup_one(uint8_t k) {
  if (k < 4u)
    return link_send(P_DECK, (uint16_t)(my_bag_[k * 2u] | ((uint16_t)(k * 2u) << 13)),
                     (uint16_t)(my_bag_[k * 2u + 1u] | ((uint16_t)(k * 2u + 1u) << 13)));
  if (k == 4u)
    return link_send(P_AVATAR, (uint16_t)(pl.genome[0] | ((uint16_t)pl.genome[1] << 8)),
                     (uint16_t)(pl.genome[2] | ((uint16_t)pl.genome[3] << 8)));
  if (k == 5u)
    return link_send(P_AVATAR2, (uint16_t)(pl.genome[4] | ((uint16_t)pl.genome[5] << 8)),
                     (uint16_t)(pl.level | ((uint16_t)pl.attrs << 8)));
  if (k == 6u) return link_send(P_CAT, core.items, core.recipes);
  return link_send(P_PAS, (uint16_t)(my_n_ | ((uint16_t)(my_cell() | (lp_have == SETUP_ALL ? 0x80u : 0u)) << 8)),
                   my_seed_);
}
/* ---- the resync: the host's whole engine state (host order) as P_FSYNC chunks of three bytes ---- */
#define SYNC_BYTES ((uint8_t)sizeof(cx_state))
#define SYNC_CHUNKS ((uint8_t)((sizeof(cx_state) + 2u) / 3u))
static uint8_t sync_buf_[((sizeof(cx_state) + 2u) / 3u) * 3u], sync_got_[4],
    sync_k_ = 0xffu, ask_t_, want_sync_; /* want_sync_: the guest waits on a resync (asks again until it is whole) */
/* every snapshot the host takes has an epoch (2 bits, in each chunk's index byte): the guest collects chunks of one
 * epoch only and never applies the same epoch twice, so a chunk lost on a noisy wire is re-sent from the same snapshot
 * and late copies of an old one are ignored. The guest's ask carries its ask number: a new number (a new mismatch)
 * takes a fresh snapshot, the same number (a chunk missing, or a late ask) re-sends the snapshot it already has. */
static uint8_t sync_ep_, got_ep_ = 0xffu, done_ep_ = 0xffu, ask_id_, served_id_ = 0xffu;
static uint8_t pas_more_;
void link_play_begin(void) BANKED {
  lp_ready = lp_have = lp_they_have = 0;
  lp_wins[0] = lp_wins[1] = 0;
  lp_bout = 0;
  setup_t_ = RESEND_SETUP;
  setup_k_ = 0;
  cat_ok_ = 1;
  lp_desyncs = 0;
  lp_resends = 0;
  lp_synced = 0;
  my_key_ = 0xffu;
  acked_ = 1;
  in_ = 0;
  lp_sync_n = 0;
  want_sync_ = 0;
  memset(sync_got_, 0, sizeof sync_got_);
  got_ep_ = done_ep_ = served_id_ = 0xffu;
  sync_k_ = 0xffu;
  pas_more_ = 8;
  lp_match = 0;
  lp_result = 0;
  lp_again[0] = lp_again[1] = 0;
  lp_left = 0;
  my_seed_ = (uint16_t)(core.variant_seed ^ core.rng);
  if (!my_seed_) my_seed_ = 0x5eedu;
  memset(my_bag_, 0, sizeof my_bag_);
  my_n_ = 0;
  if (link_mode == LINK_FIGHT) bag_now();
}
static void sync_send(uint8_t fresh) { /* fresh: a new snapshot (a new epoch); else the one taken last, again */
  if (fresh) {
    if (fight_swap) cx_swap();
    memcpy(sync_buf_, &cx, sizeof cx);
    if (fight_swap) cx_swap();
    sync_ep_ = (uint8_t)((sync_ep_ + 1u) & 3u);
  } else if (sync_k_ != 0xffu)
    return; /* (already streaming it) */
  sync_k_ = 0;
  lp_desyncs++;
}
static void sync_stream(void) {
  uint8_t i = sync_k_;
  if (i == 0xffu)
    return; /* (not only on an idle line: a busy one starved the stream; send() refuses when the queue is full) */
  if (link_send(P_FSYNC, (uint16_t)((uint8_t)(i | (uint8_t)(sync_ep_ << 5)) | ((uint16_t)sync_buf_[i * 3u] << 8)),
                (uint16_t)(sync_buf_[i * 3u + 1u] | ((uint16_t)sync_buf_[i * 3u + 2u] << 8))))
    sync_k_ = (uint8_t)(i + 1u) < SYNC_CHUNKS ? (uint8_t)(i + 1u) : 0xffu;
}
static void sync_apply(void) { /* the guest adopts the host's state; the turn starts again from it */
  memcpy(&cx, sync_buf_, sizeof cx);
  if (fight_swap) cx_swap();
  in_ = 0;
  my_key_ = 0xffu;
  acked_ = 1;
  lp_desyncs++;
  lp_synced = 1;
  want_sync_ = 0;
}
uint8_t link_synced(void) BANKED {
  uint8_t s = lp_synced;
  lp_synced = 0;
  return s;
}
void link_fsync_ask(void) BANKED {
  if (link_role == LINK_HOST) {
    sync_send(1);
    return;
  }
  if (!want_sync_) {
    want_sync_ = 1;
    ask_id_ = (uint8_t)((ask_id_ + 1u) & 3u);
    ask_t_ = 0;
  }
  if (!ask_t_ && link_send(P_FSYNC, (uint16_t)(0xffu | ((uint16_t)ask_id_ << 8)), 0)) ask_t_ = 120u;
}
uint8_t link_play_packet(uint8_t type, uint16_t a, uint16_t b) BANKED {
  uint8_t lo = (uint8_t)a, hi = (uint8_t)(a >> 8);
  switch (type) {
  case P_DECK: {
    uint8_t s1 = (uint8_t)((a >> 13) & 7u), s2 = (uint8_t)((b >> 13) & 7u);
    their_bag_[s1] = (uint16_t)(a & 0x1fffu);
    their_bag_[s2] = (uint16_t)(b & 0x1fffu);
    lp_have |= BIT8[s1 >> 1];
  } break;
  case P_AVATAR:
    their_g_[0] = lo;
    their_g_[1] = hi;
    their_g_[2] = (uint8_t)b;
    their_g_[3] = (uint8_t)(b >> 8);
    lp_have |= 16u;
    break;
  case P_AVATAR2:
    their_g_[4] = lo;
    their_g_[5] = hi;
    their_level_ = (uint8_t)b;
    their_attrs_ = (uint8_t)(b >> 8);
    lp_have |= 32u;
    break;
  case P_CAT:
    cat_ok_ = a == core.items && b == core.recipes;
    lp_have |= 64u;
    break;
  case P_PAS:
    their_n_ = lo > 8u ? 8u : lo;
    their_cell_ = (uint8_t)(hi & 15u);
    their_seed_ = b;
    lp_have |= 128u;
    if (hi & 0x80u) lp_they_have = 1;
    if (lp_have == SETUP_ALL) (void)send_setup_one(7);
    break;
  case P_FTURN:
    if (b & 0x4000u) {
      if (hi == my_key_) acked_ = 1;
      break;
    } /* an ack: they hold my move */
    {
      uint8_t k = (uint8_t)(hi & 1u);
      if (!(in_ & BIT8[k]) || their_key_[k] != hi || their_hash_[k] != (uint8_t)b) {
        their_word_[k] = (uint16_t)(lo | ((b >> 8) & 3u) << 8);
        their_key_[k] = hi;
        their_hash_[k] = (uint8_t)b;
        in_ |= BIT8[k];
      }
      (void)link_send(P_FTURN, (uint16_t)((uint16_t)hi << 8), 0x4000u); /* ack every copy */
      if (my_key_ != 0xffu && hi == (uint8_t)((my_key_ & 0xc0u) | ((my_key_ + 1u) & 63u))) acked_ = 1;
    } /* their move after mine: mine arrived (a stale copy of their last one proves nothing) */
    break;
  case P_FSYNC:
    if (lo == 0xffu) {
      if (link_role == LINK_HOST) {
        sync_send(hi != served_id_);
        served_id_ = hi;
      }
      break;
    }
    if (link_role != LINK_HOST && (lo & 31u) < SYNC_CHUNKS && (uint8_t)(lo >> 5) != done_ep_) {
      uint8_t k, i, all = 1, ep = (uint8_t)(lo >> 5);
      if (ep != got_ep_) {
        got_ep_ = ep;
        memset(sync_got_, 0, sizeof sync_got_);
      }
      lo &= 31u;
      k = (uint8_t)(lo * 3u);
      sync_buf_[k] = hi;
      sync_buf_[k + 1u] = (uint8_t)b;
      sync_buf_[k + 2u] = (uint8_t)(b >> 8);
      sync_got_[lo >> 3] |= BIT8[lo & 7u];
      lp_sync_n |= 1u;
      for (i = 0; i < SYNC_CHUNKS; i++)
        if (!(sync_got_[i >> 3] & BIT8[i & 7u])) {
          all = 0;
          break;
        }
      if (all) {
        memset(sync_got_, 0, sizeof sync_got_);
        done_ep_ = ep;
        got_ep_ = 0xffu;
        sync_apply();
      } else
        want_sync_ = 1; /* (a chunk lost on the wire: ask again until it is whole) */
    }
    break;
  case P_LEAVE: link_end(LINK_LOST); break;
  case P_READY:
    if (lo == 0xffu) {
      link_ask = (uint8_t)(b != 0u);
      return LINK_EV_RULES;
    }
    if (lo == 0xfeu && link_mode == LINK_FIGHT) { /* after a match: 1 again, 2 the partner leaves */
      if ((uint8_t)b == 1u && hi == lp_match)
        lp_again[1] = 1;
      else if ((uint8_t)b == 2u) {
        lp_left = 1;
        if (lp_result) link_end(lp_result);
      }
      break;
    }
    return link_coop_packet(type, a, b);
  default: return link_coop_packet(type, a, b);
  }
  return LINK_EV_NONE;
}
void link_play_tick(uint8_t dt) BANKED {
  if (!link_started) return;
  if (link_mode != LINK_FIGHT) {
    link_coop_tick(dt);
    return;
  }
  sync_stream();
  if (ask_t_) ask_t_ = ask_t_ > dt ? (uint8_t)(ask_t_ - dt) : 0u;
  if (want_sync_ && !ask_t_ && link_send(P_FSYNC, (uint16_t)(0xffu | ((uint16_t)ask_id_ << 8)), 0)) ask_t_ = 120u;
  if (!(lp_have == SETUP_ALL && lp_they_have)) {
    if (setup_t_ < 255u - dt) setup_t_ = (uint8_t)(setup_t_ + dt);
    if (setup_t_ >= SETUP_GAP && link_idle() && send_setup_one(setup_k_)) {
      setup_t_ = 0;
      setup_k_ = setup_k_ < 7u ? (uint8_t)(setup_k_ + 1u) : 0u;
    }
  } else {
    lp_ready = cat_ok_ ? 1u : 2u;
    /* "I hold all of yours" crosses a few more times: a lost reply left the partner waiting in its entrance */
    if (pas_more_) {
      if (setup_t_ < 255u - dt) setup_t_ = (uint8_t)(setup_t_ + dt);
      if (setup_t_ >= RESEND_SETUP && link_idle() && send_setup_one(7)) {
        setup_t_ = 0;
        pas_more_--;
      }
    }
  }
  if (lp_again[0] && !lp_again[1]) {
    again_t_ = (uint8_t)(again_t_ + dt);
    if (again_t_ >= RESEND_TURN) {
      again_t_ = 0;
      (void)link_send(P_READY, (uint16_t)(0xfeu | ((uint16_t)lp_match << 8)), 1u);
    }
  }
  if (my_key_ != 0xffu && !acked_) {
    turn_t_ = (uint8_t)(turn_t_ + dt);
    if (turn_t_ >= RESEND_TURN) {
      turn_t_ = 0;
      (void)link_send(P_FTURN, (uint16_t)((my_word_ & 0xffu) | ((uint16_t)my_key_ << 8)),
                      (uint16_t)(my_hash_ | ((my_word_ >> 8) & 3u) << 8));
      lp_resends++;
    }
  }
}
uint8_t link_fight_ready(void) BANKED { return lp_ready; }
/* ---- the versus' setup (sides in the host's order) ---- */
uint16_t link_fight_seed(void) BANKED {
  uint16_t host = link_role == LINK_HOST ? my_seed_ : their_seed_,
           guest = link_role == LINK_HOST ? their_seed_ : my_seed_;
  uint8_t ch = link_role == LINK_HOST ? my_cell() : their_cell_, cg = link_role == LINK_HOST ? their_cell_ : my_cell();
  uint16_t x = (uint16_t)(host ^ (uint16_t)((guest << 5) | (guest >> 11)) ^ ((uint16_t)ch << 8) ^ cg ^ link_seed);
  x ^= (uint16_t)(x << 7);
  x ^= (uint16_t)(x >> 9);
  x ^= (uint16_t)(x << 8);
  return x ? x : 0x1d2bu;
}
uint8_t link_fight_first(uint8_t round) BANKED { return (uint8_t)((link_fight_seed() ^ round ^ lp_match) & 1u); }
void link_bags(uint16_t *host, uint8_t *hn, uint16_t *guest, uint8_t *gn) BANKED {
  uint8_t i, me = link_role == LINK_HOST ? 0u : 1u;
  uint16_t *mine = me ? guest : host, *theirs = me ? host : guest;
  uint8_t *mn = me ? gn : hn, *tn = me ? hn : gn;
  memcpy(mine, my_bag_, sizeof my_bag_);
  *mn = my_n_;
  for (i = 0; i < 8u; i++) theirs[i] = their_bag_[i] < core.items ? their_bag_[i] : 4u;
  *tn = their_n_;
  if (link_rule(3) & 64u) {
    if (me) {
      memcpy(guest, host, 16);
      *gn = *hn;
    } else {
      memcpy(guest, host, 16);
      *gn = *hn;
    }
  } /* MIRROR: both bring the host's */
}
static uint8_t attr_of(uint8_t side) {
  uint8_t mode = (uint8_t)((link_rule(1) >> 2) & 3u), a, mine = (uint8_t)(side == (link_role == LINK_HOST ? 0u : 1u));
  a = (uint8_t)((mine ? pl.attrs : their_attrs_) & 7u);
  if (mode == 0u) return 2u;
  if (mode == 1u && a > 2u) return 2u;
  return a;
}
uint8_t link_fight_hp(uint8_t side) BANKED {
  uint8_t r2 = link_rule(2) & 7u;
  return (uint8_t)(8u + r2 + r2 + attr_of(side) + (link_rule((uint8_t)(4u + side)) & 7u));
}
void link_partner_genome(uint8_t *g) BANKED { memcpy(g, their_g_, 6); }
uint8_t link_partner_level(void) BANKED { return their_level_; }
uint8_t link_partner_attrs(void) BANKED { return their_attrs_; }
/* ---- turns ---- */
void link_fturn(uint16_t w, uint8_t turn, uint8_t hash) BANKED {
  my_word_ = w;
  my_key_ = key_(turn);
  my_hash_ = hash;
  acked_ = 0;
  turn_t_ = 0;
  (void)link_send(P_FTURN, (uint16_t)((w & 0xffu) | ((uint16_t)my_key_ << 8)), (uint16_t)(hash | ((w >> 8) & 3u) << 8));
}
uint8_t link_xturn_in(uint16_t *w, uint8_t turn, uint8_t *hash) BANKED {
  uint8_t key = key_(turn), k = (uint8_t)(key & 1u);
  if (!(in_ & BIT8[k]) || their_key_[k] != key) return 0;
  *w = their_word_[k];
  *hash = their_hash_[k];
  in_ &= (uint8_t)~BIT8[k];
  return 1;
}
void link_round_reset(void) BANKED {
  in_ = 0;
  my_key_ = 0xffu;
  acked_ = 1;
}
static const uint8_t PIPS[4] = {6, 4, 3, 2}, NEED[4] = {1, 2, 3, 2};
uint8_t link_fight_pips(void) BANKED {
  return (uint8_t)(PIPS[(link_rule(1) >> 6) & 3u] + ((link_rule(link_role == LINK_HOST ? 4u : 5u) & 0x80u) ? 1u : 0u));
}
/* a round ended (r for this side: 1 won, 2 lost, 3 a draw); 1: the match goes on. At the match's end the result is
 * kept (the end screen shows it; LEAVE ends the session with it, AGAIN asks for another match) */
uint8_t link_fight_over(uint8_t r) BANKED {
  uint8_t xp, need = NEED[(link_rule(1) >> 4) & 3u];
  if (r == 1u)
    lp_wins[0]++;
  else if (r == 2u)
    lp_wins[1]++;
  lp_bout++;
  if (lp_wins[0] < need && lp_wins[1] < need && lp_bout < 9u) return 1;
  r = lp_wins[0] > lp_wins[1] ? 1u : lp_wins[0] < lp_wins[1] ? 2u : 3u;
  xp = r == 1u ? 15u : 10u;
  if ((link_rule(1) & 3u) == 0u && r == 1u) cru_link(&core); /* only FAIR counts for the feats */
  (void)player_xp(xp);
  player_save();
  lp_result = r == 1u ? LINK_WON : r == 2u ? LINK_LOSTRACE : LINK_DRAW;
  lp_again[0] = lp_again[1] = 0;
  again_t_ = 0;
  if (lp_left) link_end(lp_result);
  return 0;
}
/* the end screen's AGAIN (1) or LEAVE (0); link_rematch_go: both asked, a new match */
void link_rematch(uint8_t again) BANKED {
  if (again) {
    if (!lp_again[0]) {
      lp_again[0] = 1;
      again_t_ = RESEND_TURN;
    }
    return;
  }
  (void)link_send(P_READY, (uint16_t)(0xfeu | ((uint16_t)lp_match << 8)), 2u);
  (void)link_send(P_READY, (uint16_t)(0xfeu | ((uint16_t)lp_match << 8)), 2u);
  link_end(lp_result ? lp_result : LINK_DRAW);
}
uint8_t link_rematch_go(void) BANKED {
  if (!(lp_again[0] && lp_again[1])) return 0;
  (void)link_send(P_READY, (uint16_t)(0xfeu | ((uint16_t)lp_match << 8)),
                  1u); /* (one more: theirs may still be waiting for mine) */
  lp_match++;
  lp_wins[0] = lp_wins[1] = 0;
  lp_bout = 0;
  lp_result = 0;
  lp_again[0] = lp_again[1] = 0;
  in_ = 0;
  my_key_ = 0xffu;
  acked_ = 1;
  return 1;
}
