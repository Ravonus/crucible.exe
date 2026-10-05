/* Test hooks for the fight engine (docs/fight-system.md 13, steps 3-6): inert unless a harness sets fight_dbg
 * (crucible_flow.c polls it every frame). Arguments and results sit in SRAM bank 2 at 0xBA00..0xBCFF, scratch that no
 * save uses. 1: stance and power of 32 ids from the u16 at 0xBA00, into 0xBA10. 2: replay a scripted bout (the golden
 * duels): the script at 0xBA00, a record per turn at 0xBB80. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_state.h"
#include "crucible_fight_rules.h"
#include "crucible_fight.h"
#include "crucible_fight_boss.h"
#include "crucible_avatar.h"
#include "crucible_player.h"
#include "crucible_link.h"
#include "crucible_storyrun.h"
extern uint8_t work_[AVATAR_BYTES];
extern uint16_t pal_[4];
#define DBG ((uint8_t *)0xBA00u)
#define OUT ((uint8_t *)0xBB80u)
static uint16_t dbg_hand[4];
static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static void dbg_stances(void) {
  uint8_t i, buf[32];
  uint16_t start;
  ENABLE_RAM;
  SWITCH_RAM(2);
  start = u16(DBG);
  DISABLE_RAM;
  for (i = 0; i < 32u; i++) {
    uint16_t id = (uint16_t)(start + i);
    buf[i] = id < core.items ? (uint8_t)(fr_stance(id) | (fr_power(id) << 4)) : 0xffu;
  }
  ENABLE_RAM;
  SWITCH_RAM(2);
  memcpy(DBG + 16, buf, 32);
  DISABLE_RAM;
}
/* script: seed 2, arena, rules, hp0, hp1, focus0, focus1, kitA 12, kitB 12, pasA 2, pasB 2, turns, ai (persona+1, 0 none),
 * skill, uniform-every-third, then per turn a, b (b ignored when ai). Per turn out: hash, hp0, hp1, f0, f1, on0, on1,
 * rng lo, rng hi, b, n0, n1 (hand sizes) */
static void dbg_bout(void) {
  uint8_t s[64], t, a, b, o[12], n;
  uint16_t ka[6], kb[6];
  ENABLE_RAM;
  SWITCH_RAM(2);
  memcpy(s, DBG, 48);
  DISABLE_RAM;
  for (t = 0; t < 6u; t++) {
    ka[t] = u16(s + 8u + t * 2u);
    kb[t] = u16(s + 20u + t * 2u);
  }
  fr_begin(u16(s));
  fr.arena = s[2];
  fr.rules = s[3];
  fr_side_init(0, ka, 6, s + 32, s[4], s[6]);
  fr_side_init(1, kb, 6, s + 34, s[5], s[7]);
  n = s[36];
  if (s[37]) fr_ai_setup((uint8_t)(s[37] - 1u), s[38]);
  for (t = 0; t < n && !fr_over(); t++) {
    CRITICAL { fr_draw(); } /* interrupts off around the engine's calls: a harness times them without the ISRs */
    o[0] = fr_hash();
    do {
      CRITICAL { a = fr_pairs_step(); }
    } while (!a);
    ENABLE_RAM;
    SWITCH_RAM(2);
    a = DBG[48u + t * 2u];
    b = DBG[49u + t * 2u];
    DISABLE_RAM;
    if (s[37]) {
      uint8_t u = s[39] && !(fr.turn % 3u) ? 1u : 0u;
      CRITICAL { b = fr_ai_choose(u); }
    }
    CRITICAL { fr_resolve(a, b); }
    o[1] = (uint8_t)fr.s[0].hp;
    o[2] = (uint8_t)fr.s[1].hp;
    o[3] = fr.s[0].focus;
    o[4] = fr.s[1].focus;
    o[5] = fr.s[0].on;
    o[6] = fr.s[1].on;
    o[7] = (uint8_t)fr.rng;
    o[8] = (uint8_t)(fr.rng >> 8);
    o[9] = b;
    o[10] = fr.s[0].nhand;
    o[11] = fr.s[1].nhand;
    ENABLE_RAM;
    SWITCH_RAM(2);
    memcpy(OUT + (uint16_t)t * 12u, o, 12);
    DISABLE_RAM;
  }
  ENABLE_RAM;
  SWITCH_RAM(2);
  OUT[360] = t;
  OUT[361] = fr_over();
  DISABLE_RAM;
}
void fight_debug2(uint8_t c) BANKED;
void fight_debug(void) BANKED {
  uint8_t c = fight_dbg;
  if (c == 1u)
    dbg_stances();
  else if (c == 2u)
    dbg_bout();
  else if (c == 4u) { /* a boss: 0xBA00 seed 2, faction, tier, memory, hp, focus, pip adj, kit 12, its hand 8 */
    uint8_t d[32], i;
    uint16_t k[6];
    static const uint8_t none[2] = {FP_NONE, FP_NONE};
    ENABLE_RAM;
    SWITCH_RAM(2);
    memcpy(d, DBG, 32);
    DISABLE_RAM;
    for (i = 0; i < 6u; i++) k[i] = u16(d + 8u + i * 2u);
    for (i = 0; i < 4u; i++) dbg_hand[i] = u16(d + 20u + i * 2u);
    fr_begin(u16(d));
    fr_side_init(0, k, 6, none, d[5], d[6]);
    fb_begin(d[2], d[3], d[4], (int8_t)d[7]);
  } else if (c == 5u) { /* a boss round: you draw, it plans and moves; the round into 0xBB80 */
    fr_side *p = &fr.s[0];
    fb_state *b = FB;
    uint8_t o[24], i;
    fr_draw();
    fr_pairs_all();
    while (b->erode && p->nhand > 2u) {
      uint8_t s = p->hand[--p->nhand];
      p->disc[p->ndisc++] = s;
      p->dmask |= (uint8_t)(1u << s);
    }
    while (!fb_plan_step(dbg_hand)) {}
    fb_round();
    o[0] = b->kind;
    o[1] = b->shown;
    o[2] = b->real;
    o[3] = b->pw;
    o[4] = b->n;
    o[5] = b->veiled;
    o[6] = b->demo;
    o[7] = b->shield;
    o[8] = (uint8_t)b->atk;
    o[9] = (uint8_t)(b->atk >> 8);
    o[10] = p->nhand;
    for (i = 0; i < 4u; i++) o[11u + i] = p->hand[i];
    o[15] = p->focus;
    o[16] = (uint8_t)p->hp;
    o[17] = b->hp;
    o[18] = b->phase;
    o[19] = (uint8_t)fr.ai;
    o[20] = (uint8_t)(fr.ai >> 8);
    o[21] = (uint8_t)fr.rng;
    o[22] = (uint8_t)(fr.rng >> 8);
    o[23] = b->possess_slot;
    ENABLE_RAM;
    SWITCH_RAM(2);
    memcpy(OUT, o, 24);
    DISABLE_RAM;
  } else if (c == 6u) { /* resolve with the picks at 0xBA00 (two actions, two close flags) */
    uint8_t d[4], o[6];
    ENABLE_RAM;
    SWITCH_RAM(2);
    memcpy(d, DBG, 4);
    DISABLE_RAM;
    o[0] = fb_resolve(d, d + 2);
    o[1] = FB->hp;
    o[2] = (uint8_t)fr.s[0].hp;
    o[3] = fb_over();
    o[4] = FB->phase;
    o[5] = fr.s[0].focus;
    ENABLE_RAM;
    SWITCH_RAM(2);
    memcpy(OUT + 32, o, 6);
    DISABLE_RAM;
  } else if (c == 3u) { /* the kit cost of six ids at 0xBA00 (the stack discount), into 0xBA10 */
    uint16_t k[6];
    uint8_t i, r;
    ENABLE_RAM;
    SWITCH_RAM(2);
    for (i = 0; i < 6u; i++) k[i] = u16(DBG + i * 2u);
    DISABLE_RAM;
    r = fr_kit_cost(k, 6);
    ENABLE_RAM;
    SWITCH_RAM(2);
    DBG[16] = r;
    DISABLE_RAM;
  } else if (c >= 7u)
    fight_debug2(c); /* the avatar, the player record and link harness commands (crucible_fight_dbg2.c) */
  fight_dbg = 0;
}
