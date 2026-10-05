/* The sky over the room (crucible_sky.h): cheap integer astronomy for rare flavour, and the sign loader.
 *
 * The moon: a mean synodic month of 42524 minutes (29.530589 days) from the new moon of 2000-01-06 18:14 UTC; the
 * phases are eighths of it, "full" and "new" a window of 18 hours either side (the true moon wanders up to about 14
 * hours from the mean). A supermoon is a full moon within a day and a half of perigee (an anomalistic month of 39679
 * minutes from 2016-11-14 11:23). Equinoxes and solstices are fixed days (one either side counts), the meteor showers
 * their usual peak nights. Eclipses: the solar and lunar eclipses of 2026-2040 (69 of them, as day gaps and a type bit),
 * wrapped by the Saros (6585 days) outside those years; the Saros' last three years have none. The planets' backward
 * months: one known start each, repeated by the synodic period (Mercury 115.88 days, Venus 583.92, Mars 779.94), with
 * a fixed length; rough, and fine for a line someone says.
 * Clock time is the player's (local); the tables are UTC dates. A day either way is the error budget everywhere. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "crucible_sky.h"
#include "crucible_sign_marks.h"
#include "crucible_ui.h"

#define SYNODIC 42524ul /* minutes */
#define NEW0_DATE 5u /* 2000-01-06 */
#define NEW0_MOD 1094u /* 18:14 */
#define NEAR 1080u /* 18 hours */
#define ANOM 39679ul /* minutes */
#define PERIGEE_DATE 6162u /* 2016-11-14 */
#define PERIGEE_MOD 683u /* 11:23 */
#define SAROS 6585u
#define ECL_FIRST 9544u /* 2026-02-17 */
#define ECL_LAST 14932u /* 2040-11-18 */
static const uint8_t ECL_GAP[68] = {14,  162, 16,  162, 14,  148, 15,  15,  148, 14,  162, 16,  162, 14,  149, 14,  15,
                                    147, 15,  163, 14,  163, 14,  149, 14,  15,  147, 15,  163, 14,  162, 16,  147, 15,
                                    162, 15,  163, 14,  162, 16,  147, 15,  163, 14,  162, 16,  147, 15,  14,  148, 15,
                                    163, 14,  162, 16,  147, 15,  14,  148, 15,  162, 15,  162, 15,  148, 15,  162, 14};
static const uint8_t ECL_MOON[9] = {0x6a, 0x2b, 0xa9, 0xad, 0x54,
                                    0x2b, 0xa9, 0xad, 0x14}; /* bit i: eclipse i is the moon's */
/* the planets: a backward month's start (days since 2000), its length (days), the synodic period (hundredths of a day) */
static const uint16_t RETRO_AT[3] = {9553u, 9772u, 9871u}; /* 2026-02-26, 2026-10-03, 2027-01-10 */
static const uint8_t RETRO_LEN[3] = {23u, 42u, 80u};
static const uint32_t RETRO_SYN[3] = {11588ul, 58392ul, 77994ul};
static const uint8_t SEASON[8] = {3, 20, 6, 21, 9, 22, 12, 21};
static const uint8_t METEOR[14] = {1, 3, 4, 22, 5, 6, 8, 12, 10, 21, 11, 17, 12, 14};

static uint32_t since_(uint16_t date, uint16_t mod, uint16_t d0, uint16_t m0, uint32_t period) {
  int32_t t = ((int32_t)date - (int32_t)d0) * 1440l + (int32_t)mod - (int32_t)m0;
  t %= (int32_t)period;
  if (t < 0) t += (int32_t)period;
  return (uint32_t)t;
}
static uint32_t age_(uint16_t date, uint16_t mod) { return since_(date, mod, NEW0_DATE, NEW0_MOD, SYNODIC); }
uint8_t sky_phase(uint16_t date, uint16_t mod) BANKED {
  return (uint8_t)(((age_(date, mod) * 8ul + SYNODIC / 2u) / SYNODIC) & 7u);
}
uint8_t sky_eclipse(uint16_t date) BANKED {
  uint16_t d = date, x = ECL_FIRST;
  uint8_t i;
  while (d < ECL_FIRST) d = (uint16_t)(d + SAROS);
  if (d > ECL_LAST) d = (uint16_t)(ECL_FIRST + (d - ECL_FIRST) % SAROS);
  for (i = 0;; i++) {
    if (x == d) return (ECL_MOON[i >> 3] >> (i & 7u)) & 1u ? 2u : 1u;
    if (x > d || i >= sizeof ECL_GAP) return 0;
    x = (uint16_t)(x + ECL_GAP[i]);
  }
}
uint8_t sky_retro(uint16_t date) BANKED {
  uint8_t k, r = 0;
  int32_t t;
  for (k = 0; k < 3u; k++) {
    t = ((int32_t)date - (int32_t)RETRO_AT[k]) * 100l;
    t %= (int32_t)RETRO_SYN[k];
    if (t < 0) t += (int32_t)RETRO_SYN[k];
    if (t < (int32_t)RETRO_LEN[k] * 100l) r |= (uint8_t)(1u << k);
  }
  return r;
}
static uint8_t on_(const uint8_t *tab, uint8_t n, uint8_t m, uint8_t d, uint8_t slack) {
  uint8_t i;
  for (i = 0; i < n; i += 2u)
    if (tab[i] == m && d + slack >= tab[i + 1u] && d <= tab[i + 1u] + slack) return 1;
  return 0;
}
uint8_t sky_event(const crucible_time_ctx *t) BANKED {
  uint32_t a;
  uint8_t e, r, y, m, d;
  if (!(t->flags & CT_F_KNOWN)) return SKY_NONE;
  e = sky_eclipse(t->date);
  if (e) return e == 1u ? SKY_ECLIPSE_SUN : SKY_ECLIPSE_MOON;
  if (t->bmonth && t->bmonth == t->month && t->bday == t->day) return SKY_BIRTHDAY;
  a = age_(t->date, t->mod);
  if (a + NEAR >= SYNODIC / 2u && a <= SYNODIC / 2u + NEAR) {
    /* the full moon before this one fell in the same month: a blue moon */
    a = (uint32_t)t->date * 1440ul + t->mod - SYNODIC;
    time_civil((uint16_t)(a / 1440ul), &y, &m, &d);
    if (m == t->month) return SKY_BLUE;
    a = since_(t->date, t->mod, PERIGEE_DATE, PERIGEE_MOD, ANOM);
    return a <= 2160u || a + 2160u >= ANOM ? SKY_SUPER : SKY_FULL;
  }
  if (a <= NEAR || a + NEAR >= SYNODIC) return SKY_NEW;
  if (on_(SEASON, sizeof SEASON, t->month, t->day, 1u)) return SKY_SEASON;
  if (on_(METEOR, sizeof METEOR, t->month, t->day, 0u)) return SKY_METEORS;
  if (t->weekday == 5u && t->day == 13u) return SKY_FRI13;
  r = sky_retro(t->date);
  return r & 1u ? SKY_MERCURY : r & 2u ? SKY_VENUS : r & 4u ? SKY_MARS : SKY_NONE;
}

/* ---- the marks ---- */
static const uint8_t BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
/* t: 0..63, the loader's frame: dissolves in over 16, the dither crawls, dissolves out over the last 16 */
void sky_mark_tiles(uint8_t sign, uint8_t t, uint8_t *out) BANKED {
  const uint8_t *m = sign_marks + (uint16_t)(sign < SIGN_MARKS - 1u ? sign : SIGN_MARKS - 1u) * 32u;
  uint8_t tile, y, x, X, Y, b, lo, hi, v, off = (uint8_t)(t >> 2),
                                          show = t < 16u    ? (uint8_t)t
                                                 : t >= 48u ? (uint8_t)(63u - t)
                                                            : 16u;
  for (tile = 0; tile < 4u; tile++)
    for (y = 0; y < 8u; y++) {
      Y = (uint8_t)(y + ((tile & 2u) << 2));
      lo = hi = 0;
      for (x = 0; x < 8u; x++) {
        X = (uint8_t)(x + ((tile & 1u) << 3));
        if (!((m[Y * 2u + (X >> 3)] << (X & 7u)) & 0x80u)) continue;
        b = BAYER[((Y & 3u) << 2) | ((X + off) & 3u)];
        if (b >= show) continue;
        v = (uint8_t)((30u - X - Y) * 4u + b * 8u);
        v = v >= 96u ? 3u : v >= 48u ? 2u : 1u;
        if (v & 1u) lo |= (uint8_t)(0x80u >> x);
        if (v & 2u) hi |= (uint8_t)(0x80u >> x);
      }
      *out++ = lo;
      *out++ = hi;
    }
}
/* ---- the loader ---- */
#define LX 76u /* the mark's top left on screen; the moon to its right */
#define LY 84u
static uint8_t sign_, last_, saved_[8];
static uint16_t t0_;
/* the mark's own colours in OBJ palette 6 (the chrome's is put back after): a night-violet ramp that reads on the pale
 * title panel, 3 the lit edge (palette RAM is read and written just after a VBlank) */
static const uint16_t MARK_PAL[4] = {0, RGB8(36, 24, 72), RGB8(108, 64, 184), RGB8(196, 112, 236)};
static void pal_save_(void) {
  uint8_t i;
  wait_vbl_done();
  for (i = 0; i < 8u; i++) {
    OCPS_REG = (uint8_t)(6u * 8u + i);
    saved_[i] = OCPD_REG;
  }
}
static void pal_back_(void) {
  uint8_t i;
  wait_vbl_done();
  OCPS_REG = (uint8_t)(0x80u | 6u * 8u);
  for (i = 0; i < 8u; i++) OCPD_REG = saved_[i];
}
void sky_loader_open(void) BANKED {
  uint8_t i, buf[64], sign = time_sign();
  crucible_time_ctx c;
  sign_ = sign;
  t0_ = sys_time;
  last_ = 0xffu;
  memset(buf, GLYPH(' '), 20);
  memset(buf + 20, 7u, 20); /* the menu panel and its cues go quiet: only the mark */
  for (i = 9u; i < 18u; i++)
    if (i != 16u) {
      VBK_REG = 1;
      set_bkg_tiles(i == 17u ? 0u : 3u, i, i == 17u ? 20u : 14u, 1, buf + 20);
      VBK_REG = 0;
      set_bkg_tiles(i == 17u ? 0u : 3u, i, i == 17u ? 20u : 14u, 1, buf);
    }
  crucible_time_context(&c);
  pal_save_();
  set_sprite_palette(6, 1, MARK_PAL);
  VBK_REG = 0;
  sky_mark_tiles(sign, 0, buf);
  set_sprite_data(0, 4, buf);
  set_sprite_data(4, 1, moon_tiles + (uint16_t)((c.flags & CT_F_KNOWN) ? sky_phase(c.date, c.mod) : 4u) * 16u);
  for (i = 0; i < 5u; i++) {
    set_sprite_tile(i, i);
    set_sprite_prop(i, 6u);
  }
  move_sprite(0, LX + 8u, LY + 16u);
  move_sprite(1, LX + 16u, LY + 16u);
  move_sprite(2, LX + 8u, LY + 24u);
  move_sprite(3, LX + 16u, LY + 24u);
  move_sprite(4, 0, 0);
}
uint8_t sky_loader_tick(void) BANKED {
  uint8_t t = (uint8_t)((uint16_t)(sys_time - t0_) > 63u ? 64u : (uint16_t)(sys_time - t0_)), i, buf[64];
  if (t >= 64u) {
    for (i = 0; i < 5u; i++) move_sprite(i, 0, 0);
    pal_back_();
    return 1;
  }
  if ((t >> 1) != last_) {
    last_ = (uint8_t)(t >> 1);
    sky_mark_tiles(sign_, t, buf);
    VBK_REG = 0;
    set_sprite_data(0, 4, buf);
  }
  if (t >= 12u && t < 52u)
    move_sprite(4, LX + 8u + 22u, LY + 16u + 4u);
  else
    move_sprite(4, 0, 0); /* the moon shows while the mark does */
  return 0;
}
