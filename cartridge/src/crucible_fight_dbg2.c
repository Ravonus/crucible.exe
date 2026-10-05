/* The debug mailbox, continued (crucible_fight_dbg.c): the avatar, the player record and the link harness's commands.
 * Inert unless a test harness writes fight_dbg. */
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
#include "crucible_lines.h"
extern uint8_t work_[AVATAR_BYTES];
extern uint16_t pal_[4];
#define DBG ((uint8_t *)0xBA00u)
#define OUT ((uint8_t *)0xBB80u)
static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
void fight_debug2(uint8_t c) BANKED {
  if (c == 7u) { /* a face from the genome at 0xBA00: its palette into 0xBA10, its 36 tiles into 0xBA20 */
    uint8_t g[6];
    ENABLE_RAM;
    SWITCH_RAM(2);
    memcpy(g, DBG, 6);
    DISABLE_RAM;
    avatar_make_genome(g);
    ENABLE_RAM;
    SWITCH_RAM(2);
    memcpy(DBG + 16, pal_, 8);
    memcpy(DBG + 32, work_, AVATAR_BYTES);
    DISABLE_RAM;
  } else if (
      c == 8u ||
      c ==
          9u) { /* 8: player_xp(u16 at 0xBA00); 9: player_spend(0xBA00). Into 0xBB80: the result, level, xp, pts, attrs, unlock[11], flags */
    uint8_t o[18], r;
    uint16_t n;
    ENABLE_RAM;
    SWITCH_RAM(2);
    n = u16(DBG);
    DISABLE_RAM;
    r = c == 8u ? player_xp(n) : player_spend((uint8_t)n);
    o[0] = r;
    o[1] = pl.level;
    o[2] = (uint8_t)pl.xp;
    o[3] = (uint8_t)(pl.xp >> 8);
    o[4] = pl.pts;
    o[5] = pl.attrs;
    memcpy(o + 6, pl.unlock, 11);
    o[17] = pl.flags;
    ENABLE_RAM;
    SWITCH_RAM(2);
    memcpy(OUT, o, 18);
    DISABLE_RAM;
  } else if (c == 10u) {
    uint16_t id;
    ENABLE_RAM;
    SWITCH_RAM(2);
    id = u16(DBG);
    DISABLE_RAM;
    link_found(id);
  } /* the link harness: a make, as if just discovered */
  else if (c == 11u)
    link_missed();
  else if (
      c ==
      12u) { /* a simulated co-op session (step 22): 0xBA00 their seed, their order, heart, scenes, my order, heart, their last make */
    uint8_t d[10], o[16], i;
    crucible_story *st = talk_saga();
    uint8_t *k = st->flags + 16u;
    uint32_t s0 = st->seed;
    ENABLE_RAM;
    SWITCH_RAM(2);
    memcpy(d, DBG, 10);
    DISABLE_RAM;
    k[14] = d[5];
    k[15] = d[6];
    pl.tint[0] = pl.tint[1] = 0;
    lc_seed_got = 0;
    pl.plast = u16(d + 7);
    link_seed_session(u16(d), (int8_t)d[2], (int8_t)d[3]);
    for (i = 0; i < d[4]; i++) link_seed_scene();
    o[0] = k[14];
    o[1] = k[15];
    o[2] = (uint8_t)pl.tint[0];
    o[3] = (uint8_t)pl.tint[1];
    memcpy(o + 4, &s0, 4);
    memcpy(o + 8, &st->seed, 4);
    {
      uint16_t m = cru_story_recall(st, 0);
      o[12] = (uint8_t)m;
      o[13] = (uint8_t)(m >> 8);
    }
    o[14] = pl.sessions;
    o[15] = 0;
    ENABLE_RAM;
    SWITCH_RAM(2);
    memcpy(OUT, o, 16);
    DISABLE_RAM;
  } else if (c == 13u) { /* render line id (u16 at 0xBA00) into 0xBB80 (its length at 0xBB7F) */
    uint16_t id;
    char o[48];
    uint8_t n;
    ENABLE_RAM;
    SWITCH_RAM(2);
    id = u16(DBG);
    DISABLE_RAM;
    n = crucible_text_line(id, o, sizeof o, 0);
    ENABLE_RAM;
    SWITCH_RAM(2);
    OUT[-1] = n;
    memcpy(OUT, o, sizeof o);
    DISABLE_RAM;
  }
}
