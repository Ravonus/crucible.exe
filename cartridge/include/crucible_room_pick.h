#ifndef CRUCIBLE_ROOM_PICK_H
#define CRUCIBLE_ROOM_PICK_H
/* The living rooms' composer: pure and deterministic, so the cartridge and the host tests share it.
 *
 * A room is picked from (save seed, area, lap, chapter, time bucket, leaning, lucidity): the same save meets the same
 * room in the same place and time, another save meets other rooms. Depth (chapter + time + laps) pulls the rooms
 * away from the pastels toward the strange families; low lucidity and depth make glitch rooms likelier. A secret
 * (portal, backwards) overrides parts of the pick. Include crucible_room_data.h with ROOM_TABLES defined first. */
#include <stdint.h>
typedef struct room_ctx {
  uint16_t seed; /* the save's identity: core.variant_seed, a story run's seed folded in */
  uint8_t area; /* where: the focus's category (0..6) */
  uint8_t lap; /* laps through the shelf's far edge (the tunnel), 0..3 */
  uint8_t chapter; /* progress: the story's chapter (+2 per cycle), or finds/16 in free play; 0..15 */
  uint8_t when; /* time played, in 20-minute buckets; 0..15 */
  uint8_t lean; /* 0, or 1 + the "where" truth leading the story's matrix */
  uint8_t lucid; /* the story's lucidity (255 clear .. 0 deep dream); 255 in free play */
  uint8_t secret; /* ROOM_SECRET_* or 0 */
  uint8_t stage; /* how far the rooms have opened up (room_stage): 1 pastel, 2 soft, 3 everything; 0 counts as 3 */
} room_ctx;
typedef struct room_t {
  uint8_t family, sky, floor, drift, critter;
  uint8_t glitch; /* 0 none, 1 flickers and a few corrupt tiles, 2 a glitch room */
  uint8_t anomaly; /* ROOM_ANOMALY_* or 0: something odd that may show up here, unannounced */
  uint8_t hash; /* low bits of the room's hash: placement and corruption patterns */
  uint8_t mirror; /* drawn backwards */
} room_t;
#define ROOM_SECRET_PORTAL 1u
#define ROOM_SECRET_BACKWARDS 2u
#define ROOM_ANOMALY_DOOR 1u /* a door at the top edge: press UP past the top of the shelf while it shows */
#define ROOM_ANOMALY_SKY 2u /* a glitching tile in the sky: press SELECT during the mix that follows */
#define ROOM_ANOMALY_EDGE 3u /* a glitching tile at a screen edge: hold B and push toward it */
/* Progress opens the rooms up. Until then the bench is the home room: the original baked pastel scene, untouched.
 * Free play: 64 discoveries unlock pastel rooms (other pastel families and motifs, no glitches or secrets), 80 the soft
 * families, critters and secrets, 112 everything. A story run: chapter 3, 4, 5 (each cycle past an ending counts 2). */
#define ROOM_FREE_UNLOCK 64u
#define ROOM_STORY_UNLOCK 3u
static uint8_t room_stage(uint8_t story, uint8_t chapter, uint16_t found) {
  if (story)
    return chapter < ROOM_STORY_UNLOCK         ? 0u
           : chapter == ROOM_STORY_UNLOCK      ? 1u
           : chapter == ROOM_STORY_UNLOCK + 1u ? 2u
                                               : 3u;
  return found < ROOM_FREE_UNLOCK ? 0u : found < ROOM_FREE_UNLOCK + 16u ? 1u : found < ROOM_FREE_UNLOCK + 48u ? 2u : 3u;
}
static uint16_t room_mix(uint16_t h, uint8_t v) {
  h ^= (uint16_t)(((uint16_t)v << 8) | v);
  if (!h) h = 0x2f6bu;
  h ^= h << 7;
  h ^= h >> 9;
  h ^= h << 8;
  return h;
}
/* the n-th (mod count) set bit of mask */
static uint8_t room_bit(uint8_t mask, uint8_t n) {
  uint8_t i, c = 0;
  for (i = 0; i < 8u; i++)
    if (mask & (1u << i)) c++;
  if (!c) return 0;
  n %= c;
  for (i = 0; i < 8u; i++)
    if (mask & (1u << i)) {
      if (!n) return i;
      n--;
    }
  return 0;
}
static void room_pick(const room_ctx *c, room_t *r) {
  uint16_t h = c->seed ^ 0x5a3cu;
  uint8_t depth, cut, roll, g, t2, t1;
  h = room_mix(h, c->area);
  h = room_mix(h, c->lap);
  h = room_mix(h, c->chapter);
  h = room_mix(h, c->when);
  h = room_mix(h, c->lean);
  h = room_mix(h, 0x2du);
  depth = (uint8_t)(c->chapter + c->when + c->lap);
  if (depth > 15u) depth = 15u;
  /* family: pastels are likeliest early and never vanish; the leaning truth claims half the rooms it touches */
  roll = (uint8_t)(h & 15u);
  cut = depth >= 8u ? 2u : (uint8_t)(10u - depth);
  if (c->stage == 1u)
    r->family = room_tier0[(uint8_t)(h >> 4) % ROOM_TIER_N0];
  else if (c->stage == 2u)
    r->family =
        roll < 11u ? room_tier0[(uint8_t)(h >> 4) % ROOM_TIER_N0] : room_tier1[(uint8_t)(h >> 4) % ROOM_TIER_N1];
  else if (c->lean && (h & 0x100u))
    r->family = room_lean_family[(uint8_t)(c->lean - 1u) & 7u];
  else if (roll < cut)
    r->family = room_tier0[(uint8_t)(h >> 4) % ROOM_TIER_N0];
  else if (roll < (uint8_t)(cut + 3u))
    r->family = room_tier1[(uint8_t)(h >> 4) % ROOM_TIER_N1];
  else
    r->family = room_tier2[(uint8_t)(h >> 4) % ROOM_TIER_N2];
  h = room_mix(h, r->family);
  r->sky = room_bit(room_fam_sky[r->family], (uint8_t)h);
  r->floor = room_bit(room_fam_floor[r->family], (uint8_t)(h >> 8));
  h = room_mix(h, 0x71u);
  r->drift = room_bit(room_fam_drift[r->family], (uint8_t)h);
  r->critter = room_bit(room_fam_critter[r->family], (uint8_t)(h >> 8));
  h = room_mix(h, 0x13u);
  /* glitch: rare, likelier deep in a run, late in play and when the dream is thick */
  g = (uint8_t)((h >> 3) & 31u);
  t2 = (uint8_t)(1u + (c->chapter >= 3u) + (c->when >= 6u) + (c->lucid < 96u ? 2u : 0u));
  t1 = (uint8_t)(t2 + 3u + (c->lucid < 160u ? 3u : 0u) + (depth >= 6u));
  r->glitch = g < t2 ? 2u : g < t1 ? 1u : 0u;
  if (c->stage == 1u)
    r->glitch = 0;
  else if (c->stage == 2u && r->glitch == 2u)
    r->glitch = 1u;
  if (r->glitch == 2u) r->drift = ROOM_DRIFT_STATIC;
  /* one room in eight hides something */
  r->anomaly = ((h >> 9) & 7u) ? 0u : (uint8_t)(1u + (uint8_t)(h >> 12) % 3u);
  if (c->stage == 1u) r->anomaly = 0;
  r->hash = (uint8_t)(h >> 4);
  r->mirror = 0;
  if (c->secret == ROOM_SECRET_PORTAL) {
    r->family = (h & 1u) ? ROOM_FAMILY_TRON : ROOM_FAMILY_VOID;
    r->sky = ROOM_SKY_OBSERVATORY;
    r->floor = ROOM_FLOOR_TRON;
    r->drift = ROOM_DRIFT_GLYPHS;
    r->critter = ROOM_CRITTER_GHOST;
    r->glitch = 1u;
    r->anomaly = 0;
  } else if (c->secret == ROOM_SECRET_BACKWARDS) {
    r->mirror = 1u;
    r->anomaly = 0;
  }
}
/* 1 when two picks need different tiles or maps (the palette alone can change in place) */
static uint8_t room_tiles_differ(const room_t *a, const room_t *b) {
  return a->sky != b->sky || a->floor != b->floor || a->glitch != b->glitch || a->mirror != b->mirror ||
         (a->glitch && a->hash != b->hash);
}
#endif
