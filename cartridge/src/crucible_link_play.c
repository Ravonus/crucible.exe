/* Link play over crucible_link.c's packets (docs/fight-system.md 9): the versus fight in lockstep. Saves stay
 * separate; only inputs and each player's own setup cross the cable, and every protocol here survives per-byte delays
 * (a cable bridged over the network moves a byte every 100-200 ms at worst): everything that matters is re-sent until
 * the partner says it arrived, nothing assumes timing, and silence ends a session only after 8 s (crucible_link.c).
 *
 * Versus setup: each side sends its kit (P_DECK x3), its face and level (P_AVATAR x2), its passives, cell and seed
 * (P_PAS) and its catalogue size (P_CAT), again every 1.5 s until it holds the partner's whole setup and the partner's
 * P_PAS says it holds ours. The session seed mixes both seeds and both alignment cells, symmetric because host and guest
 * are fixed. Each turn: P_FTURN (action, turn, the state hash before it, the lock pip, and an ack bit: "I hold your
 * turn"), re-sent every half second until acked; a partner still on the previous turn gets that one again. A hash that
 * differs: the guest asks (P_FSYNC 0xff) and the host sends its whole engine state as P_FSYNC chunks; the guest adopts
 * it. The ticker tears, nothing more. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_data.h"
#include "crucible_state.h"
#include "crucible_link.h"
#include "crucible_fight.h"
#include "crucible_fight_rules.h"
#include "crucible_player.h"
#include "crucible_storyrun.h"
#define RESEND_SETUP 90u
#define SETUP_GAP 6u /* frames between the setup's packets (each only on an idle line) */
#define RESEND_TURN 30u
uint8_t link_send(uint8_t type, uint16_t a, uint16_t b) BANKED; /* crucible_link.c: 1 queued, 0 the queue was full */

/* ---- state (globals: the link harness reads them) ---- */
uint8_t lp_ready, lp_have, lp_they_have, lp_sync_n, lp_desyncs, lp_resends, lp_wins[2], lp_bout;
/* the partner's setup waits in the fight engine's scratch (no AI runs during a link session; the resync uses it only
 * after the setup was read) */
#define their_kit_ ((uint16_t *)fr_scratch)
#define their_g_ (fr_scratch + 12)
static uint16_t their_seed_, my_seed_, setup_t_;
static uint8_t their_pas_[2], their_level_, their_attrs_, their_cell_, cat_ok_ = 1;
/* the turn in flight: mine (current and previous), theirs */
/* the partner's turns in two slots by the turn's parity: a partner a turn ahead never overwrites the turn we still need */
static uint8_t my_act_[2], my_turn_[2], my_hash_[2], my_pip_, have_theirs_, their_act_[2], their_turn_[2],
    their_hash_[2], their_pip_[2], acked_, turn_t_;
static uint8_t fturn_in_; /* bit k: slot k holds a turn */
#define SETUP_ALL 0x7fu
#define SYNC_CHUNKS 26u
static uint8_t sync_k_ = 0xffu, sync_got_[4], ask_t_;
static const uint8_t BIT8[8] = {1, 2, 4, 8, 16, 32, 64, 128}; /* deck 0..2, avatar 0..1, pas, cat */
static uint8_t my_cell(void) {
  if (!story_on) return 4u;
  {
    uint8_t *k = talk_saga()->flags + 16u;
    int8_t o = (int8_t)k[14], h = (int8_t)k[15];
    return (uint8_t)((o >= 12 ? 0u : o <= -12 ? 2u : 1u) * 3u + (h >= 12 ? 0u : h <= -12 ? 2u : 1u));
  }
}
static void kit_now(uint16_t *kit, uint8_t *pas) {
  (void)player_kit(kit);
  if (link_rule(3) &
      8u) { /* random kits: a seeded walk along your own shelf (fitted to the budget, else the auto-kit) */
    uint16_t r = (uint16_t)(my_seed_ ^ link_seed), id = 0;
    uint8_t k, n;
    for (k = 0; k < 6u; k++) {
      r ^= (uint16_t)(r << 7);
      r ^= (uint16_t)(r >> 9);
      r ^= (uint16_t)(r << 8);
      for (n = (uint8_t)(1u + (r & 7u)); n; n--) id = player_owned_next(id);
      kit[k] = id;
    }
    if (fr_kit_cost(kit, 6) > player_budget()) player_autokit(kit, player_budget());
  }
  pas[0] = (uint8_t)(pl.equip & 15u);
  pas[1] = player_slots() >= 2u ? (uint8_t)(pl.equip >> 4) : 15u;
  if (pas[0] >= FP_COUNT || !(pl.known & player_bit(pas[0]))) pas[0] = FP_NONE;
  if (pas[1] >= FP_COUNT || !(pl.known & player_bit(pas[1]))) pas[1] = FP_NONE;
}
/* the setup goes out a packet at a time, in turn, and only on an idle line: in a fight a loop can take several frames
 * and moves one byte, so seven packets sent at once never drained and the same tail was dropped every time */
static uint8_t setup_k_;
static uint8_t send_setup_one(uint8_t k) {
  uint16_t kit[6];
  uint8_t pas[2];
  kit_now(kit, pas);
  if (k < 3u)
    return link_send(P_DECK, (uint16_t)(kit[k * 2u] | ((uint16_t)(k * 2u) << 13)),
                     (uint16_t)(kit[k * 2u + 1u] | ((uint16_t)(k * 2u + 1u) << 13)));
  if (k == 3u)
    return link_send(P_AVATAR, (uint16_t)(pl.genome[0] | ((uint16_t)pl.genome[1] << 8)),
                     (uint16_t)(pl.genome[2] | ((uint16_t)pl.genome[3] << 8)));
  if (k == 4u)
    return link_send(P_AVATAR2, (uint16_t)(pl.genome[4] | ((uint16_t)pl.genome[5] << 8)),
                     (uint16_t)(pl.level | ((uint16_t)pl.attrs << 8)));
  if (k == 5u) return link_send(P_CAT, core.items, core.recipes);
  return link_send(P_PAS,
                   (uint16_t)((pas[0] & 15u) | ((pas[1] & 15u) << 4) |
                              ((uint16_t)(my_cell() | (lp_have == SETUP_ALL ? 0x80u : 0u)) << 8)),
                   my_seed_);
}
/* a session begins (both sides): the setup goes out */
void link_play_begin(void) BANKED {
  lp_ready = lp_have = lp_they_have = 0;
  lp_wins[0] = lp_wins[1] = 0;
  lp_bout = 0;
  setup_t_ = RESEND_SETUP;
  setup_k_ = 0;
  cat_ok_ = 1;
  lp_desyncs = 0;
  lp_resends = 0;
  have_theirs_ = 0;
  acked_ = 0;
  my_turn_[0] = my_turn_[1] = 0xffu;
  fturn_in_ = 0;
  lp_sync_n = 0;
  sync_k_ = 0xffu;
  ask_t_ = 0;
  memset(sync_got_, 0, sizeof sync_got_);
  my_seed_ = (uint16_t)(core.variant_seed ^ core.rng);
  if (!my_seed_) my_seed_ = 0x5eedu; /* taken once: the seed sent is the seed used */
}
/* a packet crucible_link.c does not know: returns a LINK_EV_* */
#define sync_buf_ fr_scratch /* the AI's scratch: never in use during a versus */
static void sync_apply(void);
static void sync_send(void);
uint8_t link_play_packet(uint8_t type, uint16_t a, uint16_t b) BANKED {
  uint8_t lo = (uint8_t)a, hi = (uint8_t)(a >> 8);
  switch (type) {
  case P_DECK:
    their_kit_[(a >> 13) & 7u] = (uint16_t)(a & 0x1fffu);
    their_kit_[(b >> 13) & 7u] = (uint16_t)(b & 0x1fffu);
    {
      uint8_t q = (uint8_t)(((a >> 13) & 7u) >> 1);
      lp_have |= q == 0u ? 1u : q == 1u ? 2u : 4u;
    }
    break;
  case P_AVATAR:
    their_g_[0] = lo;
    their_g_[1] = hi;
    their_g_[2] = (uint8_t)b;
    their_g_[3] = (uint8_t)(b >> 8);
    lp_have |= 8u;
    break;
  case P_AVATAR2:
    their_g_[4] = lo;
    their_g_[5] = hi;
    their_level_ = (uint8_t)b;
    their_attrs_ = (uint8_t)(b >> 8);
    lp_have |= 16u;
    break;
  case P_CAT:
    cat_ok_ = a == core.items && b == core.recipes;
    lp_have |= 32u;
    break;
  case P_PAS:
    their_pas_[0] = (uint8_t)(lo & 15u);
    their_pas_[1] = (uint8_t)(lo >> 4);
    their_cell_ = (uint8_t)(hi & 15u);
    their_seed_ = b;
    lp_have |= 64u;
    if (hi & 0x80u) lp_they_have = 1;
    if (lp_have == SETUP_ALL) (void)send_setup_one(6); /* we hold all of theirs: say so (they may still be asking) */
    break;
  case P_FTURN:
    if (((uint8_t)(b >> 8) & 0x80u) && hi == my_turn_[0]) acked_ = 1; /* they hold my turn */
    {
      uint8_t k = (uint8_t)(hi & 1u);
      if (hi != their_turn_[k] || !(fturn_in_ & BIT8[k]) ||
          (uint8_t)b != their_hash_[k]) { /* (a new hash for the same turn: the partner was resynced) */
        their_act_[k] = lo;
        their_turn_[k] = hi;
        their_hash_[k] = (uint8_t)b;
        their_pip_[k] = (uint8_t)((b >> 8) & 7u);
        fturn_in_ |= BIT8[k];
      }
    }
    if (hi == my_turn_[1] && my_turn_[1] != 0xffu && !((uint8_t)(b >> 8) & 0x80u)) {
      (void)link_send(P_FTURN, (uint16_t)(my_act_[1] | ((uint16_t)my_turn_[1] << 8)),
                      (uint16_t)(my_hash_[1] | 0x8000u));
      lp_resends++;
    } /* they missed my last one */
    break;
  case P_FSYNC:
    if (lo == 0xffu) {
      if (link_role == LINK_HOST) sync_send();
      break;
    }
    if (lo < SYNC_CHUNKS &&
        link_role != LINK_HOST) { /* the guest gathers every chunk, in any order, then adopts the state */
      uint8_t k = (uint8_t)(lo * 3u), i, all = 1;
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
        sync_apply();
      }
    }
    break;
  case P_LEAVE: link_end(LINK_LOST); break;
  case P_READY:
    if (lo == 0xffu) {
      link_ask = (uint8_t)(b != 0u);
      return LINK_EV_RULES;
    }
    return link_coop_packet(type, a, b); /* 0xff: the rules, asked about */
  default: return link_coop_packet(type, a, b);
  }
  return LINK_EV_NONE;
}
/* ---- the resync (rare: identical tables and inputs never differ) ---- */
/* the engine's state that the turn needs, both sides: 78 bytes as 26 chunks of 3 */
#define SYNC_SIDE 37u
static void side_pack(uint8_t *p, const fr_side *s) {
  memcpy(p, s->deck, 6);
  p[6] = s->ndeck;
  memcpy(p + 7, s->disc, 6);
  p[13] = s->ndisc;
  memcpy(p + 14, s->hand, 4);
  p[18] = s->nhand;
  p[19] = (uint8_t)s->hp;
  p[20] = s->focus;
  p[21] = s->last;
  p[22] = s->lostrow;
  p[23] = s->wins;
  p[24] = s->lost_hp;
  p[25] = s->prog[0];
  p[26] = s->prog[1];
  p[27] = s->on;
  memcpy(p + 28, s->win, 3);
  p[31] = s->nwin;
  p[32] = s->played;
  p[33] = s->turns;
  p[34] = s->repeats;
  p[35] = s->nhist;
  p[36] = s->hist[0];
}
static void side_unpack(const uint8_t *p, fr_side *s) {
  memcpy(s->deck, p, 6);
  s->ndeck = p[6];
  memcpy(s->disc, p + 7, 6);
  s->ndisc = p[13];
  memcpy(s->hand, p + 14, 4);
  s->nhand = p[18];
  s->hp = (int8_t)p[19];
  s->focus = p[20];
  s->last = p[21];
  s->lostrow = p[22];
  s->wins = p[23];
  s->lost_hp = p[24];
  s->prog[0] = p[25];
  s->prog[1] = p[26];
  s->on = p[27];
  memcpy(s->win, p + 28, 3);
  s->nwin = p[31];
  s->played = p[32];
  s->turns = p[33];
  s->repeats = p[34];
  s->nhist = p[35];
  s->hist[0] = p[36];
}
/* the host: its state, in the host's side order, packed once and streamed a chunk at a time on an idle line (26 at
 * once overflowed the queue); a stream already running is not restarted by the partner's repeated turns */
static void sync_send(void) {
  if (sync_k_ != 0xffu) return;
  if (fight_swap) fr_swap_sides();
  side_pack(sync_buf_, &fr.s[0]);
  side_pack(sync_buf_ + SYNC_SIDE, &fr.s[1]);
  sync_buf_[74] = (uint8_t)fr.rng;
  sync_buf_[75] = (uint8_t)(fr.rng >> 8);
  sync_buf_[76] = fr.turn;
  sync_buf_[77] = 0;
  if (fight_swap) fr_swap_sides();
  sync_k_ = 0;
  lp_desyncs++;
}
static void sync_stream(void) {
  uint8_t i = sync_k_;
  if (i == 0xffu || !link_idle()) return;
  if (sync_buf_[76] != fr.turn) {
    sync_k_ = 0xffu;
    return;
  } /* the turn moved on (the partner adopted an earlier stream): this one is stale */
  if (link_send(P_FSYNC, (uint16_t)(i | ((uint16_t)sync_buf_[i * 3u] << 8)),
                (uint16_t)(sync_buf_[i * 3u + 1u] | ((uint16_t)sync_buf_[i * 3u + 2u] << 8))))
    sync_k_ = i + 1u < SYNC_CHUNKS ? (uint8_t)(i + 1u) : 0xffu;
}
static void sync_apply(void) { /* the guest adopts the host's state (only for the turn it stands at) */
  if (sync_buf_[76] != fr.turn) return;
  if (fight_swap) fr_swap_sides();
  side_unpack(sync_buf_, &fr.s[0]);
  side_unpack(sync_buf_ + SYNC_SIDE, &fr.s[1]);
  fr_side_fix(0);
  fr_side_fix(1);
  fr.rng = (uint16_t)(sync_buf_[74] | ((uint16_t)sync_buf_[75] << 8));
  fr.turn = sync_buf_[76];
  if (my_turn_[0] == fr.turn) {
    my_hash_[0] = fr_hash();
    acked_ = 0;
    turn_t_ = RESEND_TURN;
  } /* our turn goes again (at once) with the state we hold now */
  if (fight_swap) fr_swap_sides();
  lp_desyncs++; /* the partner's move for this turn stays: with the state adopted, its hash now agrees */
}
void link_fsync_ask(void) BANKED {
  if (link_role == LINK_HOST)
    sync_send();
  else if (!ask_t_) {
    (void)link_send(P_FSYNC, 0xffu, 0);
    ask_t_ = 120u;
  }
} /* (the moves held stay: a resync changes only the state) */
/* every frame of a session: the re-sends */
void link_play_tick(uint8_t dt) BANKED {
  if (!link_started) return;
  if (link_mode != LINK_FIGHT) {
    link_coop_tick(dt);
    return;
  }
  sync_stream();
  if (ask_t_) ask_t_ = ask_t_ > dt ? (uint8_t)(ask_t_ - dt) : 0u;
  if (!(lp_have == SETUP_ALL && lp_they_have)) {
    if (setup_t_ < 255u - dt) setup_t_ = (uint8_t)(setup_t_ + dt);
    if (setup_t_ >= SETUP_GAP && link_idle() && send_setup_one(setup_k_)) {
      setup_t_ = 0;
      setup_k_ = setup_k_ < 6u ? (uint8_t)(setup_k_ + 1u) : 0u;
    }
  } else
    lp_ready = cat_ok_ ? 1u : 2u;
  if (my_turn_[0] != 0xffu && !acked_) {
    turn_t_ = (uint8_t)(turn_t_ + dt);
    if (turn_t_ >= RESEND_TURN) {
      turn_t_ = 0;
      (void)link_send(P_FTURN, (uint16_t)(my_act_[0] | ((uint16_t)my_turn_[0] << 8)),
                      (uint16_t)(my_hash_[0] | ((uint16_t)(my_pip_ | (have_theirs_ ? 0x80u : 0u)) << 8)));
      lp_resends++;
    }
  }
}
uint8_t link_fight_ready(void) BANKED { return lp_ready; } /* 0 not yet, 1 ready, 2 the other room is different */

/* ---- the versus' setup, read by crucible_fight.c (sides in the host's order) ---- */
uint16_t link_fight_seed(void) BANKED {
  uint16_t mine = my_seed_, host = link_role == LINK_HOST ? mine : their_seed_,
           guest = link_role == LINK_HOST ? their_seed_ : mine;
  uint8_t ch = link_role == LINK_HOST ? my_cell() : their_cell_, cg = link_role == LINK_HOST ? their_cell_ : my_cell();
  uint16_t x = (uint16_t)(host ^ (uint16_t)((guest << 5) | (guest >> 11)) ^ ((uint16_t)ch << 8) ^ cg ^ link_seed ^
                          ((uint16_t)lp_bout * 0x9e37u)); /* each bout of a match its own */
  x ^= (uint16_t)(x << 7);
  x ^= (uint16_t)(x >> 9);
  x ^= (uint16_t)(x << 8);
  return x ? x : 0x1d2bu;
}
/* the setup's own seed is sent once and kept, so both sides compute the same session seed */
uint8_t link_fight_arena(void) BANKED {
  uint8_t r7 = link_rule(7);
  return r7 >= 1u && r7 <= 8u ? (uint8_t)(r7 - 1u) : r7 > 8u ? (uint8_t)((link_seed >> 3) & 7u) : FA_EMPTY;
}
uint8_t link_fight_rules(void) BANKED {
  uint8_t r3 = link_rule(3), r = 0;
  if (r3 & 1u) r |= FR_R_NOSTACK;
  if (r3 & 2u) r |= FR_R_NOPAS;
  if (r3 & 16u) r |= FR_R_SUDDEN;
  if (r3 & 32u) r |= FR_R_FOG;
  if (r3 & 4u)
    r |= ((link_seed ^ lp_bout) & 1u)
             ? FR_R_INVERT
             : FR_R_FOG; /* a glitch arena: upside down or in fog (DRIFT rolls locally: never in lockstep) */
  return r;
}
/* attributes by the rules (9.2): NORMALISED, CAPPED or FULL; then the side's handicap */
static uint8_t attr_of(uint8_t side, uint8_t which) {
  uint8_t mode = (uint8_t)((link_rule(1) >> 2) & 3u), a, mine = (uint8_t)(side == (link_role == LINK_HOST ? 0u : 1u));
  uint8_t attrs = mine ? pl.attrs : their_attrs_, norm = which == PA_GRIT ? 2u : 1u;
  a = which == PA_GRIT    ? (uint8_t)(attrs & 7u)
      : which == PA_FOCUS ? (uint8_t)((attrs >> 3) & 3u)
                          : (uint8_t)((attrs >> 5) & 3u);
  if (mode == 0u) return norm;
  if (mode == 1u && a > norm) return norm;
  return a;
}
static uint8_t hcap(uint8_t side) { return link_rule((uint8_t)(4u + side)); }
uint8_t link_fight_hp(uint8_t side) BANKED {
  uint8_t r2 = link_rule(2) & 7u;
  return (uint8_t)(8u + r2 + r2 + attr_of(side, PA_GRIT) + (hcap(side) & 7u));
}
uint8_t link_fight_focus(uint8_t side) BANKED {
  uint8_t f = (uint8_t)(attr_of(side, PA_FOCUS) + ((hcap(side) >> 5) & 1u));
  return f > 3u ? 3u : f;
}
void link_kit_mine(uint16_t *kit, uint8_t *pas) BANKED {
  if ((link_rule(3) & 64u) && link_role != LINK_HOST) {
    uint8_t i;
    for (i = 0; i < 6u; i++) kit[i] = their_kit_[i] < core.items ? their_kit_[i] : 0u;
    pas[0] = their_pas_[0];
    pas[1] = their_pas_[1];
  } /* mirror: the host's kit */
  else
    kit_now(kit, pas);
  if (link_rule(3) & 2u) pas[0] = pas[1] = FP_NONE;
  if (link_rule(link_role == LINK_HOST ? 4u : 5u) & 64u) pas[1] = FP_NONE; /* a handicap: one passive slot less */
}
void link_kit_theirs(uint16_t *kit, uint8_t *pas) BANKED {
  uint8_t i;
  for (i = 0; i < 6u; i++) kit[i] = their_kit_[i] < core.items ? their_kit_[i] : 0u;
  pas[0] = their_pas_[0] < FP_COUNT ? their_pas_[0] : FP_NONE;
  pas[1] = their_pas_[1] < FP_COUNT ? their_pas_[1] : FP_NONE;
  if (link_rule(3) & 2u) pas[0] = pas[1] = FP_NONE;
  if ((link_rule(3) & 64u) && link_role == LINK_HOST) {
    uint8_t p2[2];
    kit_now(kit, p2);
  } /* mirror kits: both play the host's */
  if (link_rule(link_role == LINK_HOST ? 5u : 4u) & 64u) pas[1] = FP_NONE;
}
void link_partner_genome(uint8_t *g) BANKED { memcpy(g, their_g_, 6); }
uint8_t link_partner_level(void) BANKED { return their_level_; }
uint8_t link_partner_attrs(void) BANKED { return their_attrs_; }
/* ---- turns ---- */
void link_fturn(uint8_t act, uint8_t turn, uint8_t hash, uint8_t pip) BANKED {
  my_act_[1] = my_act_[0];
  my_turn_[1] = my_turn_[0];
  my_hash_[1] = my_hash_[0];
  my_act_[0] = act;
  my_turn_[0] = turn;
  my_hash_[0] = hash;
  my_pip_ = (uint8_t)(pip & 7u);
  acked_ = 0;
  turn_t_ = 0;
  have_theirs_ = (fturn_in_ & BIT8[turn & 1u]) && their_turn_[turn & 1u] == turn
                     ? 1u
                     : 0u; /* for this turn (it was left set from the last one)*/
  (void)link_send(P_FTURN, (uint16_t)(act | ((uint16_t)turn << 8)),
                  (uint16_t)(hash | ((uint16_t)(my_pip_ | (have_theirs_ ? 0x80u : 0u)) << 8)));
}
uint8_t link_fturn_in(uint8_t *act, uint8_t *turn, uint8_t *hash, uint8_t *pip) BANKED {
  uint8_t k = (uint8_t)(*turn & 1u); /* *turn: the turn wanted */
  if (!(fturn_in_ & BIT8[k]) || their_turn_[k] != *turn) return 0;
  *act = their_act_[k];
  *hash = their_hash_[k];
  *pip = their_pip_[k];
  if (*turn == my_turn_[0] && !have_theirs_) {
    have_theirs_ = 1;
    if (!acked_) turn_t_ = RESEND_TURN;
  } /* our next re-send carries the ack */
  return 1;
}
uint8_t link_partner_locked(uint8_t turn) BANKED {
  return (fturn_in_ & BIT8[turn & 1u]) && their_turn_[turn & 1u] == turn;
}
/* a bout ended (r for this side: 1 won, 2 lost, 3 a draw): only FAIR counts for the feats (owner, 14.5) */
static const uint8_t PIPS[4] = {6, 4, 3, 2}, NEED[4] = {1, 2, 3, 2};
uint8_t link_fight_pips(void) BANKED {
  return (uint8_t)(PIPS[(link_rule(1) >> 6) & 3u] + ((link_rule(link_role == LINK_HOST ? 4u : 5u) & 0x80u) ? 1u : 0u));
}
uint8_t link_fight_over(uint8_t r) BANKED {
  uint8_t xp = r == 1u ? 15u : 10u, need = NEED[(link_rule(1) >> 4) & 3u];
  if (r == 1u)
    lp_wins[0]++;
  else if (r == 2u)
    lp_wins[1]++; /* [0] this side, [1] the partner */
  lp_bout++;
  if (lp_wins[0] < need && lp_wins[1] < need && lp_bout < 9u)
    return 1; /* the match goes on (draws count as a bout: nine at most) */
  r = lp_wins[0] > lp_wins[1] ? 1u : lp_wins[0] < lp_wins[1] ? 2u : 3u;
  xp = r == 1u ? 15u : 10u;
  if ((link_rule(1) & 3u) == 0u && r == 1u) cru_link(&core);
  (void)player_xp(xp);
  player_save();
  link_end(r == 1u ? LINK_WON : r == 2u ? LINK_LOSTRACE : LINK_DRAW);
  return 0;
}
