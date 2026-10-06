/* The encounter director: talks and fights come to you while you play; the bench is always home.
 *
 * After every mix the story run reacts (crucible_storyrun.c: STORY_VISIT, STORY_BOSS, a loss) and this decides how it
 * reaches the player, by context and seeded rolls:
 *   - forced: the sign row telegraphs it for two seconds ("!! IT COMES !!" / "SOMEONE STEPS IN") with the hint
 *     flickering at its edge, then the view pans to it (pressing its direction goes at once);
 *   - waiting: a hint at the bench's edge, a face peeking in under the header (someone to talk to, UP) or eyes at the
 *     floor's edge (something hostile, DOWN), with the sign row saying so. Approach it with UP or DOWN; ignored, it
 *     fades after about twenty seconds, and a run notices (a rival's grudge grows; a voice ignored leans the dream to
 *     standing still).
 * Progression fights keep the story's stakes. Random visitors and bosses also wait at the edge after a quiet,
 * variable interval of story bench time; ignoring them costs nothing. Free play keeps its existing visitors.
 * Random arrivals pause outside the bench and while linked. Pacing and triggers are in docs/game-flow.md.
 *
 * The approach pans the background (SCY) 112 pixels into the hidden map rows 18..31, painted as a fluorescent corridor
 * (noclipping), with sprites off; the cartridge then opens the talk or fight and sets SCY back. Coming back, the bench is
 * drawn with the view still away and pans home. Nothing here writes the save: hints, pacing and the chapter edge live in
 * RAM for this power-on (the run's own record already keeps its standing and leanings).
 * OAM 38 is the hint (37 is the loading spark, 39 the cursor; rooms use 34..36). OBJ tiles 93..94, VRAM bank 0. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "interrupts.h"
#include "crucible_data.h"
#include "crucible_ui.h"
#include "crucible_state.h"
#include "crucible_storyrun.h"
#include "crucible_fight.h"
#include "crucible_link.h"
#include "crucible_link_scene.h"
#include "crucible_time.h"
#include "crucible_flow.h"
#include "crucible_player.h"
#define T_CREAM 7u
#define T_BRASS 15u
#define LIFE 1200u /* frames a waiting hint stays (about twenty seconds on the bench) */
#define FADE 180u /* its last three seconds flicker */
#define TELEGRAPH 120u /* frames a forced encounter is announced before it pans in */
#define PAN_PX 112u /* the hidden map rows 18..31 */
#define PAN_FRAMES 30u
extern uint8_t win_pos_y; /* main.c: the toast window's position (144: off) */
uint8_t room_door(void) BANKED; /* crucible_room.c: the door anomaly shows (UP is its) */
uint8_t flow_hint, flow_force, flow_arg, flow_pending, flow_since = 4u, flow_count, flow_fgap = 6u, flow_tgap = 4u;
static uint8_t owe, inited, last, fc, poll, dirty, vis, beat = 0xffu, chap_seen = 0xffu, story_seen, time_done, sp_seen;
uint8_t flow_ambient; /* the current hint arrived by time: optional, with no ignored penalty */
uint16_t flow_idle_left; /* bench frames until a random arrival (never saved) */
static uint8_t ambient_prev, ambient_streak;
static uint16_t t, seed = 0x51f7u;
static void ambient_arm(void);
static void put_(uint8_t x, uint8_t y, uint8_t tile, uint8_t attr) {
  VBK_REG = 1;
  set_bkg_tiles(x, y, 1, 1, &attr);
  VBK_REG = 0;
  set_bkg_tiles(x, y, 1, 1, &tile);
}
static void text_(uint8_t x, uint8_t y, const char *s, uint8_t width, uint8_t attr) {
  uint8_t tiles[20], attrs[20], i = 0, c;
  if (!width) return;
  if (width > 20u) width = 20u;
  memset(attrs, attr, width);
  while (i < width) {
    c = *s;
    if (c) s++;
    tiles[i++] = GLYPH(c);
  }
  VBK_REG = 1;
  set_bkg_tiles(x, y, width, 1, attrs);
  VBK_REG = 0;
  set_bkg_tiles(x, y, width, 1, tiles);
}
static void field_(uint8_t x, uint8_t y, uint8_t width, const char *s, uint8_t attr) {
  uint8_t n = strlen(s);
  if (n > width) n = width;
  text_(x, y, " ", width, attr);
  text_(x + (width - n) / 2u, y, s, n, attr);
}
/* a seeded roll: the run's seed and the bench's stirred rng */
static uint8_t roll(void) {
  seed ^= (uint16_t)core.rng;
  if (!seed) seed = 0x51f7u;
  seed ^= seed << 7;
  seed ^= seed >> 9;
  seed ^= seed << 8;
  return (uint8_t)seed;
}
/* about 18..44 seconds of bench time, independent of recipes, standing and discoveries */
static void ambient_arm(void) { flow_idle_left = (uint16_t)(1080u + (uint16_t)roll() * 6u); }
/* the hint tiles (2bpp, OBJ palette 4: cyan, white, magenta): a head peeking down with its eyes, and a lurker's horns */
static const uint8_t TILES[32] = {0x3c, 0x00, 0x7e, 0x00, 0xff, 0x00, 0x99, 0x66, 0xbb, 0x66, 0xff,
                                  0x00, 0x7e, 0x00, 0x18, 0x00, 0x81, 0x81, 0xc3, 0xc3, 0x7e, 0x7e,
                                  0xff, 0xff, 0x99, 0xff, 0xff, 0xff, 0x5a, 0x5a, 0x00, 0x00};
static void hide(void) { move_sprite(FLOW_OAM, 0, 0); }
/* the hint at its edge: above, under the header; below, at the floor's edge under the shelf */
static void show(uint8_t kind, uint8_t on) {
  uint8_t up = kind == FLOW_TALK, bob = (uint8_t)((fc >> 4) & 1u);
  if (!on) {
    hide();
    return;
  }
  set_sprite_tile(FLOW_OAM, (uint8_t)(FLOW_TILE + (up ? 0u : 1u)));
  set_sprite_prop(FLOW_OAM, 4u);
  move_sprite(FLOW_OAM, 84u, (uint8_t)(up ? 24u + bob : 130u - bob));
}
static const char *const TELL[2] = {"SOMEONE STEPS IN", "!! IT COMES !!"};
/* row 17 with nothing held: a waiting encounter (and which way it is), else START's hint */
static void sign_idle(void) {
  const char *s = "";
  if (core.slot_a == CRU_NONE)
    s = vis && flow_hint == FLOW_TALK    ? "UP: SOMEONE WAITS"
        : vis && flow_hint == FLOW_FIGHT ? "DOWN: IT WATCHES"
                                         : "ST MENU";
  field_(1, 17, 18, s, vis ? T_CREAM : T_BRASS);
}
void flow_sign(uint8_t message, uint16_t sign) BANKED {
  char l[19];
  if (flow_force) {
    field_(1, 17, 18, TELL[flow_force - 1u], T_CREAM);
    return;
  }
  if (message == CRU_MSG_NO_RESULT)
    strcpy(l, "NO RESULT");
  else if (message == CRU_MSG_RESULT && sign < core.items) {
    l[0] = '=';
    crucible_get_name(sign, l + 1);
  } else {
    sign_idle();
    return;
  }
  field_(1, 17, 18, l, T_BRASS);
}
void flow_cues(uint8_t screen) BANKED {
  text_(0, 16, " ", 20, T_CREAM);
  if (screen == 2u) {
    put_(8, 16, UI_A, T_CREAM);
    text_(9, 16, "OK", 2, T_CREAM);
    return;
  } /* the reveal */
  put_(0, 16, UI_A, T_CREAM);
  if (screen == 3u) {
    text_(1, 16, "USE", 3, T_CREAM);
    put_(5, 16, UI_B, T_CREAM);
    text_(6, 16, "BACK", 4, T_CREAM);
  } /* the book */
  else if (core.slot_a != CRU_NONE) {
    text_(1, 16, "MIX", 3, T_CREAM);
    put_(5, 16, UI_B, T_CREAM);
    text_(6, 16, "BACK", 4, T_CREAM);
  } else {
    text_(1, 16, "ADD", 3, T_CREAM);
    if (core.filter) {
      put_(5, 16, UI_B, T_CREAM);
      text_(6, 16, "ALL", 3, T_CREAM);
    }
  } /* B clears a filter */
  text_(11, 16, "SE FILTER", 9, T_BRASS);
}
uint8_t flow_unfilter(void) BANKED {
  if (core.slot_a != CRU_NONE || !core.filter) return 0;
  cru_filter_apply(&core, 0);
  cru_filter_close(&core);
  return 1;
}
/* ---- the pan ---- */
/* The hidden rows 18..31: a fluorescent corridor that recedes from the bench's edge, the encounter's mark at its end. */
static void paint(uint8_t dir, uint8_t kind) {
  uint8_t y, x, d, tiles[20], attrs[20];
  char c;
  for (y = 18u; y < 32u; y++) {
    d = dir == FLOW_UP ? (uint8_t)(31u - y) : (uint8_t)(y - 18u); /* distance from the bench */
    for (x = 0; x < 20u; x++) {
      c = ' ';
      attrs[x] = T_CREAM;
      if (x == (uint8_t)(1u + (d >> 2)))
        c = '['; /* walls closing in */
      else if (x == (uint8_t)(18u - (d >> 2)))
        c = ']';
      else if ((d & 3u) == 1u && x > (uint8_t)(2u + (d >> 2)) && x < (uint8_t)(17u - (d >> 2)) && (x & 3u) == 1u)
        c = '='; /* the tubes */
      else if (!(roll() & 15u)) {
        c = (roll() & 1u) ? '.' : ':';
        attrs[x] = T_BRASS;
      } /* the carpet hums */
      if (d == 11u && (x == 9u || x == 10u)) {
        c = kind == FLOW_FIGHT ? '!' : '?';
        attrs[x] = T_CREAM;
      }
      tiles[x] = GLYPH(c);
    }
    VBK_REG = 1;
    set_bkg_tiles(0, y, 20, 1, attrs);
    VBK_REG = 0;
    set_bkg_tiles(0, y, 20, 1, tiles);
  }
}
static const uint8_t EASE[17] = {0, 1, 4, 9, 16, 24, 33, 43, 54, 64, 75, 84, 92, 99, 104, 107, 108};
/* Sprites off (the clouds, overlays and cursor do not scroll with the map), the toast window hidden, then the eased pan.
 * Going in, the last frames tear sideways. */
static void pan(uint8_t dir, uint8_t back) {
  uint8_t i, k, off, kinds[LCD_EVENTS], wy = win_pos_y;
  memcpy(kinds, lcd_kind, LCD_EVENTS);
  for (i = 0; i < LCD_EVENTS; i++) lcd_kind[i] &= (uint8_t)~LCD_OBJ_ON;
  LCDC_REG &= (uint8_t)~LCDCF_OBJON;
  win_pos_y = 144u;
  for (i = 0; i <= PAN_FRAMES; i++) {
    k = back ? (uint8_t)(PAN_FRAMES - i) : i;
    off = (uint8_t)((uint16_t)EASE[(uint8_t)((uint16_t)k * 16u / PAN_FRAMES)] * PAN_PX / 108u);
    vsync();
    SCY_REG = dir == FLOW_UP ? (uint8_t)(0u - off) : off;
    SCX_REG = (!back && i + 8u > PAN_FRAMES && (roll() & 1u)) ? (uint8_t)(roll() & 7u) : 0u;
    sound_tick();
  }
  SCX_REG = 0;
  memcpy(lcd_kind, kinds, LCD_EVENTS);
  win_pos_y = wy;
  (void)
      input_take(); /* presses during the pan belong to neither scene (an A that closed a talk must not ADD on the bench) */
}
static uint8_t go(uint8_t kind) {
  uint8_t d = kind == FLOW_TALK ? FLOW_UP : FLOW_DOWN, i;
  hide();
  sound_play(SFX_SWAP);
  paint(d, kind);
  pan(d, 0);
  for (i = 0; i < 40u; i++) move_sprite(i, 0, 0); /* the next scene places its own */
  LCDC_REG |= LCDCF_OBJON;
  flow_pending = d;
  flow_hint = flow_force = flow_ambient = 0;
  ambient_arm();
  flow_since = 0;
  vis = 0;
  flow_count++;
  if (kind == FLOW_FIGHT)
    flow_fgap = 0;
  else {
    flow_tgap = 0;
    owe = 0;
  }
  return kind;
}
void flow_back(uint8_t phase) BANKED {
  uint8_t d = flow_pending;
  if (!phase) (void)input_take(); /* a press made while the last scene closed (or a new run was built) stays there */
  if (!d) return;
  if (!phase) {
    paint(d, 0);
    SCY_REG = d == FLOW_UP ? (uint8_t)(0u - PAN_PX) : PAN_PX;
    LCDC_REG &= (uint8_t)~LCDCF_OBJON;
    return;
  }
  flow_pending = 0;
  pan(d, 1);
  SCY_REG = 0;
  LCDC_REG |= LCDCF_OBJON;
}
/* ---- deciding ---- */
static void wait_(uint8_t kind) {
  flow_hint = kind;
  flow_ambient = 0;
  ambient_arm();
  t = 0;
  dirty = 1;
  vis = 0;
}
static void force_(uint8_t kind) {
  flow_force = kind;
  flow_hint = flow_ambient = 0;
  ambient_arm();
  t = 0;
  dirty = 1;
  sound_play(SFX_DENY);
}
/* a champion of faction f; while the nemesis has not been met three times, it is the one who comes. Until the first
 * duel has been fought, whatever comes is that duel (the first contact, fight spec 5.7). */
static void champion(uint8_t f) {
  flow_arg = (uint8_t)((f < CRU_FACTIONS ? f : 0u) | (talk_saga()->nemesis.met < 3u ? 0x80u : 0u));
  if (!(pl.flags & PF_FIRST_DUEL)) flow_arg = (uint8_t)((flow_arg & 7u) | FIGHT_DUEL);
}
static void duelist(uint8_t f) { flow_arg = (uint8_t)((f < CRU_FACTIONS ? f : 0u) | FIGHT_DUEL); }
/* no fight until there is a bag to bring: eight things of your own beside the four (a story's first few makes) */
static uint8_t dream_seen;
static uint8_t can_fight(void) { return (pl.flags & PF_FIRST_DUEL) || core.found[0] >= 12u; }
static uint8_t coldest(void) {
  crucible_story *s = talk_saga();
  uint8_t f, best = 0;
  for (f = 1; f < CRU_FACTIONS; f++)
    if (s->stand[f] < s->stand[best]) best = f;
  return best;
}
/* Pacing, in makes (mixes): a breath of 2 after any encounter; fights at least 6 apart (a hostile champion forces itself
 * in from 12, a rival only lurks from 8, the nemesis returns from 10); talks come with the run's visits, and when a
 * champion is not due yet a voice may come instead (5 apart). A chapter's gatekeeper always comes, two makes after. */
#define FIGHT_WAIT 6u
#define FIGHT_FORCE 12u
uint8_t flow_after_mix(uint8_t r) BANKED {
  crucible_story *s = talk_saga();
  uint8_t f;
  time_mark_act(); /* a mix the minute before an angel minute calls someone in then (crucible_time.c) */
  {
    static uint16_t counted;
    uint16_t n = core.found[0]; /* 111, 222 .. 999 things on the shelf: someone counts them with you */
    if (n >= 111u && n <= 999u && !(n % 111u) && n != counted) {
      counted = n;
      talk_angel(n);
      time_angel_arm();
    }
  }
  if (r == STORY_LOSS || r == STORY_OVER) return r; /* the machine's own lines: at once */
  if (link_on && !(link_started && link_mode == LINK_COOP))
    return 0; /* a co-op session keeps its encounters (its bosses are shared, 9.4) */
  if (flow_since < 255u) flow_since++;
  if (flow_fgap < 255u) flow_fgap++;
  if (flow_tgap < 255u) flow_tgap++;
  if (story_on) {
    if (chap_seen != 0xffu && s->chapter > chap_seen) {
      beat = 2u;
      (void)player_xp(50);
    }
    chap_seen = s->chapter;
  } /* a new chapter: its gatekeeper comes two makes later */
  if (story_on && !can_fight() && r == STORY_BOSS) r = STORY_NONE; /* the first fight waits for a bag */
  if (r == STORY_VISIT)
    owe = 1;
  else if (owe && r == STORY_NONE)
    r = STORY_VISIT; /* a visit that could not come yet is kept, not lost */
  if (flow_force) return 0;
  if (flow_hint) { /* someone already waits; a hostile champion loses patience */
    if (!flow_ambient && r == STORY_BOSS && flow_hint == FLOW_FIGHT && flow_fgap >= FIGHT_FORCE) force_(FLOW_FIGHT);
    return 0;
  }
  if (story_on && beat != 0xffu) {
    if (beat)
      beat--;
    else { /* the chapter beat: from chapter 1 a gatekeeper fights (a rival, else the coldest faction); before, a voice.
              * One that could not fight yet (no bag) is owed: it comes again at the next make */
      if (s->chapter >= 1u && !can_fight()) return 0;
      beat = 0xffu;
      if (s->chapter >= 1u && can_fight()) {
        f = fight_rival();
        champion(f == 0xffu ? coldest() : f);
        if ((s->chapter == 4u || s->chapter >= 8u) && (pl.flags & PF_FIRST_DUEL)) {
          flow_arg = (uint8_t)((flow_arg & 7u) | FIGHT_DUEL);
          flow_gauntlet = 1;
        } /* the gauntlet: chapter 4 and the finale */
        force_(FLOW_FIGHT);
      } else
        force_(FLOW_TALK);
      return 0;
    }
  }
  if (flow_since < 2u) return 0; /* a breath after any encounter */
  if (r == STORY_BOSS && story_on && flow_fgap >= FIGHT_WAIT) {
    champion(fight_rival());
    if (flow_fgap >= FIGHT_FORCE)
      force_(FLOW_FIGHT);
    else
      wait_(FLOW_FIGHT);
    return 0;
  }
  /* after the first duel the first champion follows (ten makes on): every run meets a boss early (pacing, 2026-10-05) */
  if (story_on && (pl.flags & PF_FIRST_DUEL) && !s->nemesis.met && flow_fgap >= 10u) {
    champion(coldest());
    force_(FLOW_FIGHT);
    return 0;
  }
  /* the nemesis that fled comes back with what it took */
  if (story_on && s->nemesis.met && s->nemesis.met < 3u && flow_fgap >= 10u && !(roll() & 1u) &&
      (pl.flags & PF_FIRST_DUEL)) {
    flow_arg = (uint8_t)(s->nemesis.faction | 0x80u);
    force_(FLOW_FIGHT);
    return 0;
  }
  if (r == STORY_VISIT || (r == STORY_BOSS && flow_tgap >= 5u && !(roll() % 3u))) {
    owe = 0;
    if (!(roll() % 5u))
      force_(FLOW_TALK);
    else
      wait_(FLOW_TALK);
    return 0;
  }
  /* a rival lurks to duel: more likely right after a lost piece would not form (it has it); and now and then a figure of
   * any faction that dislikes you walks in (1/16, +1/32 per hostile faction: section 5.1) */
  if (story_on && flow_fgap >= 8u && (f = fight_rival()) != 0xffu && can_fight() && (core.mix.lost || !(roll() & 3u))) {
    duelist(f);
    if (!(pl.flags & PF_FIRST_DUEL)) champion(f);
    wait_(FLOW_FIGHT);
    return 0;
  }
  if (story_on && flow_fgap >= 4u && can_fight()) {
    uint8_t h = 0, w = 0xffu, k;
    for (k = 0; k < CRU_FACTIONS; k++) {
      if (s->stand[k] <= -50) h++;
      if (s->stand[k] <= -8 && w == 0xffu) w = k;
    }
    if (w != 0xffu && roll() < (uint8_t)(16u + (h << 3))) {
      duelist(w);
      wait_(FLOW_FIGHT);
    }
  }
  return 0;
}
/* ignored until it faded: the run notices */
static void ignored(void) {
  crucible_story *s = talk_saga();
  uint8_t f = flow_arg & 7u;
  if (!story_on || flow_ambient) return;
  if (flow_hint == FLOW_FIGHT && f < CRU_FACTIONS)
    s->stand[f] = s->stand[f] > -97 ? (int8_t)(s->stand[f] - 3) : -100; /* hiding: the grudge grows */
  else
    cru_story_act(s, CRU_ACT_IDLE); /* a voice ignored: standing still */
  story_save();
}
/* Random arrivals use the same hints, never force a scene or require a mix. After two of one kind, the other gets the
 * next turn, so a quiet, friendly starter-only save cannot starve its bosses. */
static void ambient_tick(uint8_t dt) {
  uint8_t kind, f;
  if (!story_on || link_on || flow_hint || flow_force) return;
  if (flow_idle_left > dt) {
    flow_idle_left -= dt;
    return;
  }
  kind = (roll() & 1u) ? FLOW_TALK : FLOW_FIGHT;
  if (ambient_streak >= 2u && kind == ambient_prev) kind = kind == FLOW_TALK ? FLOW_FIGHT : FLOW_TALK;
  ambient_streak = kind == ambient_prev ? (uint8_t)(ambient_streak + 1u) : 1u;
  ambient_prev = kind;
  /* a fight brings an actual champion, independent of the progression's tutorial/DREAM gate */
  if (kind == FLOW_FIGHT) {
    do f = (uint8_t)(roll() & 7u);
    while (f >= CRU_FACTIONS);
    flow_arg = f;
  }
  wait_(kind);
  flow_ambient = 1;
}
/* the clock: back after hours or more, or a special minute, someone is waiting */
static void time_check(void) {
  crucible_time_ctx c;
  if (flow_hint || flow_force) return;
  crucible_time_context(&c);
  if (!c.special) sp_seen = 0;
  if (!time_done && (c.flags & CT_F_RETURNING) && c.away >= CT_AWAY_HOURS) {
    time_done = 1;
    wait_(FLOW_TALK);
  } else if (c.special && c.special != sp_seen) {
    sp_seen = c.special;
    wait_(FLOW_TALK);
  }
}
void fight_debug(void) BANKED; /* crucible_fight_dbg.c: inert unless a test harness asks */
uint8_t flow_tick(uint8_t screen, uint8_t *pressed) BANKED {
  if (fight_dbg) fight_debug();
  uint8_t dt = (uint8_t)((uint8_t)sys_time - last), dir, v;
  last = (uint8_t)sys_time;
  if (dt > 8u) dt = 8u;
  fc = (uint8_t)(fc + dt);
  if (!inited) {
    inited = 1;
    VBK_REG = 0;
    set_sprite_data(FLOW_TILE, 2, TILES);
    seed ^= (uint16_t)DIV_REG << 8;
    story_seen = story_on ? (uint8_t)(story_slot_at + 1u) : 0u;
    ambient_prev = ambient_streak = 0;
    ambient_arm();
  }
  if ((story_on ? (uint8_t)(story_slot_at + 1u) : 0u) != story_seen ||
      (link_on && !(link_started && link_mode == LINK_COOP))) {
    story_seen = story_on ? (uint8_t)(story_slot_at + 1u) : 0u;
    flow_hint = flow_force = flow_ambient = owe = 0;
    ambient_prev = ambient_streak = 0;
    ambient_arm();
    flow_since = flow_tgap = 4u;
    flow_fgap = 6u;
    time_done = sp_seen = flow_gauntlet = 0;
    beat = chap_seen = 0xffu;
    dream_seen = 0;
    (void)dream_seen;
  } /* a run left or begun: nothing carries over (a co-op session keeps its encounters: they become shared, 9.4) */
  if (screen != 0u) {
    hide();
    vis = 0;
    return 0;
  }
  /* a FIGHT session over the link: the bout comes at once (the partner is down there) */
  if (link_on && link_started && link_mode == LINK_FIGHT && !link_result() && !flow_pending) {
    flow_arg = 0;
    return go(FLOW_FIGHT);
  }
  if (link_on && link_started && link_mode == LINK_COOP && !flow_pending && link_scene_pull()) {
    flow_arg = 0;
    return go(FLOW_FIGHT);
  } /* the partner's boss: pulled in at the bench, idle (9.4) */
  if (++poll >= 64u) {
    poll = 0;
    time_check();
  }
  /* an angel minute after something meaningful (or a count on the shelf): someone steps in, at most once; never while
   * linked, never over one already waiting (time-awareness.md) */
  if (time_angel_take() && !flow_hint && !flow_force && !link_on) force_(FLOW_TALK);
  ambient_tick(dt);
  if (flow_force) { /* telegraphed: it comes by itself, or at once if you go to it */
    dir = flow_force == FLOW_TALK ? J_UP : J_DOWN;
    t += dt;
    if (dirty) {
      dirty = 0;
      field_(1, 17, 18, TELL[flow_force - 1u], T_CREAM);
    }
    show(flow_force, (uint8_t)((fc >> 1) & 1u));
    if (!((uint8_t)t & 31u) && t < TELEGRAPH) sound_play(SFX_MOVE);
    if (t >= TELEGRAPH || (*pressed & dir)) {
      *pressed &= (uint8_t)~dir;
      if (flow_force == FLOW_FIGHT && !story_on) {
        flow_force = 0;
        return 0;
      }
      return go(flow_force);
    }
    return 0;
  }
  if (!flow_hint) {
    hide();
    return 0;
  }
  /* it waits at its edge (the sign row names it while nothing is held), never over the room's door (UP there is the door's) */
  v = !(flow_hint == FLOW_TALK && room_door());
  if (v != vis) {
    vis = v;
    dirty = 1;
  }
  if (dirty) {
    dirty = 0;
    if (core.slot_a == CRU_NONE) sign_idle();
  }
  if (!v) {
    hide();
    return 0;
  }
  t += dt;
  dir = flow_hint == FLOW_TALK ? J_UP : J_DOWN;
  if (*pressed & dir) {
    *pressed &= (uint8_t)~dir;
    return go(flow_hint);
  }
  if (t >= LIFE) {
    ignored();
    flow_hint = flow_ambient = 0;
    ambient_arm();
    vis = 0;
    hide();
    sign_idle();
    return 0;
  }
  show(flow_hint, t < LIFE - FADE || ((fc >> 2) & 1u));
  return 0;
}
