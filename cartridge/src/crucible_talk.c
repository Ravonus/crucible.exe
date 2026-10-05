/* TALK: a conversation with a character the program generates from a seed: a name, an archetype (program, daemon,
 * ghost, AI, operator, relic), a bust and a voice. Lines come from the packed line bank (crucible_lines.c) by
 * archetype and intent, with the slots filled from the run (the player, an owned item, its category and trait, the
 * sector), and type out letter by letter with the character's blip; {.} waits a beat and {~} types the next word
 * corrupted before it resolves. A shows the next line, B leaves. The bust is drawn by crucible.c (talk_bust).
 * Each archetype is a faction (cru_saga.c): after it asks, three distinct authored actions answer its exact request; standing
 * (shown under the name) and dialogue coherence record different parts of the answer. The dream bends nonrequest {ITEM} slots and
 * glitches lines as lucidity falls with every conversation. Where you and they lean is never named: it shows in the options
 * offered, how they answer and type (crucible_dialogue.c), what they remember of you, and rare context-triggered glitches. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_data.h"
#include "crucible_ui.h"
#include "crucible_scene.h"
#include "crucible_state.h"
#include "crucible_lines.h"
#include "crucible_avatar.h"
#include "crucible_eggs.h"
#include "crucible_truths.h"
#include "crucible_storyrun.h"
#include "crucible_dialogue.h"
#include "crucible_player.h"
#define T_CREAM 7u
#define T_BRASS 15u
#define X0 1u
#define Y0 10u
#define COLS 18u
#define ROWS_ 6u
/* local text helpers: literals live in this bank, so they never cross to code in another */
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

static const char *const SYL[16] = {"VEX", "NUL", "ARC", "SYS", "ECH", "ZER", "KAI", "LUX",
                                    "OMN", "BIT", "HEX", "NEO", "DAE", "MNE", "ION", "RAM"};
static const char *const KIND[WHO_TYPES] = {"", "PROGRAM", "DAEMON", "GHOST", "AI", "OPERATOR", "RELIC"};
static const char *const SECTORS[4] = {"PASTEL", "NEON", "GREEN RAIN", "STATIC"};
#define SAY_REACT 0xffu
static const uint8_t SAY_ORDER[6] = {SAY_GREET, SAY_LORE, SAY_ASK, SAY_REACT, SAY_GLITCH, SAY_FAREWELL};
static const char *const TIERS[5] = {"HOSTILE", "WARY", "NEUTRAL", "FRIEND", "ALLY"};
static const char *const SCALES[3] = {"EASY", "NORMAL", "HARSH"};
/* the wake-up (a new story run): the machine's lines, a name typed on a keyboard in the text box, how hard it holds.
 * Every press and its moment stir hash_, which seeds the run. */
static uint8_t intro_, ntyped_, kx_, ky_, scale_;
static uint32_t hash_;
static char typed_[CRU_NAME + 1];
static const char KEYS_[5][8] = {"ABCDEFG", "HIJKLMN", "OPQRSTU", "VWXYZ-.", "_!?*<##"};
/* faces per archetype: busts 0..15 the nostalgia icons (in bust bank order), 16..19 the marble heads */
static const uint8_t FACES[WHO_TYPES - 1u][4] = {
    {13u, 14u, 4u, 0u}, /* PROGRAM: monolith, disc warrior, arcade, game boy */
    {11u, 5u, 7u, 1u}, /* DAEMON: chomper, joystick, boombox, crt */
    {12u, 8u, 2u, 18u}, /* GHOST: pixel ghost, vhs, cassette, venus */
    {15u, 1u, 3u, 19u}, /* AI: shell, crt, floppy, athena */
    {10u, 6u, 9u, 16u}, /* OPERATOR: flip phone, walkman, tamagotchi, david */
    {17u, 3u, 0u, 2u}}; /* RELIC: helios, floppy, game boy, cassette */
static crucible_story saga_; /* this power-on's standing and dream (a story run keeps its own in the save) */
static uint8_t saga_ready_, choosing_, reply_, tear_;
/* a lost piece's first glitch (talk_lost_arm): the next talk_event is one line from the machine, put up at once (as A
 * does), that closes by itself LOST_HOLD frames (real time) later, or at a press */
#define LOST_HOLD 90u
static uint8_t lost_armed_, lost_hold_;
static uint16_t lost_id_, lost_t0_;
static uint16_t asked_, offer_, asked_tag_;
/* a script: lines played in order, no replies (an easter egg's visitor, or an ending) */
#define ENDING 0xfeu
#define MACHINE 0xfdu /* the machine itself: the wake-up and the run's turning points */
static uint8_t egg_mode_, script_n_, script_at_, ending_;
static uint16_t script_[8], script_other_[8];
static char other_[18];
static char gift_[14];
static char name_[16], player_[CRU_NAME + 1], item_[14], cat_[12], tag_[8], sector_[12], line_[96];
static const char *slots_[CRUCIBLE_TEXT_SLOTS];
static uint16_t seed_, request_seed_, fx_seed_, voice_;
static uint8_t speed_, pitch_, blen_; /* how this speaker types and sounds */
/* typing: quarter-frames per letter (dialogue_type_base), the speaker's temper, letters typed, credit, A finishes the line */
static uint8_t tq_base_, temper_, tn_, rush_, cue_;
static int8_t tq_;
/* per type: pitch band (0 low..3 high), blip length; index who_ (0 the machine) */
static const uint8_t PITCH[WHO_TYPES] = {0, 3, 0, 2, 3, 1, 1}, BLEN[WHO_TYPES] = {2, 0, 1, 2, 0, 1, 2};
static uint8_t who_, step_, at_, len_, col_, row_, wait_, glitch_, done_, bust_;
static uint8_t gx_[12], gy_[12], gn_;
static char gc_[12];
/* how a line arrives: 0 typed clean, else (three lines in five, more deeper in the dream) 1 decode (letters land as
 * static and resolve a few behind), 2 stutter (a wrong letter, then corrected), 3 flicker (written letters blink
 * into glitch and back), 4 a short run of static riding ahead of the cursor */
#define TS_PLAIN 0u
#define TS_DECODE 1u
#define TS_STUTTER 2u
#define TS_FLICKER 3u
#define TS_STATIC 4u
static uint8_t ts_, hx_[16], hy_[16], hn_, hat_, flick_, sx_, sy_, sc_, stut_;
static char hc_[16];
static uint8_t dq_x_[4], dq_y_[4], dq_n_;
static char dq_c_[4]; /* a glitched word: its letters, typed corrupted, resolve together */
static void dq_flush(void) {
  uint8_t i;
  for (i = 0; i < dq_n_; i++) put_(dq_x_[i], dq_y_[i], GLYPH(dq_c_[i]), T_CREAM);
  dq_n_ = 0;
}
static void resolve(void) {
  uint8_t i;
  for (i = 0; i < gn_; i++) put_(gx_[i], gy_[i], GLYPH(gc_[i]), T_CREAM);
  gn_ = 0;
  glitch_ = 0;
}

static uint16_t roll(void) {
  seed_ ^= seed_ << 7;
  seed_ ^= seed_ >> 9;
  seed_ ^= seed_ << 8;
  return seed_;
}
static uint16_t fx_roll(void) {
  fx_seed_ ^= fx_seed_ << 7;
  fx_seed_ ^= fx_seed_ >> 9;
  fx_seed_ ^= fx_seed_ << 8;
  return fx_seed_;
}
static void clear_box(void) {
  uint8_t y;
  for (y = Y0; y < Y0 + ROWS_; y++) text_(X0, y, " ", COLS, T_CREAM);
}
static void standing(void) {
  uint8_t t = cru_story_tier(&saga_, (uint8_t)(who_ - 1u));
  text_(1, 1, " ", 18, T_CREAM);
  text_(1, 1, TIERS[t], (uint8_t)strlen(TIERS[t]), t >= CRU_TIER_FRIEND ? T_BRASS : T_CREAM);
}
static void replies(void) {
  uint8_t i, x = 0;
  char label[19];
  if (!intro_) {
    for (i = 0; i < 3u; i++) {
      uint8_t y = (uint8_t)(15u + i);
      text_(0, y, " ", 20, T_CREAM);
      if (i == reply_) put_(0, y, GLYPH((char)dialogue_cursor()), T_BRASS);
      dialogue_label(i, label);
      text_(1, y, label, 18, i == reply_ ? T_BRASS : T_CREAM);
    }
    return;
  }
  text_(0, 16, " ", 20, T_CREAM);
  text_(0, 17, " ", 20, T_CREAM);
  for (i = 0; i < 3u; i++) {
    if (i == reply_) put_(x, 17, GLYPH('>'), T_BRASS);
    text_((uint8_t)(x + 1u), 17, SCALES[i], (uint8_t)strlen(SCALES[i]), i == reply_ ? T_BRASS : T_CREAM);
    x = (uint8_t)(x + 2u + strlen(SCALES[i]));
  }
}
static void keys_draw(void) {
  uint8_t y, x, i;
  char c;
  text_(1, 10, " ", 18, T_CREAM);
  text_(1, 10, "NAME", 4, T_BRASS);
  for (i = 0; i < CRU_NAME; i++)
    put_((uint8_t)(6u + i), 10, GLYPH(i < ntyped_ ? typed_[i] : '_'), i < ntyped_ ? T_CREAM : T_BRASS);
  for (y = 0; y < 5u; y++) {
    text_(1, (uint8_t)(11u + y), " ", 18, T_CREAM);
    for (x = 0; x < 7u; x++) {
      uint8_t on = (uint8_t)(y == ky_ && (x == kx_ || (KEYS_[y][kx_] == '#' && KEYS_[y][x] == '#')));
      c = KEYS_[y][x];
      if (c == '#') {
        if (x == 5u) text_(14, (uint8_t)(11u + y), "OK", 2, on ? T_BRASS : T_CREAM);
      } else
        put_((uint8_t)(3u + x * 2u), (uint8_t)(11u + y), c == '<' ? UI_LEFT : GLYPH(c == '_' ? ' ' : c),
             on ? T_BRASS : T_CREAM);
      if (on && c != '#') put_((uint8_t)(2u + x * 2u), (uint8_t)(11u + y), GLYPH('>'), T_BRASS);
    }
  }
  text_(0, 16, " ", 20, T_CREAM);
  text_(0, 17, " ", 20, T_CREAM);
  put_(1, 17, UI_A, T_CREAM);
  text_(2, 17, "TYPE", 4, T_CREAM);
  put_(7, 17, UI_B, T_CREAM);
  text_(8, 17, "DEL", 3, T_CREAM);
  text_(12, 17, "START OK", 8, T_CREAM);
}
/* your face, left of the box while you choose a reply (8.5): the SRAM face cache as 36 BG tiles at (0,2), the backdrop
 * kept in SRAM bank 2 (0xB460) and put back after; only when the face is cached (drawing it would wipe a talker's) */
#define ME_SAVE ((uint8_t *)0xB460u)
static uint8_t me_shown_;
static void me_show(void) {
  uint8_t r;
  if (me_shown_ || !avatar_cached(pl.genome)) return;
  for (r = 0; r < 6u; r++) {
    uint8_t t[6], a[6];
    VBK_REG = 1;
    get_bkg_tiles(0, (uint8_t)(2u + r), 6, 1, a);
    VBK_REG = 0;
    get_bkg_tiles(0, (uint8_t)(2u + r), 6, 1, t);
    ENABLE_RAM;
    SWITCH_RAM(2);
    memcpy(ME_SAVE + r * 12u, t, 6);
    memcpy(ME_SAVE + r * 12u + 6u, a, 6);
    DISABLE_RAM;
  }
  avatar_bg_genome(pl.genome, 0, 2);
  me_shown_ = 1;
}
static void me_hide(void) {
  uint8_t r;
  if (!me_shown_) return;
  me_shown_ = 0;
  for (r = 0; r < 6u; r++) {
    uint8_t t[6], a[6];
    ENABLE_RAM;
    SWITCH_RAM(2);
    memcpy(t, ME_SAVE + r * 12u, 6);
    memcpy(a, ME_SAVE + r * 12u + 6u, 6);
    DISABLE_RAM;
    VBK_REG = 1;
    set_bkg_tiles(0, (uint8_t)(2u + r), 6, 1, a);
    VBK_REG = 0;
    set_bkg_tiles(0, (uint8_t)(2u + r), 6, 1, t);
  }
  avatar_bg_end();
}
static void cue(uint8_t ready) {
  text_(0, 16, " ", 20, T_CREAM);
  text_(0, 17, " ", 20, T_CREAM);
  if (!ready) return;
  if (intro_ == 4) {
    choosing_ = 1;
    reply_ = CRU_STORY_NORMAL;
    replies();
    return;
  }
  if (!egg_mode_ && !intro_ && SAY_ORDER[step_] == SAY_ASK) {
    choosing_ = 1;
    reply_ = 0;
    replies();
    me_show();
    return;
  }
  put_(1, 17, UI_A, T_CREAM);
  {
    uint8_t more = intro_ ? intro_ != 5u : egg_mode_ ? script_at_ < script_n_ : step_ + 1u < sizeof SAY_ORDER;
    text_(2, 17, more ? "NEXT" : "END", more ? 4u : 3u, T_CREAM);
  }
  if (!intro_ && ending_ != 2u) {
    put_(10, 17, UI_B, T_CREAM);
    text_(11, 17, "LEAVE", 5, T_CREAM);
  }
}
static void fill_item(uint16_t id) { crucible_get_name(cru_dream_slot(&core, &saga_, id), item_); }
static void line_start(uint8_t g) {
  {
    uint8_t r = (uint8_t)(fx_roll() % 5u);
    hn_ = 0;
    hat_ = 0;
    flick_ = 0;
    stut_ = 0;
    dq_n_ = 0;
    ts_ = (r < 3u || saga_.lucid < 96u) ? (uint8_t)(1u + fx_roll() % 4u) : TS_PLAIN;
  } /* three lines in five glitch (all of them deep in the dream) */
  at_ = 0;
  col_ = 0;
  row_ = 0;
  wait_ = 6;
  glitch_ = g >= 2u;
  gn_ = 0;
  done_ = 0;
  tq_ = 0;
  tn_ = 0;
  rush_ = 0;
  clear_box();
  cue(0);
  if (g == 3u) tear_ = 1; /* the scene tears: crucible.c shakes the bust */
}
static void say_id(uint16_t id, uint8_t g) {
  len_ = crucible_text_line(id, line_, sizeof line_, slots_);
  line_start(g);
}
static uint8_t opened_;
static void saga_init(uint16_t seed) {
  (void)seed;
  if (!saga_ready_) {
    uint8_t i;
    memset(&saga_, 0, sizeof saga_);
    for (i = 0; i < CRU_STORY_MEMORY; i++) saga_.memory[i] = CRU_NONE;
    saga_.lucid = 220u;
    saga_.seed = 0x2545f491ul ^ core.variant_seed;
    dialogue_init(&saga_);
    saga_ready_ = 1;
  }
}
/* Lore is where the truth leaks: at first only unaware small talk (you do not know you are stuck); later, now and
 * then, a clue about whatever your play leans to (cru_saga.c's truth matrix). */
static void say(uint8_t intent) {
  if (intent == SAY_GREET && !egg_mode_ && !intro_ && dialogue_hint(&saga_, who_, roll(), slots_, line_)) {
    len_ = (uint8_t)strlen(line_);
    line_start(0);
    return;
  } /* they remember last time */
  if (intent == SAY_ASK) {
    uint16_t r = request_seed_, id = dialogue_ask(who_, &r);
    crucible_get_name(asked_, item_);
    dialogue_prepare(&saga_, who_, asked_, offer_, asked_tag_, &r);
    say_id(id, cru_dream_glitch(&saga_, 0));
    return;
  }
  if (intent == SAY_LORE) {
    if (!saga_.chapter && !saga_.cycle) {
      say_id(crucible_line_pick(WHO_ANY, SAY_UNAWARE, roll()), 0);
      return;
    }
    {
      uint8_t k = cru_story_clue_due(&saga_);
      if (k != 0xffu) {
        uint8_t v = cru_story_truth(&saga_, k);
        cru_story_clue(&saga_, k);
        (void)player_xp(10);
        say_id(TRUTH_LINE(k, v, 1u + (roll() & 7u)), cru_dream_glitch(&saga_, 1));
        return;
      }
    }
  }
  say_id(crucible_line_pick(who_, intent, roll()), cru_dream_glitch(&saga_, intent == SAY_GLITCH));
}
crucible_story *talk_saga(void) BANKED { return &saga_; }
void talk_saga_ready(void) BANKED {
  saga_ready_ = 1;
  dialogue_init(&saga_);
}
void talk_saga_reset(void) BANKED { saga_ready_ = 0; }
/* An element made on the bench: the factions notice and the truth matrix leans (this power-on's dream). */
void talk_notice(uint16_t id) BANKED {
  saga_init(id);
  cru_story_notice(&core, &saga_, id);
}
static void script_say(void) {
  uint16_t o = script_other_[script_at_];
  if (o != 0xffffu) crucible_text_line(o, other_, sizeof other_, 0);
  say_id(script_[script_at_++], ending_ ? cru_dream_glitch(&saga_, 1) : tear_);
}
uint8_t talk_tear(void) BANKED {
  uint8_t t = tear_;
  tear_ = 0;
  return t;
}
uint8_t talk_bust(void) BANKED { return bust_; }
static void open_(uint16_t seed, uint8_t sector, uint8_t egg) {
  uint16_t id;
  egg_info e;
  intro_ = 0;
  saga_init(seed);
  seed_ =
      egg == 0xffu
          ? dialogue_open(&saga_,
                          (uint16_t)(core.focus ^
                                     (story_on ? ((uint16_t)saga_.chapter << 8) ^ ((uint16_t)saga_.cycle << 12) : 0u)))
          : (uint16_t)(seed ^ (uint16_t)saga_.seed ^ (uint16_t)(++opened_ * 0x9e37u));
  if (!seed_) seed_ = 0xace1u; /* never the same twice: the run's evolving seed and a count */
  who_ = (uint8_t)(1u + roll() % (WHO_TYPES - 1u));
  strcpy(name_, SYL[roll() & 15u]);
  strcat(name_, SYL[roll() & 15u]);
  if (who_ == WHO_PROGRAM) strcat(name_, ".EXE");
  voice_ = roll();
  bust_ = FACES[who_ - 1u][roll() & 3u];
  if (bust_ >= CRUCIBLE_BUSTS) bust_ = 0;
  /* most of the machine's people get a generated face; relics are the old things themselves (a nostalgia bust) */
  if (egg == 0xffu && saga_ready_ && saga_.chapter >= CRU_STORY_CHAPTERS - 1u)
    egg = ENDING; /* the last chapter closes: the machine speaks */
  ending_ = egg == ENDING;
  if (ending_) {
    (void)player_xp(200);
    player_save();
  } /* an ending: 200 XP (8.3) */
  if (ending_ || egg == MACHINE) {
    who_ = WHO_AI;
    strcpy(name_, "CRUCIBLE.EXE");
    bust_ = 0xffu;
    avatar_make((uint16_t)(saga_.seed ^ 0x51u), CRU_FAC_AI);
    avatar_fx(ending_ ? 4u : 7u);
  } else if (egg != 0xffu && egg != MACHINE) {
    egg_get(egg, &e);
    who_ = (uint8_t)(e.faction + 1u);
    strcpy(name_, e.visitor);
    bust_ = 0xffu;
    avatar_make((uint16_t)(0xc0deu + (uint16_t)egg * 131u), e.faction);
    if (e.glitch) avatar_fx(7);
  } else if (!(roll() & 7u) && (egg = egg_cameo((uint8_t)roll())) !=
                                   0xffu) { /* a cameo: a secret's visitor wanders back in, now an ordinary resident */
    egg_get(egg, &e);
    who_ = (uint8_t)(e.faction + 1u);
    strcpy(name_, e.visitor);
    bust_ = 0xffu;
    avatar_make((uint16_t)(0xc0deu + (uint16_t)egg * 131u), e.faction);
    egg = 0xffu;
  } else if (who_ != WHO_RELIC || (roll() & 1u)) {
    bust_ = 0xffu;
    avatar_make(roll(), (uint8_t)(who_ - 1u));
  }
  saga_init(seed);
  {
    uint8_t w = (ending_ || egg == MACHINE) ? 0u : who_;
    pitch_ = PITCH[w];
    blen_ = BLEN[w];
    tq_base_ = dialogue_type_base(w, voice_);
    temper_ = w ? dialogue_temper(&saga_, w) : 0u;
    speed_ = (uint8_t)((tq_base_ + 3u) >> 2);
    if (w) pitch_ = (uint8_t)((pitch_ + (roll() & 1u)) & 3u);
  } /* a little of their own */
  cru_story_lucid(&saga_, -6); /* each conversation goes a little deeper */
  if (egg == 0xffu && !ending_ && dialogue_cadence(&saga_)) {
    if (saga_.chapter < CRU_STORY_CHAPTERS - 1u) cru_story_advance(&core, &saga_);
  } /* every few conversations, deeper: a new chapter */
  choosing_ = 0;
  tear_ = 0;
  cue_ = 0;
  lost_hold_ = 0;
  egg_mode_ = egg != 0xffu && !ending_ && egg != MACHINE;
  egg_talk_reset();
  if (egg == MACHINE) {
    egg_mode_ = 1;
    script_n_ = 0;
    script_at_ = 0;
    memset(script_other_, 0xff, sizeof script_other_);
  }
  cru_get_name(&core, player_);
  /* an item the player owns, from the shelf near the focus */
  id = core.focus;
  {
    uint8_t k = (uint8_t)(roll() & 7u);
    while (k--) id = cru_shelf_step(&core, id, 1);
  }
  /* Request slots are factual and frozen; surreal intent is explicit in replies. */
  asked_ = id;
  offer_ = core.focus;
  asked_tag_ = 0;
  fill_item(id);
  crucible_get_name(offer_, gift_);
  strcpy(cat_, "SOMETHING");
  strcpy(tag_, "STRANGE");
  {
    uint8_t c = crucible_category(id);
    if (c < CRU_CATEGORIES) crucible_category_name(c, cat_);
  }
  {
    uint16_t m = crucible_traits(id);
    uint8_t k;
    for (k = 0; k < CRU_TRAITS; k++)
      if (m & (1u << k)) {
        cru_tag_name(k, tag_);
        asked_tag_ = (uint16_t)(1u << k);
        break;
      }
  }
  strcpy(sector_, SECTORS[(egg == 0xffu ? seed_ : sector) & 3u]);
  request_seed_ = seed_ ^ asked_ ^ offer_ ^ asked_tag_;
  if (!request_seed_) request_seed_ = 0xace1u;
  fx_seed_ = seed_ ^ 0xb4edu;
  if (!fx_seed_) fx_seed_ = 0xace1u;
  slots_[0] = name_;
  slots_[1] = item_;
  slots_[2] = cat_;
  slots_[3] = tag_;
  slots_[4] = sector_;
  slots_[5] = player_;
  slots_[6] = other_;
  scene_draw(SCENE_RECORDS);
  text_(0, 0, " ", 20, T_CREAM);
  text_(1, 0, name_, (uint8_t)strlen(name_), T_CREAM);
  step_ = 0;
  if (egg == MACHINE) {
    text_(1, 1, " ", 18, T_CREAM);
    return;
  } /* the caller fills the script */
  if (ending_) { /* an ending: mostly questions. Each truth is answered only if clued and clear, hedged if suspected */
    uint8_t a, v;
    memset(script_other_, 0xff, sizeof script_other_);
    script_n_ = 0;
    script_at_ = 0;
    script_[script_n_++] = (uint16_t)(END_OPEN_FIRST + roll() % END_OPEN_N);
    for (a = 0; a < 3u; a++) {
      uint8_t k = cru_story_knows(&saga_, a);
      v = cru_story_truth(&saga_, a);
      if (k == CRU_KNOW_ANSWER)
        script_[script_n_++] = TRUTH_LINE(a, v, 11u + (roll() & 1u));
      else if (k == CRU_KNOW_SUSPECT) {
        script_other_[script_n_] = TRUTH_LINE(a, cru_story_runner_up(&saga_, a), 0u);
        script_[script_n_++] = TRUTH_LINE(a, v, 10u);
      } else
        script_[script_n_++] = TRUTH_LINE(a, v, 9u);
    }
    script_[script_n_++] = (uint16_t)(END_CLOSE_FIRST + roll() % END_CLOSE_N);
    text_(14, 0, "CYCLE", 5, T_BRASS);
    put_(19, 0, GLYPH((char)('1' + (saga_.cycle & 7u))), T_BRASS);
    text_(1, 1, " ", 18, T_CREAM);
    egg_mode_ = 1;
    sound_play(SFX_OPEN);
    script_say();
    return;
  }
  if (egg_mode_) { /* a secret's visitor: its own lines, no replies */
    uint8_t i;
    if (e.item != 0xffffu) crucible_get_name(e.item, item_);
    text_(13, 0, "SECRET", 6, T_BRASS);
    text_(1, 1, " ", 18, T_CREAM);
    script_n_ = e.lines > 8u ? 8u : e.lines;
    script_at_ = 0;
    for (i = 0; i < script_n_; i++) {
      script_[i] = (uint16_t)(e.line + i);
      script_other_[i] = 0xffffu;
    }
    sound_play(SFX_OPEN);
    tear_ = e.glitch ? 1u : 0u;
    script_say();
    return;
  }
  text_((uint8_t)(19u - strlen(KIND[who_])), 0, KIND[who_], (uint8_t)strlen(KIND[who_]), T_BRASS);
  text_(1, 9, "HELD:", 5, T_CREAM);
  text_(7, 9, gift_, 12, T_BRASS);
  dialogue_save(&saga_);
  standing();
  say(SAY_ORDER[0]);
}
void talk_open(uint16_t seed, uint8_t sector) BANKED { open_(seed, sector, 0xffu); }
/* An easter egg's visitor: its own name, face and lines; no replies. */
static void machine_script(uint16_t first, uint8_t n) {
  uint8_t i;
  for (i = 0; i < n && script_n_ < 8u; i++) script_[script_n_++] = (uint16_t)(first + i);
  sound_play(SFX_OPEN);
  script_say();
}
/* A new story run wakes: the machine speaks, you name yourself, you choose how hard it holds. */
void talk_intro(uint16_t seed) BANKED {
  open_(seed, 0, MACHINE);
  intro_ = 1;
  ntyped_ = 0;
  kx_ = 0;
  ky_ = 0;
  scale_ = CRU_STORY_NORMAL;
  hash_ = 0x811c9dc5ul ^ seed;
  machine_script(EV_INTRO_FIRST, EV_INTRO_N);
}
uint8_t talk_intro_scale(void) BANKED { return scale_; }
/* While a new run is built (a few seconds of save work): the box says so. */
void talk_wait(void) BANKED {
  clear_box();
  text_(0, 16, " ", 20, T_CREAM);
  text_(0, 17, " ", 20, T_CREAM);
  text_(2, 12, "THE GRID IS", 11, T_CREAM);
  text_(2, 13, "BUILDING YOU.", 13, T_CREAM);
  text_(2, 15, "HOLD STILL...", 13, T_BRASS);
}
uint32_t talk_intro_hash(void) BANKED { return hash_; }
const char *talk_intro_name(void) BANKED { return typed_; }
/* The machine at a turning point: an element lost to a miss, or the run over (kind STORY_LOSS / STORY_OVER). */
void talk_lost_arm(uint16_t id) BANKED {
  lost_armed_ = 1;
  lost_id_ = id;
}
void talk_event(uint8_t kind, uint16_t item) BANKED {
  if (lost_armed_ &&
      kind == STORY_LOSS) { /* a lost piece would not form: the machine glitches in, says one thing, goes */
    lost_armed_ = 0;
    open_((uint16_t)(lost_id_ ^ 0x105eu), 0, MACHINE);
    intro_ = 0;
    tear_ = 1;
    crucible_get_name(lost_id_, item_);
    machine_script((uint16_t)(EV_ECHO_FIRST + roll() % EV_ECHO_N), 1);
    rush_ = 1;
    wait_ = 0;
    lost_hold_ = 1;
    return;
  }
  lost_armed_ = 0;
  open_((uint16_t)(item ^ 0x7e57u), 0, MACHINE);
  intro_ = 0;
  if (item != CRU_NONE) crucible_get_name(item, item_);
  if (kind == STORY_LOSS)
    machine_script((uint16_t)(EV_LOSS_FIRST + roll() % EV_LOSS_N), 1);
  else {
    uint8_t st = cru_story_state(&core, &saga_);
    if (st == CRU_RUN_LOST_DELETED)
      machine_script(EV_DELETED_FIRST, EV_DELETED_N);
    else if (st == CRU_RUN_LOST_DREAM)
      machine_script(EV_DREAM_FIRST, EV_DREAM_N);
    else
      machine_script(EV_RUNOUT_FIRST, EV_RUNOUT_N);
    ending_ = 2;
  } /* heard to the end */
}
void talk_egg(uint8_t k) BANKED { open_((uint16_t)(0x5eedu ^ ((uint16_t)k * 977u)), 0, k); }
static void react(void) {
  uint8_t r = dialogue_answer(&saga_, who_, reply_, slots_, line_);
  len_ = (uint8_t)strlen(line_);
  standing();
  sound_play(r >= CRU_REACT_WARM ? SFX_MOVE : SFX_CLOSE);
  fill_item(asked_);
  temper_ = dialogue_temper(&saga_, who_);
  cue_ = dialogue_cue();
  line_start(0);
  if (cue_ == 2u)
    ts_ = TS_STUTTER;
  else if (cue_ == 4u)
    tear_ = 1;
} /* a broken promise stutters; nonsense can make the room flicker */
/* One frame: types the next letter (with its blip), or waits for A / B. 1 when the conversation closes. */
static void intro_name_done(void) {
  typed_[ntyped_] = 0;
  while (ntyped_ && typed_[ntyped_ - 1u] == ' ') typed_[--ntyped_] = 0;
  if (!ntyped_) {
    strcpy(typed_, "YOU");
    ntyped_ = 3;
  }
  strcpy(player_, typed_);
  intro_ = 3;
  clear_box();
  say_id((uint16_t)(EV_NAMED_FIRST + roll() % EV_NAMED_N), 0);
}
uint8_t talk_tick(uint8_t pressed) BANKED {
  if (intro_ && pressed)
    hash_ = ((hash_ << 5) + hash_) ^ pressed ^ ((uint32_t)sys_time << 9); /* every press and its moment seed the run */
  if (intro_ == 6) { /* the creator (crucible_avatar_edit.c): the face, then how hard it holds */
    uint8_t r = av_edit_tick(pressed);
    if (r)
      tear_ = 1;
    else if (pressed & (J_LEFT | J_RIGHT | J_SELECT | J_B))
      tear_ = 1;
    if (r == 1u) {
      avatar_make((uint16_t)(saga_.seed ^ 0x51u), CRU_FAC_AI);
      avatar_fx(7);
      avatar_glitch(1);
      intro_ = 4;
      clear_box();
      text_(0, 8, " ", 20, T_CREAM);
      text_(0, 9, " ", 20, T_CREAM);
      text_(0, 16, " ", 20, T_CREAM);
      text_(0, 17, " ", 20, T_CREAM);
      say_id(EV_SCALE_FIRST, 0);
    } else if (r == 2u) {
      avatar_make((uint16_t)(saga_.seed ^ 0x51u), CRU_FAC_AI);
      avatar_fx(7);
      avatar_glitch(1);
      intro_ = 2;
      text_(0, 8, " ", 20, T_CREAM);
      text_(0, 9, " ", 20, T_CREAM);
      clear_box();
      keys_draw();
    }
    return 0;
  }
  if (intro_ == 2) { /* the keyboard */
    char c = KEYS_[ky_][kx_];
    if (pressed & J_LEFT) {
      kx_ = kx_ ? (uint8_t)(kx_ - 1u) : 6u;
      if (KEYS_[ky_][kx_] == '#' && kx_ == 6u) kx_ = 5u;
    } else if (pressed & J_RIGHT) {
      kx_ = kx_ < 6u ? (uint8_t)(kx_ + 1u) : 0u;
    } else if (pressed & J_UP) {
      ky_ = ky_ ? (uint8_t)(ky_ - 1u) : 4u;
    } else if (pressed & J_DOWN) {
      ky_ = ky_ < 4u ? (uint8_t)(ky_ + 1u) : 0u;
    } else if (pressed & J_START || ((pressed & J_A) && c == '#')) {
      sound_play(SFX_OPEN);
      intro_name_done();
      return 0;
    } else if ((pressed & J_B) || ((pressed & J_A) && c == '<')) {
      if (ntyped_) ntyped_--;
      sound_play(SFX_UNDO);
    } else if ((pressed & J_A) && ntyped_ < CRU_NAME) {
      typed_[ntyped_++] = c == '_' ? ' ' : c;
      sound_voice(voice_, 4, 7, ntyped_);
    }
    if (pressed) keys_draw();
    return 0;
  }
  if (choosing_) {
    if (intro_) { /* how hard it holds */
      if (pressed & J_LEFT) {
        reply_ = reply_ ? (uint8_t)(reply_ - 1u) : 2u;
        sound_play(SFX_MOVE);
        replies();
      } else if (pressed & J_RIGHT) {
        reply_ = reply_ < 2u ? (uint8_t)(reply_ + 1u) : 0u;
        sound_play(SFX_MOVE);
        replies();
      } else if (pressed & J_A) {
        choosing_ = 0;
        scale_ = reply_;
        intro_ = 5;
        sound_play(SFX_OPEN);
        say_id((uint16_t)(EV_WAKE_FIRST + roll() % EV_WAKE_N), 0);
      }
      return 0;
    }
    if (pressed & (J_LEFT | J_UP)) {
      reply_ = reply_ ? (uint8_t)(reply_ - 1u) : 2u;
      sound_play(SFX_MOVE);
      replies();
    } else if (pressed & (J_RIGHT | J_DOWN)) {
      reply_ = reply_ < 2u ? (uint8_t)(reply_ + 1u) : 0u;
      sound_play(SFX_MOVE);
      replies();
    } else if (pressed & J_A) {
      choosing_ = 0;
      me_hide();
      step_++;
      react();
    } else if (pressed & J_B) {
      choosing_ = 0;
      me_hide();
      reply_ = 255u;
      step_++;
      react();
    } /* walking off is a refusal */
    return 0;
  }
  if (done_) {
    if (lost_hold_) {
      if (lost_hold_ == 1u) {
        lost_hold_ = 2;
        lost_t0_ = sys_time;
      }
      if ((uint16_t)(sys_time - lost_t0_) < LOST_HOLD && !(pressed & (J_A | J_B))) return 0;
      lost_hold_ = 0;
      sound_play(SFX_CLOSE);
      return 1;
    } /* brief: it goes by itself */
    if (intro_) { /* the wake-up runs to its end: lines, the keyboard, the name, how hard, then it wakes */
      if (!(pressed & J_A)) return 0;
      if (intro_ == 1) {
        if (script_at_ < script_n_) {
          sound_play(SFX_MOVE);
          script_say();
          return 0;
        }
        intro_ = 2;
        clear_box();
        keys_draw();
        return 0;
      }
      if (intro_ == 3) {
        intro_ = 6;
        clear_box();
        text_(0, 16, " ", 20, T_CREAM);
        text_(0, 17, " ", 20, T_CREAM);
        av_edit_open((uint16_t)(hash_ ^ (hash_ >> 16)));
        tear_ = 1;
        return 0;
      }
      if (intro_ == 5) {
        intro_ = 0;
        sound_play(SFX_CLOSE);
        return 1;
      }
      return 0;
    }
    if (egg_mode_ && ending_ && !(pressed & J_A)) return 0; /* an ending is heard to the end */
    if (pressed & J_B) {
      if (!egg_mode_) dialogue_save(&saga_);
      sound_play(SFX_CLOSE);
      return 1;
    }
    if (egg_mode_ && (pressed & J_A)) {
      if (script_at_ >= script_n_) {
        sound_play(SFX_CLOSE);
        if (ending_ == 1u) cru_story_continue(&saga_);
        ending_ = 0;
        return 1;
      }
      sound_play(SFX_MOVE);
      script_say();
      return 0;
    }
    if (pressed & J_A) {
      if (++step_ >= sizeof SAY_ORDER) {
        dialogue_save(&saga_);
        sound_play(SFX_CLOSE);
        return 1;
      }
      sound_play(SFX_MOVE);
      say(SAY_ORDER[step_] == SAY_REACT ? SAY_LORE : SAY_ORDER[step_]);
    }
    return 0;
  }
  if ((pressed & J_SELECT) && who_ == WHO_RELIC && !egg_mode_) {
    egg_talk_select();
    sound_play(SFX_MOVE);
  } /* blowing on it */
  if (pressed & J_A) rush_ = 1; /* A finishes the line at once */
  if (pressed & (J_A | J_B)) {
    wait_ = 0;
  } /* hurry: no pauses */
  if (flick_) {
    flick_ = 0;
    put_(hx_[hat_], hy_[hat_], GLYPH(hc_[hat_]), T_CREAM);
  } /* a flickered letter comes back */
  if (ts_ == TS_FLICKER && hn_ && !(fx_roll() & 3u)) {
    hat_ = (uint8_t)(fx_roll() % hn_);
    put_(hx_[hat_], hy_[hat_], GLYPH((char)('!' + (fx_roll() & 31u))), T_BRASS);
    flick_ = 1;
  }
  if (stut_ && !wait_) {
    stut_ = 0;
    put_(sx_, sy_, GLYPH(sc_), T_CREAM);
    sound_play(SFX_UNDO);
  } /* the stutter corrects itself */
  if (wait_) {
    wait_--;
    return 0;
  }
  tq_ = (int8_t)(tq_ + 4);
  if (tq_ > 8) tq_ = 8; /* four quarter-frames of typing per frame */
  while (at_ < len_) {
    char c = line_[at_++];
    if (c == CRUCIBLE_TEXT_PAUSE) {
      if (rush_) continue;
      if (!(pressed & (J_A | J_B))) wait_ = (uint8_t)(10u + speed_ * 6u);
      return 0;
    }
    if (c == CRUCIBLE_TEXT_LINE) {
      col_ = 0;
      if (++row_ >= ROWS_) row_ = ROWS_ - 1u;
      continue;
    }
    if (c == CRUCIBLE_TEXT_GLITCH) {
      glitch_ = 1;
      continue;
    }
    if (c == ' ' && col_ == 0u) continue;
    if (c != ' ' && col_ && line_[at_ - 2u] == ' ') { /* a word that would not fit starts the next row */
      uint8_t k = at_, n = 1;
      while (k < len_ && line_[k] != ' ' && (uint8_t)line_[k] >= ' ') {
        k++;
        n++;
      }
      if (col_ + n > COLS) {
        col_ = 0;
        if (++row_ >= ROWS_) row_ = ROWS_ - 1u;
      }
    }
    if (col_ >= COLS) {
      col_ = 0;
      if (++row_ >= ROWS_) row_ = ROWS_ - 1u;
      if (c == ' ') continue;
    }
    if (glitch_ && c == ' ') resolve();
    if (glitch_ && gn_ < sizeof gx_) {
      gx_[gn_] = (uint8_t)(X0 + col_);
      gy_[gn_] = (uint8_t)(Y0 + row_);
      gc_[gn_++] = c;
      put_((uint8_t)(X0 + col_), (uint8_t)(Y0 + row_), GLYPH((char)('!' + (fx_roll() & 31u))), T_BRASS);
    } else {
      uint8_t x = (uint8_t)(X0 + col_), y = (uint8_t)(Y0 + row_);
      if (ts_ == TS_DECODE && c != ' ') {
        if (dq_n_ == 4u) {
          put_(dq_x_[0], dq_y_[0], GLYPH(dq_c_[0]), T_CREAM);
          memmove(dq_x_, dq_x_ + 1, 3);
          memmove(dq_y_, dq_y_ + 1, 3);
          memmove(dq_c_, dq_c_ + 1, 3);
          dq_n_ = 3;
        }
        dq_x_[dq_n_] = x;
        dq_y_[dq_n_] = y;
        dq_c_[dq_n_++] = c;
        put_(x, y, GLYPH((char)('!' + (fx_roll() & 31u))), T_BRASS);
      } else if (ts_ == TS_STUTTER && !rush_ && c != ' ' && !(fx_roll() & 7u)) {
        sx_ = x;
        sy_ = y;
        sc_ = c;
        stut_ = 1;
        put_(x, y, GLYPH((char)('A' + fx_roll() % 26u)), T_CREAM);
        col_++;
        wait_ = (uint8_t)(speed_ * 4u + 6u);
        return 0;
      } else
        put_(x, y, GLYPH(c), T_CREAM);
      if (ts_ == TS_STATIC) {
        uint8_t k;
        for (k = 1; k <= 3u; k++)
          if (col_ + k < COLS)
            put_((uint8_t)(x + k), y, (fx_roll() & 1u) ? GLYPH((char)('!' + (fx_roll() & 31u))) : GLYPH(' '), T_BRASS);
      }
      if (c != ' ' && hn_ < 16u) {
        hx_[hn_] = x;
        hy_[hn_] = y;
        hc_[hn_++] = c;
      } else if (c != ' ') {
        uint8_t k = (uint8_t)(fx_roll() & 15u);
        hx_[k] = x;
        hy_[k] = y;
        hc_[k] = c;
      }
    }
    if (c != ' ' && !rush_)
      sound_voice(voice_, (uint8_t)(who_ % 7u), 7, (uint8_t)((at_ & 7u) | (pitch_ << 3) | (blen_ << 5)));
    if (bust_ == 0xffu) avatar_say(c);
    col_++;
    if (cue_ == 1u && c != ' ' && at_ >= (uint8_t)(len_ >> 1) && hn_) {
      cue_ = 0;
      hat_ = (uint8_t)(hn_ - 1u);
      put_(hx_[hat_], hy_[hat_], GLYPH((char)('!' + (fx_roll() & 31u))), T_CREAM);
      flick_ = 1;
    } /* one frame, one letter */
    if (!rush_) {
      uint8_t k = dialogue_type_cost(temper_, tq_base_, c, tn_++, fx_roll());
      if (glitch_) k = (uint8_t)(k + 8u);
      tq_ = (int8_t)(tq_ - (int8_t)k);
      if (tq_ <= 0) return 0;
    }
  }
  if (glitch_) resolve();
  dq_flush();
  if (stut_) {
    stut_ = 0;
    put_(sx_, sy_, GLYPH(sc_), T_CREAM);
  }
  if (flick_) {
    flick_ = 0;
    put_(hx_[hat_], hy_[hat_], GLYPH(hc_[hat_]), T_CREAM);
  }
  if (ts_ == TS_STATIC) {
    uint8_t k;
    for (k = 0; k < 3u; k++)
      if (col_ + k < COLS) put_((uint8_t)(X0 + col_ + k), (uint8_t)(Y0 + row_), GLYPH(' '), T_CREAM);
  }
  if (bust_ == 0xffu) avatar_say(' ');
  done_ = 1;
  cue(1);
  return 0;
}
