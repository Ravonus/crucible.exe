/* Material overlay: each object's second three-colour layer, drawn as 8x8 sprites over its background cell.
 * The art toolchain stores it after the object's frame packets (flag bit 1): four tints of OBJ colours 1..3, then a
 * keel-retro-ctx clip (key, resting chain, turntable chain) whose frame masks pick the tiles that carry a sprite.
 * This bank has its own decoder and model copy, so it never shares row scratch with the art stream.
 * Decoded overlay frames live in WRAM bank 7, two per 256-byte page (mask + up to 7 tiles each: 32 frames); tiles
 * go to OBJ tiles 0..127 of VRAM bank 1, two
 * 16-tile regions per cell (shown and hidden), and the sprites flip with the cell's own VBlank swap.
 * Sprites: OAM 0..23 (focus 0..7, slots A/B/result 8..23); OBJ palettes 1,2,3,5. The overlay only claims OAM on
 * the bench and the book, and on the reveal for its one object (cell B, OAM 13..17: the burst has 0..11), and only hides
 * entries that still hold exactly what it wrote. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "crucible_data.h"
#include "crucible_codec.h"
#include "crucible_state.h"
#include "crucible_lost.h"
#include "crucible_overlay.h"
#include "crucible_ui.h"
#include "crucible_ctx_model.h"
#define KRC_MODEL crucible_ctx_model
#define KRC_FN static /* a private copy: the art bank keeps the global entry points */
#include "keel_retro_ctx.h"
#ifndef SRAM_PTR
#define SRAM_PTR(a) (a)
#endif
#define BASE ((uint16_t *)SRAM_PTR(0xbd00u))
#define SLOTS 32u
#define KEEP 7u /* overlay tiles kept per cached frame (a cell shows at most 8 sprites) */
#define OC 4u
#define ROWS_PER_TICK 8u
#define NO_ID 0xffffu
extern uint8_t room_dirty;
/* crucible.c's pal_changed (crucible_room.h) */
extern uint8_t crucible_art_urgent; /* crucible_art.c: a visible loop is still decoding */
/* cells 0 A, 1 B, 2 result, 4 focus (crucible.c CA,CB,CR,CF) -> overlay cells 0..3 */
static const uint8_t oam_first[OC] = {8, 13, 18, 0}, oam_count[OC] = {5, 5, 6, 8}, obj_pal[OC] = {2, 3, 5, 1};
typedef struct {
  uint16_t id;
  uint8_t frame, x, y, buf, shown, ready, pending, dirty;
  uint16_t pal_id;
  uint8_t pal_v;
} ocell;
static ocell oc[OC];
static uint16_t slot_id[SLOTS], slot_mask[SLOTS];
static uint8_t slot_frame[SLOTS], slot_age[SLOTS], age;
static uint8_t written[24][4]; /* y,x,tile,prop as last written to each owned OAM entry */
static uint8_t on, inited;
uint8_t crucible_overlay_cloud = 0;
/* the decode stream */
static krc_stream S;
static uint16_t s_id = NO_ID, s_bank;
static uint8_t s_chain, s_at, s_count, s_busy, s_target;
static const uint8_t *s_ov;
static uint8_t hdr[9];
#define H16(o) ((uint16_t)hdr[o] | ((uint16_t)hdr[(o) + 1] << 8))
static uint8_t krc_fill(krc_stream *s) {
  uint8_t n = s->left < KRC_IN ? (uint8_t)s->left : KRC_IN;
  crucible_rom_copy(s->bank, s->src, s->in, n);
  s->src += n;
  s->left -= n;
  return n;
}
static void init(void) {
  uint8_t i;
  if (inited) return;
  for (i = 0; i < SLOTS; i++) slot_id[i] = NO_ID;
  for (i = 0; i < OC; i++) {
    oc[i].id = NO_ID;
    oc[i].pal_id = NO_ID;
  }
  inited = 1;
}
/* Overlay section of id: bank, *ov at the 24 palette bytes, hdr = its ctx header. 0 when the record has none. */
static uint8_t locate(uint16_t id, uint16_t *bank, const uint8_t **ov) {
  const uint8_t *at;
  uint8_t h[9];
  *bank = crucible_art_locate(id, &at);
  crucible_rom_copy(*bank, at + 5u, h, 9);
  if (!(h[0] & 2u)) return 0;
  *ov = at + 14u + ((uint16_t)h[3] | ((uint16_t)h[4] << 8)) + ((uint16_t)h[5] | ((uint16_t)h[6] << 8)) +
        ((uint16_t)h[7] | ((uint16_t)h[8] << 8));
  crucible_rom_copy(*bank, *ov + 24u, hdr, 9);
  return 1;
}
static uint8_t find(uint16_t id, uint8_t frame) {
  uint8_t i;
  for (i = 0; i < SLOTS; i++)
    if (slot_id[i] == id && slot_frame[i] == frame) {
      slot_age[i] = age;
      return i;
    }
  return 255u;
}
static void page_io(uint8_t s, uint8_t put) {
  wram_bank = 7u;
  wram_at = (uint8_t *)(0xD000u + ((uint16_t)(s >> 1) << 8));
  wram_buf = crucible_decoded;
  wram_dir = put;
  crucible_wram_copy();
}
static uint8_t packed[128]; /* one half page: the kept tiles of a frame, in mask order */
/* the frame in crucible_decoded (a full 2bpp plane) into slot s's half page; returns the kept mask */
static uint16_t slot_put(uint8_t s, uint16_t mask) {
  uint8_t t, n = 0;
  uint16_t kept = 0;
  for (t = 0; t < 16u && n < KEEP; t++)
    if (mask & ((uint16_t)1u << t)) {
      memcpy(packed + (uint16_t)n * 16u, crucible_decoded + (uint16_t)t * 16u, 16);
      kept |= (uint16_t)1u << t;
      n++;
    }
  page_io(s, 0);
  memcpy(crucible_decoded + (uint16_t)(s & 1u) * 128u, packed, 128);
  page_io(s, 1);
  return kept;
}
/* slot s's kept tiles into packed (tile k of the mask at packed+k*16) */
static void slot_get(uint8_t s) {
  page_io(s, 0);
  memcpy(packed, crucible_decoded + (uint16_t)(s & 1u) * 128u, 128);
}
static uint8_t wanted(uint16_t id, uint8_t frame) {
  uint8_t c;
  for (c = 0; c < OC; c++)
    if (oc[c].id == id && oc[c].frame == frame) return 1;
  return 0;
}
/* Store crucible_decoded as (id, frame): the oldest slot not on screen gives way. */
static void store(uint16_t id, uint8_t frame, uint16_t mask) {
  uint8_t i, s = 0, best = 0, a;
  if (find(id, frame) != 255u) return;
  for (i = 0; i < SLOTS; i++) {
    if (slot_id[i] == NO_ID) {
      s = i;
      break;
    }
    if (wanted(slot_id[i], slot_frame[i])) continue;
    a = (uint8_t)(age - slot_age[i]);
    if (a >= best) {
      best = a;
      s = i;
    }
  }
  slot_id[s] = id;
  slot_frame[s] = frame;
  slot_age[s] = age;
  slot_mask[s] = slot_put(s, mask);
}
/* Begin decoding (id, frame): a keyframe completes at once; a chain frame opens (or continues) the stream. */
static void begin(uint16_t id, uint8_t frame) {
  const uint8_t *ov;
  uint16_t bank, kl, rl;
  uint8_t chain = (frame & 128u) ? 1u : 0u, index = frame & 15u;
  if (!locate(id, &bank, &ov)) return;
  kl = H16(3);
  rl = H16(5);
  if (find(id, 0) == 255u) {
    crucible_rom_key(bank, ov + 33u, kl, crucible_decoded);
    store(id, 0, H16(1));
  }
  if (!index) return;
  if (!chain && (hdr[0] & 1u)) return; /* rigid: every resting pose is the keyframe */
  /* the stream continues while it is on this chain and short of the frame; otherwise it reopens at the key */
  if (!(s_id == id && s_chain == chain && s_at < index)) {
    crucible_rom_key(bank, ov + 33u, kl, S.tiles);
    if (chain)
      krc_open(&S, ov + 33u + kl + rl, H16(7), bank, S.tiles, H16(1));
    else
      krc_open(&S, ov + 33u + kl, rl, bank, S.tiles, H16(1));
    s_id = id;
    s_chain = chain;
    s_at = 0;
    s_count = chain ? (uint8_t)(CRUCIBLE_VIEWS - 1u) : (uint8_t)(CRUCIBLE_POSES - 1u);
  }
  /* one job decodes the rest of the loop: every frame on the way is cached, so a turntable never waits twice */
  s_target = s_count;
  s_busy = 1;
}
static void step(void) {
  if (S.y >= 32u) krc_frame(&S);
  if (!krc_rows(&S, ROWS_PER_TICK)) return;
  s_at++;
  memcpy(crucible_decoded, S.tiles, 256);
  store(s_id, s_chain ? (uint8_t)(128u | s_at) : s_at, S.mask);
  if (s_at >= s_target || s_at >= s_count) s_busy = 0;
}
/* The cached frame a cell's shown frame maps to: index 0 is the keyframe (the turn flag aside); rigid objects show
 * their keyframe overlay on every resting pose. */
static uint8_t frame_of(uint16_t id, uint8_t frame) {
  const uint8_t *ov;
  uint16_t bank;
  if (!(frame & 15u)) return 0; /* view or pose 0 of either chain is the keyframe */
  if (!(frame & 128u) && locate(id, &bank, &ov) && (hdr[0] & 1u)) return 0;
  return frame;
}
static void hide(uint8_t c) {
  uint8_t i, o;
  for (i = 0; i < oam_count[c]; i++) {
    o = oam_first[c] + i;
    if (shadow_OAM[o].y == written[o][0] && shadow_OAM[o].x == written[o][1] && shadow_OAM[o].tile == written[o][2] &&
        shadow_OAM[o].prop == written[o][3]) {
      shadow_OAM[o].y = 0;
      written[o][0] = 0;
    }
  }
  oc[c].shown = 0;
}
static void put(uint8_t o, uint8_t y, uint8_t x, uint8_t tile, uint8_t prop) {
  shadow_OAM[o].y = y;
  shadow_OAM[o].x = x;
  shadow_OAM[o].tile = tile;
  shadow_OAM[o].prop = prop;
  written[o][0] = y;
  written[o][1] = x;
  written[o][2] = tile;
  written[o][3] = prop;
}
/* The object's OBJ palette for its tint, into the faded base table (colour 0 is transparent and unused). */
static void palette(uint8_t c) {
  const uint8_t *ov;
  uint16_t bank, w[4];
  uint8_t v = VARIANT(oc[c].id) & 3u;
  if (oc[c].pal_id == oc[c].id && oc[c].pal_v == v) return;
  if (!locate(oc[c].id, &bank, &ov)) return;
  w[0] = 0;
  crucible_rom_copy(bank, ov + (uint16_t)v * 6u, (uint8_t *)(w + 1), 6);
  ENABLE_RAM;
  SWITCH_RAM(2);
  memcpy(BASE + 32u + (uint16_t)obj_pal[c] * 4u, w, 8);
  DISABLE_RAM;
  room_dirty = 1;
  oc[c].pal_id = oc[c].id;
  oc[c].pal_v = v;
}
/* Upload the cached frame's tiles into the cell's hidden region; sprites follow at the next sync. */
static void prepare(uint8_t c) {
  uint8_t s = find(oc[c].id, frame_of(oc[c].id, oc[c].frame)), t, n = 0, base;
  uint16_t mask;
  if (s == 255u) return;
  slot_get(s);
  mask = slot_mask[s];
  base = (uint8_t)(c * 32u + (oc[c].buf ^ 1u) * 16u);
  VBK_REG = 1;
  for (t = 0; t < 16u && n < oam_count[c]; t++)
    if (mask & ((uint16_t)1u << t)) {
      set_sprite_data((uint8_t)(base + t), 1, packed + (uint16_t)n * 16u);
      n++;
    }
  VBK_REG = 0;
  oc[c].pending = 1;
  oc[c].ready = (uint8_t)(mask & 0xffu);
  oc[c].dirty = (uint8_t)(mask >> 8);
  palette(c);
}
void crucible_overlay_cell(uint8_t cell, uint16_t id, uint8_t frame, uint8_t x, uint8_t y) BANKED {
  uint8_t c = cell < 3u ? cell : cell == 4u ? 3u : 255u;
  init();
  if (c == 255u) return;
  oc[c].x = x;
  oc[c].y = y;
  if (oc[c].id != id || oc[c].frame != frame) {
    oc[c].id = id;
    oc[c].frame = frame;
    oc[c].pending = 0;
    oc[c].shown = 2; /* 2: hide at the swap */
    /* a cached overlay is staged now, so it appears at the same VBlank as the base frame it belongs to */
    if (on && id < CRUCIBLE_ITEMS && find(id, frame_of(id, frame)) != 255u) prepare(c);
  }
}
void crucible_overlay_sync(void) BANKED {
  uint8_t c, i, n, t, o, base;
  uint16_t mask;
  if (!on) return;
  for (c = 0; c < OC; c++) {
    if (oc[c].shown == 2u && !oc[c].pending) {
      hide(c);
      continue;
    }
    if (!oc[c].pending) continue;
    oc[c].pending = 0;
    hide(c);
    oc[c].buf ^= 1u;
    base = (uint8_t)(c * 32u + oc[c].buf * 16u);
    mask = (uint16_t)oc[c].ready | ((uint16_t)oc[c].dirty << 8);
    n = 0;
    o = oam_first[c];
    for (t = 0; t < 16u && n < oam_count[c]; t++)
      if (mask & ((uint16_t)1u << t)) {
        put(o + n, (uint8_t)((oc[c].y + (t >> 2)) * 8u + 16u), (uint8_t)((oc[c].x + (t & 3u)) * 8u + 8u),
            (uint8_t)(base + t), (uint8_t)(obj_pal[c] | 8u));
        n++;
      }
    for (i = n; i < oam_count[c]; i++) {
      o = oam_first[c] + i;
      if (written[o][0] == shadow_OAM[o].y && shadow_OAM[o].y) {
        shadow_OAM[o].y = 0;
        written[o][0] = 0;
      }
    }
    oc[c].shown = 1;
  }
}
/* screen: crucible.c's screen enum; overlays run on the bench (0) and the book (3) */
#define OAM_LOAD 37u
/* While the focus's loop is still loading (load 0..254) a spark twinkles at its window's top-right corner, so the
 * name below stays readable; it brightens as the loop fills in. */
static void loading(uint8_t on_bench, uint8_t load) {
  if (!on_bench || load == 255u) {
    move_sprite(OAM_LOAD, 0, 0);
    return;
  }
  set_sprite_tile(OAM_LOAD, (uint8_t)(load < 128u ? SPR_SPARK0 + ((age >> 3) & 1u) : SPR_SPARK1 + ((age >> 2) & 1u)));
  set_sprite_prop(OAM_LOAD, 4);
  move_sprite(OAM_LOAD, (uint8_t)((oc[3].x + 4u) * 8u + 4u), (uint8_t)(oc[3].y * 8u + 16u - 4u));
}
void crucible_overlay_tick(uint8_t screen, uint8_t load) BANKED {
  uint8_t c, allowed = screen == 0u || screen == 3u || screen == 2u, one = screen == 2u;
  if (screen == 1u) crucible_lost_merge(); /* the merge is placed: a lost piece's tears on top */
  init();
  age++;
  loading(screen == 0u && oc[3].id != NO_ID, load);
  if (!allowed) {
    if (on) {
      for (c = 0; c < OC; c++) hide(c);
      on = 0;
      crucible_overlay_cloud = 0;
    }
    return;
  }
  if (!on) {
    on = 1;
    crucible_overlay_cloud = 1;
    for (c = 0; c < OC; c++) {
      oc[c].pal_id = NO_ID;
      oc[c].shown = 0;
    }
  }
  /* every visible cell whose frame is cached and not yet on screen gets its tiles; one upload per tick */
  for (c = 0; c < OC; c++)
    if ((!one || c == 1u) && oc[c].id != NO_ID && oc[c].id < CRUCIBLE_ITEMS && oc[c].shown != 1u && !oc[c].pending &&
        find(oc[c].id, frame_of(oc[c].id, oc[c].frame)) != 255u) {
      prepare(c);
      return;
    }
  if (one) {
    if (s_busy) step();
    return;
  } /* the reveal: its object only (crucible_overlay_ahead decoded it during the merge) */
  /* the base art comes first: while a visible object's loop is still loading, overlays only show what is cached */
  if (crucible_art_urgent) {
    /* the base shows keyframes while its loops load: decode only overlay keyframes (one cheap step each); the
   * loops follow once the base's are in (decoding both at once halves the base's rate) */
    for (c = OC; c--;)
      if (oc[c].id != NO_ID && oc[c].id < CRUCIBLE_ITEMS && find(oc[c].id, 0) == 255u) {
        begin(oc[c].id, 0);
        return;
      }
    return;
  }
  if (s_busy) {
    step();
    return;
  }
  /* otherwise decode the next missing visible frame, the focus first */
  for (c = OC; c--;) {
    uint8_t f;
    if (oc[c].id == NO_ID || oc[c].id >= CRUCIBLE_ITEMS) continue;
    f = frame_of(oc[c].id, oc[c].frame);
    if (find(oc[c].id, f) == 255u) {
      begin(oc[c].id, f);
      return;
    }
  }
}
/* The merge's result: its turntable overlay decoded before the reveal (the merge itself shows no overlay). */
uint8_t crucible_overlay_ahead(uint16_t id) BANKED {
  const uint8_t *ov;
  uint16_t bank;
  uint8_t k, miss = 0;
  init();
  if (id >= CRUCIBLE_ITEMS || !locate(id, &bank, &ov)) return 255u;
  for (k = CRUCIBLE_VIEWS - 1u; k; k--)
    if (find(id, (uint8_t)(128u | k)) == 255u) {
      miss = k;
      break;
    }
  if (!miss && find(id, 0) != 255u) return 255u;
  if (s_busy && s_id == id && s_chain)
    step();
  else
    begin(id, (uint8_t)(128u | miss));
  return 0;
}
