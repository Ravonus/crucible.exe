#pragma bank 255
/* Object art on the cartridge: frames rendered by the real KEEL engine at bake time (the catalogue sheets' pixels),
 * stored per object as a keel-retro-v1 keyframe plus two keel-retro-ctx P-frame chains (8 resting poses, 12-view
 * turntable). Decoded frames live in the CGB work-RAM cache: banks 2..6 hold 24 keyframes and 56 motion frames (every bench cell's loop
 * at once); bank 7 is the material overlay's (crucible_overlay.c).
 * A keyframe a visible cell needs is decoded at once (one I-frame); motion frames and the predicted neighbours are
 * queued and decoded a few rows per main-loop frame by one stream, cheapest-first by priority, so input never waits.
 * The decoder and its 8 KB context model share this ROM bank. */
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "crucible_data.h"
#include "crucible_codec.h"
#include "crucible_state.h"
#include "crucible_predict.h"
#include "crucible_ctx_model.h"
#define KRC_MODEL crucible_ctx_model
#include "keel_retro_ctx.h"
#define KEYS 24u
#define MOTION 56u
#define WINDOW 16u
#define QUEUE 12u
#define ROWS 16u /* changed-tile rows decoded per main-loop frame while something on screen waits */
#define ROWS_IDLE 4u /* ...and while everything on screen plays: background prefetch leaves the loop its full rate */
uint8_t crucible_art_urgent; /* set by the screen each frame: 1 while a visible object's loop is still loading */
#define REC 5u /* record prefix: u16 0, ticks, u16 palette parameters */
typedef struct {
  uint16_t id;
  uint8_t frame, priority;
} request;
static uint16_t key_ids[KEYS], motion_ids[MOTION], pins[WINDOW], key_mask[KEYS], motion_mask[MOTION];
static uint8_t ages[KEYS], motion_frames[MOTION], motion_age[MOTION], np, initialized, tick;
static request jobs[QUEUE], job;
static uint8_t nj, busy, last_filter = 255u, scroll_rate, scan;
static int8_t direction = 1;
static uint16_t selected = CRU_NONE, last_cursor_time, job_start;
uint16_t crucible_cache_min, crucible_cache_max, crucible_gen_jobs, crucible_cache_hits;
uint8_t crucible_gen_last_frames, crucible_gen_max_frames, crucible_art_shown;
/* the decode stream: object, chain (0 rest, 1 turn), frames decoded so far in that chain, frames in the chain */
static krc_stream S;
static uint16_t s_id = CRU_NONE;
static uint8_t s_chain, s_at, s_count, rec[9];
#define REC16(o) ((uint16_t)rec[o] | ((uint16_t)rec[(o) + 1] << 8))
static uint8_t krc_fill(krc_stream *s) {
  uint8_t n = s->left < KRC_IN ? (uint8_t)s->left : KRC_IN;
  crucible_rom_copy(s->bank, s->src, s->in, n);
  s->src += n;
  s->left -= n;
  return n;
}
static void init(void) {
  uint8_t i;
  if (initialized) return;
  for (i = 0; i < KEYS; i++) key_ids[i] = CRU_NONE;
  for (i = 0; i < MOTION; i++) motion_ids[i] = CRU_NONE;
  initialized = 1;
}
static void cache_io(uint8_t slot, uint8_t put) {
  wram_bank = 2u + (slot >> 4);
  wram_at = (uint8_t *)(0xD000u + ((uint16_t)(slot & 15u) << 8));
  wram_buf = crucible_decoded;
  wram_dir = put;
  crucible_wram_copy();
}
/* objects on screen: their motion frames are never evicted to make room for others */
static uint16_t vis[8];
static uint8_t nvis;
static uint8_t visible(uint16_t id) {
  uint8_t i;
  for (i = 0; i < nvis; i++)
    if (vis[i] == id) return 1;
  return 0;
}
static uint8_t pinned(uint16_t id) {
  uint8_t i;
  for (i = 0; i < np; i++)
    if (pins[i] == id) return 1;
  return 0;
}
static uint8_t find(uint16_t id, uint8_t frame) {
  uint8_t i;
  init();
  if (!frame) {
    for (i = 0; i < KEYS; i++)
      if (key_ids[i] == id) {
        ages[i] = tick;
        return i;
      }
  } else
    for (i = 0; i < MOTION; i++)
      if (motion_ids[i] == id && motion_frames[i] == frame) {
        motion_age[i] = tick;
        return KEYS + i;
      }
  return 255u;
}
static uint8_t load(uint16_t id, uint8_t frame) {
  uint8_t s = find(id, frame);
  if (s == 255u) return 0;
  cache_io(s, 0);
  crucible_shade_mask = s < KEYS ? key_mask[s] : motion_mask[s - KEYS];
  crucible_cache_hits++;
  return 1;
}
/* crucible_decoded (and crucible_shade_mask) into the cache as (id, frame); the oldest unpinned slot gives way */
static void store(uint16_t id, uint8_t frame) {
  uint8_t i, s = 0, old = 0, age;
  if (find(id, frame) != 255u) return;
  if (!frame) {
    for (i = 0; i < KEYS; i++) {
      if (key_ids[i] == CRU_NONE) {
        s = i;
        break;
      }
      if (pinned(key_ids[i])) continue;
      age = tick - ages[i];
      if (age >= old) {
        s = i;
        old = age;
      }
    }
    key_ids[s] = id;
    ages[s] = tick;
    key_mask[s] = crucible_shade_mask;
  } else {
    uint8_t any = 0;
    for (i = 0; i < MOTION; i++) {
      if (motion_ids[i] == CRU_NONE) {
        s = i;
        any = 1;
        break;
      }
      if (visible(motion_ids[i])) continue;
      age = tick - motion_age[i];
      if (age >= old) {
        s = i;
        old = age;
        any = 1;
      }
    }
    /* every slot holds an on-screen loop: a prefetch never evicts one (it would stall that loop); only another
   * on-screen object's frame may take the oldest */
    if (!any) {
      if (!visible(id)) return;
      for (i = 0; i < MOTION; i++) {
        age = tick - motion_age[i];
        if (age >= old) {
          s = i;
          old = age;
        }
      }
    }
    motion_ids[s] = id;
    motion_frames[s] = frame;
    motion_age[s] = tick;
    motion_mask[s] = crucible_shade_mask;
    s += KEYS;
  }
  cache_io(s, 1);
}
/* Record header of object id (flags, key mask, key/rest/turn lengths); returns the bank, *at = the key packet. */
static uint16_t head(uint16_t id, const uint8_t **at) {
  uint16_t bank = crucible_art_locate(id, at);
  *at += REC;
  crucible_rom_copy(bank, *at, rec, 9);
  *at += 9;
  return bank;
}
/* The keyframe of id into crucible_decoded at once (an I-frame: no chain to walk). */
static void keyframe(uint16_t id) {
  const uint8_t *at;
  uint16_t bank = head(id, &at);
  crucible_rom_key(bank, at, REC16(3), crucible_decoded);
  crucible_shade_mask = REC16(1);
}
/* Rigid objects have no resting chain: every rest pose is the keyframe. */
static uint8_t rigid(uint16_t id) {
  const uint8_t *at;
  head(id, &at);
  return rec[0] & 1u;
}
/* Open the stream on chain (0 rest, 1 turn) of id at its keyframe (also cached as frame 0). */
static void s_open(uint16_t id, uint8_t chain) {
  const uint8_t *at;
  uint16_t bank = head(id, &at), kl = REC16(3), rl = REC16(5);
  crucible_rom_key(bank, at, kl, S.tiles);
  memcpy(crucible_decoded, S.tiles, 256);
  crucible_shade_mask = REC16(1);
  store(id, 0);
  if (chain) {
    krc_open(&S, at + kl + rl, REC16(7), bank, S.tiles, REC16(1));
    s_count = CRUCIBLE_VIEWS - 1u;
  } else {
    krc_open(&S, at + kl, rl, bank, S.tiles, REC16(1));
    s_count = (rec[0] & 1u) ? 0u : (uint8_t)(CRUCIBLE_POSES - 1u);
  }
  s_id = id;
  s_chain = chain;
  s_at = 0;
}
static void enqueue(uint16_t id, uint8_t frame, uint8_t priority) {
  uint8_t i, w = 0;
  if (id >= CRUCIBLE_ITEMS || find(id, frame) != 255u) return;
  if (busy && job.id == id && job.frame == frame) return;
  for (i = 0; i < nj; i++) {
    if (jobs[i].id == id && jobs[i].frame == frame) {
      if (priority > jobs[i].priority) jobs[i].priority = priority;
      return;
    }
    if (jobs[i].priority < jobs[w].priority) w = i;
  }
  if (nj < QUEUE) {
    w = nj++;
  } else if (priority <= jobs[w].priority)
    return;
  jobs[w].id = id;
  jobs[w].frame = frame;
  jobs[w].priority = priority;
}
/* Start a job: a keyframe completes at once; a chain frame reuses the stream when it is already on the way there. */
static void begin(request r) {
  uint8_t chain = (r.frame & 128u) ? 1u : 0u, index = r.frame & 15u;
  job = r;
  job_start = sys_time;
  busy = 0;
  if (!index) {
    if (find(r.id, 0) == 255u) {
      keyframe(r.id);
      store(r.id, 0);
    }
    crucible_gen_jobs++;
    return;
  }
  if (s_id != r.id || s_chain != chain || s_at >= index) s_open(r.id, chain);
  if (index > s_count) {
    crucible_gen_jobs++;
    return;
  } /* a rigid object's rest poses are its keyframe */
  busy = 1;
}
/* Decode ROWS changed-tile rows of the running job; every finished chain frame is cached on the way to the target. */
static void step(void) {
  uint8_t index = job.frame & 15u;
  if (S.y >= 32u) krc_frame(&S);
  if (!krc_rows(&S, crucible_art_urgent ? ROWS : ROWS_IDLE)) return;
  s_at++;
  memcpy(crucible_decoded, S.tiles, 256);
  crucible_shade_mask = S.mask;
  store(s_id, s_chain ? (uint8_t)(128u | s_at) : s_at);
  if (s_at >= index) {
    busy = 0;
    crucible_gen_jobs++;
    crucible_gen_last_frames = (uint8_t)(sys_time - job_start);
    if (crucible_gen_last_frames > crucible_gen_max_frames) crucible_gen_max_frames = crucible_gen_last_frames;
  }
}
/* Direction/rate-dependent window in the CURRENT filter. When the whole owned
 * filter fits, every member is pinned, making repeated drill-downs cache hits. */
void crucible_art_cursor(uint16_t id, uint8_t book) BANKED {
  uint16_t p, n, elapsed;
  uint8_t i, target, before;
  uint16_t row, total;
  init();
  if (selected == id && last_filter == core.filter) return;
  elapsed = sys_time - last_cursor_time;
  if (selected != CRU_NONE && last_filter == core.filter) {
    n = cru_shelf_step(&core, selected, 1);
    direction = n == id ? 1 : -1;
    scroll_rate = elapsed < 8u ? 12u : elapsed < 20u ? 6u : 2u;
  } else
    scroll_rate = 2;
  selected = id;
  last_filter = core.filter;
  last_cursor_time = sys_time;
  np = 0;
  nj = 0;
  scan = 0;
  target = core.found[core.filter] < WINDOW ? (uint8_t)core.found[core.filter] : WINDOW;
  before = target > 16u ? (direction > 0 ? 12u : target - 13u) : target / 2u;
  p = id;
  for (i = 0; i < before; i++) p = cru_shelf_step(&core, p, -1);
  crucible_cache_min = p;
  for (i = 0; i < target; i++) {
    pins[np++] = p;
    p = cru_shelf_step(&core, p, 1);
  }
  crucible_cache_max = np ? pins[np - 1u] : id;
  enqueue(id, 0, 120u);
  p = id;
  for (i = 0; i < scroll_rate; i++) {
    p = cru_shelf_step(&core, p, direction);
    enqueue(p, 0, 95u - i);
  }
  p = cru_shelf_step(&core, id, direction);
  enqueue(p, (uint8_t)(128u | (CRUCIBLE_VIEWS - 1u)), 60u); /* the predicted next focus's turntable */
  p = cru_shelf_step(&core, id, -direction);
  enqueue(p, 0, 90u);
  if (book) {
    total = cru_book_count(&core);
    row = cru_book_index(&core, id);
    if (row != CRU_NONE) {
      for (i = 1; i < 4u; i++) {
        n = row + i;
        if (n < total && cru_book_rows_from(&core, n, &p, 1)) enqueue(p, 0, 80u - i);
        if (row >= i && cru_book_rows_from(&core, row - i, &p, 1)) enqueue(p, 0, 75u - i);
      }
    }
  }
}
void crucible_art_tick(void) BANKED {
  uint8_t i, best;
  init();
  tick++;
  if (busy) {
    /* a background chain (prefetch) gives way at once to anything on screen: its frames so far stay cached, and it
   * goes back in the queue to finish later */
    if (job.priority < 125u) {
      for (i = 0; i < nj; i++)
        if (jobs[i].priority >= 125u || (job.priority < 60u && jobs[i].priority >= 80u)) {
          request r = job;
          busy = 0;
          enqueue(r.id, r.frame, r.priority);
          break;
        }
    }
    if (busy) {
      step();
      return;
    }
  }
  if (!nj && np) {
    for (i = 0; i < np; i++) {
      scan = (scan + 1u) % np;
      if (find(pins[scan], 0) == 255u) {
        enqueue(pins[scan], 0, 10u);
        break;
      }
    }
  }
  /* idle with every keyframe in: turntables ahead in the scroll direction (then behind), so the next focus already turns */
  if (!nj && selected != CRU_NONE) {
    uint16_t p = selected;
    uint8_t k;
    for (k = 0; k < 2u && !nj; k++) {
      p = cru_shelf_step(&core, p, direction);
      enqueue(p, (uint8_t)(128u | (CRUCIBLE_VIEWS - 1u)), (uint8_t)(6u - k));
    }
  }
  if (!nj) return;
  best = 0;
  for (i = 1; i < nj; i++)
    if (jobs[i].priority > jobs[best].priority) best = i;
  job = jobs[best];
  jobs[best] = jobs[--nj];
  begin(job);
}
/* Show frame index of object id's resting loop (turn 0) or turntable (turn 1). A keyframe is always available at
 * once; a motion frame not decoded yet is queued and the keyframe stands in (crucible_art_shown says which drew). */
uint8_t crucible_art(uint8_t slot, uint16_t id, uint8_t turn, uint8_t index, uint8_t variant, uint8_t tile,
                     uint8_t palette, uint8_t mode) BANKED {
  uint8_t frame = (turn && index) ? 128u | index : index;
  uint16_t pal[4];
  init();
  if (id >= CRUCIBLE_ITEMS) return 0;
  if ((frame & 15u) && !(frame & 128u) && find(id, frame) == 255u && rigid(id)) frame = 0;
  if ((mode & CRUCIBLE_ART_CAPTURE) || !load(id, frame)) {
    if (frame && !(mode & CRUCIBLE_ART_CAPTURE))
      enqueue(id, frame, id == selected ? 110u : slot < CRUCIBLE_ART_SLOTS ? 100u : 85u);
    frame = 0;
    if (!load(id, 0)) {
      keyframe(id);
      store(id, 0);
    }
  }
  crucible_art_shown = frame & 15u;
  crucible_emit(tile, mode);
  if (palette < 8u) {
    crucible_get_palette(id, variant, pal);
    if (mode & CRUCIBLE_ART_SPRITE)
      set_sprite_palette(palette, 1, pal);
    else
      set_bkg_palette(palette, 1, pal);
  }
  return 1;
}
/* The on-screen objects (whose motion frames are kept). */
void crucible_art_visible(const uint16_t *ids, uint8_t n) BANKED {
  uint8_t i;
  if (n > 8u) n = 8u;
  for (i = 0; i < n; i++) vis[i] = ids[i];
  nvis = n;
}
/* Is an object's whole loop cached (turn 0: resting poses 1..7; turn 1: turntable steps 1..11)? 255 when it is, else
 * the progress 0..254 with the whole chain queued at priority: one job decodes the chain and caches every frame on
 * the way, so a loop plays only once it is complete and never falls back mid-play. */
uint8_t crucible_art_ready(uint16_t id, uint8_t turn, uint8_t priority) BANKED {
  uint8_t n, k, have = 0;
  init();
  if (id >= CRUCIBLE_ITEMS) return 255u;
  if (turn)
    n = CRUCIBLE_VIEWS - 1u;
  else {
    if (rigid(id)) return 255u;
    n = CRUCIBLE_POSES - 1u;
  }
  uint8_t miss = 0;
  for (k = 1; k <= n; k++)
    if (find(id, turn ? (uint8_t)(128u | k) : k) != 255u)
      have++;
    else
      miss = k;
  if (have >= n) return 255u;
  /* ask for the highest frame still missing: one job walks the chain to it and re-caches every evicted frame on the
  * way (asking for the last frame alone would stall forever once it is cached and an earlier one has been evicted) */
  if (!(busy && job.id == id && ((job.frame & 128u) != 0u) == (turn != 0u)))
    enqueue(id, turn ? (uint8_t)(128u | miss) : miss, priority);
  return (uint8_t)(((uint16_t)have * 254u) / n);
}
