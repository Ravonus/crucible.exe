/* The saga layer of a story run: factions that like or hate you by what you make and say, the dream that bends what
 * the program tells you, and bosses that fight with combinations.
 *
 * Factions. The machine's people fall in six archetypes, each drawn to a category and some traits and repelled by
 * others. Everything you make or keep is noticed: standing drifts toward the factions your collection leans to, so
 * leaning into GHOST things makes the ghosts warm to you (and the relics, their rivals, cool) without a word said.
 * Replies in conversations move it faster; a gift that matches their taste moves it most.
 *
 * The dream. lucid (255 clear .. 0 deep) falls as the run goes deeper and with glitches, rises with splits (going back
 * to what is real) and memories. A line's {ITEM} is the real element while lucid; deeper, it drifts to something
 * related by the recipes (what it is made of, what it makes, a cousin with a shared trait, a thing you made before),
 * so the program still makes sense, just not always the sense you expect.
 *
 * Bosses. A hostile faction's champion holds a hand of its favourite elements and combines them, with the real
 * recipes, into attacks it telegraphs a round or two ahead; you answer with an element whose traits beat the attack's.
 * Its results join its hand, it steals what it beats, and the nemesis flees at half strength to come back later. */
#include "cru_internal.h"

#define T_HOT 0x001u
#define T_COLD 0x002u
#define T_WET 0x004u
#define T_AIRY 0x008u
#define T_STONE 0x010u
#define T_SHINY 0x020u
#define T_GLOWS 0x040u
#define T_ALIVE 0x080u
#define T_GREEN 0x100u
#define T_MADE 0x200u
#define T_BIG 0x400u
#define T_MAGIC 0x800u
/* ELEMENT MATTER WEATHER ENERGY LIFE CRAFT PLACE */
static const uint8_t LIKE_CAT[CRU_FACTIONS] = {5u, 3u, 2u, 1u, 4u, 6u};
static const uint16_t LIKE[CRU_FACTIONS] = {T_MADE | T_GLOWS | T_SHINY, T_HOT | T_BIG | T_STONE,
                                            T_AIRY | T_COLD | T_MAGIC,  T_STONE | T_SHINY | T_COLD,
                                            T_ALIVE | T_GREEN | T_WET,  T_STONE | T_MAGIC | T_MADE};
static const uint16_t FEAR[CRU_FACTIONS] = {T_MAGIC | T_WET,   T_COLD | T_GREEN, T_HOT | T_GLOWS,
                                            T_ALIVE | T_GREEN, T_SHINY | T_MADE, T_HOT | T_AIRY};
static const uint8_t RIVAL[CRU_FACTIONS] = {CRU_FAC_DAEMON,   CRU_FAC_PROGRAM, CRU_FAC_RELIC,
                                            CRU_FAC_OPERATOR, CRU_FAC_AI,      CRU_FAC_GHOST};
/* what beats each trait: water and cold put out heat, stone and weight stop air, rust and magic undo the made... */
#ifndef CRU_HAND_FIGHT
static const uint16_t BEATS[CRU_TRAITS] = {T_WET | T_COLD, /* HOT */
                                           T_HOT | T_GLOWS, /* COLD */
                                           T_COLD | T_GREEN, /* WET: frozen, drunk */
                                           T_STONE | T_BIG, /* AIRY */
                                           T_WET | T_GREEN, /* STONE: worn, split by roots */
                                           T_STONE | T_WET, /* SHINY: scratched, rusted */
                                           T_BIG | T_STONE, /* GLOWS: shadowed */
                                           T_HOT | T_MAGIC, /* ALIVE */
                                           T_HOT | T_COLD, /* GREEN */
                                           T_MAGIC | T_WET, /* MADE */
                                           T_MADE | T_MAGIC, /* BIG: levers, spells */
                                           T_MADE | T_GLOWS}; /* MAGIC: iron and light */
#endif

static uint8_t bits(uint16_t m) {
  uint8_t n = 0;
  while (m) {
    n = (uint8_t)(n + (m & 1u));
    m >>= 1;
  }
  return n;
}
static int8_t clamp(int16_t v) { return (int8_t)(v > 100 ? 100 : v < -100 ? -100 : v); }

/* ---- factions ---- */
int8_t cru_affinity(crucible_core *c, uint8_t f, uint16_t id) CORE_BANKED {
  uint16_t t;
  int8_t a = 0;
  if (f >= CRU_FACTIONS || id >= c->items) return 0;
  t = cri_traits(c, id);
  if (cri_category(c, id) == LIKE_CAT[f]) a = 2;
  a = (int8_t)(a + bits(t & LIKE[f]) - bits(t & FEAR[f]));
  return a;
}
static uint8_t tier(int8_t v) {
  return v <= -50   ? CRU_TIER_HOSTILE
         : v <= -15 ? CRU_TIER_WARY
         : v >= 50  ? CRU_TIER_ALLY
         : v >= 15  ? CRU_TIER_FRIEND
                    : CRU_TIER_NEUTRAL;
}
uint8_t cru_story_tier(const crucible_story *s, uint8_t f) CORE_BANKED {
  return f < CRU_FACTIONS ? tier(s->stand[f]) : CRU_TIER_NEUTRAL;
}
static uint8_t shift(crucible_story *s, uint8_t f, int8_t d) { /* 1 if the tier changed */
  uint8_t was = tier(s->stand[f]);
  s->stand[f] = clamp((int16_t)s->stand[f] + d);
  return was != tier(s->stand[f]);
}
uint8_t cru_story_shift(crucible_story *s, uint8_t f, int8_t d) CORE_BANKED {
  return f < CRU_FACTIONS ? shift(s, f, d) : 0;
}
uint8_t cru_faction_rival(uint8_t f) CORE_BANKED { return f < CRU_FACTIONS ? RIVAL[f] : 0; }
uint8_t cru_story_notice(crucible_core *c, crucible_story *s, uint16_t id) CORE_BANKED {
  uint8_t f, changed = 0;
  int8_t a;
  if (id >= c->items) return 0;
  cru_story_remember(s, id);
  s->fails = 0; /* a real make breaks the chain of misses */
  cru_story_lean(c, s, id);
  if (cru_story_recall(s, 1) == id) cru_story_act(s, CRU_ACT_LOOP);
  if (s->lucid < 64u) cru_story_act(s, CRU_ACT_DEEP);
  for (f = 0; f < CRU_FACTIONS; f++) {
    a = cru_affinity(c, f, id);
    if (a > -2 && a < 2) continue; /* only a clear lean is noticed */
    if ((a > 0 && s->stand[f] >= 30) || (a < 0 && s->stand[f] <= -30))
      a = (int8_t)(a >> 1); /* drift slows near the ends */
    if (shift(s, f, a)) changed |= (uint8_t)(1u << f);
  }
  return changed;
}
static uint16_t favourite(crucible_core *c, crucible_story *s, uint8_t f, uint8_t level);
/* a faction pays back: one of its favourites you do not hold yet (or any of them) */
static void repay(crucible_core *c, crucible_story *s, uint8_t f) {
  uint8_t k;
  uint16_t id = CRU_NONE;
  for (k = 0; k < 8u; k++) {
    id = favourite(c, s, f, (uint8_t)(s->chapter + 1u));
    if (!cri_owned(c, id)) break;
  }
  if (id != CRU_NONE && cru_story_gain(c, s, id)) s->gift = id;
}
uint8_t cru_story_choose(crucible_core *c, crucible_story *s, uint8_t f, uint8_t say, uint16_t offer) CORE_BANKED {
  int8_t a;
  uint8_t react;
  s->gift = CRU_NONE;
  if (f >= CRU_FACTIONS) return CRU_REACT_COLD;
  cru_story_event(s, CRU_EV_CHOICE, (uint16_t)(f << 4 | say), offer);
  cru_story_act(s, say == CRU_SAY_AGREE ? CRU_ACT_AGREE : say == CRU_SAY_REFUSE ? CRU_ACT_REFUSE : CRU_ACT_GIFT);
  if (say == CRU_SAY_AGREE) {
    shift(s, f, 6);
    shift(s, RIVAL[f], -3);
    if (cru_story_tier(s, f) >= CRU_TIER_FRIEND && !cri_story_pick(s, 4u)) repay(c, s, f);
    return CRU_REACT_WARM;
  }
  if (say == CRU_SAY_REFUSE) {
    shift(s, f, -6);
    shift(s, RIVAL[f], 2);
    return CRU_REACT_COLD;
  }
  if (offer >= c->items || !cru_owned(c, offer)) return CRU_REACT_COLD;
  a = cru_affinity(c, f, offer);
  if (a >= 3) {
    shift(s, f, 14);
    shift(s, RIVAL[f], -4);
    react = CRU_REACT_LOVE;
  } else if (a > 0) {
    shift(s, f, 5);
    react = CRU_REACT_WARM;
  } else {
    shift(s, f, (int8_t)(a < 0 ? -10 : -3));
    cru_story_lucid(s, -8);
    return a < 0 ? CRU_REACT_HATE : CRU_REACT_COLD;
  }
  cru_drop_any(c, offer); /* they keep a gift they like */
  cru_story_remember(s, offer);
  if (react == CRU_REACT_LOVE || !cri_story_pick(s, 3u)) repay(c, s, f); /* and often give something back */
  return react;
}
uint8_t cru_story_ending(const crucible_story *s) CORE_BANKED {
  uint8_t f, best = 0xffu, friends = 0;
  int8_t top = 49;
  for (f = 0; f < CRU_FACTIONS; f++) {
    if (s->stand[f] >= 15) friends++;
    if (s->stand[f] >= 50 && s->stand[RIVAL[f]] >= 50) return CRU_END_SCHISM;
    if (s->stand[f] > top) {
      top = s->stand[f];
      best = f;
    }
  }
  if (friends == CRU_FACTIONS) return CRU_END_WAKE;
  return best == 0xffu ? CRU_END_LOOP : (uint8_t)(1u + best);
}

/* ---- misses: the chain that destroys ---- */
static const uint8_t FAIL_STEP[3] = {16u, 32u, 56u}; /* Gentle, Normal, Harsh: how much each miss in a row adds */
uint8_t cru_story_fail_odds(const crucible_story *s) CORE_BANKED {
  uint16_t o = 128u;
  uint8_t k, step = FAIL_STEP[s->scale > 2u ? 1u : s->scale];
  for (k = 0; k < s->fails && o < 245u; k++) o = (uint16_t)(o + step); /* no multiply on the SM83 */
  return (uint8_t)(o > 245u ? 245u : o);
}
uint8_t cru_story_fail(crucible_core *c, crucible_story *s, uint16_t a, uint16_t b, uint16_t *lost) CORE_BANKED {
  uint8_t odds = cru_story_fail_odds(s);
  uint16_t id;
  if (lost) *lost = CRU_NONE;
  if (s->fails < 15u) s->fails++;
  cru_story_lucid(s, -4);
  cru_story_event(s, CRU_EV_LOSS, a, b);
  if (cri_story_pick(s, 256u) >= odds) return 0; /* spared, this time */
  id = cri_story_pick(s, 2u) ? a : b;
  if (id >= c->items || !cri_owned(c, id)) id = id == a ? b : a;
  if (!cru_drop_any(c, id)) return 0;
  cru_story_act(s, CRU_ACT_LOSS);
  cru_story_remember(s, id);
  if (lost) *lost = id;
  return 1;
}

/* ---- the run's course: early losses, chapters, a hidden grade, going on ---- */
static uint8_t hostile_count(const crucible_story *s) {
  uint8_t f, n = 0;
  for (f = 0; f < CRU_FACTIONS; f++) n = (uint8_t)(n + (s->stand[f] <= -50));
  return n;
}
uint8_t cru_story_state(crucible_core *c, const crucible_story *s) CORE_BANKED {
  if (!s->lucid) return CRU_RUN_LOST_DREAM;
  if (hostile_count(s) >= 4u) return CRU_RUN_LOST_DELETED;
  if (c->found[0] < 2u) return CRU_RUN_LOST_HOLLOW; /* ran out: nothing left to combine, for good */
  if (s->chapter >= CRU_STORY_CHAPTERS) return CRU_RUN_END;
  return CRU_RUN_ON;
}
uint8_t cru_story_grade(crucible_core *c, const crucible_story *s) CORE_BANKED {
  uint8_t g = 0;
  if (s->lucid >= 128u) g++;
  if (c->found[0] >= (uint16_t)(24u + ((uint16_t)s->chapter << 3))) g++;
  if (!hostile_count(s)) g++;
  return g;
}
uint8_t cru_story_pressure(const crucible_story *s) CORE_BANKED {
  uint16_t p = (uint16_t)(s->chapter + ((uint16_t)s->cycle << 1));
  return (uint8_t)(p > 31u ? 31u : p);
}
uint8_t cru_story_advance(crucible_core *c, crucible_story *s) CORE_BANKED {
  uint8_t f, p;
  if (s->chapter < CRU_STORY_CHAPTERS) s->chapter++;
  p = cru_story_pressure(s);
  cru_story_lucid(s, (int8_t)-(int8_t)(6u + (p << 2) > 100u ? 100u : 6u + (p << 2)));
  for (f = 0; f < CRU_FACTIONS; f++) { /* grudges grow: the cold get colder */
    if (s->stand[f] < 0)
      shift(s, f, (int8_t)-(int8_t)(2u + (p >> 1)));
    else if (s->stand[f] > 0 && s->stand[f] < 15)
      shift(s, f, -1); /* and lukewarm friendships fade */
  }
  {
    uint8_t a, v;
    for (a = 0; a < 3u; a++)
      for (v = 0; v < 8u; v++) s->lean[a][v] = (int16_t)(s->lean[a][v] - (s->lean[a][v] >> 2));
  } /* the truth evolves: old leanings fade */
  cru_story_event(s, CRU_EV_MOVE, s->chapter, p);
  return cru_story_state(c, s);
}
void cru_story_continue(crucible_story *s) CORE_BANKED {
  uint8_t f;
  int16_t start;
  s->cycle = (uint8_t)(s->cycle + 1u);
  s->chapter = 0;
  for (f = 0; f < CRU_FACTIONS; f++) s->stand[f] = (int8_t)(s->stand[f] >> 1);
  start = (int16_t)(200 - ((int16_t)s->cycle << 5));
  s->lucid = (uint8_t)(start < 64 ? 64 : start);
  s->nemesis.met = 0;
  cru_story_act(s, CRU_ACT_CYCLE);
  cru_story_event(s, CRU_EV_MOVE, 0xffu, s->cycle);
}

/* ---- the truths: a matrix from play to what you are, where, and why ---- */
/* per axis and truth: the traits that lean to it (+1 each), its category (+2), the acts that lean to it (+3 each) */
#define A(x) (1u << CRU_ACT_##x)
static const uint16_t T_TRAITS[3][8] = {
    {T_MADE | T_GLOWS | T_SHINY, T_ALIVE | T_GREEN | T_WET, T_AIRY | T_COLD | T_MAGIC,
     T_SHINY | T_STONE | T_COLD, /* PROGRAM PERSON GHOST COPY */
     T_MAGIC | T_ALIVE | T_BIG, T_STONE | T_COLD | T_MADE, T_MAGIC | T_AIRY | T_WET, 0}, /* CHILD AI DREAMER NOBODY */
    {T_MADE | T_SHINY, T_STONE | T_BIG, T_ALIVE | T_WET | T_COLD,
     T_GLOWS | T_HOT | T_MADE, /* CARTRIDGE BACKROOMS HOSPITAL ARCADE */
     T_COLD | T_MADE | T_STONE, T_ALIVE | T_MAGIC | T_GREEN, T_GLOWS | T_SHINY,
     T_AIRY}, /* SERVER BEDROOM GRID NOWHERE */
    {T_AIRY | T_MAGIC, T_HOT | T_GLOWS, T_STONE | T_BIG, T_MADE | T_SHINY, /* ASLEEP POWERCUT NOCLIP UPLOADED */
     T_COLD | T_WET, T_MADE | T_GLOWS, T_HOT | T_STONE,
     T_GREEN | T_ALIVE | T_STONE}}; /* FORGET PLAYED BATTERY NEVERLEFT */
/* ELEMENT 0 MATTER 1 WEATHER 2 ENERGY 3 LIFE 4 CRAFT 5 PLACE 6 */
static const uint8_t T_CAT[3][8] = {{5, 4, 2, 1, 6, 3, 2, 0}, {5, 6, 4, 3, 1, 6, 3, 0}, {2, 3, 6, 5, 2, 5, 3, 4}};
/* SPARE (a broken boss let go) leans to a person, a hospital, forgetting; TAKE (its element kept) to an AI, a server,
 * an upload */
static const uint16_t T_ACTS[3][8] = {
    {A(AGREE) | A(LOOP), A(REFUSE) | A(GIFT) | A(SPARE), A(LOSS) | A(SPLIT), A(LOOP) | A(SPLIT), A(EGG) | A(IDLE),
     A(AGREE) | A(WIN) | A(TAKE), A(DEEP) | A(IDLE), A(LOSS) | A(HIT)},
    {A(EGG) | A(LOOP), A(IDLE) | A(LOOP), A(LOSS) | A(HIT) | A(SPARE), A(WIN) | A(HIT), A(SPLIT) | A(AGREE) | A(TAKE),
     A(GIFT) | A(EGG), A(AGREE) | A(WIN), A(DEEP) | A(LOSS)},
    {A(IDLE) | A(DEEP), A(LOSS) | A(HIT), A(SPLIT) | A(IDLE), A(AGREE) | A(WIN) | A(TAKE),
     A(REFUSE) | A(SPLIT) | A(SPARE), A(EGG) | A(LOOP), A(HIT) | A(LOSS), A(LOOP) | A(CYCLE)}};
#undef A
static void nudge(crucible_story *s, uint8_t a, uint8_t v, int8_t d) {
  int16_t x = (int16_t)(s->lean[a][v] + d);
  s->lean[a][v] = x > 9999 ? 9999 : x < -9999 ? -9999 : x;
}
void cru_story_lean(crucible_core *c, crucible_story *s, uint16_t id) CORE_BANKED {
  uint8_t a, v, cat;
  uint16_t t;
  if (id >= c->items) return;
  t = cri_traits(c, id);
  cat = cri_category(c, id);
  for (a = 0; a < 3u; a++)
    for (v = 0; v < 8u; v++) nudge(s, a, v, (int8_t)(bits(t & T_TRAITS[a][v]) + (cat == T_CAT[a][v] ? 2 : 0)));
}
void cru_story_act(crucible_story *s, uint8_t act) CORE_BANKED {
  uint8_t a, v;
  if (act > CRU_ACT_TAKE) return;
  for (a = 0; a < 3u; a++)
    for (v = 0; v < 8u; v++)
      if ((T_ACTS[a][v] >> act) & 1u) nudge(s, a, v, 3);
}
static uint8_t lead(const crucible_story *s, uint8_t a, uint8_t skip) {
  uint8_t v, best = 0xffu;
  int16_t top = -32767;
  for (v = 0; v < 8u; v++)
    if (v != skip && s->lean[a][v] > top) {
      top = s->lean[a][v];
      best = v;
    }
  return best;
}
uint8_t cru_story_truth(const crucible_story *s, uint8_t k) CORE_BANKED { return k < 3u ? lead(s, k, 0xffu) : 0; }
uint8_t cru_story_runner_up(const crucible_story *s, uint8_t k) CORE_BANKED {
  return k < 3u ? lead(s, k, lead(s, k, 0xffu)) : 0;
}
static uint8_t clues_of(const crucible_story *s, uint8_t a, uint8_t v) {
  uint8_t i = (uint8_t)((a << 2) + (v >> 1));
  return (uint8_t)((v & 1u) ? s->clue[i] >> 4 : s->clue[i] & 15u);
}
uint8_t cru_story_clue(crucible_story *s, uint8_t k) CORE_BANKED {
  uint8_t v, i, n;
  if (k >= 3u) return 0;
  v = lead(s, k, 0xffu);
  i = (uint8_t)((k << 2) + (v >> 1));
  n = clues_of(s, k, v);
  if (n < 15u) {
    n++;
    s->clue[i] = (uint8_t)((v & 1u) ? (s->clue[i] & 15u) | (n << 4) : (s->clue[i] & 0xf0u) | n);
  }
  cru_story_event(s, CRU_EV_FLAG, (uint16_t)(0x80u | (k << 3) | v), 0);
  return n;
}
uint8_t cru_story_knows(const crucible_story *s, uint8_t k) CORE_BANKED {
  uint8_t v, w, n;
  if (k >= 3u) return CRU_KNOW_NOTHING;
  v = lead(s, k, 0xffu);
  w = lead(s, k, v);
  n = clues_of(s, k, v);
  if (n >= 4u && s->lean[k][v] - s->lean[k][w] >= 6) return CRU_KNOW_ANSWER; /* clued, and clearly what you are */
  return n >= 2u ? CRU_KNOW_SUSPECT : CRU_KNOW_NOTHING;
}
uint8_t cru_story_clue_due(crucible_story *s) CORE_BANKED {
  uint8_t k, best = 0xffu, low = 99, n, odds;
  if (!s->chapter && !s->cycle) return 0xffu; /* at first you do not even know you are stuck */
  odds = (uint8_t)(2u + s->chapter + (s->cycle << 1) + ((255u - s->lucid) >> 5)); /* out of 32 */
  if (cri_story_pick(s, 32u) >= odds) return 0xffu;
  for (k = 0; k < 3u; k++) {
    n = (uint8_t)(clues_of(s, k, lead(s, k, 0xffu)) + cri_story_pick(s, 2u));
    if (n < low) {
      low = n;
      best = k;
    }
  }
  return best;
}

/* ---- the dream ---- */
void cru_story_remember(crucible_story *s, uint16_t id) CORE_BANKED {
  if (s->memory[s->mem_at & (CRU_STORY_MEMORY - 1u)] == id) return;
  s->mem_at = (uint8_t)((s->mem_at + 1u) & (CRU_STORY_MEMORY - 1u));
  s->memory[s->mem_at] = id;
}
uint16_t cru_story_recall(const crucible_story *s, uint8_t back) CORE_BANKED {
  if (back >= CRU_STORY_MEMORY) return CRU_NONE;
  return s->memory[(uint8_t)(s->mem_at - back) & (CRU_STORY_MEMORY - 1u)];
}
void cru_story_lucid(crucible_story *s, int8_t d) CORE_BANKED {
  int16_t v = (int16_t)s->lucid + d;
  s->lucid = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}
uint16_t cru_dream_slot(crucible_core *c, crucible_story *s, uint16_t id) CORE_BANKED {
  uint16_t r, first, end, at, other;
  uint8_t how;
  if (id >= c->items) return id;
  r = cri_story_pick(s, 256u);
  if (r < s->lucid) return id; /* clear: the thing itself */
  how = (uint8_t)cri_story_pick(s, 4u);
  if (how == 0u) { /* what it is made of */
    first = cri_route_first(c, id);
    if (first != cri_route_first(c, (uint16_t)(id + 1u))) {
      r = cri_route(c, first);
      return cri_story_pick(s, 2u) ? cri_recipe_a(c, r) : cri_recipe_b(c, r);
    }
  }
  if (how <= 1u) { /* what it makes, with something you hold */
    for (at = 0, other = cri_story_pick(s, c->items); at < 64u;
         at++, other = (uint16_t)(other + 1u >= c->items ? 0 : other + 1u)) {
      if (!cri_owned(c, other)) continue;
      r = cru_recipe(c, id, other);
      if (r != CRU_NONE) return r;
    }
  }
  if (how == 2u) { /* a cousin: same category, a shared trait */
    uint8_t cat = cri_category(c, id);
    uint16_t t = cri_traits(c, id);
    first = c->cat_first[cat];
    end = c->cat_first[cat + 1u];
    if (end > first)
      for (at = 0, r = (uint16_t)(first + cri_story_pick(s, (uint16_t)(end - first))); at < 48u; at++) {
        other = cri_shelf(c, r);
        if (other != id && (cri_traits(c, other) & t)) return other;
        if (++r >= end) r = first;
      }
  }
  r = cru_story_recall(s, (uint8_t)(1u + cri_story_pick(s, CRU_STORY_MEMORY - 1u))); /* deja vu */
  return r != CRU_NONE ? r : id;
}
uint8_t cru_dream_glitch(crucible_story *s, uint8_t cut) CORE_BANKED {
  uint16_t r = cri_story_pick(s, 256u), depth = (uint16_t)(255u - s->lucid);
  if (cut) depth = (uint16_t)(depth + 48u);
  if (r >= depth) return 0;
  if (r < (depth >> 3)) return 3;
  if (r < (depth >> 1)) return 2;
  return 1;
}

/* ---- bosses ---- */
#ifndef CRU_HAND_FIGHT /* the shelf answer (the classic and NES fights); the cartridge's fight answers from its hand (crucible_fight_rules.c) */
uint8_t cru_counter(crucible_core *c, uint16_t attack, uint16_t answer) CORE_BANKED {
  uint16_t at, an;
  uint8_t k, score = 0;
  if (attack >= c->items || answer >= c->items) return 0;
  at = cri_traits(c, attack);
  an = cri_traits(c, answer);
  for (k = 0; k < CRU_TRAITS; k++)
    if ((at >> k) & 1u)
      if (an & BEATS[k]) score++;
  if (score && cri_depth(c, answer) > cri_depth(c, attack)) score++; /* a deeper make lands harder */
  return score;
}
#endif
static uint16_t favourite(crucible_core *c, crucible_story *s, uint8_t f, uint8_t level) {
  uint8_t cat = LIKE_CAT[f];
  uint16_t first = c->cat_first[cat], end = c->cat_first[cat + 1u], at, n, id;
  if (end <= first) return cri_shelf(c, cri_story_pick(s, c->items));
  for (n = 0, at = (uint16_t)(first + cri_story_pick(s, (uint16_t)(end - first))); n < 256u; n++) {
    id = cri_shelf(c, at);
    if (cri_depth(c, id) <= (uint8_t)(level + 2u) && cru_affinity(c, f, id) >= 2) return id;
    if (++at >= end) at = first;
  }
  return cri_shelf(c, at);
}
void cru_boss_begin(crucible_core *c, crucible_story *s, crucible_boss *b, uint8_t f, uint8_t level) CORE_BANKED {
  uint8_t i;
  if (f >= CRU_FACTIONS) f = 0;
  for (i = 0; i < 4u; i++) b->hand[i] = favourite(c, s, f, level);
  b->next = b->next_a = b->next_b = b->stolen = CRU_NONE;
  b->faction = f;
  b->level = level;
  b->phase = 0;
  b->cue = 0;
  b->met = (uint8_t)(b->met + 1u);
  b->max = (uint8_t)(4u + (level << 1));
  if (b->max > 24u) b->max = 24u;
  b->hp = b->max;
  cru_story_event(s, CRU_EV_FIGHT, f, level);
}
#ifndef CRU_HAND_FIGHT
uint16_t cru_boss_plan(crucible_core *c, crucible_story *s, crucible_boss *b) CORE_BANKED {
  uint8_t i, j, k, start = (uint8_t)cri_story_pick(s, 16u);
  uint16_t r, mine;
  for (k = 0; k < 16u; k++) { /* two of its own, in a seeded order */
    i = (uint8_t)(((start + k) >> 2) & 3u);
    j = (uint8_t)((start + k) & 3u);
    r = cru_recipe(c, b->hand[i], b->hand[j]);
    if (r != CRU_NONE) {
      b->next_a = b->hand[i];
      b->next_b = b->hand[j];
      goto planned;
    }
  }
  for (k = 0; k < 64u; k++) { /* one of its with one of yours: it reaches into your shelf */
    mine = cri_shelf(c, cri_story_pick(s, c->items));
    if (!cri_owned(c, mine)) continue;
    i = (uint8_t)cri_story_pick(s, 4u);
    r = cru_recipe(c, b->hand[i], mine);
    if (r != CRU_NONE) {
      b->next_a = b->hand[i];
      b->next_b = mine;
      goto planned;
    }
  }
  b->next_a = b->next_b = r = b->hand[cri_story_pick(s, 4u)]; /* nothing combines: it throws one raw */
planned:
  b->next = r;
  b->cue = (b->phase >= 2u || b->level >= 6u) ? 1u : 2u; /* late phases and deep levels warn once */
  return r;
}
uint8_t cru_boss_answer(crucible_core *c, crucible_story *s, crucible_boss *b, uint16_t answer,
                        uint16_t *lost) CORE_BANKED {
  uint8_t score = 0, dmg, out, was = b->phase, i;
  if (lost) *lost = CRU_NONE;
  if (answer != CRU_NONE && (answer >= c->items || !cru_owned(c, answer))) answer = CRU_NONE;
  if (answer != CRU_NONE) score = cru_counter(c, b->next, answer);
  cru_story_event(s, CRU_EV_FIGHT, b->next, answer);
  /* its make joins its hand either way (replacing the weakest slot: the first ingredient) */
  for (i = 0; i < 4u; i++)
    if (b->hand[i] == b->next_a) {
      b->hand[i] = b->next;
      break;
    }
  if (!score) {
    uint16_t gone[3];
    uint8_t n = cru_story_loss(c, s, gone, 3);
    if (n) {
      if (lost) *lost = gone[0];
      if (b->stolen == CRU_NONE) b->stolen = gone[0];
    }
    cru_story_lucid(s, -12);
    cru_story_act(s, CRU_ACT_HIT);
    b->next = CRU_NONE;
    return CRU_BOSS_HIT;
  }
  dmg = score;
  out = score >= 2u ? CRU_BOSS_COUNTER : CRU_BOSS_PARRY;
  b->hp = dmg >= b->hp ? 0 : (uint8_t)(b->hp - dmg);
  b->next = CRU_NONE;
  cru_story_remember(s, answer);
  if (!b->hp) {
    shift(s, b->faction, -10);
    shift(s, RIVAL[b->faction], 10);
    if (b->stolen != CRU_NONE) {
      cru_story_gain(c, s, b->stolen);
      b->stolen = CRU_NONE;
    }
    cru_story_lucid(s, 24);
    cru_story_act(s, CRU_ACT_WIN);
    return CRU_BOSS_DOWN;
  }
  if (b->hp <= (b->max >> 2))
    b->phase = 2;
  else if (b->hp <= (b->max >> 1))
    b->phase = 1;
  if (b->phase != was) {
    if (was == 0u && b == &s->nemesis && b->met < 3u) return CRU_BOSS_FLED; /* it will be back, with what it took */
    return CRU_BOSS_PHASE;
  }
  return out;
}
#endif
