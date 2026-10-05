#ifndef KEEL_DIALOGUE_CHOICE_H
#define KEEL_DIALOGUE_CHOICE_H
/* Portable bounded semantic choices driven by a persisted alignment matrix.
 * No rendering, hardware, allocations, clocks or game RNG. State is sixteen
 * caller-owned bytes with stable authored history keys; old zeroed state
 * initializes lazily and v1 state migrates in place. Persist bytes unchanged.
 *
 * State v2 (v1 differs only where noted):
 *  [0] 0xd7 magic          [1] version<<4 | coherence 0..15
 *  [2] last speaker who<<4 | last reply tag 1..15, 0 none (v1: last action)
 *  [3] surreal habit<<4 | key count<<2 | next slot (3 keys; v1: 4 keys, no habit)
 *  [4..5] conversation serial
 *  [6] turned<<6 | (last action+1)<<2 | chapter cadence 0..2 (v1: cadence only)
 *  [7] chapter mark: cycle<<4 | chapter (v1: zero)
 *  [8..13] three recent authored keys (v1: four, [8..15])
 *  [14] ORDER  +lawful .. -chaotic  (int8, -60..60)
 *  [15] HEART  +good   .. -evil     (int8, -60..60)
 *
 * Cells are ORDER third * 3 + HEART third: LG LN LE NG TN NE CG CN CE.
 * The axes and the surreal habit are the player's history: what they keep
 * choosing appears more often and in more extreme words; both fade a quarter
 * per chapter, and every set keeps one option that pulls the other way.
 * The last act (speaker, action, whether it turned: caught or reclaimed) is
 * remembered so the next conversation can answer to it in context. */
#include <stdint.h>
#include <string.h>
#define KDC_STATE_BYTES 16u
#define KDC_RECORD_BYTES 28u
#define KDC_VERSION 2u
#define KDC_KEYS 3u
#define KDC_AXIS_MAX 60
#define KDC_AXIS_STEP 3
#define KDC_CELL_EDGE 12
#define KDC_CENTER_PULL 3
#define KDC_HABIT_MAX 15u
enum {
  KDC_DEFER,
  KDC_GIVE,
  KDC_SHOW,
  KDC_REFUSE,
  KDC_QUESTION,
  KDC_WORDPLAY,
  KDC_IMAGINARY,
  KDC_BARTER,
  KDC_DECEIVE,
  KDC_THREATEN,
  KDC_MOCK,
  KDC_BLESS,
  KDC_STEAL,
  KDC_RIDDLE,
  KDC_NONSENSE,
  KDC_ACTIONS
};
enum { KDC_LG, KDC_LN, KDC_LE, KDC_NG, KDC_TN, KDC_NE, KDC_CG, KDC_CN, KDC_CE, KDC_CELLS };
enum { KDC_WARM, KDC_NEUTRAL, KDC_COLD, KDC_ANY_ATTITUDE };
/* Action flags: which request kind it fulfils, surreal (dream/habit-gated),
 * needs awareness (story past its unknowing start), pulls the axes to center,
 * or is the nonsense tier (surreal beyond wordplay: opened by the habit). */
#define KDC_F_GIVE 1u
#define KDC_F_SHOW 2u
#define KDC_F_SURREAL 4u
#define KDC_F_AWARE 8u
#define KDC_F_CENTER 16u
#define KDC_F_NONSENSE 32u
#define KDC_R_DEEP 4u
typedef struct {
  uint16_t line;
  uint8_t kind, fulfill, category;
  uint16_t all, any, forbid, key;
} kdc_request_t;
/* tier: 0 plain, 1 strong, 2 extreme; shown as the player's lean along it grows.
 * The label text lives in the caller's own table, indexed like the variants. */
typedef struct {
  uint8_t action, who, tier;
  uint16_t key;
} kdc_variant_t;
typedef struct {
  uint8_t action, variant;
} kdc_choice_t;
/* cell, semantic effect group (equal groups never share a set), flags, axis vector, coherence change */
typedef struct {
  uint8_t cell, group, flags;
  int8_t order, heart, coherence;
} kdc_action_t;
/* reply class, speaker (0 any), attitude (KDC_ANY_ATTITUDE any) | KDC_R_DEEP, no-repeat tag 1..15, text offset */
typedef struct {
  uint8_t cls, who, att, tag;
  uint16_t at;
} kdc_reply_t;
/* What a selection may know: speaker, its faction's disposition cell, the request's
 * fulfilment, whether the held item meets it, lucidity 0 deep..255 clear, story awareness. */
typedef struct {
  uint8_t who, disposition, fulfill, compatible, lucid, aware;
} kdc_context_t;
#if !defined(KDC_TYPES_ONLY) || defined(KDC_REPLY_ONLY)
static uint16_t kdc_roll(uint16_t *r) {
  *r ^= *r << 7;
  *r ^= *r >> 9;
  *r ^= *r << 8;
  return *r;
}
/* deep: 0 lucid rows only, 1 the dream's rows join, 2 the dream answers alone.
 * avoid: the last reply's tag, skipped while another row fits. */
static uint8_t kdc_reply_fits(const kdc_reply_t *t, uint8_t cls, uint8_t who, uint8_t att, uint8_t deep) {
  uint8_t a = t->att & 3u, d = (t->att & KDC_R_DEEP) != 0;
  return t->cls == cls && (!t->who || t->who == who) && (a == att || a == KDC_ANY_ATTITUDE) &&
         (deep == 2u ? d : deep || !d);
}
static uint16_t kdc_reply_pick(const kdc_reply_t *t, uint16_t n, uint8_t cls, uint8_t who, uint8_t att, uint8_t deep,
                               uint8_t avoid, uint16_t *r) {
  uint16_t i, count, pick;
  uint8_t pass, mode;
  for (mode = deep;; mode--) {
    for (pass = 0; pass < 2u; pass++) {
      for (count = 0, i = 0; i < n; i++)
        if (kdc_reply_fits(t + i, cls, who, att, mode) && (pass || t[i].tag != (avoid & 15u))) count++;
      if (!count) continue;
      pick = (uint16_t)(kdc_roll(r) % count);
      for (i = 0; i < n; i++)
        if (kdc_reply_fits(t + i, cls, who, att, mode) && (pass || t[i].tag != (avoid & 15u))) {
          if (!pick) return i;
          pick--;
        }
    }
    if (!mode) break;
  }
  return 0xffffu;
}
#endif
#ifndef KDC_TYPES_ONLY
static const uint8_t KDC_O3[KDC_CELLS] = {0, 0, 0, 1, 1, 1, 2, 2, 2}, KDC_H3[KDC_CELLS] = {0, 1, 2, 0, 1, 2, 0, 1, 2};
static uint16_t kdc_u16(const uint8_t *s) { return (uint16_t)s[0] | ((uint16_t)s[1] << 8); }
static void kdc_put16(uint8_t *s, uint16_t v) {
  s[0] = (uint8_t)v;
  s[1] = (uint8_t)(v >> 8);
}
static uint8_t kdc_axis_ok(uint8_t v) {
  int8_t a = (int8_t)v;
  return a >= -KDC_AXIS_MAX && a <= KDC_AXIS_MAX;
}
static uint8_t kdc_v2(const uint8_t *s) {
  return s[0] == 0xd7u && (s[1] & 0xf0u) == (KDC_VERSION << 4) && ((s[3] >> 2) & 3u) <= KDC_KEYS &&
         (s[3] & 3u) < KDC_KEYS && (s[6] & 3u) < 3u && ((s[6] >> 2) & 15u) <= KDC_ACTIONS && (s[2] >> 4) < 8u &&
         kdc_axis_ok(s[14]) && kdc_axis_ok(s[15]);
}
static uint8_t kdc_v1(const uint8_t *s) {
  return s[0] == 0xd7u && (s[1] & 0xf0u) == 0x10u && s[3] < 20u && s[6] < 3u && !s[7];
}
/* v1 -> v2 keeps magic, coherence, serial and cadence, the three newest keys
 * (oldest first, so the ring continues in order) and starts at true neutral
 * with no habit. Anything else (zeroed or foreign) starts fresh. */
static void kdc_init(uint8_t *s) {
  uint8_t i, n, at, keys[6];
  if (kdc_v2(s)) return;
  if (kdc_v1(s)) {
    n = s[3] >> 2;
    at = s[3] & 3u;
    if (n > KDC_KEYS) n = KDC_KEYS;
    for (i = 0; i < n; i++) {
      uint8_t from = (uint8_t)((at + 4u - n + i) & 3u);
      keys[i * 2u] = s[8u + from * 2u];
      keys[i * 2u + 1u] = s[9u + from * 2u];
    }
    memset(s + 8, 0, 8);
    memcpy(s + 8, keys, (size_t)(n * 2u));
    s[1] = (uint8_t)((KDC_VERSION << 4) | (s[1] & 15u));
    s[2] = 0;
    s[3] = (uint8_t)((n << 2) | (n % KDC_KEYS));
    s[7] = 0;
    return;
  }
  memset(s, 0, KDC_STATE_BYTES);
  s[0] = 0xd7u;
  s[1] = (uint8_t)((KDC_VERSION << 4) | 14u);
}
static int8_t kdc_get(const uint8_t *s, uint8_t k) { return (int8_t)s[14u + k]; }
static uint8_t kdc_habit(const uint8_t *s) { return s[3] >> 4; }
static void kdc_set_habit(uint8_t *s, uint8_t h) {
  s[3] = (uint8_t)((s[3] & 15u) | ((h > KDC_HABIT_MAX ? KDC_HABIT_MAX : h) << 4));
}
/* The last act: who it was toward (1..7, 0 none), what (255 none), whether it turned. */
static uint8_t kdc_last_who(const uint8_t *s) { return s[2] >> 4; }
static uint8_t kdc_last_action(const uint8_t *s) {
  uint8_t a = (s[6] >> 2) & 15u;
  return a ? (uint8_t)(a - 1u) : 255u;
}
static uint8_t kdc_last_turned(const uint8_t *s) { return (s[6] >> 6) & 1u; }
static void kdc_remember(uint8_t *s, uint8_t who, uint8_t action, uint8_t turned, uint8_t tag) {
  s[2] = (uint8_t)(((who & 7u) << 4) | (tag & 15u));
  s[6] = (uint8_t)((s[6] & 3u) | ((uint8_t)((action + 1u) & 15u) << 2) | (turned ? 0x40u : 0u));
}
static uint8_t kdc_third(int8_t v) { return v >= KDC_CELL_EDGE ? 0u : v <= -KDC_CELL_EDGE ? 2u : 1u; }
static uint8_t kdc_cell(const uint8_t *s) {
  return (uint8_t)(kdc_third(kdc_get(s, 0)) * 3u + kdc_third(kdc_get(s, 1)));
}
static uint8_t kdc_gap(uint8_t a, uint8_t b) { return a > b ? (uint8_t)(a - b) : (uint8_t)(b - a); }
static uint8_t kdc_distance(uint8_t a, uint8_t b) {
  return (uint8_t)(kdc_gap(KDC_O3[a], KDC_O3[b]) + kdc_gap(KDC_H3[a], KDC_H3[b]));
}
static uint8_t kdc_attitude(uint8_t player, uint8_t disposition) {
  uint8_t d = kdc_distance(player, disposition);
  return d <= 1u ? KDC_WARM : d == 2u ? KDC_NEUTRAL : KDC_COLD;
}
static void kdc_set(uint8_t *s, uint8_t k, int16_t v) {
  s[14u + k] = (uint8_t)(int8_t)(v > KDC_AXIS_MAX ? KDC_AXIS_MAX : v < -KDC_AXIS_MAX ? -KDC_AXIS_MAX : v);
}
static void kdc_toward0(uint8_t *s, uint8_t k, uint8_t by) {
  int8_t v = kdc_get(s, k);
  kdc_set(s, k, v > (int8_t)by ? (int16_t)(v - by) : v < -(int8_t)by ? (int16_t)(v + by) : 0);
}
/* Old leanings fade by a quarter (rounded up, so small ones reach zero). */
static void kdc_fade(uint8_t *s) {
  uint8_t k, m;
  int8_t v;
  for (k = 0; k < 2u; k++) {
    v = kdc_get(s, k);
    m = (uint8_t)(v < 0 ? -v : v);
    kdc_toward0(s, k, (uint8_t)((m + 3u) >> 2));
  }
  m = kdc_habit(s);
  kdc_set_habit(s, (uint8_t)(m - ((m + 3u) >> 2)));
}
/* Lazy chapter bookkeeping: each chapter passed fades once; a new cycle halves
 * first. Going backwards (a new run, free play after power-on) only resyncs. */
static uint8_t kdc_sync(uint8_t *s, uint8_t chapter, uint8_t cycle) {
  uint8_t mark = (uint8_t)(((cycle & 15u) << 4) | (chapter & 15u)), old, n = 0, k;
  kdc_init(s);
  old = s[7];
  if (mark == old) return 0;
  if ((mark >> 4) == (old >> 4)) {
    if ((mark & 15u) > (old & 15u)) n = (uint8_t)((mark & 15u) - (old & 15u));
  } else if ((mark >> 4) == (uint8_t)(((old >> 4) + 1u) & 15u)) {
    for (k = 0; k < 2u; k++) kdc_set(s, k, (int16_t)(kdc_get(s, k) / 2));
    kdc_set_habit(s, (uint8_t)(kdc_habit(s) >> 1));
    n = (uint8_t)(mark & 15u);
  }
  while (n--) kdc_fade(s);
  s[7] = mark;
  return 1;
}
static uint16_t kdc_open(uint8_t *s, uint16_t root, uint16_t context) {
  uint8_t i;
  uint16_t r;
  kdc_init(s);
  kdc_put16(s + 4, (uint16_t)(kdc_u16(s + 4) + 1u));
  r = root ^ context ^ (uint16_t)(kdc_u16(s + 4) * 0x9e37u) ^ kdc_u16(s + 14);
  for (i = 0; i < KDC_KEYS; i++) {
    r ^= kdc_u16(s + 8u + i * 2u);
    r = (uint16_t)((r << 5) | (r >> 11));
  }
  return r ? r : 0xace1u;
}
static uint8_t kdc_recent(const uint8_t *s, uint16_t key) {
  uint8_t i, n = (s[3] >> 2) & 3u;
  for (i = 0; i < n; i++)
    if (kdc_u16(s + 8u + i * 2u) == key) return 1;
  return 0;
}
/* How far the player's history leans along an action, 0..60: the habit for
 * surreal acts, else the axes projected on its vector (0 when opposed). */
static uint8_t kdc_lean(const kdc_action_t *a, const uint8_t *s) {
  int16_t dot;
  uint8_t len;
  if (a->flags & KDC_F_SURREAL) return (uint8_t)(kdc_habit(s) * 4u);
  len = (uint8_t)((a->order < 0 ? -a->order : a->order) + (a->heart < 0 ? -a->heart : a->heart));
  if (!len) return 0;
  dot = (int16_t)((int16_t)a->order * kdc_get(s, 0) + (int16_t)a->heart * kdc_get(s, 1));
  return dot > 0 ? (uint8_t)(dot / len) : 0u;
}
static uint8_t kdc_tier(const kdc_action_t *a, const uint8_t *s) {
  uint8_t l = kdc_lean(a, s);
  return l >= 36u ? 2u : l >= 18u ? 1u : 0u;
}
/* A variant at the target tier or one below (smooth escalation), avoiding
 * recent keys; then any lower tier; then recent ones. */
static uint8_t kdc_variant(const kdc_variant_t *v, uint8_t n, uint8_t action, uint8_t who, uint8_t tier,
                           const uint8_t *s, uint16_t *r) {
  uint8_t i, count, pick, pass, ok;
  for (pass = 0; pass < 3u; pass++) {
    for (count = 0, i = 0; i < n; i++) {
      ok = v[i].action == action && (!v[i].who || v[i].who == who) && v[i].tier <= tier &&
           (pass || v[i].tier + 1u >= tier) && (pass == 2u || !kdc_recent(s, v[i].key));
      count = (uint8_t)(count + ok);
    }
    if (!count) continue;
    pick = (uint8_t)(((uint16_t)(uint8_t)kdc_roll(r) * count) >> 8);
    for (i = 0; i < n; i++) {
      ok = v[i].action == action && (!v[i].who || v[i].who == who) && v[i].tier <= tier &&
           (pass || v[i].tier + 1u >= tier) && (pass == 2u || !kdc_recent(s, v[i].key));
      if (ok) {
        if (!pick) return i;
        pick--;
      }
    }
  }
  return 255u;
}
static uint8_t kdc_matches(const kdc_request_t *q, uint16_t item, uint16_t expected_item, uint8_t category,
                           uint8_t expected_category, uint16_t traits, uint16_t expected_tag) {
  if (q->kind == 1u && item != expected_item) return 0;
  if (q->kind == 2u && category != (q->category == 255u ? expected_category : q->category)) return 0;
  if (q->kind == 3u && !(traits & expected_tag)) return 0;
  if ((traits & q->all) != q->all || (q->any && !(traits & q->any)) || (traits & q->forbid)) return 0;
  return q->kind >= 1u && q->kind <= 4u;
}
/* An option pulls back when it opposes the current drift (or recenters it);
 * from true neutral, anything off-center is a change of course. While the
 * surreal habit runs high, any lucid (non-surreal) option pulls back too. */
static uint8_t kdc_pulls(const kdc_action_t *a, const uint8_t *s, uint8_t player) {
  if (kdc_habit(s) >= 8u && !(a->flags & KDC_F_SURREAL) && KDC_O3[a->cell] != 2u) return 1;
  if (player == KDC_TN) return a->cell != KDC_TN;
  if (a->flags & KDC_F_CENTER) return 1;
  return (int16_t)a->order * kdc_get(s, 0) + (int16_t)a->heart * kdc_get(s, 1) < 0;
}
#define KDC_PICK_FULFIL 0u
#define KDC_PICK_PULL 1u
#define KDC_PICK_NEAR 2u
#define KDC_PICK_ANY 3u
/* Weighted, deterministic. A candidate is a new action, cell and effect group;
 * a third surreal option is never offered (one lucid way out always remains). */
static uint8_t kdc_pick(const kdc_action_t *act, const uint8_t *w, uint8_t kind, uint16_t used_cells,
                        uint16_t used_groups, uint8_t surreal, const uint8_t *s, uint8_t player, uint16_t *r) {
  uint8_t a, ok, pass;
  uint16_t total = 0, pick = 0;
  for (pass = 0; pass < 2u; pass++) {
    for (a = 0; a < KDC_ACTIONS; a++) {
      if (!w[a] || (used_cells >> act[a].cell) & 1u || (used_groups >> act[a].group) & 1u) continue;
      if (surreal >= 2u && (act[a].flags & KDC_F_SURREAL)) continue;
      ok = kind == KDC_PICK_FULFIL ? (act[a].flags & (KDC_F_GIVE | KDC_F_SHOW)) != 0
                                   : !(act[a].flags & (KDC_F_GIVE | KDC_F_SHOW));
      if (ok && kind == KDC_PICK_PULL) ok = kdc_pulls(act + a, s, player);
      if (ok && kind == KDC_PICK_NEAR) ok = kdc_distance(act[a].cell, player) <= 1u;
      if (!ok) continue;
      if (!pass)
        total = (uint16_t)(total + w[a]);
      else {
        if (pick < w[a]) return a;
        pick = (uint16_t)(pick - w[a]);
      }
    }
    if (!total) return 255u;
    if (!pass) pick = (uint16_t)(kdc_roll(r) % total);
  }
  return 255u;
}
static const uint8_t KDC_NEAR_W[5] = {14, 7, 3, 1, 0}, KDC_DISP_W[5] = {4, 2, 0, 0, 0};
/* Distinct actions, cells and effect groups; at most one fulfilment and at most two surreal. */
static uint8_t kdc_set_ok(const kdc_choice_t *out, const kdc_action_t *act) {
  uint8_t i, j, f = 0, z = 0;
  for (i = 0; i < 3u; i++) {
    if (out[i].action >= KDC_ACTIONS) return 0;
    f = (uint8_t)(f + ((act[out[i].action].flags & (KDC_F_GIVE | KDC_F_SHOW)) != 0));
    z = (uint8_t)(z + ((act[out[i].action].flags & KDC_F_SURREAL) != 0));
    for (j = 0; j < i; j++)
      if (out[i].action == out[j].action || act[out[i].action].cell == act[out[j].action].cell ||
          act[out[i].action].group == act[out[j].action].group)
        return 0;
  }
  return f <= 1u && z <= 2u;
}
/* An action's offer weight: near the player's cell, near the speaker's
 * disposition, plus the reinforcement of the player's own lean along it
 * (up to +15 at the axis limit). Surreal acts open with the dream (low
 * coherence/lucidity) and the habit; nonsense needs the habit or a deep dream
 * before it shows at all, then grows fastest with the habit. */
static uint8_t kdc_weight(const kdc_action_t *x, const kdc_context_t *c, const uint8_t *s, uint8_t player,
                          uint8_t dream) {
  uint8_t near = KDC_NEAR_W[kdc_distance(x->cell, player)], disp = KDC_DISP_W[kdc_distance(x->cell, c->disposition)],
          lean = kdc_lean(x, s), habit = kdc_habit(s);
  if (x->flags & KDC_F_NONSENSE)
    return dream + habit * 2u < 8u ? 0u : (uint8_t)((dream >> 1) + habit * 4u + (disp >> 1));
  if (x->flags & KDC_F_SURREAL) return (uint8_t)(dream + habit + (KDC_O3[player] == 2u ? near : 0u) + (disp >> 1));
  return (uint8_t)(1u + near + disp + (lean >> 2));
}
/* Three options from three different alignment cells. The request's fulfilment
 * is always offered when compatible; the rest lean toward the player's drift,
 * habit and the speaker's disposition, at least one pulls the other way, and
 * the wording escalates with how far the player already leans that way. */
static uint8_t kdc_choices(kdc_choice_t *out, const kdc_action_t *act, const kdc_variant_t *v, uint8_t n,
                           const kdc_context_t *c, uint8_t *s, uint16_t *r) {
  uint8_t i, j, a, w[KDC_ACTIONS], player, dream, kind, pulled = 0, close = 0, surreal = 0;
  uint16_t cells = 0, groups = 0;
  kdc_choice_t temp;
  kdc_init(s);
  player = kdc_cell(s);
  dream = (uint8_t)((15u - (s[1] & 15u)) + ((255u - c->lucid) >> 4));
  for (a = 0; a < KDC_ACTIONS; a++) {
    const kdc_action_t *x = act + a;
    uint8_t f = x->flags;
    w[a] = 0;
    if (f & (KDC_F_GIVE | KDC_F_SHOW)) {
      if (!c->compatible || !(f & (c->fulfill == KDC_SHOW ? KDC_F_SHOW : KDC_F_GIVE))) continue;
    }
    if ((f & KDC_F_AWARE) && !c->aware) continue;
    for (i = 0; i < n; i++)
      if (v[i].action == a && (!v[i].who || v[i].who == c->who)) break;
    if (i == n) continue;
    w[a] = kdc_weight(x, c, s, player, dream);
  }
  for (i = 0; i < 3u; i++) {
    kind = !i && c->compatible ? KDC_PICK_FULFIL : !pulled ? KDC_PICK_PULL : !close ? KDC_PICK_NEAR : KDC_PICK_ANY;
    a = kdc_pick(act, w, kind, cells, groups, surreal, s, player, r);
    if (a == 255u && kind != KDC_PICK_FULFIL) a = kdc_pick(act, w, KDC_PICK_ANY, cells, groups, surreal, s, player, r);
    if (a == 255u) return 0;
    out[i].action = a;
    cells |= (uint16_t)(1u << act[a].cell);
    groups |= (uint16_t)(1u << act[a].group);
    surreal = (uint8_t)(surreal + ((act[a].flags & KDC_F_SURREAL) != 0));
    pulled |= kdc_pulls(act + a, s, player);
    close |= (uint8_t)(kdc_distance(act[a].cell, player) <= 1u);
  }
  if (!kdc_set_ok(out, act)) return 0;
  for (i = 0; i < 3u; i++) {
    out[i].variant = kdc_variant(v, n, out[i].action, c->who, kdc_tier(act + out[i].action, s), s, r);
    if (out[i].variant == 255u) return 0;
    for (j = 0; j < i; j++)
      if (v[out[j].variant].key == v[out[i].variant].key) return 0;
  }
  for (i = 2u; i; i--) {
    j = (uint8_t)(((uint16_t)(uint8_t)kdc_roll(r) * (uint8_t)(i + 1u)) >> 8);
    temp = out[i];
    out[i] = out[j];
    out[j] = temp;
  }
  return 1;
}
/* A committed act: coherence (a failed fulfilment costs like a surreal act),
 * the alignment axes (or a pull toward center), the surreal habit (+2 for
 * surreal, +3 for nonsense, -1 for anything lucid) and the recent-key ring. */
static void kdc_commit(uint8_t *s, const kdc_action_t *act, uint8_t action, uint16_t key, uint8_t compatible) {
  uint8_t coherence, at, count, habit;
  int8_t d;
  const kdc_action_t *x = act + action;
  kdc_init(s);
  coherence = s[1] & 15u;
  at = s[3] & 3u;
  count = (s[3] >> 2) & 3u;
  habit = kdc_habit(s);
  d = ((x->flags & (KDC_F_GIVE | KDC_F_SHOW)) && !compatible) ? -2 : x->coherence;
  coherence = (uint8_t)(d < 0 ? (coherence > (uint8_t)-d ? coherence - (uint8_t)-d : 0u)
                              : (coherence + d > 15 ? 15u : coherence + (uint8_t)d));
  if (x->flags & KDC_F_CENTER) {
    kdc_toward0(s, 0, KDC_CENTER_PULL);
    kdc_toward0(s, 1, KDC_CENTER_PULL);
  } else {
    kdc_set(s, 0, (int16_t)(kdc_get(s, 0) + x->order * KDC_AXIS_STEP));
    kdc_set(s, 1, (int16_t)(kdc_get(s, 1) + x->heart * KDC_AXIS_STEP));
  }
  habit = (uint8_t)(x->flags & KDC_F_NONSENSE  ? habit + 3u
                    : x->flags & KDC_F_SURREAL ? habit + 2u
                    : habit                    ? habit - 1u
                                               : 0u);
  s[1] = (uint8_t)((KDC_VERSION << 4) | coherence);
  kdc_put16(s + 8u + at * 2u, key);
  if (count < KDC_KEYS) count++;
  s[3] = (uint8_t)((count << 2) | ((at + 1u) % KDC_KEYS));
  kdc_set_habit(s, habit);
}
/* Caller owns two alternating records and writes byte 26 last. A torn write
 * cannot supersede the last complete record. The caller keeps save profiles apart.
 * The record layout is unchanged; the state inside carries its own version. */
static uint16_t kdc_crc(const uint8_t *p, uint8_t n) {
  uint16_t c = 0xffffu;
  uint8_t i;
  while (n--) {
    c ^= (uint16_t)*p++ << 8;
    for (i = 0; i < 8u; i++) c = (c & 0x8000u) ? (uint16_t)((c << 1) ^ 0x1021u) : (uint16_t)(c << 1);
  }
  return c;
}
static void kdc_record_encode(uint8_t *p, const uint8_t *s, uint16_t owner, uint16_t serial) {
  p[0] = 'K';
  p[1] = 'D';
  p[2] = '0';
  p[3] = '1';
  kdc_put16(p + 4, serial);
  kdc_put16(p + 6, owner);
  memcpy(p + 8, s, KDC_STATE_BYTES);
  kdc_put16(p + 24, kdc_crc(p, 24));
  p[26] = 0xa5u;
  p[27] = 0x5au;
}
static uint8_t kdc_record_valid(const uint8_t *p, uint16_t owner) {
  return p[0] == 'K' && p[1] == 'D' && p[2] == '0' && p[3] == '1' && p[26] == 0xa5u && p[27] == 0x5au &&
         kdc_u16(p + 6) == owner && kdc_u16(p + 24) == kdc_crc(p, 24);
}
static uint8_t kdc_record_latest(const uint8_t *a, const uint8_t *b, uint16_t owner) {
  uint8_t av = kdc_record_valid(a, owner), bv = kdc_record_valid(b, owner);
  if (!av) return bv ? 1u : 255u;
  if (!bv) return 0;
  return (int16_t)(kdc_u16(b + 4) - kdc_u16(a + 4)) > 0 ? 1u : 0u;
}
#endif
#endif
