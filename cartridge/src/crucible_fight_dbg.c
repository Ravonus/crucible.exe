/* Test hooks for THE CRUCIBLE (inert unless a harness sets fight_dbg; crucible_flow.c polls it every frame). Arguments
 * and results sit in SRAM bank 2 at 0xBA00..0xBCFF, scratch no save uses.
 *   1  replay a golden bout (docs/fight-system/sim/crux.py --golden): the script at 0xBA00, per turn the word played
 *      and the state hash at 0xBB80 (three bytes a turn), then the turns, the result and both HPs at 0xBC00
 *   2  the player's bag (crucible_fight_story.c fs_bag): its size and ids at 0xBB80
 *   7.. the avatar, the player record, the link harness (crucible_fight_dbg2.c)
 * fight_dbg goes back to 0 when the command is done.
 * Script: first, rng lo, rng hi; per side: n, hp, tw, bag[10] (u16); per side: ai (0 scripted, 1 AI), seed, know,
 * skill (u16 each); per turn: the scripted word (u16; 0xffff: the side's AI decides). */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_state.h"
#include "crucible_fight.h"
#include "crucible_fight_int.h"
#define DBG ((uint8_t *)0xBA00u)
#define OUT ((uint8_t *)0xBB80u)
static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static void bout(void) {
  uint8_t s[150], o[3], k, t, n, *p;
  uint16_t bag[CX_BAG], w, ai[2][3];
  uint8_t isai[2];
  ENABLE_RAM;
  SWITCH_RAM(2);
  memcpy(s, DBG, sizeof s);
  DISABLE_RAM;
  p = s + 3;
  for (k = 0; k < 2u; k++) {
    for (t = 0; t < CX_BAG; t++) bag[t] = u16(p + 3 + t * 2u);
    cx_side_set(k, bag, p[0], (int8_t)p[1], p[2]);
    p += 23;
  }
  for (k = 0; k < 2u; k++) {
    isai[k] = p[0];
    ai[k][0] = u16(p + 1);
    ai[k][1] = u16(p + 3);
    ai[k][2] = u16(p + 5);
    p += 7;
  }
  cx_begin(s[0]);
  cx_ai_rng = u16(s + 1);
  for (t = 0; t < CX_TURNS && !cx_over(); t++) {
    k = cx.act;
    CRITICAL { cx_begin_turn(); }
    w = u16(p + t * 2u);
    if (isai[k] && w == 0xffffu) {
      CRITICAL {
        cx_ai_begin(k, ai[k][0], ai[k][1], ai[k][2]);
        while (!cx_ai_step()) {}
      }
      w = cx_ai_word;
    }
    if (!cx_legal(k, w)) break;
    CRITICAL { cx_do(w); }
    o[0] = (uint8_t)w;
    o[1] = (uint8_t)(w >> 8);
    o[2] = cx_hash();
    ENABLE_RAM;
    SWITCH_RAM(2);
    memcpy(OUT + (uint16_t)t * 3u, o, 3);
    DISABLE_RAM;
  }
  n = t;
  ENABLE_RAM;
  SWITCH_RAM(2);
  ((uint8_t *)0xBC00u)[0] = n;
  ((uint8_t *)0xBC00u)[1] = cx_over();
  ((uint8_t *)0xBC00u)[2] = (uint8_t)cx.s[0].hp;
  ((uint8_t *)0xBC00u)[3] = (uint8_t)cx.s[1].hp;
  DISABLE_RAM;
}
void fight_debug2(uint8_t c) BANKED;
void fight_debug(void) BANKED {
  uint8_t c = fight_dbg;
  if (c == 1u)
    bout();
  else if (c == 2u) {
    uint16_t bag[8];
    uint8_t n = fs_bag(bag);
    ENABLE_RAM;
    SWITCH_RAM(2);
    OUT[0] = n;
    memcpy(OUT + 1, bag, 16);
    DISABLE_RAM;
  } else if (c >= 7u)
    fight_debug2(c);
  fight_dbg = 0; /* done */
}
