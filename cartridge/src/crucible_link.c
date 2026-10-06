/* LINK: two cartridges on a link cable. One byte each way per frame: the HOST clocks the transfer (internal clock),
 * the GUEST stays armed on the external clock. Bytes carry packets [A5 type a.lo a.hi b.lo b.hi sum]; 00 is idle.
 * A live partner never sends FF (FE and FF go out as FE 00 / FE 01), so FF means its side was not listening: the host
 * then sends the same byte again. Each side says HELLO every second; a packet within the last three seconds means LINKED.
 *
 * Sessions run on each player's own free-play save:
 *   CO-OP   every new discovery is sent across and lands in the partner's book too; SEND gives the focused element;
 *           with a TIME the session ends when it runs out (TIME UP), else when the cable goes
 *   RACE    a race to GOAL new finds before TIME runs out; the score travels every second
 *   FIGHT   a bout, each with their own things (crucible_fight.c); only inputs travel (P_TURN), each side resolves
 * The host makes the rules (mode, time, goal, a seed word) in the menu; START sends them with GO. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_data.h"
#include "crucible_ui.h"
#include "crucible_scene.h"
#include "crucible_state.h"
#include "crucible_lines.h"
#include "crucible_truths.h"
#include "crucible_link.h"
#include "crucible_link_io.h"
#include "crucible_link_scene.h"
#include "crucible_storyrun.h"
#include "crucible_link_rules.h"
#include "crucible_player.h"
#include "crucible_fight.h"
#define T_CREAM 7u
#define T_BRASS 15u
#define SYNC 0xA5u
#define QUEUE LK_TXQ
#define ESC 0xFEu

uint8_t link_on, link_role, link_mode, link_time, link_goal, link_linked, link_started;
uint16_t link_seed;
static uint8_t rx_[7], rx_n_, esc_, hello_t_, sec_t_, mine_, theirs_, told_;
#define tx_ lk_tx /* the queue the serial interrupt empties (crucible_link_io.c) */
#define tx_head_ lk_th
#define tx_tail_ lk_tt
uint8_t link_res, link_ask, link_save; /* the session's result (LINK_*), global for the link harness */
uint8_t link_rules[8] = {0, 0x50u, 2u, 0, 0, 0, 0, 0};
/* FAIR: NORMALISED, best of 3, 4 pips; HP 12 */ /* R0..R7; R0's mode and time are link_mode / link_time */
uint8_t link_rule(uint8_t k) BANKED {
  if (!k) return (uint8_t)(link_mode | (link_time << 4));
  if (k == 2u && link_mode != LINK_FIGHT) return link_goal;
  return k < 8u ? link_rules[k] : 0u;
}
#define result_ link_res
static uint16_t left_s_, inbox_, quiet_t_, poll_t_;
static char peer_[CRU_NAME + 1u];
static uint8_t hellos_, heard_, end_tx_;
static const uint8_t TIMES[4] = {0, 3, 5, 10}, GOALS[3] = {10, 25, 50};

static void put_(uint8_t x, uint8_t y, uint8_t tile, uint8_t attr) {
  VBK_REG = 1;
  set_bkg_tiles(x, y, 1, 1, &attr);
  VBK_REG = 0;
  set_bkg_tiles(x, y, 1, 1, &tile);
}
static void text_(uint8_t x, uint8_t y, const char *s, uint8_t width, uint8_t attr) {
  uint8_t tiles[20], attrs[20], i = 0, c;
  if (!width) return;
  if (width > 20u) width = 20u;
  memset(attrs, attr, width);
  while (i < width) {
    c = *s;
    if (c) s++;
    tiles[i++] = GLYPH(c);
  }
  VBK_REG = 1;
  set_bkg_tiles(x, y, width, 1, attrs);
  VBK_REG = 0;
  set_bkg_tiles(x, y, width, 1, tiles);
}
static void num_(char *o, uint16_t n, uint8_t w) {
  o[w] = 0;
  while (w) {
    o[--w] = (char)('0' + n % 10u);
    n /= 10u;
  }
}

static void raw(uint8_t b) {
  uint8_t n = (uint8_t)((tx_head_ + 1u) & (QUEUE - 1u));
  if (n == tx_tail_) return;
  tx_[tx_head_] = b;
  tx_head_ = n;
}
static void push(uint8_t b) {
  if (b >= ESC) {
    raw(ESC);
    raw((uint8_t)(b - ESC));
  } else if (b == SYNC) {
    raw(ESC);
    raw(2u);
  } else
    raw(b);
}
/* A packet goes in whole or not at all (at most 13 bytes escaped): dropping a byte from a full queue would tear packets
 * apart, and a partner whose loop runs slow (a long merge) would then hear only bad checksums until the link timed out. */
static uint8_t send(uint8_t type, uint16_t a, uint16_t b) {
  uint8_t sum = (uint8_t)(0x5Au + type + (uint8_t)a + (uint8_t)(a >> 8) + (uint8_t)b + (uint8_t)(b >> 8));
  if ((uint8_t)((tx_tail_ - tx_head_ - 1u) & (QUEUE - 1u)) < 13u) return 0;
  raw(SYNC);
  push(type);
  push((uint8_t)a);
  push((uint8_t)(a >> 8));
  push((uint8_t)b);
  push((uint8_t)(b >> 8));
  push(sum);
  return 1;
}
uint8_t link_send(uint8_t type, uint16_t a, uint16_t b) BANKED { return send(type, a, b); }
uint8_t link_idle(void) BANKED { return tx_tail_ == tx_head_; }
uint16_t link_quiet(void) BANKED { return quiet_t_; } /* frames since the last packet */ /* nothing waits to go out */
/* RULES keeps its old meaning in a.lo and b.lo (an older guest reads mode, time and goal) and carries R1 and R3 in the
 * high bytes; RULES2 the handicaps, banned categories and the arena */
static void send_rules(void) {
  send(P_RULES, (uint16_t)(link_mode | (link_time << 4) | ((uint16_t)link_rules[1] << 8)),
       (uint16_t)((link_mode == LINK_FIGHT ? link_rules[2] : link_goal) | ((uint16_t)link_rules[3] << 8)));
  send(P_RULES2, (uint16_t)(link_rules[4] | ((uint16_t)link_rules[5] << 8)),
       (uint16_t)(link_rules[6] | ((uint16_t)link_rules[7] << 8)));
  send(P_HELLO, link_seed, link_role);
}

/* a whole packet arrived */
static uint8_t packet(void) {
  uint8_t type = rx_[1], ev = LINK_EV_NONE;
  uint16_t a = (uint16_t)(rx_[2] | ((uint16_t)rx_[3] << 8)), b = (uint16_t)(rx_[4] | ((uint16_t)rx_[5] << 8));
  quiet_t_ = 0;
  if (!link_linked) {
    link_linked = 1;
    ev = LINK_EV_LINKED;
  }
  switch (type) {
  case P_HELLO:
    if (link_role == LINK_GUEST)
      link_seed = a;
    else if (b & 0x80u)
      heard_ = 1;
    break; /* b bit 7: the guest's session began */
  case P_RULES:
    if (link_role == LINK_GUEST) {
      link_mode = (uint8_t)(a & 15u);
      link_time = (uint8_t)((a >> 4) & 15u);
      link_rules[1] = (uint8_t)(a >> 8);
      link_rules[3] = (uint8_t)(b >> 8);
      if (link_mode == LINK_FIGHT)
        link_rules[2] = (uint8_t)b;
      else
        link_goal = (uint8_t)b;
      ev = LINK_EV_RULES;
    }
    break;
  case P_RULES2:
    if (link_role == LINK_GUEST) {
      link_rules[4] = (uint8_t)a;
      link_rules[5] = (uint8_t)(a >> 8);
      link_rules[6] = (uint8_t)b;
      link_rules[7] = (uint8_t)(b >> 8);
      ev = LINK_EV_RULES;
    }
    break;
  case P_GO:
    if (link_role == LINK_GUEST && !link_started) {
      link_mode = (uint8_t)(a & 15u);
      link_time = (uint8_t)((a >> 4) & 15u);
      if (link_mode != LINK_FIGHT) link_goal = (uint8_t)b;
      ev = LINK_EV_GO;
    }
    break;
  case P_FIND:
    if (link_mode == LINK_VERSUS) {
      if ((uint8_t)b > theirs_) link_coop_race(a, (uint8_t)b);
      theirs_ = (uint8_t)b;
      ev = LINK_EV_SCORE;
    } else {
      inbox_ = a;
      ev = LINK_EV_GIFT;
    }
    break; /* (co-op: an older partner's find) */
  case P_GIFT:
    inbox_ = a;
    ev = LINK_EV_GIFT;
    break;
  case P_SCORE: theirs_ = (uint8_t)a; break;
  case P_END:
    if (!result_) result_ = (uint8_t)a;
    heard_ = 1;
    break;
  case P_NAME:
  case P_NAME2: {
    uint8_t o = type == P_NAME ? 0u : 4u;
    peer_[o] = (char)rx_[2];
    peer_[o + 1u] = (char)rx_[3];
    peer_[o + 2u] = (char)rx_[4];
    peer_[o + 3u] = (char)rx_[5];
    peer_[CRU_NAME] = 0;
    ev = LINK_EV_RULES;
  } break;
  default:
    if (type >= P_FTURN) {
      uint8_t e = link_play_packet(type, a, b);
      if (e) ev = e;
    }
    break;
  }
  return ev;
}
static uint8_t take(uint8_t b) {
  uint8_t i, sum;
  if (b == ESC && !esc_) {
    esc_ = 1;
    return LINK_EV_NONE;
  }
  if (esc_) {
    esc_ = 0;
    b = b == 2u ? SYNC : (uint8_t)(ESC + (b & 1u));
    if (!rx_n_) return LINK_EV_NONE;
    rx_[rx_n_++] = b;
    goto have;
  } else if (b == SYNC && rx_n_)
    rx_n_ = 0; /* an unescaped sync always starts a packet */
  if (!rx_n_) {
    if (b == SYNC) rx_[rx_n_++] = b;
    return LINK_EV_NONE;
  }
  rx_[rx_n_++] = b;
have:
  if (rx_n_ < 7u) return LINK_EV_NONE;
  rx_n_ = 0;
  for (sum = 0x5Au, i = 1; i < 6u; i++) sum = (uint8_t)(sum + rx_[i]);
  if (sum != rx_[6]) return LINK_EV_NONE; /* a bad packet: wait for the next sync */
  return packet();
}

void link_open(uint8_t role) BANKED {
  poll_t_ = sys_time;
  link_on = 1;
  link_role = role;
  link_linked = 0;
  link_started = 0;
  rx_n_ = 0;
  esc_ = 0;
  quiet_t_ = 0;
  hello_t_ = 0;
  link_ask = 0;
  mine_ = theirs_ = 0;
  result_ = 0;
  told_ = 0;
  inbox_ = CRU_NONE;
  peer_[0] = 0;
  hellos_ = 0;
  heard_ = 0;
  end_tx_ = 0;
  if (role == LINK_HOST && !link_seed) link_seed = (uint16_t)(DIV_REG | ((uint16_t)core.rng << 8));
  link_io_open(role == LINK_HOST);
}
void link_close(void) BANKED {
  link_scene_reset();
  link_on = 0;
  link_linked = 0;
  link_started = 0;
  link_io_close();
}
/* One frame of the cable. Returns a LINK_EV_* for the cartridge. */
uint8_t link_poll(uint8_t dt) BANKED {
  uint8_t ev = LINK_EV_NONE, e, rx;
  if (!link_on) return ev;
  {
    uint16_t now = sys_time, d = (uint16_t)(now - poll_t_);
    poll_t_ = now;
    dt = d > 120u ? 120u : (uint8_t)d;
  } /* real frames: the game's dt stops at 8, and a fight's loop can take 26 (a cut then took minutes to notice) */
  /* the bytes the interrupt took since the last loop; one event a loop (the rest wait in the ring) */
  while (!ev && lk_rt != lk_rh) {
    rx = lk_rx[lk_rt];
    lk_rt = (uint8_t)((lk_rt + 1u) & (LK_RXQ - 1u));
    e = take(rx);
    if (e) ev = e;
  }
  hello_t_ = (uint8_t)(hello_t_ + dt);
  /* the periodic packets only on an idle line, so they never pile up behind a partner that reads slowly */
  if (hello_t_ >= 60u && tx_tail_ == tx_head_) {
    hello_t_ = 0;
    if (!link_started && (hellos_ & 3u) == 0u)
      send(P_AVATAR, (uint16_t)(pl.genome[0] | ((uint16_t)pl.genome[1] << 8)),
           (uint16_t)(pl.genome[2] | ((uint16_t)pl.genome[3] << 8)));
    if (!link_started && (hellos_ & 3u) == 0u)
      send(P_AVATAR2, (uint16_t)(pl.genome[4] | ((uint16_t)pl.genome[5] << 8)),
           (uint16_t)(pl.level | ((uint16_t)pl.attrs << 8))); /* level and attributes: the host's edge */
    if (!link_started && (++hellos_ & 3u) == 2u) {
      char n[CRU_NAME + 1u];
      cru_get_name(&core, n);
      send(P_NAME, (uint16_t)((uint8_t)n[0] | ((uint16_t)(uint8_t)n[1] << 8)),
           (uint16_t)((uint8_t)n[2] | ((uint16_t)(uint8_t)n[3] << 8)));
      send(P_NAME2, (uint16_t)((uint8_t)n[4] | ((uint16_t)(uint8_t)n[5] << 8)),
           (uint16_t)((uint8_t)n[6] | ((uint16_t)(uint8_t)n[7] << 8)));
    } /* who is on the other end */
    else if (link_role == LINK_HOST && !link_started)
      send_rules();
    /* what must not be lost is said again each second: GO until the guest's HELLO says its session began, the end
     * while the cable lives (a dropped packet is otherwise a session the other side never starts or never ends) */
    else if (link_role == LINK_HOST && link_started && !heard_)
      send(P_GO, (uint16_t)(link_mode | (link_time << 4)), link_goal);
    else if (end_tx_)
      send(P_END, end_tx_, 0);
    else
      send(P_HELLO, link_seed, (uint16_t)(link_role | (link_started ? 0x80u : 0u)));
    if (link_started && link_mode == LINK_VERSUS) send(P_SCORE, mine_, left_s_);
  }
  /* silence: 3 s in the lobby; 8 s in a session, where a busy scene (a pan, a fight opening, a long merge) on either
   * side moves only a byte per loop and a whole packet can take a few seconds to cross */
  if (quiet_t_ < 1000u) quiet_t_ = (uint16_t)(quiet_t_ + dt);
  if (link_linked && quiet_t_ >= (link_started ? 480u : 180u)) {
    link_linked = 0;
    if (link_started && !result_)
      result_ = LINK_LOST;
    else
      ev = LINK_EV_UNLINKED;
  }
  /* the session clock (versus with a time limit) */
  if (link_started && !result_ && link_time && link_mode != LINK_FIGHT) { /* co-op ends at the time too (TIME UP) */
    sec_t_ = (uint8_t)(sec_t_ + dt);
    while (sec_t_ >= 60u && !result_) {
      sec_t_ = (uint8_t)(sec_t_ - 60u);
      if (left_s_) left_s_--;
      if (!left_s_) {
        result_ = link_mode != LINK_VERSUS || mine_ == theirs_ ? LINK_DRAW : mine_ > theirs_ ? LINK_WON : LINK_LOSTRACE;
        end_tx_ = result_ == LINK_WON ? LINK_LOSTRACE : result_ == LINK_LOSTRACE ? LINK_WON : LINK_DRAW;
        send(P_END, end_tx_, 0);
      }
    }
  }
  if (link_started && !result_) link_play_tick(dt);
  if (result_ && !told_ && link_started) {
    told_ = 1;
    ev = LINK_EV_END;
    if (link_mode != LINK_FIGHT) link_coop_end();
  } /* the end, however it came */
  return ev;
}
/* The host starts the session (both sides then play). */
void link_go(void) BANKED {
  lr_save();
  send_rules();
  send(P_GO, (uint16_t)(link_mode | (link_time << 4)), link_goal);
  link_begin();
}
void link_begin(void) BANKED {
  link_scene_reset();
  link_started = 1;
  heard_ = 0;
  end_tx_ = 0;
  mine_ = theirs_ = 0;
  result_ = 0;
  told_ = 0;
  sec_t_ = 0;
  left_s_ = (uint16_t)TIMES[link_time & 3u] * 60u;
  link_play_begin();
  link_coop_begin();
}
/* the save brought (9.3): free play, or a story slot's run, which is your game for the session */
void link_bring(void) BANKED {
  crucible_story st;
  story_leave();
  if (link_save && story_peek((uint8_t)(link_save - 1u), &st)) (void)story_resume((uint8_t)(link_save - 1u));
}
void link_inbox_put(uint16_t id) BANKED { inbox_ = id; }
/* A new discovery on this side. */
void link_found(uint16_t id) BANKED {
  if (!link_started || result_) return;
  if (mine_ < 255u) mine_++;
  if (link_mode == LINK_COOP)
    link_made(id, 1);
  else
    send(P_FIND, id, mine_); /* co-op: acked, kept or shown (crucible_link_coop.c) */
  if (link_mode == LINK_VERSUS && mine_ >= GOALS[link_goal % 3u]) {
    result_ = LINK_WON;
    end_tx_ = LINK_LOSTRACE;
    send(P_END, LINK_LOSTRACE, 0);
  }
}
uint8_t link_result(void) BANKED { return result_; }
void link_gift(uint16_t id) BANKED {
  if (link_started) send(P_GIFT, id, 0);
}
void link_end(uint8_t result) BANKED {
  if (!result_) result_ = result;
}
void link_peer(char *out) BANKED {
  uint8_t i;
  for (i = 0; i < CRU_NAME; i++) {
    char c = peer_[i];
    out[i] = c >= ' ' && c < '`' ? c : (c >= 'a' && c <= 'z' ? (char)(c - 32) : ' ');
  }
  out[CRU_NAME] = 0;
  if (!peer_[0]) out[0] = 0;
}
uint16_t link_inbox(void) BANKED {
  uint16_t i = inbox_;
  inbox_ = CRU_NONE;
  return i;
}
/* row 0 between the counters: CO-OP: LINK and the partner's finds; VERSUS: score and clock */
void link_hud(void) BANKED {
  char s[12];
  uint16_t m;
  if (!link_started) return;
  text_(7, 0, " ", 7, T_BRASS);
  /* RACE: the score and the clock, M:SS (two digits of minutes from 10 up, so a 10 minute race never reads "0"), over the points */
  if (link_mode == LINK_VERSUS) {
    num_(s, mine_, 2);
    s[2] = ':';
    num_(s + 3, theirs_, 2);
    s[5] = 0;
    if (link_time) {
      m = left_s_ / 60u;
      s[5] = ' ';
      if (m >= 10u) {
        num_(s + 6, m, 2);
        s[8] = ':';
        num_(s + 9, left_s_ % 60u, 2);
      } else {
        num_(s + 6, m, 1);
        s[7] = ':';
        num_(s + 8, left_s_ % 60u, 2);
      }
    }
    text_(7, 0, s, 12, link_linked ? T_CREAM : T_BRASS);
  } else
    text_(8, 0, link_linked ? "LINKED" : "LOST", 6, link_linked ? T_CREAM : T_BRASS);
}
/* the end of a session: who won, the finds */
void link_result_draw(void) BANKED {
  char s[6];
  uint8_t y;
  crucible_load_font(); /* a fight may have swapped its icons into the font (a cable pulled mid-fight) */
  if (link_mode == LINK_FIGHT) { /* the versus: the end screen's frame, its pixel title */
    char t[14], l1[20], l2[20]; /* (copied to WRAM: the card is drawn from another bank) */
    strcpy(t, result_ == LINK_WON        ? "YOU WIN"
              : result_ == LINK_LOSTRACE ? "THEY WIN"
              : result_ == LINK_DRAW     ? "NEITHER FALLS"
                                         : "NO CONTEST");
    if (result_ == LINK_LOST) {
      char l[12];
      crucible_text_line(EV_CONTEST_FIRST, l, sizeof l, 0);
      if (l[0]) strcpy(t, l);
    }
    strcpy(l1, "THE MATCH IS OVER");
    strcpy(l2, "WHAT YOU MADE STAYS");
    fight_card(t, result_ == LINK_WON, l1, l2);
    return;
  }
  scene_draw(SCENE_RECORDS);
  for (y = 0; y < 18u; y++) text_(0, y, " ", 20, T_CREAM);
  text_(4, 4,
        result_ == LINK_WON        ? "YOU WIN"
        : result_ == LINK_LOSTRACE ? "THEY WIN"
        : result_ == LINK_DRAW     ? (link_mode == LINK_COOP ? "TIME UP" : "A DRAW")
                                   : "LINK LOST",
        9u, T_CREAM);
  if (result_ == LINK_LOST && link_mode == LINK_FIGHT) {
    char l[12];
    crucible_text_line(EV_CONTEST_FIRST, l, sizeof l, 0);
    text_(4, 4, l, 10, T_CREAM);
  } /* a FIGHT the cable broke: nobody's feat, nobody's loss */
  if (link_mode == LINK_FIGHT) {
    text_(1, 8, "THE MATCH IS OVER", 17, T_BRASS);
    text_(0, 9, "WHAT YOU MADE STAYS", 19, T_BRASS);
  } /* (THE CRUCIBLE: your new forges are discoveries) */
  else {
    text_(3, 8, "YOUR FINDS", 10, T_BRASS);
    num_(s, mine_, 3);
    text_(14, 8, s, 3, T_CREAM);
  }
  if (link_mode == LINK_VERSUS) {
    text_(3, 10, "THEIR FINDS", 11, T_BRASS);
    num_(s, theirs_, 3);
    text_(14, 10, s, 3, T_CREAM);
  }
  put_(1, 17, UI_A, T_CREAM);
  text_(2, 17, "MENU", 4, T_CREAM);
}
/* menu labels, copied into the caller's buffer (out[8]) */
void link_label(uint8_t which, char *out) BANKED {
  static const char *const MODES[3] = {"CO-OP", "RACE", "FIGHT"},
                           *const TIMES_N[4] = {"NONE", "3 MIN", "5 MIN", "10 MIN"},
                           *const GOALS_N[3] = {"10", "25", "50"};
  strcpy(out, which == 0u   ? MODES[link_mode < 3u ? link_mode : 0u]
              : which == 1u ? TIMES_N[link_time & 3u]
                            : GOALS_N[link_goal % 3u]);
}
