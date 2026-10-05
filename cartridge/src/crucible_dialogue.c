/* Semantic request selection, the alignment matrix and persisted bounded
 * variation. Rendering and typewriter RNG never change these choices. The small
 * tables live here so ASK needs no scan through the banked narrative metadata;
 * reply and hint prose lives in crucible_dialogue_text.c's own bank. Nothing
 * here names the player's leaning: it shows only in what is offered, how the
 * speakers answer, type and remember, and in rare context-triggered glitches. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_data.h"
#include "crucible_state.h"
#include "crucible_player.h"
#include "crucible_codec.h"
#include "crucible_storyrun.h"
#include "keel_dialogue_choice.h"
#include "crucible_dialogue.h"
#include "crucible_dialogue_data.h"
#define STATE(s) ((s)->flags + 16u)
/* SRAM bank 2 BD90..BDC7: below the v3 migration area at BE00, above
 * the scene-palette lease at BD80..BD88. Never part of a story slot. */
#define RECORD_AT 0x5d90ul
static kdc_choice_t choices_[3];
static uint8_t request_, compatible_, cue_, cursor_ = '>';
static uint16_t offer_, roll_;
static void records(uint8_t *a, uint8_t *b) {
  uint8_t i;
  for (i = 0; i < KDC_RECORD_BYTES; i++) {
    a[i] = crucible_sram_read(0, RECORD_AT + i);
    b[i] = crucible_sram_read(0, RECORD_AT + KDC_RECORD_BYTES + i);
  }
  SWITCH_RAM(0);
  DISABLE_RAM;
}
void dialogue_reset_free(void) BANKED {
  crucible_sram_write(0, RECORD_AT + 26u, 0);
  crucible_sram_write(0, RECORD_AT + KDC_RECORD_BYTES + 26u, 0);
  SWITCH_RAM(0);
  DISABLE_RAM;
}
void dialogue_init(crucible_story *s) BANKED {
  if (!story_on) {
    uint8_t a[KDC_RECORD_BYTES], b[KDC_RECORD_BYTES], at;
    records(a, b);
    at = kdc_record_latest(a, b, core.variant_seed);
    if (at != 255u) memcpy(STATE(s), (at ? b : a) + 8, KDC_STATE_BYTES);
  }
  kdc_init(STATE(s));
}
void dialogue_save(crucible_story *s) BANKED {
  uint8_t a[KDC_RECORD_BYTES], b[KDC_RECORD_BYTES], at, i;
  uint16_t serial = 0;
  uint32_t address;
  if (story_on) {
    story_save();
    return;
  }
  records(a, b);
  at = kdc_record_latest(a, b, core.variant_seed);
  if (at != 255u) serial = (uint16_t)(kdc_u16((at ? b : a) + 4) + 1u);
  address = RECORD_AT + (at == 0u ? KDC_RECORD_BYTES : 0u);
  kdc_record_encode(a, STATE(s), core.variant_seed, serial);
  crucible_sram_write(0, address + 26u, 0);
  for (i = 0; i < KDC_RECORD_BYTES; i++)
    if (i != 26u) crucible_sram_write(0, address + i, a[i]);
  crucible_sram_write(0, address + 26u, a[26]);
  SWITCH_RAM(0);
  DISABLE_RAM;
}
uint16_t dialogue_open(crucible_story *s, uint16_t context) BANKED {
  return kdc_open(STATE(s), core.variant_seed, context);
}
/* bits 0..1 of state byte 6; the rest holds the last act */
uint8_t dialogue_cadence(crucible_story *s) BANKED {
  uint8_t *v = STATE(s) + 6, n;
  kdc_init(STATE(s));
  n = (uint8_t)((*v & 3u) + 1u);
  if (n < 3u) {
    *v = (uint8_t)((*v & 0xfcu) | n);
    return 0;
  }
  *v &= 0xfcu;
  return 1;
}
uint16_t dialogue_ask(uint8_t who, uint16_t *r) BANKED {
  uint8_t i, n = 0, k;
  for (i = 0; i < DIALOGUE_REQUEST_COUNT; i++)
    if (!dialogue_request_who[i] || dialogue_request_who[i] == who) n++;
  k = (uint8_t)(((uint16_t)(uint8_t)kdc_roll(r) * n) >> 8);
  for (i = 0; i < DIALOGUE_REQUEST_COUNT; i++)
    if (!dialogue_request_who[i] || dialogue_request_who[i] == who) {
      if (!k) {
        request_ = i;
        return dialogue_requests[i].line;
      }
      k--;
    }
  request_ = 0;
  return dialogue_requests[0].line;
}
static uint8_t deep_lean(const uint8_t *st) {
  int8_t o = kdc_get(st, 0), h = kdc_get(st, 1);
  return kdc_habit(st) >= 10u || o >= 48 || o <= -48 || h >= 48 || h <= -48;
}
void dialogue_prepare(crucible_story *s, uint8_t who, uint16_t asked, uint16_t offer, uint16_t tag,
                      uint16_t *r) BANKED {
  const kdc_request_t *q = dialogue_requests + request_;
  uint8_t *st = STATE(s);
  kdc_context_t c;
  offer_ = offer;
  kdc_sync(st, s->chapter, s->cycle);
  compatible_ = cru_owned(&core, offer) && kdc_matches(q, offer, asked, crucible_category(offer),
                                                       crucible_category(asked), crucible_traits(offer), tag);
  /* GIVE and BARTER promise an actual transfer: keep the existing faction
  * acceptance rule. SHOW is a separate act and never transfers the item. */
  if (q->fulfill == KDC_GIVE && cru_affinity(&core, (uint8_t)(who - 1u), offer) <= 0) compatible_ = 0;
  /* the frozen seed also carries the speaker and the request */
  *r ^= (uint16_t)(((uint16_t)who << 12) ^ q->key);
  if (!*r) *r = 0xace1u;
  c.who = who;
  c.disposition = dialogue_disposition[who];
  c.fulfill = q->fulfill;
  c.compatible = compatible_;
  c.lucid = s->lucid;
  c.aware = (uint8_t)(s->chapter || s->cycle);
  kdc_choices(choices_, dialogue_actions, dialogue_variants, DIALOGUE_VARIANT_COUNT, &c, st, r);
  roll_ = kdc_roll(r);
  /* a deep lean, now and then: the option cursor is not quite itself */
  cursor_ = deep_lean(st) && !(kdc_roll(r) % 10u) ? ')' : '>';
}
void dialogue_label(uint8_t i, char *out) BANKED { dialogue_label_text(choices_[i].variant, out); }
uint8_t dialogue_cursor(void) BANKED { return cursor_; }
uint8_t dialogue_cue(void) BANKED {
  uint8_t c = cue_;
  cue_ = 0;
  return c;
}
static void stand(crucible_story *s, uint8_t f, int8_t d) {
  int16_t v = (int16_t)s->stand[f] + d;
  s->stand[f] = (int8_t)(v > 100 ? 100 : v < -100 ? -100 : v);
}
/* A changed cell leans the truth matrix toward its corner: order loops,
 * disorder deepens the dream, care gives, harm hits. */
static void lean_cell(crucible_story *s, uint8_t cell) {
  if (KDC_O3[cell] == 0u)
    cru_story_act(s, CRU_ACT_LOOP);
  else if (KDC_O3[cell] == 2u)
    cru_story_act(s, CRU_ACT_DEEP);
  if (KDC_H3[cell] == 0u)
    cru_story_act(s, CRU_ACT_GIFT);
  else if (KDC_H3[cell] == 2u)
    cru_story_act(s, CRU_ACT_HIT);
}
uint8_t dialogue_answer(crucible_story *s, uint8_t who, uint8_t choice, const char *const *slots, char *out) BANKED {
  uint8_t *st = STATE(s), f = (uint8_t)(who - 1u), action = choice < 3u ? choices_[choice].action : KDC_REFUSE,
          r = CRU_REACT_COLD, cls, att, before, after, turned = 0, last_who, last, deep, say, special, tag;
  int8_t extra;
  uint16_t key = choice < 3u ? dialogue_variants[choices_[choice].variant].key : dialogue_requests[request_].key,
           roll = roll_;
  const dialogue_effect_t *e;
  kdc_init(st);
  last_who = kdc_last_who(st);
  last = kdc_last_action(st);
  before = kdc_cell(st);
  if ((action == KDC_GIVE || action == KDC_BARTER) && (!compatible_ || !cru_owned(&core, offer_))) {
    action = KDC_DEFER;
    compatible_ = 0;
  }
  if (action == KDC_SHOW && !compatible_) action = KDC_DEFER;
  e = dialogue_effects + action;
  cls = action;
  say = e->say;
  special = e->special;
  extra = e->stand;
  if (special == DIALOGUE_SPECIAL_DECEIVE &&
      (cru_story_tier(s, f) <= CRU_TIER_WARY || !(roll & (s->lucid >= 128u ? 1u : 3u)))) {
    say = CRU_SAY_REFUSE;
    extra = -4;
    cls = DIALOGUE_CLASS_CAUGHT;
    turned = 1;
  }
  if (special == DIALOGUE_SPECIAL_RIDDLE) extra = KDC_O3[dialogue_disposition[who]] == 2u ? 4 : -2;
  if (special == DIALOGUE_SPECIAL_NONSENSE) extra = (roll >> 3) & 1u ? 5 : -5;
  if (say != 0xffu)
    r = cru_story_choose(&core, s, f, say, say == CRU_SAY_OFFER ? offer_ : CRU_NONE);
  else
    cru_story_event(s, CRU_EV_CHOICE, (uint16_t)((uint16_t)f << 4 | action), CRU_NONE);
  if (special == DIALOGUE_SPECIAL_CLUE) {
    cru_story_clue(s, (uint8_t)((roll >> 5) % 3u));
    (void)player_xp(10);
  }
  if (special == DIALOGUE_SPECIAL_RECLAIM) {
    uint8_t back;
    uint16_t id;
    for (back = 0; back < CRU_STORY_MEMORY; back++) {
      id = cru_story_recall(s, back);
      if (id != CRU_NONE && !cru_owned(&core, id) && cru_story_gain(&core, s, id)) {
        cls = DIALOGUE_CLASS_PAID;
        turned = 1;
        break;
      }
    }
  }
  if (e->lucid) cru_story_lucid(s, e->lucid);
  if (e->act != 0xffu) cru_story_act(s, e->act);
  kdc_commit(st, dialogue_actions, action, key, compatible_);
  after = kdc_cell(st);
  if (after != before) lean_cell(s, after);
  /* how they take it: their leaning against yours, then what you did to them last time */
  att = kdc_attitude(after, dialogue_disposition[who]);
  cue_ = 0;
  if (last_who == who && last != 255u) {
    if (last == KDC_DECEIVE || last == KDC_MOCK || last == KDC_THREATEN || last == KDC_STEAL ||
        (last == KDC_DEFER && (action == KDC_DEFER || action == KDC_REFUSE))) {
      if (att < KDC_COLD) att++;
    } else if ((last == KDC_GIVE || last == KDC_BLESS || last == KDC_BARTER) && att > KDC_WARM)
      att--;
    if (last == KDC_DEFER && (action == KDC_DEFER || action == KDC_REFUSE))
      cue_ = 2; /* a broken promise: the reply stutters */
  }
  extra = (int8_t)(extra + (att == KDC_WARM ? 2 : att == KDC_COLD ? -2 : 0));
  if (extra) stand(s, f, extra);
  if (say == 0xffu) r = extra > 0 ? CRU_REACT_WARM : CRU_REACT_COLD;
  /* chaotic answers sometimes ripple through the room (rare and easy to miss) */
  if (!cue_ && (dialogue_actions[action].flags & KDC_F_SURREAL || action == KDC_DECEIVE || action == KDC_MOCK)) {
    if (action == KDC_NONSENSE && !((roll >> 7) & 7u))
      cue_ = 4;
    else if (!((roll >> 9) % 6u))
      cue_ = 1;
  }
  deep = (s->lucid < 48u && kdc_habit(st) >= 12u) ? 2u : (s->lucid < 96u || kdc_habit(st) >= 8u) ? 1u : 0u;
  tag = dialogue_reply_text(cls, who, att, deep, st[2], &roll, slots, out);
  kdc_remember(st, who, action, turned, tag);
  dialogue_save(s);
  return r;
}
/* The greeting answers to what you did last time: that faction remembers
 * (3 in 4), and surreal acts travel to anyone (1 in 3). Frozen-seed draw. */
uint8_t dialogue_hint(crucible_story *s, uint8_t who, uint16_t r, const char *const *slots, char *out) BANKED {
  uint8_t *st = STATE(s), last, after, same;
  kdc_init(st);
  last = kdc_last_action(st);
  if (last == 255u) return 0;
  same = kdc_last_who(st) == who;
  if (same ? !(r & 3u) : (r % 3u) != 0u) return 0;
  after = kdc_last_turned(st) ? (last == KDC_DECEIVE    ? DIALOGUE_CLASS_CAUGHT
                                 : last == KDC_THREATEN ? DIALOGUE_CLASS_PAID
                                                        : last)
                              : last;
  return dialogue_hint_text(after, same, (uint16_t)(r >> 2), slots, out);
}
/* How a speaker types (never a label): bits 0..1 its own order (0 steady ..
 * 2 restless), 2..3 its attitude to the player, 4 the dream runs deep. */
uint8_t dialogue_temper(crucible_story *s, uint8_t who) BANKED {
  uint8_t *st = STATE(s), own = who < 7u ? dialogue_disposition[who] : KDC_TN;
  kdc_init(st);
  return (uint8_t)(KDC_O3[own] | (kdc_attitude(kdc_cell(st), own) << 2) |
                   ((s->lucid < 64u || kdc_habit(st) >= 10u) ? 16u : 0u));
}
/* Quarter-frames per letter: most speakers are quick; one program in four,
 * and one ghost in four, is a deliberately slow, eerie typer. */
static const uint8_t TYPE_Q[7] = {6, 4, 3, 7, 5, 4, 6};
uint8_t dialogue_type_base(uint8_t who, uint16_t r) BANKED {
  if (who > 6u) who = 0;
  if (who == 1u && !(r & 3u)) return (uint8_t)(18u + ((r >> 2) & 3u));
  if (who == 3u && !(r & 3u)) return (uint8_t)(14u + ((r >> 2) & 1u));
  return (uint8_t)(TYPE_Q[who] + ((r >> 4) & 1u));
}
uint8_t dialogue_type_cost(uint8_t temper, uint8_t base, char c, uint8_t n, uint16_t fx) BANKED {
  uint8_t q = base, order = temper & 3u, att = (temper >> 2) & 3u;
  if (att == KDC_COLD && q > 2u) q--; /* clipped */
  if (order == 1u)
    q = (uint8_t)(q + (fx & 1u)); /* a little unevenness */
  else if (order == 2u) {
    q = (n & 4u) ? (uint8_t)(q >> 1) : (uint8_t)(q + 1u);
    if (!(fx & 15u)) q = (uint8_t)(q + 8u);
  } /* bursts, and a hitch */
  if (temper & 16u) {
    uint8_t m = (uint8_t)(((n >> 3) * 7u + base) & 3u);
    if (!m)
      q = (uint8_t)(q << 1);
    else if (m == 1u)
      q = (uint8_t)(q >> 1);
  } /* the dream changes gear */
  if (c == '.' || c == '?' || c == '!')
    q = (uint8_t)(q + 8u);
  else if (c == ',' || c == ':')
    q = (uint8_t)(q + 4u);
  return q < 2u ? 2u : q;
}
