/* Easter eggs (generated into cartridge/data/include/crucible_eggs_data.h): nostalgic secrets with a visitor each.
 * This watches for the triggers (a button code or SELECT taps on the title, the title left alone, SELECT while a relic
 * talks, an element made, the player's name, the same element made again and again), remembers which eggs were found
 * (SRAM bank 15 at 0x1F00, past the story record; neither save writes there) and hands crucible.c an egg to send.
 * A found egg's grant is given once; codes, taps and the idle egg can be shown again, the rest come once. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_data.h"
#include "crucible_state.h"
#include "crucible_player.h"
#include "crucible_eggs.h"
#include "crucible_eggs_data.h"

#define K_CODE 0u
#define K_TAP 1u
#define K_IDLE 2u
#define K_TALK 3u
#define K_MAKE 4u
#define K_NAME 5u
#define K_LOOP 6u
#define FOUND ((uint8_t *)0xBF00u) /* SRAM bank 15, 0x1F00: 'E' 'G' then a bit per egg */

static uint8_t found_[4], dmg_, pending_, code_at_[4], taps_, talks_, loop_n_, idle_s_, idle_f_;
static uint16_t item_, last_made_;

static void store(void) {
  ENABLE_RAM;
  SWITCH_RAM(15);
  FOUND[0] = 'E';
  FOUND[1] = 'G';
  memcpy(FOUND + 2, found_, sizeof found_);
  FOUND[6] = dmg_;
  SWITCH_RAM(0);
  DISABLE_RAM;
}
static uint8_t was_found(uint8_t k) { return (uint8_t)((found_[k >> 3] >> (k & 7u)) & 1u); }
static void fire(uint8_t k, uint8_t again) {
  if (pending_ || (!again && was_found(k))) return;
  if (!was_found(k)) {
    found_[k >> 3] |= (uint8_t)(1u << (k & 7u));
    store();
    if (egg_grant[k] != 0xffffu) cru_grant(&core, egg_grant[k], 0);
  }
  if (egg_flags[k] & 2u) {
    dmg_ ^= 1u;
    store();
    if (!(pl.flags & PF_DMG)) {
      pl.flags |= PF_DMG;
      player_save();
    }
  } /* the world changes (and your face may wear it: 8.4) */
  pending_ = (uint8_t)(k + 1u);
}
void egg_boot(void) BANKED {
  uint8_t k;
  char name[CRU_NAME + 1];
  ENABLE_RAM;
  SWITCH_RAM(15);
  if (FOUND[0] == 'E' && FOUND[1] == 'G') {
    memcpy(found_, FOUND + 2, sizeof found_);
    dmg_ = FOUND[6] == 1u;
  } else {
    memset(found_, 0, sizeof found_);
    dmg_ = 0;
  }
  SWITCH_RAM(0);
  DISABLE_RAM;
  cru_get_name(&core, name);
  for (k = 0; k < EGG_COUNT; k++)
    if (egg_kind[k] == K_NAME && !strcmp(name, egg_names[egg_arg[k]])) fire(k, 0);
}
void egg_input(uint8_t pressed, uint8_t title, uint8_t dt) BANKED {
  uint8_t k, b, c = 0;
  if (!title) {
    taps_ = 0;
    idle_s_ = 0;
    idle_f_ = 0;
    return;
  }
  if (pressed) {
    idle_s_ = 0;
    idle_f_ = 0;
    if (pressed & J_SELECT)
      taps_++;
    else
      taps_ = 0;
    for (k = 0; k < EGG_COUNT; k++) {
      if (egg_kind[k] == K_TAP && taps_ == (uint8_t)egg_arg[k]) {
        taps_ = 0;
        fire(k, 1);
      }
      if (egg_kind[k] != K_CODE || c >= sizeof code_at_) continue;
      b = egg_codes[egg_arg[k]][code_at_[c]];
      if (pressed & b) {
        if (!egg_codes[egg_arg[k]][++code_at_[c]]) {
          code_at_[c] = 0;
          fire(k, 1);
        }
      } else
        code_at_[c] = (pressed & egg_codes[egg_arg[k]][0]) ? 1u : 0u;
      c++;
    }
    return;
  }
  idle_f_ = (uint8_t)(idle_f_ + dt);
  if (idle_f_ >= 60u) {
    idle_f_ = (uint8_t)(idle_f_ - 60u);
    if (idle_s_ < 255u) idle_s_++;
    for (k = 0; k < EGG_COUNT; k++)
      if (egg_kind[k] == K_IDLE && idle_s_ == (uint8_t)egg_arg[k]) fire(k, 1);
  }
}
void egg_talk_select(void) BANKED {
  uint8_t k;
  talks_++;
  for (k = 0; k < EGG_COUNT; k++)
    if (egg_kind[k] == K_TALK && talks_ == (uint8_t)egg_arg[k]) {
      talks_ = 0;
      fire(k, 1);
    }
}
void egg_talk_reset(void) BANKED { talks_ = 0; }
void egg_made(uint16_t id) BANKED {
  uint8_t k;
  if (id == last_made_)
    loop_n_++;
  else {
    loop_n_ = 1;
    last_made_ = id;
  }
  for (k = 0; k < EGG_COUNT; k++) {
    if (egg_kind[k] == K_MAKE && egg_arg[k] == id) {
      item_ = id;
      fire(k, 0);
    }
    if (egg_kind[k] == K_LOOP && loop_n_ == (uint8_t)egg_arg[k]) {
      item_ = id;
      fire(k, 1);
    }
  }
}
uint8_t egg_take(void) BANKED {
  uint8_t p = pending_;
  pending_ = 0;
  return p;
}
void egg_get(uint8_t k, egg_info *out) BANKED {
  memcpy(out->visitor, egg_visitor[k], 11);
  out->visitor[10] = 0;
  out->line = egg_line[k];
  out->lines = egg_nlines[k];
  out->faction = egg_fac[k];
  out->glitch = egg_flags[k] & 1u;
  out->item = egg_kind[k] == K_MAKE || egg_kind[k] == K_LOOP ? item_ : egg_grant[k];
}
uint8_t egg_count(uint8_t *total) BANKED {
  uint8_t k, n = 0;
  for (k = 0; k < EGG_COUNT; k++) n = (uint8_t)(n + was_found(k));
  if (total) *total = EGG_COUNT;
  return n;
}
uint8_t egg_dmg(void) BANKED { return dmg_; }
/* A found egg's visitor, to drop in as a cameo in an ordinary conversation (0xff: none found yet). */
uint8_t egg_cameo(uint8_t r) BANKED {
  uint8_t k, n = egg_count(0), i = 0;
  if (!n) return 0xffu;
  r = (uint8_t)(r % n);
  for (k = 0; k < EGG_COUNT; k++)
    if (was_found(k)) {
      if (i == r) return k;
      i++;
    }
  return 0xffu;
}
/* every colour to the original Game Boy's four greens by brightness (32 background + 32 sprite colours) */
void egg_dmg_filter(uint16_t *bg, uint16_t *sp) BANKED {
  static const uint16_t DMG[4] = {0x04C1u, 0x1565u, 0x0690u, 0x06D2u};
  uint8_t i;
  uint16_t c, l, *p;
  for (i = 0; i < 64u; i++) {
    p = i < 32u ? bg + i : sp + (i - 32u);
    c = *p;
    l = (uint16_t)((c & 31u) * 2u + ((c >> 5) & 31u) * 5u + ((c >> 10) & 31u));
    *p = DMG[l < 62u ? 0 : l < 124u ? 1 : l < 186u ? 2 : 3];
  }
}
