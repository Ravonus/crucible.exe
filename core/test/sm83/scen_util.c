/* Scenario plumbing shared by every scenario file: checks, CRC-32 hashes of the core state and of the store image,
 * session helpers, and a v3 save as v3 cartridges wrote it. */
#ifdef __SDCC
#pragma bank 255
#endif
#include "scen.h"

static uint8_t scenario;
static uint32_t hs; /* running hash */
static const uint32_t crc_nib[16] = {0x00000000ul, 0x1db71064ul, 0x3b6e20c8ul, 0x26d930acul, 0x76dc4190ul, 0x6b6b51f4ul,
                                     0x4db26158ul, 0x5005713cul, 0xedb88320ul, 0xf00f9344ul, 0xd6d6a3e8ul, 0xcb61b38cul,
                                     0x9b64c2b0ul, 0x86d3d2d4ul, 0xa00ae278ul, 0xbdbdf21cul};
static void hb(uint8_t b) {
  uint8_t k;
  hs ^= b;
  k = (uint8_t)(hs & 15u);
  hs = (hs >> 4) ^ crc_nib[k];
  k = (uint8_t)(hs & 15u);
  hs = (hs >> 4) ^ crc_nib[k];
}
static void hw(uint16_t v) {
  hb((uint8_t)v);
  hb((uint8_t)(v >> 8));
}
static void put16(uint8_t at, uint16_t v) {
  scen_res[at] = (uint8_t)v;
  scen_res[(uint8_t)(at + 1u)] = (uint8_t)(v >> 8);
}
static uint16_t get16(uint8_t at) {
  uint8_t lo = scen_res[at], hi = scen_res[(uint8_t)(at + 1u)];
  return (uint16_t)(lo | ((uint16_t)hi << 8));
}
static void put32(uint8_t at, uint32_t v) {
  put16(at, (uint16_t)v);
  put16((uint8_t)(at + 2u), (uint16_t)(v >> 16));
}

void sc_begin(uint8_t s) SCEN_FN {
  scenario = s;
  scen_res[208] = s;
  scen_res[11] = (uint8_t)(s + 1u);
  plat_wipe();
}
void sc_pass(void) SCEN_FN { put16(4, (uint16_t)(get16(4) + 1u)); }
void sc_fail(uint16_t line) SCEN_FN {
  uint16_t f = get16(6);
  put16(4, (uint16_t)(get16(4) + 1u));
  if (!f) {
    put16(8, line);
    scen_res[10] = scenario;
  }
  put16(6, (uint16_t)(f + 1u));
  if (scen_res[12u + scenario] < 255u) scen_res[12u + scenario]++;
}
void sc_torn(uint8_t i, uint16_t before, uint16_t after) SCEN_FN {
  put16((uint8_t)(176u + (i << 2)), before);
  put16((uint8_t)(178u + (i << 2)), after);
}

/* the observable state: everything a player could see after a power-on */
uint32_t sc_state(uint8_t full) SCEN_FN {
  crucible_core *c = &scen_core;
  uint16_t a, b, lim;
  uint8_t i;
  hs = 0xffffffffu;
  hw(c->sequence);
  hw(c->points);
  hw(c->rng);
  hw(c->made);
  hw(c->session);
  hw(c->variant_seed);
  hw(c->minutes);
  hw(c->fails);
  hw(c->book_views);
  hw(c->complete);
  hw(c->best_gap);
  hw(c->best_first);
  hw(c->last_new);
  hb(c->title);
  hb(c->filter);
  hb(c->flags);
  hb(c->routes);
  hb(c->options);
  hb(c->seconds);
  hb(c->streak);
  hb(c->best_streak);
  hb(c->chain);
  hb(c->best_chain);
  hb(c->best_flurry);
  hb(c->nofail);
  hb(c->best_nofail);
  hb(c->best_swift);
  hb(c->links);
  for (i = 0; i < CRU_NAME; i++) hb((uint8_t)c->name[i]);
  for (i = 0; i < CRU_FEATS; i++) hb(c->feats[i]);
  for (i = 0; i < 64u; i++) hb(c->variants[i]);
  for (i = 0; i < CRU_BOARD_ROWS * CRU_BOARD_ROW; i++) hb(c->board[i]);
  for (i = 0; i < CRU_BOARD_ROWS * 5u; i++) hb(c->tails[i]);
  for (i = 0; i < CRU_FILTERS; i++) {
    hw(c->found[i]);
    hw(c->total[i]);
  }
  hw(c->gilded);
  for (i = 0; i < CRU_LOST_BYTES; i++) hb(c->lost[i]);
  for (i = 0; i <= CRU_PV_PAIRS; i++) hw(cru_play_value(c, i));
  for (a = 0; a < CW_ITEMS; a++) {
    hb(cru_owned(c, a));
    hw(cru_play_uses(c, a));
  }
  lim = 16u;
  if (full) lim = CW_ITEMS;
  for (a = 0; a < lim; a++)
    for (b = a; b < lim; b++) hb(cru_tried(c, a, b));
  hb(cru_tried(c, CW_SUN, CW_WATER));
  return hs;
}
static uint32_t image_hash(uint8_t layout) {
  uint32_t at;
  hs = 0xffffffffu;
  for (at = 0; at < 0x2000ul; at++) hb(plat_peek(at));
  for (at = 0x5e00ul; at < 0x5f08ul; at++) hb(plat_peek(at));
  if (layout)
    for (at = 0x8000ul; at < 0x18000ul; at++) hb(plat_peek(at));
  return hs;
}
void sc_end(uint8_t layout) SCEN_FN {
  uint8_t at = (uint8_t)(scenario << 2);
  put32((uint8_t)(32u + at), sc_state(1));
  put32((uint8_t)(104u + at), image_hash(layout));
}

/* ---- sessions ---- */
uint8_t sc_boot(uint8_t layout, uint8_t entropy) SCEN_FN {
  crucible_store s;
  plat_store(&s);
  cru_init(&scen_core, plat_tables(), &s, layout);
  return cru_load(&scen_core, entropy);
}
void sc_tick(uint16_t frames) SCEN_FN {
  while (frames >= 6u) {
    cru_tick(&scen_core, 6);
    frames = (uint16_t)(frames - 6u);
  }
  if (frames) cru_tick(&scen_core, (uint8_t)frames);
}
uint8_t sc_mix(uint16_t a, uint16_t b) SCEN_FN {
  uint8_t o = cru_mix_begin(&scen_core, a, b, 0x3c);
  sc_tick(60);
  cru_mix_finish(&scen_core);
  return o;
}
void sc_drain(void) SCEN_FN {
  while (cru_toast_peek(&scen_core) != CRU_TOAST_NONE) cru_toast_done(&scen_core);
}
uint8_t sc_has_toast(uint8_t code) SCEN_FN {
  crucible_core *c = &scen_core;
  uint8_t i, at, v;
  for (i = 0; i < c->toast_n; i++) {
    at = (uint8_t)((c->toast_first + i) & 7u);
    v = c->toast[at];
    if (v == code) return 1;
  }
  return 0;
}
uint16_t sc_feat_points(void) SCEN_FN {
  static const uint8_t pts[4] = {5, 10, 20, 40};
  uint16_t p = 0;
  uint8_t f, k, t;
  for (f = 0; f < CRU_FEATS; f++) {
    t = cru_feat_tier(&scen_core, f);
    for (k = 0; k < t; k++) p = (uint16_t)(p + pts[k]);
  }
  return p;
}
/* ---- a v3 save as v3 cartridges wrote it (nibble-table CRC-16/CCITT) ---- */
static uint16_t crc16_range(uint16_t at, uint16_t n) {
  static const uint16_t nib[16] = {0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
                                   0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef};
  uint16_t crc = 0xffffu;
  uint8_t b, k;
  while (n--) {
    b = plat_peek(at++);
    k = (uint8_t)(((crc >> 12) ^ (b >> 4)) & 15u);
    crc = (uint16_t)((crc << 4) ^ nib[k]);
    k = (uint8_t)(((crc >> 12) ^ b) & 15u);
    crc = (uint16_t)((crc << 4) ^ nib[k]);
  }
  return crc;
}
static void p8(uint16_t at, uint8_t v) { plat_poke(at, v); }
static void p16(uint16_t at, uint16_t v) {
  plat_poke(at, (uint8_t)v);
  plat_poke((uint16_t)(at + 1u), (uint8_t)(v >> 8));
}
static const uint8_t mask8[8] = {1, 2, 4, 8, 16, 32, 64, 128};
static void set_bit(uint16_t base, uint16_t i) {
  uint16_t at = (uint16_t)(base + (i >> 3));
  uint8_t v = plat_peek(at);
  v |= mask8[i & 7u];
  plat_poke(at, v);
}
static const uint8_t v3_owned[10] = {CW_EARTH, CW_WATER, CW_FIRE, CW_AIR,   CW_STEAM,
                                     CW_MUD,   CW_LAVA,  CW_SUN,  CW_OCEAN, CW_STONE};
static const uint8_t v3_tried[11][2] = {{CW_FIRE, CW_WATER}, {CW_EARTH, CW_WATER}, {CW_EARTH, CW_FIRE},
                                        {CW_FIRE, CW_FIRE},  {CW_WATER, CW_WATER}, {CW_LAVA, CW_WATER},
                                        {CW_SUN, CW_WATER},  {CW_AIR, CW_AIR},     {CW_STEAM, CW_MUD},
                                        {CW_SUN, CW_LAVA},   {CW_FIRE, CW_WATER}};
void sc_make_v3(uint16_t at, uint16_t seq, uint16_t points) SCEN_FN {
  uint16_t i, a, b, t, q;
  for (i = 0; i < 512u; i++) plat_poke((uint16_t)(at + i), 0);
  p8(at, 0xc1);
  p8((uint16_t)(at + 1u), 3);
  p16((uint16_t)(at + 2u), seq);
  p16((uint16_t)(at + 4u), points);
  p8((uint16_t)(at + 6u), 15);
  p8((uint16_t)(at + 7u), 9);
  p16((uint16_t)(at + 8u), 0x1234);
  p16((uint16_t)(at + 10u), 10);
  p8((uint16_t)(at + 12u), 1);
  p8((uint16_t)(at + 13u), 1);
  p8((uint16_t)(at + 14u), CW_ITEMS);
  for (i = 0; i < 10u; i++) set_bit((uint16_t)(at + 16u), v3_owned[i]);
  for (i = 0; i < 64u; i++) {
    q = 0;
    for (t = 0; t < 37u; t++) q = (uint16_t)(q + i);
    p8((uint16_t)(at + 48u + i), (uint8_t)q);
  } /* i*37 */
  for (i = 0; i < 11u; i++) {
    a = v3_tried[i][0];
    b = v3_tried[i][1];
    if (a > b) {
      t = a;
      a = b;
      b = t;
    }
    q = 0;
    for (t = 1; t <= b; t++) q = (uint16_t)(q + t); /* tri(b) */
    set_bit((uint16_t)(at + 112u), (uint16_t)(q + a));
  }
  p8((uint16_t)(at + 376u), 2);
  p8((uint16_t)(at + 377u), 1);
  p8((uint16_t)(at + 380u), 1);
  p8((uint16_t)(at + 382u), 1);
  p16((uint16_t)(at + 400u), 75);
  p8((uint16_t)(at + 402u), 30);
  p16((uint16_t)(at + 403u), 3);
  p16((uint16_t)(at + 405u), 12);
  p16((uint16_t)(at + 409u), 900);
  p8((uint16_t)(at + 413u), 2);
  p8((uint16_t)(at + 414u), 6);
  p8((uint16_t)(at + 415u), 1);
  p8((uint16_t)(at + 416u), 3);
  p8((uint16_t)(at + 417u), CW_SUN);
  p8((uint16_t)(at + 418u), 3);
  p8((uint16_t)(at + 419u), 1);
  p8((uint16_t)(at + 420u), 5);
  p8((uint16_t)(at + 421u), 7);
  p8((uint16_t)(at + 423u), 1);
  p8((uint16_t)(at + 424u), 'R');
  p8((uint16_t)(at + 425u), 'A');
  p8((uint16_t)(at + 426u), 'V');
  p16((uint16_t)(at + 427u), 7);
  p8((uint16_t)(at + 429u), 'R');
  p8((uint16_t)(at + 430u), 'A');
  p8((uint16_t)(at + 431u), 'V');
  p16((uint16_t)(at + 432u), 60);
  p8((uint16_t)(at + 434u), 6);
  p16((uint16_t)(at + 435u), 3);
  p8((uint16_t)(at + 437u), 15);
  p8((uint16_t)(at + 439u), 'B');
  p8((uint16_t)(at + 440u), 'O');
  p8((uint16_t)(at + 441u), 'B');
  p16((uint16_t)(at + 442u), 25);
  p8((uint16_t)(at + 444u), 2);
  p16((uint16_t)(at + 445u), 0xffffu);
  t = crc16_range(at, 510u);
  p16((uint16_t)(at + 510u), t);
}
void sc_crc_fix(uint16_t at) SCEN_FN {
  uint16_t t = crc16_range(at, 510u);
  p16((uint16_t)(at + 510u), t);
}
