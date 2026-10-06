/* The player record (docs/fight-system.md 8.3, 10.4): who you are across fights. Levels come from play
 * (discoveries, remakes, chapters, fights, link sessions) on a curve with no table (40 + 20 L to the next level); each
 * level opens one cosmetic option in an order seeded per save, every third level gives an attribute point (GRIT, FOCUS,
 * REACH, capped so that a full build is level 27), level 6 opens the second passive slot. The kit is persistent: the
 * one you set at the bench, re-fitted when an element in it is gone (auto-kit: the balanced builder).
 *
 * A story slot keeps its record at 0x1DC0 of its SRAM bank (outside the story record 0x1D00..0x1DB7 and the clock at
 * 0x1F00) with its own CRC; free play keeps the first 82 bytes in the Classic record (bytes 264..345, the core writes
 * them under its own CRC through core.card). A save from before has no record: defaults, the face rolled from its seed. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_state.h"
#include "crucible_storyrun.h"
#include "crucible_player.h"
#include "crucible_avatar.h"
uint8_t cri_owned(crucible_core *c, uint16_t id) BANKED;

player_rec pl;
uint8_t pl_slot = 0xffu;
/* STYLE 9, HEAD 8, EYES 10, MOUTH 8, CROWN 8, MARK 8, HUE 16, GRAIN 6, AURA 8 */
static const uint8_t AV_COUNT[AV_ROWS] = {9, 8, 10, 8, 8, 8, 16, 6, 8};
static const uint8_t AV_BASE[AV_ROWS] = {0, 9, 17, 27, 35, 43, 51, 67, 73};
uint8_t player_row_count(uint8_t row) BANKED { return row < AV_ROWS ? AV_COUNT[row] : 0u; }
uint8_t player_row_base(uint8_t row) BANKED { return row < AV_ROWS ? AV_BASE[row] : 0u; }
/* open from the start (8.2): robot human pixel; 4 heads, eyes, mouths; no crown or an antenna; no mark or freckles;
 * 8 hues; 2 grains; no aura */
static const uint8_t START[] = {0,  1,  3,  9,  10, 11, 12, 17, 18, 20, 22, 27, 28, 30, 32,
                                35, 36, 43, 48, 51, 52, 53, 54, 55, 56, 57, 58, 67, 68, 73};
/* never in the level order: the secret styles (cloud, AI eye, sheet ghost, flame, radiant), halo and horns, the glitch
 * mark, the style-fixed eyes (human 8, the AI's great eye 9) */
static const uint8_t SECRET[] = {4, 5, 6, 7, 8, 38, 39, 47, 25, 26};
static uint16_t crc16(uint16_t h, const uint8_t *p, uint8_t n) {
  uint8_t i;
  while (n--) {
    h ^= *p++;
    for (i = 0; i < 8u; i++) h = (h & 1u) ? (uint16_t)((h >> 1) ^ 0xa001u) : (uint16_t)(h >> 1);
  }
  return h;
}
/* the slot's record: pl (82 bytes, RAM) + bytes 82..125 (SRAM only) + CRC; SRAM must be on and the slot's bank in */
#define REC ((uint8_t *)(0xA000u + PL_AT))
static uint16_t rec_crc(void) {
  return crc16(crc16(0xffffu, (const uint8_t *)&pl, PL_CARD), REC + PL_CARD, (uint8_t)(PL_BYTES - 2u - PL_CARD));
}
static uint8_t slot_bank(uint8_t s) { return s == 1u ? 1u : s == 2u ? 3u : 15u; }

static const uint8_t BIT8[8] = {1, 2, 4, 8, 16, 32, 64, 128}; /* masks: sdcc miscompiles (field >> var) & 1 */
uint16_t player_bit(uint8_t k) BANKED { return k < 8u ? BIT8[k] : (uint16_t)((uint16_t)BIT8[k & 7u] << 8); }
uint8_t player_unlocked(uint8_t o) BANKED { return o < AV_OPTIONS && (pl.unlock[o >> 3] & BIT8[o & 7u]) ? 1u : 0u; }
void player_unlock(uint8_t o) BANKED {
  if (o < AV_OPTIONS) pl.unlock[o >> 3] |= BIT8[o & 7u];
}
static uint8_t secret(uint8_t o) {
  uint8_t i;
  for (i = 0; i < sizeof SECRET; i++)
    if (SECRET[i] == o) return 1;
  return 0;
}
/* the level order: the options neither open at the start nor secret, shuffled by the save's own seed */
static uint8_t perm(uint8_t *perm_) {
  uint8_t i, n = 0, j, t, k;
  uint16_t r = (uint16_t)(0x5eedu ^ ((uint16_t)pl.order * 257u)) | 1u; /* the record's own order seed */
  for (i = 0; i < AV_OPTIONS; i++) {
    for (k = 0, j = 0; j < sizeof START; j++)
      if (START[j] == i) k = 1;
    if (!k && !secret(i)) perm_[n++] = i;
  }
  for (i = (uint8_t)(n - 1u); i; i--) {
    r ^= (uint16_t)(r << 7);
    r ^= (uint16_t)(r >> 9);
    r ^= (uint16_t)(r << 8);
    j = (uint8_t)(((uint16_t)(uint8_t)r * (uint8_t)(i + 1u)) >> 8);
    t = perm_[i];
    perm_[i] = perm_[j];
    perm_[j] = t;
  }
  return n;
}

/* ---- a face within the open options ---- */
static uint8_t pick_open(uint8_t row, uint16_t *r) {
  uint8_t n = 0, k, v = 0, c = AV_COUNT[row];
  for (k = 0; k < c; k++)
    if (player_unlocked((uint8_t)(AV_BASE[row] + k))) n++;
  if (!n) return 0;
  *r ^= (uint16_t)(*r << 7);
  *r ^= (uint16_t)(*r >> 9);
  *r ^= (uint16_t)(*r << 8);
  n = (uint8_t)(((uint16_t)(uint8_t)*r * n) >> 8);
  for (k = 0; k < c; k++)
    if (player_unlocked((uint8_t)(AV_BASE[row] + k))) {
      if (!n) {
        v = k;
        break;
      }
      n--;
    }
  return v;
}
/* genome (8.1): 0 style | hue << 4; 1 head | eyes << 3 | wild << 7; 2 mouth | gear << 3 | neck << 6;
 * 3 pattern | dither << 3 | size << 6; 4 fx | mark << 3 | glitch-in << 6; 5 secret bits */
void player_genome_roll(uint8_t *g, uint16_t seed) BANKED {
  uint16_t r = seed ? seed : 0x5eedu;
  uint8_t style = pick_open(0, &r), head = pick_open(1, &r), eyes = pick_open(2, &r), mouth = pick_open(3, &r),
          gear = pick_open(4, &r);
  uint8_t mark = pick_open(5, &r), hue = pick_open(6, &r), grain = pick_open(7, &r), aura = pick_open(8, &r);
  g[0] = (uint8_t)(style | (hue << 4));
  g[1] = (uint8_t)(head | (eyes << 3));
  g[2] = (uint8_t)(mouth | (gear << 3) | ((r & 3u) << 6));
  g[3] = (uint8_t)(mark | (grain << 3) | (((r >> 4) % 3u) << 6));
  g[4] = (uint8_t)(aura);
  g[5] = 0;
}

void player_defaults(uint32_t seed) BANKED {
  uint8_t i;
  memset(&pl, 0, sizeof pl);
  pl.magic[0] = 'P';
  pl.magic[1] = 'L';
  pl.version = PL_VERSION;
  pl.level = 1;
  for (i = 0; i < sizeof START; i++) player_unlock(START[i]);
  pl.known = 0x000Bu; /* ECHO, PRISM, VIGIL */
  for (i = 0; i < 6u; i++) pl.kit[i] = CRU_NONE;
  pl.equip = 0xFFu; /* (the passives' slots: unused since THE CRUCIBLE) */
  pl.rules[0] = 2u;
  pl.rules[1] = 0x50u;
  pl.rules[2] = 2u; /* FAIR: FIGHT, NORMALISED, best of 3, 4 pips, HP 12 (8 + 2 + GRIT 2) */
  player_genome_roll(pl.genome, (uint16_t)(seed ^ (seed >> 16)));
  pl.order = (uint8_t)(seed ^ (seed >> 8) ^ (seed >> 16) ^
                       (seed >> 24)); /* the level order's seed: two saves open different things first */
}
static uint8_t valid(void) {
  return pl.magic[0] == 'P' && pl.magic[1] == 'L' && pl.version >= 1u && pl.version <= PL_VERSION && pl.level >= 1u &&
         pl.level <= 30u;
}
/* migration: version 1 is the first; a later version adds its defaults here */
static void migrate(void) { pl.version = PL_VERSION; }

void player_classic_attach(void) BANKED {
  memset(&pl, 0, sizeof pl);
  core.card = (uint8_t *)&pl;
  pl_slot = 0xffu;
}
void player_classic_loaded(void) BANKED {
  if (!valid())
    player_defaults(((uint32_t)core.variant_seed << 8) ^ core.rng);
  else if (pl.version < PL_VERSION)
    migrate();
}
void player_story_load(uint8_t slot, uint32_t seed) BANKED {
  uint8_t ok;
  pl_slot = slot;
  ENABLE_RAM;
  SWITCH_RAM(slot_bank(slot));
  memcpy(&pl, REC, PL_CARD);
  ok = valid() && rec_crc() == (uint16_t)(REC[PL_BYTES - 2u] | ((uint16_t)REC[PL_BYTES - 1u] << 8));
  SWITCH_RAM(0);
  DISABLE_RAM;
  if (!ok) {
    player_defaults(seed);
    ENABLE_RAM;
    SWITCH_RAM(slot_bank(slot));
    memset(REC + PL_CARD, 0, PL_BYTES - PL_CARD);
    SWITCH_RAM(0);
    DISABLE_RAM;
    player_save();
    return;
  }
  if (pl.version < PL_VERSION) {
    migrate();
    player_save();
  }
}
/* A new run in a slot. Who you were in the slot's last run stays (level, attributes, unlocks, secrets, the passives you
 * know: "the unlock is kept by the slot's next run", 8.4); the run's own things start again (kit, nemesis, counters). */
void player_story_new(uint8_t slot, const uint8_t *genome) BANKED {
  uint8_t ok, i;
  pl_slot = slot;
  ENABLE_RAM;
  SWITCH_RAM(slot_bank(slot));
  memcpy(&pl, REC, PL_CARD);
  ok = valid() && rec_crc() == (uint16_t)(REC[PL_BYTES - 2u] | ((uint16_t)REC[PL_BYTES - 1u] << 8));
  SWITCH_RAM(0);
  DISABLE_RAM;
  if (!ok)
    player_defaults(talk_saga()->seed ^ slot);
  else {
    for (i = 0; i < 6u; i++) pl.kit[i] = CRU_NONE;
    pl.nemesis[0] = pl.nemesis[1] = 0;
    pl.remake = 0;
    pl.chapter = 0;
    pl.cell = 0;
    pl.flags &= (uint8_t)~PF_FIRST_DUEL;
    migrate();
  }
  if (genome) memcpy(pl.genome, genome, 6);
  pl.flags |= PF_CREATED;
  player_save();
  avatar_cache(pl.genome);
}
void player_save(void) BANKED {
  uint16_t h;
  if (pl_slot == 0xffu) {
    core.card = (uint8_t *)&pl;
    cru_save(&core);
    return;
  }
  ENABLE_RAM;
  SWITCH_RAM(slot_bank(pl_slot));
  memcpy(REC, &pl, PL_CARD);
  h = rec_crc();
  REC[PL_BYTES - 2u] = (uint8_t)h;
  REC[PL_BYTES - 1u] = (uint8_t)(h >> 8);
  SWITCH_RAM(0);
  DISABLE_RAM;
}

/* ---- levels and attributes ---- */
uint8_t player_attr(uint8_t w) BANKED {
  return w == PA_GRIT    ? (uint8_t)(pl.attrs & 7u)
         : w == PA_FOCUS ? (uint8_t)((pl.attrs >> 3) & 3u)
                         : (uint8_t)((pl.attrs >> 5) & 3u);
}
uint8_t player_slots(void) BANKED { return pl.level >= 6u ? 2u : 1u; }
static uint16_t need(uint8_t level) {
  uint16_t n = 40u;
  while (level--) n = (uint16_t)(n + 20u);
  return n;
} /* 40 + 20 L, no multiply */
uint16_t player_next(void) BANKED { return pl.level >= 30u ? 0u : (uint16_t)(need(pl.level) - pl.xp); }
uint8_t player_xp(uint16_t xp) BANKED {
  uint8_t gained = 0, n, perm_[AV_OPTIONS];
  if (pl.level >= 30u) return 0;
  pl.xp = (uint16_t)(pl.xp + xp);
  while (pl.level < 30u && pl.xp >= need(pl.level)) {
    pl.xp = (uint16_t)(pl.xp - need(pl.level));
    pl.level++;
    gained++;
    n = perm(perm_);
    if ((uint8_t)(pl.level - 2u) < n) player_unlock(perm_[pl.level - 2u]); /* one cosmetic option per level */
    if (pl.level <= 27u && (pl.level == 3u || pl.level == 6u || pl.level == 9u || pl.level == 12u || pl.level == 15u ||
                            pl.level == 18u || pl.level == 21u || pl.level == 24u || pl.level == 27u))
      pl.pts++;
    if (pl.level == 6u) pl.attrs |= 0x80u; /* the second passive slot */
  }
  if (pl.level >= 30u) pl.xp = 0;
  return gained;
}
uint8_t player_spend(uint8_t w) BANKED {
  uint8_t v = player_attr(w);
  if (!pl.pts) return 0;
  if (w == PA_GRIT) {
    if (v >= 4u) return 0;
    pl.attrs = (uint8_t)((pl.attrs & ~7u) | (v + 1u));
  } else if (w == PA_FOCUS) {
    if (v >= 2u) return 0;
    pl.attrs = (uint8_t)((pl.attrs & ~0x18u) | ((v + 1u) << 3));
  } else {
    if (v >= 3u) return 0;
    pl.attrs = (uint8_t)((pl.attrs & ~0x60u) | ((v + 1u) << 5));
  }
  pl.pts--;
  return 1;
}
/* ---- what you own, in shelf order (the core's owned bitmap scan; the filter in use is set aside) ---- */
uint16_t player_owned_next(uint16_t id) BANKED {
  uint8_t f = core.filter;
  uint16_t r;
  core.filter = 0;
  r = cru_shelf_step(&core, id < core.items ? id : 0u, 1);
  core.filter = f;
  return r;
}
/* ---- the story's course ---- */
/* a whole chapter in one alignment cell opens a style, silently: lawful good the radiant, chaotic evil the flame, true
 * neutral the cloud (8.4) */
void player_chapter(uint8_t chapter, uint8_t cell) BANKED {
  if (chapter != pl.chapter) {
    if ((pl.cell & 15u) == 0u && (pl.cell & 0x80u))
      player_unlock(8);
    else if ((pl.cell & 15u) == 8u && (pl.cell & 0x80u))
      player_unlock(7);
    else if ((pl.cell & 15u) == 4u && (pl.cell & 0x80u))
      player_unlock(4);
    {
      uint8_t m = (uint8_t)((pl.genome[4] >> 3) & 7u),
              want = (uint8_t)(1u + ((cell * 6u + 4u) >> 3)); /* the mark steps once toward the cell (8.4) */
      if (m < want)
        m++;
      else if (m > want)
        m--;
      pl.genome[4] = (uint8_t)((pl.genome[4] & 0xc7u) | (uint8_t)(m << 3));
      avatar_cache(pl.genome);
    }
    pl.chapter = chapter;
    pl.remake = 0;
    pl.kept_ch = 0;
    pl.cell = (uint8_t)(0x80u | cell);
    return;
  }
  if ((pl.cell & 15u) != cell) pl.cell = 0x0fu; /* it moved: no streak this chapter */
}
