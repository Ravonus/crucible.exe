/* THE CRUCIBLE's AI (crucible_crux.h; reference: crux.py ai_choose). Each decision it plays the BAITER with chance
 * skill/256 (never leaves you something it knows you can take; sets traps it can take back), else the GREEDY (the
 * biggest thing now). It plans only with the recipes it knows: a seeded hash says which (know/256 of them).
 * It thinks one candidate a call (cx_ai_step), so the screen keeps moving; the choice never depends on the slicing. */
#ifndef CRUX_HOST
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#else
#include <string.h>
#endif
#include "crucible_crux.h"

uint16_t cx_ai_word, cx_ai_rng = 0x2b1du;
static cx_state bk_; /* the state, kept while a candidate is tried on cx */
static uint16_t seed_, know_, best_, base_;
static int16_t bv_;
static uint8_t k_, bait_, phase_, ti_, tj_, nt_, any_, tl_[14];
static const uint8_t W8[5] = {0, 4, 8, 13, 18};
static const uint8_t BIT8[8] = {1, 2, 4, 8, 16, 32, 64, 128};

static uint16_t xs16(uint16_t x) {
  x ^= (uint16_t)(x << 7);
  x ^= (uint16_t)(x >> 9);
  x ^= (uint16_t)(x << 8);
  return x;
}
uint8_t cx_knows(uint16_t seed, uint16_t know, uint16_t a, uint16_t b) BANKED {
  uint16_t lo = a, hi = b, h;
  if (know >= 256u) return 1;
  if (lo > hi) {
    lo = b;
    hi = a;
  }
  h = xs16((uint16_t)(lo * 0x9E37u + hi * 0x7F4Bu + seed));
  return (uint8_t)(h >> 8) < know;
}
static uint8_t knows_split(uint16_t x, uint16_t s) {
  uint16_t y = cx_split(x, s);
  return y != CX_NONE && cx_knows(seed_, know_, x, y);
}
static int16_t mat(const cx_side *p) {
  int16_t v = 0;
  uint8_t i, e;
  for (i = 0; i < p->n; i++) v += W8[cx_stars(p->bag[i])];
  for (e = 0; e < 4u; e++)
    if (p->four & BIT8[e]) v += 3;
  return v;
}
static int16_t value(uint8_t k) {
  int16_t v = (int16_t)((int16_t)cx.s[k].hp - (int16_t)cx.s[1u - k].hp) * 16 + mat(&cx.s[k]) - mat(&cx.s[1u - k]);
  if (cx.pot != CX_NONE) {
    int16_t pw = (int16_t)cx_power() * 8;
    v = cx.owner == k ? v + pw : v - pw;
  }
  return v;
}
/* would this side play w (no planned misses; only recipes it knows)? */
static uint8_t seen(uint16_t w) {
  uint8_t kind = CX_KIND(w), o;
  uint16_t x, r;
  if (kind == CX_FORGE) return cx_knows(seed_, know_, cx_tool(k_, CX_X(w)), cx_tool(k_, CX_Y(w)));
  if (kind != CX_ADD || cx.pot == CX_NONE) return 1;
  x = cx_tool(k_, CX_X(w));
  o = cx_outcome(k_, x, &r);
  if (o == CX_BUILD || o == CX_HIJACK) return cx_knows(seed_, know_, x, cx.pot);
  if (o == CX_SPLIT) return knows_split(x, cx.pot);
  return o != CX_MISS;
}
static int16_t greedy(uint16_t w) {
  uint8_t kind = CX_KIND(w), i, o;
  uint16_t x, r;
  if (kind == CX_PASS) return 0;
  if (kind == CX_POUR) return cx.heat >= 1u || cx_stars(cx.pot) >= 2u ? (int16_t)(50 + cx_power()) : 20;
  if (kind == CX_FORGE) return (int16_t)(40 + cx_stars(cx_recipe(cx_tool(k_, CX_X(w)), cx_tool(k_, CX_Y(w)))) * 4);
  i = CX_X(w);
  x = cx_tool(k_, i);
  o = cx_outcome(k_, x, &r);
  if (o == CX_HIJACK) return (int16_t)(200 + cx_stars(r) * 4);
  if (o == CX_SPLIT) return 150;
  if (o == CX_BREAK) return 120;
  if (o == CX_BUILD) return (int16_t)(60 + cx_stars(r) * 4);
  if (o == CX_START) return (int16_t)(30 + cx_stars(x) * 2 - (i >= 12u ? 6 : 0));
  return -50;
}
/* after k's move, on cx: what the other side can do to k's pot, as k knows (3 hijack, 2 split, 1 break), and the
 * first hijacker */
static uint8_t exposure(uint16_t *hy) {
  uint8_t o = (uint8_t)(1u - k_), i, e = 0, oc;
  uint16_t y, r;
  *hy = CX_NONE;
  if (cx.pot == CX_NONE || cx.owner != k_) return 0;
  for (i = 0; i < 16u; i++) {
    if (!cx_has(o, i)) {
      if (i < 12u) i = 11u;
      continue;
    }
    y = cx_tool(o, i);
    oc = cx_outcome(o, y, &r);
    if (oc == CX_HIJACK && cx_knows(seed_, know_, y, cx.pot)) {
      *hy = y;
      return 3;
    }
    if (oc == CX_SPLIT) {
      if (knows_split(y, cx.pot) && e < 2u) e = 2;
    } else if (oc == CX_BREAK && e < 1u)
      e = 1;
  }
  return e;
}
static int16_t baiter(uint16_t w) {
  int16_t v;
  uint8_t e, i, trap = 0;
  uint16_t y, r2, z;
  int16_t pw;
  memcpy(&bk_, &cx, sizeof cx);
  cx_do(w);
  v = (int16_t)(value(k_) - (int16_t)base_);
  if (cx.pot != CX_NONE && cx.owner == k_) {
    e = exposure(&y);
    pw = (int16_t)cx_power();
    if (e == 3u) {
      r2 = cx_recipe(y, cx.pot);
      for (i = 0; i < 16u; i++) {
        if (!cx_has(k_, i)) {
          if (i < 12u) i = 11u;
          continue;
        }
        z = cx_tool(k_, i);
        if (cx_recipe(z, r2) != CX_NONE && cx_knows(seed_, know_, z, r2)) {
          trap = 1;
          break;
        }
      }
      v -= trap ? (int16_t)(3 * pw) : (int16_t)(22 * pw + 16);
    } else if (e == 2u)
      v -= (int16_t)(13 * pw);
    else if (e == 1u)
      v -= (int16_t)(10 * pw);
    else if (CX_KIND(w) == CX_ADD && cx.heat > 0u)
      v += 5;
  }
  memcpy(&cx, &bk_, sizeof cx);
  return v;
}
void cx_ai_begin(uint8_t k, uint16_t seed, uint16_t know, uint16_t skill) BANKED {
  uint8_t i;
  k_ = k;
  seed_ = seed;
  know_ = know;
  cx_ai_rng = xs16(cx_ai_rng);
  bait_ = (uint8_t)((cx_ai_rng & 255u) < skill);
  base_ = bait_ ? (uint16_t)value(k) : 0u;
  nt_ = 0;
  for (i = 0; i < 16u; i++)
    if (cx_has(k, i))
      tl_[nt_++] = i;
    else if (i < 12u)
      i = 11u;
  phase_ = 0;
  ti_ = 0;
  tj_ = 1;
  any_ = 0;
  best_ = CX_NONE;
  bv_ = 0;
  cx_ai_word = CX_NONE;
}
/* the next legal word in the canonical order (crux.py legal), or CX_NONE when all are seen */
static uint16_t next_(void) {
  uint16_t w;
  for (;;) {
    if (phase_ == 0u) { /* the lead: POUR on your own pot, PASS on theirs or with nothing to play */
      phase_ = 1;
      if (cx.pot == CX_NONE) {
        if (!nt_) {
          phase_ = 3;
          return CX_ACT(CX_PASS, 0, 0);
        }
        continue;
      }
      return CX_ACT(cx.owner == k_ ? CX_POUR : CX_PASS, 0, 0);
    }
    if (phase_ == 1u) { /* every tool */
      if (cx.pot != CX_NONE && cx.owner == k_ && cx_locked_to(k_)) {
        phase_ = 3;
        continue;
      }
      if (ti_ < nt_) return CX_ACT(CX_ADD, tl_[ti_++], 0);
      phase_ = cx.pot == CX_NONE ? 2u : 3u;
      ti_ = 0;
      tj_ = 1;
      continue;
    }
    if (phase_ == 2u) { /* an empty pot: every pair that makes something */
      while (ti_ < nt_) {
        if (tj_ >= nt_) {
          ti_++;
          tj_ = (uint8_t)(ti_ + 1u);
          continue;
        }
        if (tl_[ti_] >= 12u) {
          ti_ = nt_;
          break;
        }
        w = CX_ACT(CX_FORGE, tl_[ti_], tl_[tj_]);
        tj_++;
        if (cx_recipe(cx_tool(k_, CX_X(w)), cx_tool(k_, CX_Y(w))) != CX_NONE) return w;
      }
      phase_ = 3;
      continue;
    }
    return CX_NONE;
  }
}
uint8_t cx_ai_step(void) BANKED {
  uint16_t w;
  int16_t v;
  if (cx_ai_word != CX_NONE) return 1;
  w = next_();
  if (w == CX_NONE) {
    cx_ai_word = best_;
    return 1;
  }
  if (!any_ && best_ == CX_NONE) best_ = w; /* nothing seen at all: the first legal move */
  if (!seen(w)) return 0;
  v = bait_ ? baiter(w) : greedy(w);
  if (!any_ || v > bv_) {
    best_ = w;
    bv_ = v;
    any_ = 1;
  }
  return 0;
}
