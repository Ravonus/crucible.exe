/* Living rooms: the bench and reveal backdrops, composed per save, place and time (crucible_room_pick.h), and kept
 * alive while you play.
 *
 * - Places: the focus's category is the area; moving into another area wipes the room out and the next one in from
 *   the side you came (Pac-Man's tunnel: leave right, arrive from the left). Crossing the shelf's far edge is a lap:
 *   the world past it is another version of itself.
 * - Palette states (through crucible.c's pipeline): a find flashes the maze white and blue; a recipe flashes it
 *   briefly; a miss dims and flickers the room like a failing tube; a fight turns the floor hostile and it pulses; a
 *   new chapter, a turn in the story's leaning or a new hour of play brings a new family; the sky's colours drift
 *   slowly; glitch rooms (and, rarely, any room) flicker.
 * - Life: drifters (clouds, birds, paper, static, glyphs, comets) and a critter that wanders the floor, pauses,
 *   turns and walks out one edge to come back in the other.
 * - Secrets, never announced: a room may hide an anomaly that shows for a few seconds now and then.
 *     a door at the top edge: press UP past the top of the shelf while it shows -> the portal room;
 *     a glitching tile in the sky: press SELECT during the mix you start while it shows -> the backwards room;
 *     a glitching tile at a screen edge: hold B and push toward it -> a tiny visitor walks in, the room inverts, it
 *     whispers on the sign row and walks out through the tunnel.
 *   A secret room lasts until you change area (or about 40 seconds). Nothing here writes the save.
 * Home room: until play has gone far enough (room_stage in crucible_room_pick.h: 64 discoveries in free play, story
 * chapter 3) the bench and reveal are the original baked pastel scene and none of this runs: room_draw returns 0 and
 * the room touches no palette, tile, sprite or input. Past that the rooms ease in: pastel families first, then the soft
 * ones with critters and secrets, then everything.
 * Sprites: OAM 34 critter, 35 visitor, 36 anomaly (34..38 are free on the bench), OBJ palette 7, OBJ tiles 112..127 in
 * VRAM bank 0 (drifters reuse the cloud tiles 77..92, bank 0). None of them sit on the object cell scanlines (tile rows
 * 4..7 and 10..13), so the overlays there keep their 10-per-line budget. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "crucible_state.h"
#include "crucible_data.h"
#include "crucible_ui.h"
#include "crucible_storyrun.h"
#include "crucible_eggs.h"
#include "crucible_room.h"
#include "crucible_lost.h"
#define ROOM_TABLES
#include "crucible_room_data.h"
#include "crucible_room_pick.h"
#ifndef SRAM_PTR
#define SRAM_PTR(a) (a)
#endif
#define BASE ((uint16_t *)SRAM_PTR(ROOM_PAL_BASE))
#define OAM_CRIT 34u
#define OAM_VISIT 35u
#define OAM_ANOM 36u
#define T_BRASS 15u
#define EV_FIND 1u
#define EV_ROUTE 2u
#define EV_KNOWN 3u
#define EV_FAIL 4u
#define D_RIGHT 0u
#define D_LEFT 1u
#define D_DOWN 2u
#define D_UP 3u
uint16_t room_plaster = 32510u;
uint8_t room_dirty;
uint16_t room_salt;
static room_ctx ctx;
static room_t cur;
static uint8_t kind; /* 0 no room on screen (or the home room), 1 bench, 2 reveal */
static uint8_t unlocked; /* the last bench or reveal drawn was a living room */
static uint8_t applied; /* palettes: 0 default, 1 the room's, 2 hostile */
static uint8_t loaded, base_sky, base_floor, drift_up, crit_up;
static uint8_t inited, last_time, fc, prev_screen, area_seen, last_dir, lap;
static uint16_t made_seen, poll_t, rng = 0x1d2bu;
static uint8_t ev, ev_pending, ev_t, ev_state, amb, amb_phase, amb_t, flick_t;
static uint8_t job, job_step, job_dir, job_at;
static room_t job_room;
static uint8_t crit_on, crit_x, crit_dir, crit_wait, crit_wraps;
static uint8_t an_vis, an_t, push_t, up_armed, sel_window, sel_armed;
static uint16_t an_cool;
static uint8_t secret, secret_s, secret_f, whisper_due;
static uint8_t vis, vis_x, vis_t, vis_side, vis_wrapped;
static uint8_t rnd(void) {
  rng ^= rng << 7;
  rng ^= rng >> 9;
  rng ^= rng << 8;
  return (uint8_t)rng;
}
static uint8_t slot_tile(uint8_t i) {
  return i < 32u ? 224u + i : i < 64u ? 64u + i : i < 96u ? 160u + i : i < 128u ? 96u + i : 64u + i;
}
static uint8_t slot_bank(uint8_t i) { return (i >= 32u && i < 96u) || i >= 128u; }
/* ---- palettes: base colours in SRAM bank 2, then crucible.c's prepare/fade/upload ---- */
static void pal_put(uint8_t at, const uint16_t *p, uint8_t n) {
  ENABLE_RAM;
  SWITCH_RAM(2);
  memcpy(BASE + at, p, (uint16_t)n * 2u);
  DISABLE_RAM;
  room_dirty = 1;
}
static void fam(uint8_t f, uint8_t first, uint8_t n, uint8_t at) {
  uint16_t c[4];
  roomgfx_colours(f, first, n, c);
  pal_put(at, c, n);
}
static void solid(uint8_t at, uint16_t v, uint8_t n) {
  uint16_t c[4];
  c[0] = c[1] = c[2] = c[3] = v;
  pal_put(at, c, n);
}
/* the sky (BG 2): the backwards room wears the inverted sky; the ambient cycle replaces colours 1..3 */
static void sky_paint(void) {
  fam(cur.family, cur.mirror ? ROOM_C_GLITCH_SKY : ROOM_C_SKY, 4, 8u);
  if (amb && !cur.mirror) fam(cur.family, (uint8_t)(ROOM_C_CYCLE + (amb - 1u) * 3u), 3, 9u);
}
static void paint_family(uint8_t f) {
  fam(f, ROOM_C_SKY, 4, 8u);
  fam(f, ROOM_C_FLOOR, 4, 28u);
  fam(f, ROOM_C_MUTED, 4, 16u);
  fam(f, ROOM_C_DRIFT, 3, 61u);
  roomgfx_colours(f, ROOM_C_PLASTER, 1, &room_plaster);
  room_dirty = 1;
}
static void paint(void) {
  paint_family(cur.family);
  sky_paint();
  applied = 1;
}
/* A screen change can take several frames to draw; the base colours reach the hardware only at the end of the frame.
 * Sync the hardware now (in VBlank, without a fade, the same plaster rule as prepare_palettes) so a new map never
 * shows under the old room's colours. The next prepare_palettes upload is identical. */
static void hw_sync(void) {
  uint16_t bg[32], sp[4];
  uint8_t i;
  if (egg_dmg()) return;
  ENABLE_RAM;
  SWITCH_RAM(2);
  memcpy(bg, BASE, 64);
  memcpy(sp, BASE + 60, 8);
  DISABLE_RAM;
  for (i = 0; i < 32u; i += 4u)
    if (i != 8u && i != 16u && i != 28u) bg[i] = room_plaster;
  wait_vbl_done();
  set_bkg_palette(0, 8, bg);
  set_sprite_palette(7, 1, sp);
}
/* ---- maps ---- */
static void put_row(uint8_t x, uint8_t y, uint8_t n, const uint8_t *m, const uint8_t *a) {
  VBK_REG = 1;
  set_bkg_tiles(x, y, n, 1, a);
  VBK_REG = 0;
  set_bkg_tiles(x, y, n, 1, m);
}
static void resolve(uint8_t *m, uint8_t *a, uint8_t base, uint8_t n) {
  uint8_t i, s;
  for (i = 0; i < n; i++) {
    if (m[i]) {
      s = (uint8_t)(base + m[i] - 1u);
      if (slot_bank(s)) a[i] |= 8u;
      m[i] = slot_tile(s);
    } else
      m[i] = ROOM_BLANK;
  }
}
static uint8_t is_hole(uint8_t x, uint8_t y) {
  if (y >= 4u && y <= 7u) return (x >= 1u && x <= 4u) || (x >= 8u && x <= 11u) || (x >= 15u && x <= 18u);
  if (y >= 10u && y <= 13u) return (x >= 2u && x <= 5u) || (x >= 8u && x <= 11u) || (x >= 14u && x <= 17u);
  return 0;
}
/* one map row of the room on screen, as VRAM tiles and attributes */
static void row_of(uint8_t y, uint8_t *m, uint8_t *a) {
  uint8_t i, t;
  uint8_t cm[18], ca[18];
  if (y == 0u || y >= 15u) {
    roomgfx_hud_row(y ? (uint8_t)(y - 14u) : 0u, m, a);
    resolve(m, a, 0, 20);
    return;
  }
  if (y <= 7u) {
    roomgfx_sky_row(cur.sky, (uint8_t)(y - 1u), m, a);
    resolve(m, a, base_sky, 20);
  } else {
    roomgfx_floor_row(cur.floor, (uint8_t)(y - 8u), m, a);
    resolve(m, a, base_floor, 20);
    if (kind == 2u && y >= 9u) {
      roomgfx_card_row((uint8_t)(y - 9u), cm, ca);
      resolve(cm, ca, 0, 18);
      memcpy(m + 1, cm, 18);
      memcpy(a + 1, ca, 18);
    }
  }
  /* a glitch room: two rows slipped sideways */
  if (cur.glitch == 2u && (y == 2u || y == 9u)) {
    uint8_t k = (uint8_t)((cur.hash & 3u) + 1u), tm[20], ta[20];
    for (i = 0; i < 20u; i++) {
      t = (uint8_t)((i + k) % 20u);
      tm[i] = m[t];
      ta[i] = a[t];
    }
    memcpy(m, tm, 20);
    memcpy(a, ta, 20);
  }
  if (cur.mirror)
    for (i = 0; i < 10u; i++) {
      t = m[i];
      m[i] = m[19u - i];
      m[19u - i] = t;
      t = a[i];
      a[i] = a[19u - i];
      a[19u - i] = t;
    }
  if (cur.mirror)
    for (i = 0; i < 20u; i++)
      if (m[i] != ROOM_BLANK) a[i] ^= 0x20u;
}
/* write columns x0..x1 of rows y0..y1, skipping object cells; blank writes the flat tile instead */
static void block(uint8_t x0, uint8_t x1, uint8_t y0, uint8_t y1, uint8_t blank) {
  uint8_t m[20], a[20], y, x, s;
  for (y = y0; y <= y1; y++) {
    if (blank) {
      memset(m, ROOM_BLANK, 20);
      memset(a, y <= 7u ? 2u : 7u, 20);
    } else
      row_of(y, m, a);
    x = x0;
    while (x <= x1) {
      while (x <= x1 && is_hole(x, y)) x++;
      s = x;
      while (x <= x1 && !is_hole(x, y)) x++;
      if (x > s) put_row(s, y, (uint8_t)(x - s), m + s, a + s);
    }
  }
}
static void tile_put(uint8_t s, const uint8_t *d) {
  VBK_REG = slot_bank(s);
  set_bkg_data(slot_tile(s), 1, d);
  VBK_REG = 0;
}
/* glitch: a few tiles come up corrupt (rows rolled, a row flipped) */
static void corrupt(void) {
  uint8_t n = cur.glitch == 2u ? 6u : 2u, i, k, c, buf[16], tmp[4];
  for (i = 0; i < n; i++) {
    k = (uint8_t)(cur.hash + i * 37u);
    if (i & 1u) {
      c = roomgfx_floor_count(cur.floor);
      k %= c;
      roomgfx_floor_tile(cur.floor, k, buf);
    } else {
      c = roomgfx_sky_count(cur.sky);
      k %= c;
      roomgfx_sky_tile(cur.sky, k, buf);
    }
    memcpy(tmp, buf, 4);
    memmove(buf, buf + 4, 12);
    memcpy(buf + 12, tmp, 4);
    buf[6] ^= 0xffu;
    buf[9] ^= 0x5au;
    tile_put((uint8_t)((i & 1u ? base_floor : base_sky) + k), buf);
  }
}
static void bases(void) { base_floor = (uint8_t)(base_sky + roomgfx_sky_count(cur.sky)); }
static void load_all(void) {
  base_sky = roomgfx_common(0);
  bases();
  roomgfx_sky_upload(cur.sky, 0, roomgfx_sky_count(cur.sky), base_sky);
  roomgfx_floor_upload(cur.floor, 0, roomgfx_floor_count(cur.floor), base_floor);
  if (cur.glitch) corrupt();
  loaded = 1;
}
/* drift_up: 0 the original cloud tiles (from crucible_ui), else 1 + the drifter uploaded (drifter 0 is the same clouds) */
static void sprites_up(void) {
  if (drift_up != cur.drift + 1u) {
    roomgfx_drift(cur.drift);
    drift_up = (uint8_t)(cur.drift + 1u);
  }
  if (!crit_up) {
    roomgfx_critters();
    crit_up = 1;
  }
}
static void hide(void) {
  move_sprite(OAM_CRIT, 0, 0);
  move_sprite(OAM_VISIT, 0, 0);
  move_sprite(OAM_ANOM, 0, 0);
}
/* ---- context ---- */
static void read_ctx(void) {
  crucible_story *s;
  uint16_t w = core.minutes / 20u;
  ctx.seed = core.variant_seed ^ room_salt;
  ctx.lean = 0;
  ctx.lucid = 255u;
  ctx.stage = room_stage(story_on, 0, core.found[0]);
  if (story_on) {
    s = talk_saga();
    ctx.seed ^= (uint16_t)s->seed ^ (uint16_t)(s->seed >> 16);
    ctx.chapter = (uint8_t)(s->chapter + (s->cycle << 1));
    ctx.lucid = s->lucid;
    ctx.lean = (uint8_t)(1u + (cru_story_truth(s, 1) & 7u));
    ctx.stage = room_stage(1, ctx.chapter > 15u ? 15u : ctx.chapter, 0);
  } else
    ctx.chapter = (uint8_t)(core.found[0] >> 4);
  if (ctx.chapter > 15u) ctx.chapter = 15u;
  ctx.when = w > 15u ? 15u : (uint8_t)w;
  ctx.area = crucible_category(core.focus);
  ctx.lap = lap;
  ctx.secret = secret;
}
static void anomaly_reset(void) {
  an_vis = 0;
  an_t = 0;
  push_t = 0;
  an_cool = (uint16_t)(600u + ((uint16_t)cur.hash << 3));
}
static void whisper(void) {
  char s[20];
  uint8_t n = roomgfx_whisper((uint8_t)(cur.hash + lap + secret), s), m[18], a[18], i;
  memset(m, GLYPH(' '), 18);
  memset(a, T_BRASS, 18);
  for (i = 0; i < n; i++) m[(uint8_t)((18u - n) / 2u + i)] = GLYPH(s[i]);
  put_row(1, 17, 18, m, a);
}
/* ---- scene_draw's hooks ---- */
/* in three parts, so a screen can draw its rows a few a frame (the reveal: crucible_scene.c scene_begin/scene_rows) */
uint8_t room_begin(uint8_t k) BANKED {
  room_t old = cur;
  read_ctx();
  ev_pending = 0;
  unlocked = ctx.stage != 0u;
  if (!unlocked) {
    secret = 0;
    sel_armed = 0;
    sel_window = 0;
    lap = 0;
    return 0;
  } /* the home room: the baked scene draws */
  if (sel_armed && !k) {
    secret = ROOM_SECRET_BACKWARDS;
    secret_s = 0;
    whisper_due = 1;
  }
  if (!k) {
    sel_armed = 0;
    sel_window = 0;
  } /* the reveal keeps the window open: SELECT counts there too */
  read_ctx();
  room_pick(&ctx, &cur);
  job = 0;
  kind = (uint8_t)(k + 1u);
  if (!loaded || room_tiles_differ(&cur, &old)) load_all();
  sprites_up();
  return 1;
}
void room_row(uint8_t y) BANKED {
  uint8_t m[32], a[32];
  row_of(y, m, a);
  memset(m + 20, ROOM_BLANK, 12);
  memset(a + 20, 0, 12);
  put_row(0, y, 32, m, a);
}
void room_end(uint8_t k) BANKED {
  area_seen = ctx.area;
  amb = 0;
  amb_phase = 0;
  ev = 0;
  paint();
  if (!k) hw_sync();
  anomaly_reset();
  crit_on = 0;
  vis = 0;
  hide();
}
uint8_t room_draw(uint8_t k) BANKED {
  uint8_t y;
  if (!room_begin(k)) return 0;
  for (y = 0; y < 18u; y++) room_row(y);
  room_end(k);
  return 1;
}
/* the door anomaly is showing: UP past the top is its (crucible_flow.c keeps a waiting talker out of the way) */
uint8_t room_door(void) BANKED { return an_vis && cur.anomaly == ROOM_ANOMALY_DOOR; }
void room_leave(void) BANKED {
  if (applied) {
    paint_family(0);
    applied = 0;
    hw_sync();
  }
  if (drift_up > 1u) roomgfx_drift(0);
  drift_up = 0;
  kind = 0;
  job = 0;
  ev = 0;
  vis = 0;
  crit_on = 0;
  an_vis = 0;
  hide();
  loaded = 0; /* the other screen's tiles take the slots */
}
uint8_t room_twinkle(uint8_t phase) BANKED {
  uint8_t i, j, buf[16], lo;
  if (!kind) return 0;
  if (job || cur.glitch == 2u) return 1;
  i = roomgfx_twinkle(cur.sky, (uint8_t)(phase >> 1));
  if (i == 255u) return 1;
  roomgfx_sky_tile(cur.sky, i, buf);
  if (phase & 1u)
    for (j = 0; j < 16u; j += 2u) {
      lo = buf[j];
      buf[j] = buf[j + 1u];
      buf[j + 1u] = lo & buf[j + 1u];
    }
  tile_put((uint8_t)(base_sky + i), buf);
  return 1;
}
/* ---- recomposition: a new place (an area, a lap, the story turning, the hours) ---- */
static void recompose(uint8_t dir) {
  room_t nr;
  read_ctx();
  room_pick(&ctx, &nr);
  if (room_tiles_differ(&nr, &cur)) {
    job_room = nr;
    job = 1;
    job_step = 0;
    job_dir = dir;
    job_at = 0;
    flick_t = 4;
    return;
  }
  if (nr.family != cur.family || nr.drift != cur.drift || nr.critter != cur.critter || nr.anomaly != cur.anomaly) {
    cur = nr;
    sprites_up();
    amb = 0;
    paint();
    flick_t = 3;
    anomaly_reset();
  }
}
/* a wipe in four blocks: columns toward the travel direction, or rows */
static void job_block(uint8_t blank) {
  uint8_t s = job_dir == D_LEFT || job_dir == D_UP ? (uint8_t)(3u - job_step) : job_step;
  if (job_dir < D_DOWN)
    block((uint8_t)(s * 5u), (uint8_t)(s * 5u + 4u), 1, 14, blank);
  else
    block(0, 19, (uint8_t)(1u + s * 4u), s == 3u ? 14u : (uint8_t)(4u + s * 4u), blank);
}
static void job_tick(void) {
  uint8_t ns, nf, n;
  if (job == 1u) {
    job_block(1);
    if (++job_step >= 4u) {
      job = 2;
      job_at = 0;
      cur = job_room;
      bases();
      sprites_up();
      amb = 0;
      paint();
      anomaly_reset();
      crit_on = 0;
    }
    return;
  }
  if (job == 2u) {
    ns = roomgfx_sky_count(cur.sky);
    nf = roomgfx_floor_count(cur.floor);
    n = 16;
    if (job_at < ns) {
      if (job_at + n > ns) n = (uint8_t)(ns - job_at);
      roomgfx_sky_upload(cur.sky, job_at, n, base_sky);
    } else {
      if (job_at - ns + n > nf) n = (uint8_t)(nf - (job_at - ns));
      roomgfx_floor_upload(cur.floor, (uint8_t)(job_at - ns), n, base_floor);
    }
    job_at = (uint8_t)(job_at + n);
    if (job_at >= ns + nf) {
      if (cur.glitch) corrupt();
      job = 3;
      job_step = 0;
    }
    return;
  }
  job_block(0);
  if (++job_step >= 4u) {
    job = 0;
    if (secret) whisper_due = 1;
  }
}
/* ---- palette states ---- */
static const uint8_t fail_mask[5] = {0x0fu, 0x03u, 0x3fu, 0x0cu, 0x33u}; /* a tube that will not catch */
static void ev_run(uint8_t dt) {
  uint8_t len = ev == EV_FIND ? 64u : ev == EV_ROUTE ? 32u : ev == EV_KNOWN ? 8u : 40u, st;
  ev_t = (uint8_t)(ev_t + dt);
  if (ev_t >= len) {
    ev = 0;
    paint();
    return;
  }
  if (ev == EV_FAIL)
    st = (uint8_t)((fail_mask[ev_t >> 3] >> (ev_t & 7u)) & 1u);
  else
    st = (uint8_t)((ev_t >> 3) & 1u);
  if (st == ev_state) return;
  ev_state = st;
  if (ev == EV_FAIL) {
    if (st) {
      fam(cur.family, ROOM_C_DIM_SKY, 4, 8u);
      fam(cur.family, ROOM_C_DIM_FLOOR1, 1, 29u);
    } else
      paint();
    return;
  }
  /* the maze at level clear: the floor's lines and the sky's light go white, blue, white... */
  if (ev == EV_KNOWN) {
    solid(11u, ROOM_FLASH_WHITE, 1);
    return;
  }
  solid(10u, st ? ROOM_FLASH_BLUE : ROOM_FLASH_WHITE, 2);
  solid(29u, st ? ROOM_FLASH_BLUE : ROOM_FLASH_WHITE, 1);
}
static void ambient(uint8_t dt) {
  static const uint8_t steps[6] = {0, 1, 2, 3, 2, 1};
  uint8_t every = secret == ROOM_SECRET_PORTAL ? 8u : 120u;
  amb_t = (uint8_t)(amb_t + dt);
  if (amb_t < every) return;
  amb_t = 0;
  if (++amb_phase >= 6u) amb_phase = 0;
  amb = steps[amb_phase];
  sky_paint();
}
static void flicker(uint8_t dt) {
  uint8_t r;
  if (flick_t) {
    if (flick_t <= dt) {
      flick_t = 0;
      sky_paint();
    } else {
      flick_t = (uint8_t)(flick_t - dt);
      fam(cur.family, ROOM_C_GLITCH_SKY, 4, 8u);
    }
    return;
  }
  r = rnd();
  if (cur.glitch == 2u ? r < 4u : cur.glitch == 1u ? (r == 7u && !(rnd() & 3u)) : (r == 7u && !(rnd() & 63u)))
    flick_t = (uint8_t)(2u + (rnd() & 3u));
}
/* ---- life: a critter on the floor, walking out one edge and back in the other ---- */
static uint8_t crit_mv, vis_mv;
/* half a pixel a frame, whatever the frame rate of the main loop */
static uint8_t steps(uint8_t *mv, uint8_t dt) {
  uint8_t n;
  *mv = (uint8_t)(*mv + dt);
  n = (uint8_t)(*mv >> 1);
  *mv &= 1u;
  return n > 4u ? 4u : n;
}
static void crit_tick(uint8_t dt) {
  uint8_t t, n;
  if (!crit_on) {
    if (ctx.stage == 1u) return;
    if (rnd() < (core.session >= 3u ? 2u : 1u) && !(rnd() & 3u)) {
      crit_on = 1;
      crit_dir = rnd() & 1u;
      crit_x = crit_dir ? 0u : 175u;
      crit_wait = 0;
      crit_wraps = 0;
    } else
      return;
  }
  if (crit_wait) {
    crit_wait = crit_wait > dt ? (uint8_t)(crit_wait - dt) : 0u;
  } else
    for (n = steps(&crit_mv, dt); n; n--) {
      if (crit_dir)
        crit_x++;
      else
        crit_x--;
      if (crit_x >= 176u) {
        if (crit_wraps++) {
          crit_on = 0;
          move_sprite(OAM_CRIT, 0, 0);
          return;
        }
        crit_x = crit_dir ? 0u : 175u;
      } /* the tunnel */
      if (rnd() < 3u) {
        crit_wait = (uint8_t)(30u + (rnd() & 63u));
        if (rnd() & 1u) crit_dir ^= 1u;
        break;
      }
    }
  t = (uint8_t)(ROOM_SPR_CRITTER + cur.critter * 2u + (crit_wait ? 0u : ((fc >> 3) & 1u)));
  set_sprite_tile(OAM_CRIT, t);
  set_sprite_prop(OAM_CRIT, (uint8_t)(7u | (crit_dir ? 0x20u : 0u)));
  move_sprite(OAM_CRIT, crit_x, (uint8_t)(68u + 16u));
}
/* ---- secrets ---- */
static void an_place(void) {
  uint8_t x, y, t;
  if (cur.anomaly == ROOM_ANOMALY_DOOR) {
    x = 80u + 8u - 4u;
    y = 8u + 16u;
    t = (uint8_t)(ROOM_SPR_DOOR + ((fc >> 4) & 1u));
  } else if (cur.anomaly == ROOM_ANOMALY_SKY) {
    x = (uint8_t)((cur.hash & 1u ? 108u : 52u) + 8u - 4u + (push_t ? 0u : (rnd() & 1u)));
    y = 18u + 16u;
    t = (uint8_t)(ROOM_SPR_GLITCH + ((fc >> 2) & 1u));
  } else {
    x = (uint8_t)(cur.hash & 2u ? 160u : 8u);
    if (push_t) x = (uint8_t)(x + (rnd() & 3u) - 1u);
    y = 112u + 16u;
    t = (uint8_t)(ROOM_SPR_GLITCH + ((fc >> 2) & 1u));
  }
  if ((rnd() & 15u) == 0u) {
    move_sprite(OAM_ANOM, 0, 0);
    return;
  } /* it never quite holds still */
  set_sprite_tile(OAM_ANOM, t);
  set_sprite_prop(OAM_ANOM, 7u);
  move_sprite(OAM_ANOM, x, y);
}
static void an_tick(uint8_t dt, uint8_t *pressed) {
  uint8_t held, toward;
  if (!cur.anomaly || vis) return;
  if (!an_vis) {
    if (an_cool > dt) {
      an_cool -= dt;
      return;
    }
    an_vis = 1;
    an_t = 0;
  }
  if (cur.anomaly == ROOM_ANOMALY_EDGE) {
    held = joypad();
    toward = cur.hash & 2u ? J_RIGHT : J_LEFT;
    if ((held & J_B) && (held & toward)) {
      *pressed &= (uint8_t)~(J_LEFT | J_RIGHT | J_UP | J_DOWN);
      push_t = (uint8_t)(push_t + dt);
      if (push_t >= 60u) {
        an_vis = 0;
        push_t = 0;
        an_cool = 0xffffu;
        move_sprite(OAM_ANOM, 0, 0);
        vis = 1;
        vis_side = (cur.hash & 2u) ? 1u : 0u;
        vis_x = vis_side ? 175u : 0u;
        vis_t = 0;
        vis_wrapped = 0;
        return;
      }
    } else
      push_t = 0;
  }
  if (!push_t) {
    an_t = (uint8_t)(an_t + dt);
    if (an_t >= 240u) {
      an_vis = 0;
      an_cool = (uint16_t)(1800u + ((uint16_t)rnd() << 3));
      move_sprite(OAM_ANOM, 0, 0);
      return;
    }
  }
  an_place();
}
/* the visitor: walks in from the edge it was pushed through, stops, the room inverts and it whispers; then it walks
 * out the other edge, through the tunnel once, and is gone */
static void vis_tick(uint8_t dt) {
  uint8_t dir = vis_side ? 0u : 1u, n;
  if (vis == 2u) {
    vis_t = (uint8_t)(vis_t + dt);
    if (vis_t >= 150u) {
      vis = 3;
      paint();
    }
  } else
    for (n = steps(&vis_mv, dt); n; n--) {
      if (dir)
        vis_x++;
      else
        vis_x--;
      if (vis == 1u && vis_x == 84u) {
        vis = 2;
        vis_t = 0;
        fam(cur.family, ROOM_C_GLITCH_SKY, 4, 8u);
        solid(29u, ROOM_FLASH_WHITE, 1);
        whisper();
        break;
      }
      if (vis_x >= 176u) {
        if (vis_wrapped++) {
          vis = 0;
          move_sprite(OAM_VISIT, 0, 0);
          return;
        }
        vis_x = dir ? 0u : 175u;
      }
    }
  set_sprite_tile(OAM_VISIT,
                  (uint8_t)(ROOM_SPR_CRITTER + ROOM_CRITTER_CURSOR * 2u + (vis == 2u ? 0u : ((fc >> 3) & 1u))));
  set_sprite_prop(OAM_VISIT, (uint8_t)(7u | (dir ? 0x20u : 0u)));
  move_sprite(OAM_VISIT, vis_x, (uint8_t)(66u + 16u));
}
static void area_check(void) {
  uint8_t a = crucible_category(core.focus), wrap, dir = last_dir;
  if (a == area_seen) return;
  wrap = (dir == D_RIGHT || dir == D_DOWN) ? a < area_seen : a > area_seen;
  if (wrap && dir == D_RIGHT)
    lap = (uint8_t)((lap + 1u) & 3u);
  else if (wrap && dir == D_LEFT)
    lap = (uint8_t)((lap + 3u) & 3u);
  if (wrap && dir == D_UP && up_armed && cur.anomaly == ROOM_ANOMALY_DOOR) {
    secret = ROOM_SECRET_PORTAL;
    secret_s = 0;
  } else
    secret = 0;
  up_armed = 0;
  area_seen = a;
  crit_on = 0;
  vis = 0;
  hide();
  recompose(dir);
}
/* the living room's family (the fight's arena, docs/fight-system.md 3.1); 0xff in the home room */
uint8_t room_family(void) BANKED { return unlocked ? cur.family : 0xffu; }
uint8_t room_tick(uint8_t screen, uint8_t pressed) BANKED {
  uint8_t dt = (uint8_t)((uint8_t)sys_time - last_time);
  last_time = (uint8_t)sys_time;
  if (dt > 8u) dt = 8u;
  fc = (uint8_t)(fc + dt);
  crucible_lost_tick(screen); /* a lost piece's pair tears the bench's result cell */
  if (!inited) {
    inited = 1;
    made_seen = core.made;
    rng ^= (uint16_t)DIV_REG << 8;
  }
  if (core.made != made_seen) {
    uint8_t o = core.mix.outcome;
    made_seen = core.made;
    ev_pending = o == CRU_NEW ? EV_FIND : o == CRU_ROUTE ? EV_ROUTE : o == CRU_NOTHING ? EV_FAIL : EV_KNOWN;
  }
  if (!unlocked) {
    prev_screen = screen;
    return pressed;
  } /* the home room: nothing changes */
  if (screen == ROOM_S_FIGHT) {
    /* hostile: the floor and the plaster turn; the sky is leased to the answer and left alone; the floor pulses */
    if (applied != 2u) {
      fam(cur.family, ROOM_C_HOSTILE, 4, 28u);
      roomgfx_colours(cur.family, ROOM_C_HOSTILE_PLASTER, 1, &room_plaster);
      room_dirty = 1;
      applied = 2;
      ev_state = 0;
    }
    if (((fc >> 5) & 1u) != ev_state) {
      ev_state = (uint8_t)((fc >> 5) & 1u);
      if (ev_state)
        solid(29u, ROOM_FLASH_BLUE, 1);
      else
        fam(cur.family, ROOM_C_HOSTILE + 1u, 1, 29u);
    }
    prev_screen = screen;
    return pressed;
  }
  if (screen > ROOM_S_REVEAL) {
    if (applied || kind) room_leave();
    prev_screen = screen;
    return pressed;
  }
  if (!kind) {
    prev_screen = screen;
    return pressed;
  }
  if (screen == ROOM_S_BENCH) {
    /* back from a mix that made nothing (no reveal redrew the room): the SELECT still opens the way */
    if (sel_armed && prev_screen != ROOM_S_BENCH) {
      sel_armed = 0;
      sel_window = 0;
      secret = ROOM_SECRET_BACKWARDS;
      secret_s = 0;
      recompose(D_LEFT);
    }
    if (pressed & J_RIGHT)
      last_dir = D_RIGHT;
    else if (pressed & J_LEFT)
      last_dir = D_LEFT;
    else if (pressed & J_DOWN)
      last_dir = D_DOWN;
    else if (pressed & J_UP) {
      last_dir = D_UP;
      up_armed = an_vis && cur.anomaly == ROOM_ANOMALY_DOOR;
    }
  } else {
    if (prev_screen == ROOM_S_BENCH) {
      sel_window = an_vis && cur.anomaly == ROOM_ANOMALY_SKY;
      hide();
      crit_on = 0;
      an_vis = 0;
      if (vis) {
        vis = 0;
        paint();
      }
    }
    if (sel_window && (pressed & J_SELECT) && !sel_armed) {
      sel_armed = 1;
      flick_t = 2;
    }
  }
  if (job)
    job_tick();
  else if (screen == ROOM_S_BENCH) {
    if (!(fc & 1u)) area_check();
    if (!job) {
      poll_t += dt;
      if (poll_t >= 256u) {
        poll_t = 0;
        if (secret && ++secret_s >= 10u) {
          secret = 0;
          recompose(D_RIGHT);
        } else
          recompose(D_RIGHT);
      }
    }
  }
  if (ev_pending && screen == ROOM_S_BENCH && !job) {
    ev = ev_pending;
    ev_pending = 0;
    ev_t = 0;
    ev_state = 255u;
  }
  if (ev)
    ev_run(dt);
  else if (!vis || vis == 3u) {
    ambient(dt);
    flicker(dt);
  }
  if (screen == ROOM_S_BENCH && !job) {
    if (whisper_due) {
      whisper_due = 0;
      whisper();
    }
    crit_tick(dt);
    an_tick(dt, &pressed);
    if (vis) {
      vis_tick(dt);
      if (joypad() & J_B) pressed &= (uint8_t)~(J_LEFT | J_RIGHT | J_UP | J_DOWN);
    }
  }
  prev_screen = screen;
  return pressed;
}
