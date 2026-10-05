/* The boss screen and its flow (docs/fight-system.md 6, 7): the telegraph, the tells, the answers, the end
 * choice and what a boss gives and takes. The engine is crucible_fight_boss.c; the duel screen it shares its hand and
 * panels with is crucible_fight.c (crucible_fight_int.h). Literals stay in this bank: only numbers and RAM cross. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_data.h"
#include "crucible_ui.h"
#include "crucible_scene.h"
#include "crucible_state.h"
#include "crucible_lines.h"
#include "crucible_truths.h"
#include "crucible_lines.h"
#include "crucible_avatar.h"
#include "crucible_storyrun.h"
#include "crucible_fight.h"
#include "crucible_fight_rules.h"
#include "crucible_fight_boss.h"
#include "crucible_player.h"
#include "crucible_fight_int.h"
#include "crucible_link.h"
#include "crucible_link_scene.h"
#define T_CREAM 7u
#define T_BRASS 15u
#define PIP_FRAMES 150u
#define SHOW_FRAMES 100u
#define INTRO_FRAMES 60u
static const char *const FNAME[6] = {"PROGRAM", "DAEMON", "GHOST", "AI", "OPERATOR", "RELIC"};
static const char STANCE_CH[4] = {'=', '#', '*', ' '};
static const uint8_t BIT8[8] = {1, 2, 4, 8, 16, 32, 64, 128};
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
static void num_(char *o, uint16_t n, uint8_t w) {
  o[w] = 0;
  while (w) {
    o[--w] = (char)('0' + n % 10u);
    n /= 10u;
  }
}
static void clear_rows(uint8_t y0, uint8_t y1) {
  for (; y0 <= y1; y0++) text_(0, y0, " ", 20, T_CREAM);
}
static void status(const char *s, uint8_t attr) {
  text_(0, 17, " ", 20, T_CREAM);
  text_(1, 17, s, (uint8_t)strlen(s), attr);
}
static void say_text(const char *s) {
  strncpy(ticker_, s, sizeof ticker_ - 1u);
  ticker_[sizeof ticker_ - 1u] = 0;
  ticker_len_ = (uint8_t)strlen(ticker_);
  ticker_at_ = 0;
  ticker_t_ = 0;
}
static void kdc_nudge(uint8_t *k, uint8_t axis, int8_t d) {
  int16_t v = (int16_t)(int8_t)k[14u + axis] + d;
  k[14u + axis] = (uint8_t)(int8_t)(v > 60 ? 60 : v < -60 ? -60 : v);
}
#define say fui_say
#define cosm fui_cosm
#define hp_bar fui_hp_bar
#define me_block fui_me_block
#define hand_draw fui_hand_draw
#define answer_line fui_answer_line
#define live_stance fui_live_stance
#define fuse_act fui_fuse_act
#define fuse_product fui_fuse_product
#define face_open fui_face_open
#define draw_duel fui_draw_duel
#define arena_now fui_arena
#define equip fui_equip
/* ---- the boss (6): it combines two of its hand and shows "a + b = ?"; you answer from your hand before its pips run
 * out. Plain strikes show the result's stance by the "?"; veiled ones only the ingredients. Tricks tell themselves on the
 * ticker and around the cells, and each is rehearsed at half power the first time it is used. ---- */
#define UI_PLAN 8u
#define UI_CHOICE 9u
static uint8_t picks_[2], close_[2], npick_, used_, adapted_, shared_hp_, veto_t_;
static uint16_t veto_at_;
uint8_t fbui_outc, fbui_nemesis;
#define outc_ fbui_outc
#define nemesis_ fbui_nemesis
uint8_t fight_tier; /* the boss's tier (harness) */
static const char *const STNAME[3] = {"THE HUM", "THE WALL", "THE DREAM"};
static uint8_t tier_of(const crucible_story *s) {
  uint8_t t;
  if (s->cycle) return 6;
  t = (uint8_t)(1u + (s->chapter >> 1));
  return t > 5u ? 5u : t;
}
void fbui_top(void) BANKED {
  char n[4];
  fb_state *b = FB;
  text_(0, 0, " ", 20, T_CREAM);
  text_(1, 0, who_, 7, T_BRASS);
  hp_bar(9, 0, (int8_t)b->hp, b->max);
  num_(n, b->hp, 2);
  text_(18, 0, n, 2, T_CREAM);
  n[0] = 'T';
  n[1] = (char)('0' + b->tier);
  n[2] = 0;
  text_(14, 2, n, 2, T_BRASS);
  text_(14, 3, b->phase >= 2u ? "!!" : b->phase ? "! " : "  ", 2, T_CREAM); /* its phase */
  if (b->shield != FR_NONE) {
    put_(17, 1, GLYPH('X'), T_CREAM);
    put_(18, 1, GLYPH(STANCE_CH[b->shield]), T_CREAM);
  } /* its shield */
}
/* the telegraph's marks by the "?" cell: the stance it shows (plain), x2, the charge */
static void boss_marks(void) {
  fb_state *b = FB;
  text_(19, 9, " ", 1, T_CREAM);
  text_(19, 10, " ", 1, T_CREAM);
  text_(19, 11, " ", 1, T_CREAM);
  if (fight_ui != UI_CHOOSE && fight_ui != UI_SHOW) return;
  if (b->kind == FB_CHARGE) {
    put_(19, 10, GLYPH('!'), T_CREAM);
    return;
  }
  if (!b->veiled && b->shown < 3u) put_(19, 10, GLYPH(STANCE_CH[b->shown]), T_CREAM);
  if (b->n > 1u) {
    put_(19, 11, GLYPH('2'), T_BRASS);
  }
}
static void boss_tell(void) {
  fb_state *b = FB;
  char s[40], n[14];
  const fr_side *p = &fr.s[0];
  uint8_t i;
  s[0] = 0;
  if (b->demo) strcpy(s, "SLOWLY: ");
  switch (b->kind) {
  case FB_STRIKE:
    if (b->veiled)
      strcat(s, "WHAT WILL IT MAKE?");
    else {
      strcat(s, "IT LEANS TO ");
      strcat(s, STNAME[b->shown]);
    }
    break;
  case FB_RECALL:
    strcat(s, "IT REMEMBERS ");
    strcat(s, STNAME[b->memory < 3u ? b->memory : 0u]);
    break;
  case FB_LOOP: strcat(s, "AGAIN AND AGAIN"); break;
  case FB_DOUBLE: strcat(s, "TWO AT ONCE"); break;
  case FB_FEINT:
    strcat(s, "IT SHIMMERS TWICE");
    fx_ |= FIGHT_FX_CHARGE;
    break;
  case FB_SHIELD:
    strcat(s, "A WALL AGAINST ");
    strcat(s, STNAME[b->shield < 3u ? b->shield : 0u]);
    break;
  case FB_COMPILE:
    strcat(s, "IT READS YOU: ");
    for (i = 0; i < p->nhist && i < 3u; i++) {
      n[0] = STANCE_CH[p->hist[p->nhist - 1u - i]];
      n[1] = ' ';
      n[2] = 0;
      strcat(s, n);
    }
    break;
  case FB_CHARGE:
    strcat(s, "IT GATHERS ITSELF");
    fx_ |= FIGHT_FX_CHARGE;
    break;
  case FB_STEAL:
    strcat(s, "IT WANTS ");
    if (p->nhand) {
      crucible_get_name(p->kit[p->hand[0]], n);
      strcat(s, n);
    }
    break;
  case FB_OVERHEAT:
    strcat(s, "THE AIR BURNS");
    fx_ |= FIGHT_FX_FLASH;
    break;
  case FB_HAUNT: strcat(s, "THE ? IS EMPTY"); break;
  case FB_POSSESS: strcat(s, "ONE OF YOURS FLICKERS"); break;
  case FB_MIRROR: strcat(s, "IT WEARS YOUR FACE"); break;
  case FB_ADAPT:
  case FB_PREDICT: strcat(s, b->kind == FB_PREDICT ? "ITS EYE MOVES FIRST" : "ITS EYE FOLLOWS YOU"); break;
  case FB_GROW: strcat(s, "IT GROWS GREEN"); break;
  case FB_JACK: strcat(s, "A CABLE REACHES YOUR HAND"); break;
  case FB_ERODE: strcat(s, "THE FLOOR CRUMBLES"); break;
  default: strcat(s, "IT COMES"); break;
  }
  if (b->demo) fx_ |= FIGHT_FX_SHAKE;
  say_text(s);
}
void fbui_round(void) BANKED { /* the turn starts: you draw; it plans its combination, a lookup a frame */
  fr_side *p = &fr.s[0];
  fr_draw();
  fr_pairs_all();
  ready_ = 1;
  while (FB->erode && p->nhand > 2u) {
    uint8_t s = p->hand[--p->nhand];
    p->disc[p->ndisc++] = s;
    p->dmask |= BIT8[s];
  } /* ERODE: two in the hand */
  fight_ui = UI_PLAN;
  npick_ = 0;
  used_ = 0xffu;
  fight_cursor = 0;
  flip_ = 0;
  fuse_ = 0xffu;
  hold_b_ = 0;
  picks_[0] = picks_[1] = FB_MISS;
  close_[0] = close_[1] = 0;
  veto_t_ = 0;
  if (link_scene_shared()) { /* a shared boss (9.4): the shared HP once, then who answers this round */
    if (!shared_hp_ && ls_role == LS_OWNER) {
      uint8_t h = (uint8_t)(((uint8_t)(ls_word(12) >> 8) + ls_their_hp) >> 1) + 2u;
      p->hp = (int8_t)h;
      p->max = h;
    }
    shared_hp_ = 1;
    link_scene_round((uint8_t)fr.turn);
  }
  fbui_top();
  me_block();
  hand_draw();
  text_(0, 17, " ", 20, T_CREAM);
}
void fbui_ready(void) BANKED { /* planned: its move, the tell, the pips */
  fb_round();
  pips_ = (uint8_t)(FB->pips - (FB->phase >= 2u && FB->pips > 1u ? 1u : 0u));
  pip_t_ = 0;
  fight_ui = UI_CHOOSE;
  boss_tell();
  fbui_top();
  boss_marks();
  me_block();
  hand_draw();
  answer_line();
  fx_ |= FIGHT_FX_CHARGE;
  if (link_scene_shared()) {
    char l[20];
    crucible_text_line(link_scene_mine() ? EV_ANSWER_FIRST : EV_WATCH_FIRST, l, sizeof l, 0);
    status(l, link_scene_mine() ? T_CREAM : T_BRASS);
  } /* (the line bank: crucible_lines) */
}
/* Its hand: the two halves of a recipe for each of two things of its faction's kind (depth within reach of the chapter),
 * found from a seeded start in its category; so its attacks are real combinations. The favourites the core gave fill
 * in when no recipe turns up. */
uint16_t cri_shelf(crucible_core *c, uint16_t pos) BANKED;
uint8_t cri_depth(crucible_core *c, uint16_t id) BANKED;
uint16_t cri_route_first(crucible_core *c, uint16_t id) BANKED;
uint16_t cri_route(crucible_core *c, uint16_t at) BANKED;
uint16_t cri_recipe_a(crucible_core *c, uint16_t row) BANKED;
uint16_t cri_recipe_b(crucible_core *c, uint16_t row) BANKED;
static const uint8_t LIKE_CAT[6] = {5u, 3u, 2u, 1u, 4u, 6u};
static void boss_hand(uint8_t f, uint8_t cap, uint16_t seed) {
  uint8_t cat = LIKE_CAT[f], got = 0, d;
  uint16_t first = core.cat_first[cat], end = core.cat_first[cat + 1u], at, id, n, r0, row;
  if (end <= first) return;
  at = (uint16_t)(first + seed % (uint16_t)(end - first)); /* from the run's seed: the same story makes the same boss */
  for (n = 0; n < 400u && got < 4u; n++) {
    id = cri_shelf(&core, at);
    if (++at >= end) at = first;
    d = cri_depth(&core, id);
    if (!d || d > cap) continue;
    r0 = cri_route_first(&core, id);
    if (r0 == cri_route_first(&core, (uint16_t)(id + 1u))) continue;
    row = cri_route(&core, r0);
    b_->hand[got++] = cri_recipe_a(&core, row);
    b_->hand[got++] = cri_recipe_b(&core, row);
  }
}
void fbui_open(uint8_t f, uint8_t level, uint8_t nemesis) BANKED {
  crucible_story *s = talk_saga();
  static crucible_boss local;
  uint16_t kit[6];
  uint8_t pas[2], hp, mem = FR_NONE;
  fight_kind = FK_BOSS;
  faction_ = f < 6u ? f : 0u;
  strcpy(who_, FNAME[faction_]);
  nemesis_ = nemesis;
  fight_t0 = sys_time;
  scene_draw(SCENE_RECORDS);
  clear_rows(0, 17);
  b_ = nemesis ? &s->nemesis : &local;
  if (!nemesis) memset(&local, 0, sizeof local);
  cru_boss_begin(&core, s, b_, faction_, level); /* its meeting count, the story's event, its favourites */
  boss_hand(faction_, (uint8_t)(level + 3u), (uint16_t)s->seed);
  fight_seed = (uint16_t)((uint16_t)s->seed ^ ((uint16_t)faction_ * 0x3a1u) ^ 0xb055u);
  fr_begin(fight_seed);
  fr.arena = arena_now();
  fight_tier = tier_of(s);
  (void)player_kit(kit);
  pas[0] = equip(0);
  pas[1] = player_slots() >= 2u ? equip(1) : FP_NONE;
  hp = (uint8_t)(12u + player_attr(PA_GRIT));
  if (s->scale == CRU_STORY_GENTLE)
    hp = (uint8_t)(hp + 2u);
  else if (s->scale == CRU_STORY_HARSH)
    hp = (uint8_t)(hp - 2u);
  fr_side_init(0, kit, 6, pas, hp, player_attr(PA_FOCUS));
  if (nemesis && b_->met >= 2u && (pl.nemesis[0] & 15u) < 3u)
    mem = (uint8_t)(pl.nemesis[0] & 15u); /* it remembers how you beat it */
  fb_begin(faction_, fight_tier, mem, s->scale == CRU_STORY_GENTLE ? 1 : s->scale == CRU_STORY_HARSH ? -1 : 0);
  won_turns_ = lost_turns_ = 0;
  xp_ = 0;
  gained_ = 0;
  fight_end = 0;
  outc_ = 0;
  adapted_ = 0;
  fight_ui = UI_INTRO;
  intro_t_ = 0;
  fx_ = FIGHT_FX_TEAR;
  avatar_bg_genome(pl.genome, 0, 2);
  draw_duel();
  fbui_top();
  put_(6, 10, GLYPH('+'), T_CREAM);
  put_(13, 10, GLYPH('='), T_CREAM);
  text_(12, 10, " ", 1, T_CREAM);
  if (mem != FR_NONE) {
    char l[30];
    strcpy(l, "IT REMEMBERS ");
    strcat(l, STNAME[mem]);
    say_text(l);
  } else if (FB->moves[0] == FB_LOOP) {
    char l[20];
    uint8_t i;
    strcpy(l, "IT COUNTS: ");
    for (i = 0; i < 3u; i++) {
      l[11u + i * 2u] = STANCE_CH[FB->loop[i]];
      l[12u + i * 2u] = ' ';
    }
    l[17] = 0;
    say_text(l);
  } /* the loop, told at the start */
  else
    say(SAY_TAUNT, CRU_NONE);
  avatar_make((uint16_t)(fight_seed ^ 0xb055u), faction_); /* its face last: the screen is up already */
  avatar_fx((uint8_t)(faction_ == 1u ? 3u : faction_ == 2u ? 2u : faction_ == 3u ? 5u : 4u));
  shared_hp_ = 0;
  {
    uint16_t w[LS_WORDS];
    uint8_t i;
    int8_t adj = s->scale == CRU_STORY_GENTLE  ? 1
                 : s->scale == CRU_STORY_HARSH ? -1
                                               : 0; /* a co-op partner may join (9.4) */
    w[0] = fight_seed;
    w[1] =
        (uint16_t)(faction_ | (fight_tier << 4) | ((uint16_t)(mem & 15u) << 8) | ((uint16_t)(uint8_t)(adj + 2) << 12));
    for (i = 0; i < 6u; i++) w[2u + i] = kit[i];
    for (i = 0; i < 4u; i++) w[8u + i] = b_->hand[i];
    w[12] = (uint16_t)((pas[0] & 15u) | ((pas[1] & 15u) << 4) | ((uint16_t)hp << 8));
    w[13] = (uint16_t)(player_attr(PA_FOCUS) | (fr.arena << 4) | ((uint16_t)level << 8));
    link_scene_owner(w);
  }
}
/* the partner's boss (9.4): the owner's setup, the same engine; this save is only a witness (its XP) */
void fbui_open_shared(void) BANKED {
  static crucible_boss local;
  uint16_t kit[6], w1 = ls_word(1), w12 = ls_word(12), w13 = ls_word(13);
  uint8_t pas[2], i, hp, mine;
  fight_kind = FK_BOSS;
  faction_ = (uint8_t)(w1 & 15u);
  if (faction_ > 5u) faction_ = 0;
  strcpy(who_, FNAME[faction_]);
  nemesis_ = 0;
  fight_t0 = sys_time;
  scene_draw(SCENE_RECORDS);
  clear_rows(0, 17);
  b_ = &local;
  memset(&local, 0, sizeof local);
  for (i = 0; i < 4u; i++) local.hand[i] = ls_word((uint8_t)(8u + i));
  for (i = 0; i < 6u; i++) kit[i] = ls_word((uint8_t)(2u + i));
  fight_seed = ls_word(0);
  fr_begin(fight_seed);
  fr.arena = (uint8_t)((w13 >> 4) & 15u);
  fight_tier = (uint8_t)((w1 >> 4) & 15u);
  pas[0] = (uint8_t)(w12 & 15u);
  pas[1] = (uint8_t)((w12 >> 4) & 15u);
  if (pas[0] == 15u) pas[0] = FP_NONE;
  if (pas[1] == 15u) pas[1] = FP_NONE; /* (15 on the wire: none) */
  mine = (uint8_t)(12u + player_attr(PA_GRIT));
  hp = (uint8_t)((((uint8_t)(w12 >> 8)) + mine) >> 1);
  hp = (uint8_t)(hp + 2u); /* the shared HP: both players' average + 2 */
  fr_side_init(0, kit, 6, pas, hp, (uint8_t)(w13 & 15u));
  fb_begin(faction_, fight_tier, (uint8_t)((w1 >> 8) & 15u) == 15u ? FR_NONE : (uint8_t)((w1 >> 8) & 15u),
           (int8_t)((w1 >> 12) & 15u) - 2);
  won_turns_ = lost_turns_ = 0;
  xp_ = 0;
  gained_ = 0;
  fight_end = 0;
  outc_ = 0;
  adapted_ = 0;
  shared_hp_ = 1;
  fight_ui = UI_INTRO;
  intro_t_ = 0;
  fx_ = FIGHT_FX_TEAR;
  avatar_bg_genome(pl.genome, 0, 2);
  draw_duel();
  fbui_top();
  put_(6, 10, GLYPH('+'), T_CREAM);
  put_(13, 10, GLYPH('='), T_CREAM);
  text_(12, 10, " ", 1, T_CREAM);
  {
    char l[32];
    crucible_text_line((uint16_t)(EV_PULLED_FIRST + (fight_seed & 1u)), l, sizeof l, 0);
    say_text(l);
  }
  avatar_make((uint16_t)(fight_seed ^ 0xb055u), faction_);
  avatar_fx((uint8_t)(faction_ == 1u ? 3u : faction_ == 2u ? 2u : faction_ == 3u ? 5u : 4u));
  link_scene_joined(mine);
}
/* A locks one answer (a DOUBLE takes two); the last pip makes it a close call (+1, through ARMOR) */
static void commit(void);
static void boss_lock(uint8_t act) {
  fb_state *b = FB;
  if (npick_ < 2u) {
    picks_[npick_] = act;
    close_[npick_] = pips_ <= 1u && FR_KIND(act) != FR_GUARD;
    npick_++;
  }
  sound_play(SFX_PICK);
  if (FR_KIND(act) == FR_PLAY) used_ = FR_A(act);
  if (b->kind != FB_CHARGE && npick_ < b->n && FR_KIND(act) != FR_FUSE) {
    fight_cursor = fight_cursor + 1u < fr.s[0].nhand ? (uint8_t)(fight_cursor + 1u) : 0u;
    hand_draw();
    status("AND THE SECOND?", T_CREAM);
    return;
  }
  fight_me = act;
  if (link_scene_shared())
    link_scene_send(picks_[0], picks_[1], (uint8_t)(close_[0] | (close_[1] << 1))); /* the partner resolves the same */
  commit();
}
static void commit(void) {
  fb_state *b = FB;
  {
    uint8_t kind = b->kind;
    outc_ = fb_resolve(picks_, close_);
    if (kind == FB_ADAPT && (outc_ & FBO_HURT)) adapted_ = 1;
  }
  if (outc_ & FBO_PHASE) link_scene_phase();
  {
    char s[20], n[3];
    uint8_t d = 0;
    show_t_ = 0;
    fight_ui = UI_SHOW;
    if (outc_ & FBO_HIT) {
      fx_ |= FIGHT_FX_SHAKE | FIGHT_FX_FLASH;
      sound_play(SFX_NEW);
      won_turns_++;
    }
    if (outc_ & FBO_HURT) {
      fx_ |= FIGHT_FX_TEAR;
      sound_play(SFX_DENY);
      lost_turns_++;
    }
    if (outc_ & FBO_FREE)
      strcpy(s, "A FREE BLOW");
    else if ((outc_ & (FBO_HIT | FBO_HURT)) == (FBO_HIT | FBO_HURT))
      strcpy(s, "BOTH LAND");
    else if (outc_ & FBO_HIT)
      strcpy(s, "IT BREAKS A LITTLE");
    else if (outc_ & FBO_STOLE)
      strcpy(s, "IT TOOK YOUR CARD");
    else if (outc_ & FBO_HURT)
      strcpy(s, "IT HITS");
    else if (outc_ & FBO_SHIELDED)
      strcpy(s, "IT WAS WALLED");
    else if (outc_ & FBO_TIE)
      strcpy(s, "EVEN");
    else if (outc_ & FBO_GUARD)
      strcpy(s, "GUARDED");
    else
      strcpy(s, "NOTHING");
    if (outc_ & FBO_PHASE) {
      fx_ |= FIGHT_FX_PHASE;
      strcpy(s, "IT CHANGES...");
      sound_play(SFX_FLASH);
    }
    status(s, T_CREAM);
    (void)n;
    (void)d;
  }
  fbui_top();
  boss_marks();
  me_block();
  hand_draw();
}
void fbui_choose(uint8_t pressed, uint8_t dt) BANKED {
  const fr_side *p = &fr.s[0];
  uint8_t n = p->nhand, act, held = joypad();
  if (link_scene_shared() &&
      !link_scene_mine()) { /* watching: the partner answers; SELECT points, holding B takes the round (once a phase) */
    uint8_t c;
    if (link_scene_take(&picks_[0], &picks_[1], &c)) {
      close_[0] = (uint8_t)(c & 1u);
      close_[1] = (uint8_t)((c >> 1) & 1u);
      npick_ = picks_[1] == FB_MISS ? 1u : 2u;
      fight_me = picks_[0];
      commit();
      return;
    }
    if (pressed & J_LEFT) {
      fight_cursor = fight_cursor ? (uint8_t)(fight_cursor - 1u) : (uint8_t)(n ? n - 1u : 0u);
      hand_draw();
    } else if (pressed & J_RIGHT) {
      fight_cursor = (uint8_t)(fight_cursor + 1u >= n ? 0u : fight_cursor + 1u);
      hand_draw();
    }
    if (pressed & J_SELECT) {
      link_scene_nudge(fight_cursor);
      sound_play(SFX_MOVE);
    } /* (with a move in the same loop too) */
    if (held & J_B) {
      if (!veto_t_) {
        veto_t_ = 1;
        veto_at_ = sys_time;
      } else if ((uint16_t)(sys_time - veto_at_) >= 60u && link_scene_veto()) {
        sound_play(SFX_SWAP);
        {
          char l[20];
          crucible_text_line(EV_TAKE_FIRST, l, sizeof l, 0);
          status(l, T_CREAM);
        }
        pip_t_ = 0;
      }
    } else
      veto_t_ = 0; /* a second by the clock (a loop can take many frames) */
    return;
  }
  if (link_scene_shared() && ls_nudge != 0xffu && ls_nudge < n) {
    char t[20];
    uint8_t k;
    crucible_text_line(EV_POINT_FIRST, t, 17, 0);
    k = (uint8_t)strlen(t);
    t[k] = ' ';
    t[k + 1u] = (char)('1' + ls_nudge);
    t[k + 2u] = 0;
    status(t, T_BRASS);
    ls_nudge = 0xffu;
  }
  if (pressed & J_LEFT) {
    fight_cursor = fight_cursor ? (uint8_t)(fight_cursor - 1u) : (uint8_t)(n ? n - 1u : 0u);
    sound_play(SFX_MOVE);
    hand_draw();
    answer_line();
  } else if (pressed & J_RIGHT) {
    fight_cursor = (uint8_t)(fight_cursor + 1u >= n ? 0u : fight_cursor + 1u);
    sound_play(SFX_MOVE);
    hand_draw();
    answer_line();
  } else if ((pressed & J_UP) && fight_cursor < n) {
    uint8_t st = p->kst[p->hand[fight_cursor]];
    if (st == 3u || st == 5u || st == 6u) {
      flip_ ^= BIT8[fight_cursor];
      sound_play(SFX_SWAP);
      hand_draw();
    }
  } else if ((pressed & J_DOWN) && fuse_ == 0xffu && n >= 2u && p->focus >= 2u && npick_ == 0u) {
    fuse_ = fight_cursor;
    partner_ = (uint8_t)(fuse_ + 1u >= n ? 0u : fuse_ + 1u);
    fight_cursor = partner_;
    hand_draw();
    answer_line();
  } else if ((pressed & (J_DOWN | J_B)) && fuse_ != 0xffu) {
    fuse_ = 0xffu;
    hand_draw();
    answer_line();
  } else if ((pressed & J_A) && fuse_ != 0xffu) {
    act = fuse_act();
    if (act != 0xffu) {
      fuse_ = 0xffu;
      boss_lock(act);
      return;
    }
    sound_play(SFX_DENY);
  } else if ((pressed & J_A) && fight_cursor < n && fight_cursor != used_) {
    act = FR_ACT(FR_PLAY, fight_cursor, 0, live_stance(fight_cursor));
    if (fr_legal(0, act)) {
      boss_lock(act);
      return;
    }
  } else if ((pressed & J_B) && fuse_ == 0xffu) {
    if (FB->kind == FB_CHARGE) {
      boss_lock(FB_MISS);
      return;
    }
    boss_lock(FR_GUARD_ACT);
    return;
  }
  (void)held;
  pip_t_ = (uint16_t)(pip_t_ + dt);
  if (pip_t_ >= PIP_FRAMES) {
    pip_t_ = 0;
    fx_ |= FIGHT_FX_CHARGE;
    if (pips_) {
      pips_--;
      me_block();
      sound_play(SFX_MOVE);
    }
    if (!pips_) {
      while (npick_ < FB->n || (FB->kind == FB_CHARGE && !npick_)) {
        picks_[npick_] = FB_MISS;
        npick_++;
        if (FB->kind == FB_CHARGE) break;
      }
      npick_ = 1;
      boss_lock(FB_MISS);
    } /* too late: what is left lands */
  }
}
/* the end of a boss (7.1): what it gives and costs; the nemesis remembers how it fell */
void fbui_end(uint8_t r) BANKED {
  crucible_story *s = talk_saga();
  fb_state *b = FB;
  uint16_t lost[3];
  uint8_t best = fb_best_stance(), how;
  if (ls_role == LS_WATCHER) { /* the partner's boss: this save takes only its XP (the story is the owner's) */
    xp_ = r == 1u ? (uint16_t)(30u * fight_tier) : 10u;
    if (r == 1u) {
      char l[22];
      crucible_text_line(EV_NOTYOURS_FIRST, l, sizeof l, 0);
      status(l, T_BRASS);
    } else
      status("IT WALKS THROUGH YOU", T_BRASS);
    end_line_ = r == 1u ? 1u : 2u;
    gained_ = player_xp(xp_);
    player_save();
    link_scene_end();
    fight_ui = UI_END;
    show_t_ = 0;
    fx_ |= FIGHT_FX_TEAR;
    return;
  }
  how = b->fused >= b->close && b->fused >= b->guards ? 0u : b->close >= b->guards ? 1u : 2u;
  if (best < 3u) {
    pl.nemesis[0] = (uint8_t)(best | (how << 4));
    pl.nemesis[1] = b_->met;
  }
  if (r == 1u) {
    xp_ = (uint16_t)(30u * fight_tier);
    if (nemesis_ && b_->met >= 3u) xp_ = (uint16_t)(60u * fight_tier);
    cru_story_shift(s, faction_, -10);
    cru_story_shift(s, cru_faction_rival(faction_), 10);
    if (faction_ == CRU_FAC_AI &&
        !adapted_) { /* the AI beaten three times without its ADAPT landing: its eye is yours (8.4) */
      uint8_t n = (uint8_t)(pl.secret[0] >> 6);
      if (n < 3u) n++;
      pl.secret[0] = (uint8_t)((pl.secret[0] & 0x3fu) | (uint8_t)(n << 6));
      if (n >= 3u) {
        player_unlock(5);
        pl.flags |= PF_AI_EYE;
      }
    }
    if (b_->stolen != CRU_NONE) {
      (void)cru_story_gain(&core, s, b_->stolen);
      b_->stolen = CRU_NONE;
    }
    cru_story_lucid(s, 24);
    cru_story_act(s, CRU_ACT_WIN);
    if (pl.bosses < 255u) pl.bosses++;
    b_->hp = 0;
    status("IT BREAKS APART", T_CREAM);
    say(SAY_HURT, CRU_NONE);
    fight_ui = UI_CHOICE;
    show_t_ = 0;
    text_(0, 15, " ", 20, T_CREAM);
    text_(0, 16, " ", 20, T_CREAM);
    {
      uint8_t deep = s->lucid < 64u && (cosm() & 1u);
      put_(2, 16, UI_A, T_CREAM);
      put_(3, 16, GLYPH(deep ? '-' : '+'), T_CREAM);
      put_(8, 16, UI_B, T_CREAM);
      put_(9, 16, GLYPH(deep ? '+' : '-'), T_CREAM);
      text_(14, 16, "SE", 2, T_BRASS);
      put_(16, 16, GLYPH('/'), T_CREAM);
    }
    sound_play(SFX_FLASH);
    fx_ |= FIGHT_FX_PHASE;
    end_line_ = 1;
    return;
  }
  if (r == 3u) {
    xp_ = 5;
    status("IT FLED. FOR NOW.", T_BRASS);
    sound_play(SFX_CLOSE);
    end_line_ = 3;
  } /* the nemesis, at its first phase */
  else { /* you fell: the run's scale decides what it takes */
    xp_ = 10;
    (void)cru_story_loss(&core, s, lost, 3);
    cru_story_lucid(s, -12);
    cru_story_act(s, CRU_ACT_HIT);
    status("IT WALKS THROUGH YOU", T_BRASS);
    end_line_ = 2;
  }
  cru_story_event(s, CRU_EV_FIGHT, b->hp, r);
  gained_ = player_xp(xp_);
  player_save();
  story_save();
  fight_ui = UI_END;
  show_t_ = 0;
  fx_ |= FIGHT_FX_TEAR;
}
/* the end choice (7.2): no labels. A takes one of its hand (the heart darkens), B lets it fade (the heart lightens,
 * its faction hates you less), SELECT unmakes its attack into its two halves (order loosens) */
uint8_t fbui_choice(uint8_t pressed) BANKED {
  crucible_story *s = talk_saga();
  uint8_t *k = s->flags + 16u, i;
  if (!(pressed & (J_A | J_B | J_SELECT))) return 0;
  if (s->lucid < 64u && (cosm() & 3u) == 0u)
    pressed = (pressed & J_A) ? J_B : (pressed & J_B) ? J_A : pressed; /* the dream misreads */
  if (pressed & J_A) {
    for (i = 0; i < 4u; i++)
      if (!cru_owned(&core, b_->hand[i]) && cru_story_gain(&core, s, b_->hand[i])) break;
    kdc_nudge(k, 1, -3);
    cru_story_shift(s, faction_, -6);
    cru_story_act(s, CRU_ACT_TAKE);
    pl.secret[0] = (uint8_t)((pl.secret[0] & ~0x38u) |
                             ((((pl.secret[0] >> 3) & 7u) < 7u ? ((pl.secret[0] >> 3) & 7u) + 1u : 7u) << 3));
    if (((pl.secret[0] >> 3) & 7u) >= 3u) player_unlock(39); /* horns: you took three */
    say_text("YOU KEEP A PIECE OF IT");
  } else if (pressed & J_B) {
    kdc_nudge(k, 1, 3);
    cru_story_shift(s, faction_, 4);
    cru_story_lucid(s, 8);
    cru_story_act(s, CRU_ACT_SPARE);
    pl.secret[0] = (uint8_t)((pl.secret[0] & ~7u) | ((pl.secret[0] & 7u) < 7u ? (pl.secret[0] & 7u) + 1u : 7u));
    if ((pl.secret[0] & 7u) >= 3u) player_unlock(38); /* halo: you let three go */
    say_text("IT FADES. IT REMEMBERS");
  } else {
    (void)cru_story_gain(&core, s, FB->a);
    (void)cru_story_gain(&core, s, FB->b);
    kdc_nudge(k, 0, -3);
    cru_story_lucid(s, -8);
    say_text("IT COMES APART IN YOUR HANDS");
  }
  /* how you fought leans order quietly (2 at most): patterns kept, or tricks read */
  if (fr.s[0].act & 0x0801u) kdc_nudge(k, 0, 1);
  if (FB->seen[0] & 0x08u) kdc_nudge(k, 0, -1);
  sound_play(SFX_NEW);
  gained_ = player_xp(xp_);
  player_save();
  story_save();
  fight_ui = UI_END;
  show_t_ = 0;
  fx_ |= FIGHT_FX_TEAR;
  return 1;
}
