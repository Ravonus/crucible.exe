/* Time awareness without a clock chip (see crucible_time.h and docs/time-awareness.md).
 *
 * The date and time are asked once, when a game is first started from PLAY (crucible_menu.c), never on the title card;
 * or a host (the website's emulator) writes a HOST CLOCK block into SRAM before power-on, used silently (and consumed).
 * VBlanks keep the clock and the calendar going while the cartridge runs; switched off, the clock stops, so without a
 * host clock the time away is unknown (CT_AWAY_UNKNOWN) and nothing reacts to it. Effects are whispers (a card on the
 * bench, drawn by crucible_feats.c), the pause menu at a special minute (crucible_menu.c), quiet leanings in a story
 * run's saga (lucidity and the truth matrix), and a talk called in at an angel minute (crucible_flow.c), never explained.
 *
 * SRAM: two 32-byte records in bank 15 at 0x1F80 (A) and 0x1FA0 (B), linear 0x1FF80 / 0x1FFA0, above the eggs
 * (0x1F00..0x1F06) and the story record (0x1D00..0x1DB7); the host's 16-byte block at 0x1FE0 (linear 0x1FFE0). Neither
 * save, the slot wipes nor RESET GAME write there. Each record write invalidates the commit byte first and sets it last;
 * the newer valid record by wrapping serial wins. Version 1 records (minute of the week) are read and migrated. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_state.h"
#include "crucible_player.h"
#include "crucible_codec.h"
#include "crucible_storyrun.h"
#include "crucible_time.h"

#define CT_REC_A 0x1ff80ul
#define CT_REC_B 0x1ffa0ul
#define CT_REC_BYTES 32u
#define CT_COMMIT 27u
#define CT_COMMITTED 0xc7u
#define CT_VERSION 2u
#define CT_DAY_MIN 1440u /* minutes in a day */
#define CT_DEFAULT_DATE 9497u /* 2026-01-01: the guess before the player (or a host) ever says */
#define CT_DEFAULT_MOD                                                                                                 \
  480u /* 8:00 AM: two hours before the next special minute (10:01), so a prefilled clock that is just kept calls nobody in soon */
#define CT_V1_BASE 9500u /* a version 1 record's week 0 (no date was kept): 2026-01-04, a Sunday */
#define CT_FRAME 4389u /* one VBlank in 1/262144 s (70224 clocks at 4194304 Hz) */
#define CT_RETURNING 10u /* minutes the return colours things ("for a bit") */
#define CT_KNOWN 1u /* persisted flags: the clock was set (by the player or a host) */
#define CT_ASKED 2u /* ... the date and time were asked (answered or skipped: never again) */
#define CT_BDAY 4u /* ... the birthday was asked (given or skipped) */

/* pending whispers, highest priority first */
#define W_SPECIAL 1u
#define W_RETURN 2u
#define W_MARATHON 4u
#define W_NIGHT 8u
#define W_DAWN 16u
/* once per power-on */
#define O_NIGHT 1u
#define O_DAWN 2u
#define O_PAUSE 4u

static uint8_t loaded_, at_, flags_, sec_, reported_, streak_, best_, quick_, long_, prev_part_, special_, host_, bmon_,
    bday_;
static uint8_t pending_, w_ret_, w_sp_, w_mar_, once_, story_mask_, menu_sp_, calm_, roll_, angel_, act_set_, cy_, cm_,
    cd_;
static uint16_t serial_, mod_, date_, sess_min_, last_day_, returns_, last_vbl_, report_min_, next_mar_, w_sp_mod_,
    menu_mod_, act_mod_, act_date_, cdate_ = 0xffffu;
static uint32_t frac_, total_;

/* ---- portable rules (pure C, testable on a host) ---- */
static uint8_t ct_part(uint8_t hour) {
  return hour >= 22u || hour < 5u ? CT_NIGHT : hour < 8u ? CT_DAWN : hour < 18u ? CT_DAY : CT_DUSK;
}
static const uint8_t SP_H[14] = {1, 2, 3, 4, 5, 11, 12, 4, 10, 0, 0, 12, 12, 10};
static const uint8_t SP_M[14] = {11, 22, 33, 44, 55, 11, 34, 4, 10, 0, 0, 12, 21, 1};
static uint8_t ct_special(uint8_t hour, uint8_t minute) {
  uint8_t h = (uint8_t)(hour % 12u), i;
  if (!minute && !hour) return CT_SP_MIDNIGHT;
  if (!minute && hour == 12u) return CT_SP_NOON;
  if (!h) h = 12u;
  for (i = 0; i < 14u; i++)
    if (i != 9u && i != 10u && SP_H[i] == h && SP_M[i] == minute) return (uint8_t)(i + 1u);
  return CT_SP_NONE;
}
static uint8_t ct_bucket(uint32_t away) {
  return away < 5ul       ? CT_AWAY_NOW
         : away < 60ul    ? CT_AWAY_MINUTES
         : away < 1440ul  ? CT_AWAY_HOURS
         : away < 10080ul ? CT_AWAY_DAY
                          : CT_AWAY_WEEK;
}
/* the calendar: years since 2000 (2000 leaps; 2100 and 2200 do not) */
static uint8_t ct_leap(uint8_t y) { return (uint8_t)(!(y & 3u) && y != 100u && y != 200u); }
static const uint8_t MDAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static uint8_t ct_mdays(uint8_t y, uint8_t m) { return m == 2u && ct_leap(y) ? 29u : MDAYS[(uint8_t)((m - 1u) % 12u)]; }
static uint16_t ct_days(uint8_t y, uint8_t m, uint8_t d) {
  uint16_t n = 0;
  uint8_t i;
  for (i = 0; i < y; i++) n = (uint16_t)(n + 365u + ct_leap(i));
  for (i = 1; i < m; i++) n = (uint16_t)(n + ct_mdays(y, i));
  return (uint16_t)(n + d - 1u);
}
static void ct_civil(uint16_t n, uint8_t *y, uint8_t *m, uint8_t *d) {
  uint8_t i = 0, k = 1;
  uint16_t len;
  while (n >= (len = (uint16_t)(365u + ct_leap(i)))) {
    n = (uint16_t)(n - len);
    i++;
  }
  while (n >= ct_mdays(i, k)) {
    n = (uint16_t)(n - ct_mdays(i, k));
    k++;
  }
  *y = i;
  *m = k;
  *d = (uint8_t)(n + 1u);
}
static uint8_t ct_weekday(uint16_t n) { return (uint8_t)((n + 6u) % 7u); } /* 0 SUN: 2000-01-01 was a Saturday */
/* the sun sign of a birthday (tropical): the day each month's second sign begins */
static const uint8_t CUSP[12] = {20, 19, 21, 20, 21, 21, 23, 23, 23, 23, 22, 22};
static uint8_t ct_sign(uint8_t m, uint8_t d) {
  uint8_t s;
  if (!m || m > 12u || !d) return 0xffu;
  s = (uint8_t)((m + 8u) % 12u); /* the sign at the month's start: JAN CAPRICORN ... APR ARIES */
  return d >= CUSP[m - 1u] ? (uint8_t)((s + 1u) % 12u) : s;
}
static uint16_t ct_crc(const uint8_t *p, uint8_t n) {
  uint16_t h = 0xffffu;
  uint8_t i;
  while (n--) {
    h ^= (uint16_t)*p++ << 8;
    for (i = 0; i < 8u; i++) h = (h & 0x8000u) ? (uint16_t)((h << 1) ^ 0x1021u) : (uint16_t)(h << 1);
  }
  return h;
}
static uint16_t u16_(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static void put16_(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
static uint8_t ct_valid(const uint8_t *r) {
  if (r[0] != 'C' || r[1] != 'T' || r[CT_COMMIT] != CT_COMMITTED || ct_crc(r, 30u) != u16_(r + 30) || r[8] >= 60u)
    return 0;
  return r[2] == 1u ? u16_(r + 6) < 10080u : r[2] == CT_VERSION && u16_(r + 6) < CT_DAY_MIN;
}
/* 0 A, 1 B, 255 neither */
static uint8_t ct_latest(const uint8_t *a, const uint8_t *b) {
  uint8_t va = ct_valid(a), vb = ct_valid(b);
  if (va && vb) return (int16_t)(u16_(b + 4) - u16_(a + 4)) > 0 ? 1u : 0u;
  return va ? 0u : vb ? 1u : 255u;
}
/* The HOST CLOCK block (time-awareness.md): 'H' 'C' 'L' 'K', version 1, flags, year (u16 LE), month, day, hour, minute,
 * second, 0, CRC-16 (LE) of bytes 0..13. Returns the date (days since 2000) and fills the minute and second; 0xffff when
 * the block is absent or invalid. */
static uint16_t ct_host(const uint8_t *h, uint16_t *mod, uint8_t *sec) {
  uint16_t y;
  if (h[0] != 'H' || h[1] != 'C' || h[2] != 'L' || h[3] != 'K' || h[4] != 1u || ct_crc(h, 14u) != u16_(h + 14))
    return 0xffffu;
  y = u16_(h + 6);
  if (y < 2000u || y > 2099u || !h[8] || h[8] > 12u || !h[9] || h[9] > ct_mdays((uint8_t)(y - 2000u), h[8]) ||
      h[10] >= 24u || h[11] >= 60u || h[12] >= 60u)
    return 0xffffu;
  *mod = (uint16_t)((uint16_t)h[10] * 60u + h[11]);
  *sec = h[12];
  return ct_days((uint8_t)(y - 2000u), h[8], h[9]);
}

/* ---- the record ---- */
static void read_(uint32_t at, uint8_t *r, uint8_t n) {
  uint8_t i;
  for (i = 0; i < n; i++) r[i] = crucible_sram_read(0, at + i);
  SWITCH_RAM(0);
  DISABLE_RAM;
}
static void persist_(void);
static void day_seen_(void);
static void load_(void) {
  uint8_t a[CT_REC_BYTES], b[CT_REC_BYTES], *r, hs;
  uint16_t hd, hm;
  int32_t away;
  loaded_ = 1;
  next_mar_ = 120u;
  date_ = CT_DEFAULT_DATE;
  mod_ = CT_DEFAULT_MOD;
  reported_ = CT_AWAY_UNKNOWN;
  read_(CT_REC_A, a, CT_REC_BYTES);
  read_(CT_REC_B, b, CT_REC_BYTES);
  at_ = ct_latest(a, b);
  if (at_ != 255u) {
    r = at_ ? b : a;
    flags_ = r[3];
    serial_ = u16_(r + 4);
    sec_ = r[8];
    total_ = (uint32_t)u16_(r + 12) | ((uint32_t)u16_(r + 14) << 16);
    last_day_ = u16_(r + 18);
    streak_ = r[20];
    best_ = r[21];
    returns_ = u16_(r + 22);
    quick_ = r[24];
    long_ = r[25];
    if (r[2] == 1u) { /* a clock from before the calendar: the same weekday and time, in the week of CT_V1_BASE on */
      hm = u16_(r + 6);
      mod_ = (uint16_t)(hm % CT_DAY_MIN);
      date_ = (uint16_t)(CT_V1_BASE + u16_(r + 10) * 7u + hm / CT_DAY_MIN);
      last_day_ = (uint16_t)(last_day_ + CT_V1_BASE);
      if (flags_ & CT_KNOWN) flags_ |= CT_ASKED;
    } else {
      mod_ = u16_(r + 6);
      date_ = u16_(r + 10);
      bmon_ = r[26];
      bday_ = r[28];
    }
  }
  roll_ = (uint8_t)(serial_ ^ (serial_ >> 8) ^ (uint8_t)total_);
  /* a host's clock, left before power-on: used silently, then consumed (the host writes a fresh one each boot) */
  read_(CT_HOST_AT, a, 16u);
  hd = ct_host(a, &hm, &hs);
  if (hd != 0xffffu) {
    away = ((int32_t)hd - (int32_t)date_) * CT_DAY_MIN + (int32_t)hm - (int32_t)mod_;
    if ((flags_ & CT_KNOWN) && away >= 0) {
      reported_ = ct_bucket((uint32_t)away);
      if (reported_ >= CT_AWAY_NOW) {
        if (returns_ < 0xffffu) returns_++;
        if (reported_ <= CT_AWAY_MINUTES && quick_ < 255u) quick_++;
        if (reported_ >= CT_AWAY_DAY && long_ < 255u) long_++;
      }
      w_ret_ = reported_;
      pending_ |= W_RETURN;
    }
    date_ = hd;
    mod_ = hm;
    sec_ = hs;
    host_ = 1;
    flags_ |= CT_KNOWN | CT_ASKED;
    crucible_sram_write(0, CT_HOST_AT, 0);
    SWITCH_RAM(0);
    DISABLE_RAM;
    day_seen_();
    persist_();
  }
}
static void persist_(void) {
  uint8_t r[CT_REC_BYTES], i;
  uint32_t to;
  memset(r, 0, sizeof r);
  r[0] = 'C';
  r[1] = 'T';
  r[2] = CT_VERSION;
  r[3] = flags_;
  serial_++;
  put16_(r + 4, serial_);
  put16_(r + 6, mod_);
  r[8] = sec_;
  r[9] = reported_;
  put16_(r + 10, date_);
  put16_(r + 12, (uint16_t)total_);
  put16_(r + 14, (uint16_t)(total_ >> 16));
  put16_(r + 16, sess_min_);
  put16_(r + 18, last_day_);
  r[20] = streak_;
  r[21] = best_;
  put16_(r + 22, returns_);
  r[24] = quick_;
  r[25] = long_;
  r[26] = bmon_;
  r[28] = bday_;
  r[CT_COMMIT] = CT_COMMITTED;
  put16_(r + 30, ct_crc(r, 30u));
  at_ = at_ == 0u ? 1u : 0u;
  to = at_ ? CT_REC_B : CT_REC_A; /* never over the newest valid record */
  crucible_sram_write(0, to + CT_COMMIT, 0);
  for (i = 0; i < CT_REC_BYTES; i++)
    if (i != CT_COMMIT) crucible_sram_write(0, to + i, r[i]);
  crucible_sram_write(0, to + CT_COMMIT, CT_COMMITTED);
  SWITCH_RAM(0);
  DISABLE_RAM;
}

/* ---- effects ---- */
static uint8_t hour_(void) { return (uint8_t)(mod_ / 60u); }
static uint8_t minute_of_(void) { return (uint8_t)(mod_ % 60u); }
static void civil_(void) {
  if (cdate_ != date_) {
    cdate_ = date_;
    ct_civil(date_, &cy_, &cm_, &cd_);
  }
}
static void day_seen_(void) {
  uint16_t d = date_;
  if (streak_ && (int16_t)(d - last_day_) <= 0) return; /* the same day, or a correction back: the streak stands */
  streak_ = streak_ && d == (uint16_t)(last_day_ + 1u) ? (uint8_t)(streak_ < 255u ? streak_ + 1u : 255u) : 1u;
  if (streak_ > best_) best_ = streak_;
  last_day_ = d;
}
static crucible_story *saga_(void) { return story_on ? talk_saga() : (crucible_story *)0; }
static void story_act_(uint8_t act, int8_t lucid) {
  crucible_story *s = saga_();
  if (!s) return;
  cru_story_act(s, act);
  if (lucid) cru_story_lucid(s, lucid);
  story_save();
}
/* the clock turned to a new minute (the newest of a batch) */
static void minute_events_(void) {
  uint8_t h = hour_(), part = ct_part(h), sp = ct_special(h, minute_of_());
  crucible_story *s;
  if (sess_min_ >= next_mar_) { /* two hours, four, then every hour: the dream deepens the longer you stay */
    w_mar_ = next_mar_ >= 240u ? (uint8_t)(2u + (uint8_t)((next_mar_ - 240u) / 60u)) : 1u;
    pending_ |= W_MARATHON;
    next_mar_ = next_mar_ < 240u ? 240u : (uint16_t)(next_mar_ + 60u);
    story_act_(CRU_ACT_LOOP, -4);
  }
  if (!(flags_ & CT_KNOWN)) {
    special_ = 0;
    return;
  }
  if (part == CT_NIGHT && !(once_ & O_NIGHT)) {
    once_ |= O_NIGHT;
    pending_ |= W_NIGHT;
    story_act_(CRU_ACT_DEEP, 0);
  }
  if (part == CT_DAWN && prev_part_ == CT_NIGHT && !(once_ & O_DAWN)) {
    once_ |= O_DAWN;
    pending_ |= W_DAWN;
  } /* played through the night */
  prev_part_ = part;
  if (sp != special_) {
    special_ = sp;
    if (sp) {
      w_sp_ = sp;
      w_sp_mod_ = mod_;
      pending_ |= W_SPECIAL;
      if ((s = saga_()) != 0) {
        cru_story_event(s, CRU_EV_FLAG, (uint16_t)(0xe0u | sp), mod_);
        story_save();
      }
    }
    /* something meaningful done in the minute before an angel minute: someone is called in (once a minute) */
    if (CT_ANGEL(sp) && act_set_ && act_mod_ == (mod_ ? mod_ - 1u : CT_DAY_MIN - 1u) &&
        act_date_ == (mod_ ? date_ : (uint16_t)(date_ - 1u))) {
      act_set_ = 0;
      angel_ = 1;
    }
  }
}
static void minutes_(uint16_t n) {
  while (n--) {
    if (++mod_ >= CT_DAY_MIN) {
      mod_ = 0;
      date_++;
      if (flags_ & CT_KNOWN) day_seen_();
    }
    if (sess_min_ < 0xffffu) sess_min_++;
  }
  minute_events_();
  persist_();
}
void time_poll(void) BANKED {
  uint16_t now = sys_time, n, s;
  if (!loaded_) {
    load_();
    last_vbl_ = now;
    prev_part_ = ct_part(hour_());
    special_ = 0xffu;
    return;
  }
  n = (uint16_t)(now - last_vbl_);
  last_vbl_ = now;
  if (!n) return;
  frac_ += (uint32_t)n * CT_FRAME;
  s = (uint16_t)(frac_ >> 18);
  frac_ &= 0x3ffffu;
  if (!s) return;
  total_ += s;
  s += sec_;
  n = 0;
  while (s >= 60u) {
    s -= 60u;
    n++;
  }
  sec_ = (uint8_t)s;
  if (n) minutes_(n);
}
void time_mark_act(void) BANKED {
  time_poll();
  act_mod_ = mod_;
  act_date_ = date_;
  act_set_ = 1;
}
void time_angel_arm(void) BANKED { angel_ = 1; }
uint8_t time_angel_take(void) BANKED {
  uint8_t a;
  time_poll();
  a = angel_;
  angel_ = 0;
  return a;
}

/* ---- the asks: the date and time, then the birthday; once, at the first game start ---- */
uint8_t time_ask_mode(void) BANKED {
  time_poll();
  return (flags_ & (CT_KNOWN | CT_ASKED)) ? CT_ASK_DONE : CT_ASK_FIRST;
}
uint8_t time_birthday_due(void) BANKED {
  time_poll();
  return (uint8_t)!(flags_ & CT_BDAY);
}
uint8_t time_month_days(uint8_t year, uint8_t month) BANKED { return ct_mdays(year, month); }
void time_ask_prefill(uint8_t *f) BANKED {
  time_poll();
  civil_();
  f[CT_FY] = cy_;
  f[CT_FM] = cm_;
  f[CT_FD] = cd_;
  f[CT_FH] = hour_();
  f[CT_FMIN] = minute_of_();
}
void time_answer(uint8_t mode, uint8_t answer, const uint8_t *f) BANKED {
  uint8_t d;
  time_poll();
  if (answer == CT_ANSWER_OK) {
    d = f[CT_FD];
    if (d > ct_mdays(f[CT_FY], f[CT_FM])) d = ct_mdays(f[CT_FY], f[CT_FM]);
    date_ = ct_days(f[CT_FY], f[CT_FM], d);
    mod_ = (uint16_t)((uint16_t)(f[CT_FH] % 24u) * 60u + f[CT_FMIN] % 60u);
    sec_ = 0;
    frac_ = 0;
    if (mode == CT_ASK_FIRST || !(flags_ & CT_KNOWN)) {
      reported_ = CT_AWAY_FIRST;
      report_min_ = sess_min_;
      w_ret_ = CT_AWAY_FIRST;
      pending_ |= W_RETURN;
    }
    flags_ |= CT_KNOWN;
    day_seen_();
  }
  if (mode == CT_ASK_FIRST || answer == CT_ANSWER_OK) flags_ |= CT_ASKED;
  prev_part_ = ct_part(hour_());
  special_ = 0xffu;
  minute_events_();
  persist_();
}
void time_birthday_set(uint8_t month, uint8_t day) BANKED {
  time_poll();
  if (month && month <= 12u && day && day <= (month == 2u ? 29u : MDAYS[month - 1u])) {
    bmon_ = month;
    bday_ = day;
  } else
    bmon_ = bday_ = 0;
  flags_ |= CT_BDAY;
  persist_();
}
void time_civil(uint16_t date, uint8_t *year, uint8_t *month, uint8_t *day) BANKED { ct_civil(date, year, month, day); }
uint8_t time_sign(void) BANKED {
  time_poll();
  return ct_sign(bmon_, bday_);
}

/* ---- text ---- */
static const char DAYS[7][4] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
void time_format(char *out, uint8_t day, uint8_t hour, uint8_t minute) BANKED {
  uint8_t h = (uint8_t)(hour % 12u), pad = (uint8_t)(day & 0x80u);
  day &= 0x7fu;
  if (!h) h = 12u;
  if (day < 7u) {
    memcpy(out, DAYS[day], 3);
    out[3] = ' ';
    out += 4;
  }
  if (h >= 10u)
    *out++ = '1';
  else if (pad)
    *out++ = ' ';
  *out++ = (char)('0' + h % 10u);
  *out++ = ':';
  *out++ = (char)('0' + minute / 10u);
  *out++ = (char)('0' + minute % 10u);
  *out++ = ' ';
  *out++ = hour < 12u ? 'A' : 'P';
  *out++ = 'M';
  *out = 0;
}
void time_now_text(char *out, uint8_t with_day) BANKED {
  time_poll();
  time_format(out, with_day ? ct_weekday(date_) : 7u, hour_(), minute_of_());
}
static const char MONTHS[12][4] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
void time_date_text(char *out, const uint8_t *f) BANKED {
  uint8_t m = (uint8_t)((f[CT_FM] - 1u) % 12u), d = f[CT_FD], y = f[CT_FY];
  memcpy(out, DAYS[ct_weekday(ct_days(y, (uint8_t)(m + 1u), d))], 3);
  out[3] = ' ';
  memcpy(out + 4, MONTHS[m], 3);
  out[7] = ' ';
  out[8] = (char)('0' + d / 10u);
  out[9] = (char)('0' + d % 10u);
  out[10] = ' ';
  out[11] = '2';
  out[12] = (char)('0' + y / 100u);
  out[13] = (char)('0' + (y / 10u) % 10u);
  out[14] = (char)('0' + y % 10u);
  out[15] = 0;
}

/* the lines: plain, a little wrong, never explained (18 characters at most) */
static const char *const L_RETURN[8][3] = {{0, 0, 0},
                                           {"THE CLOCK STARTS.", "OKAY. NOTED.", "THE CLOCK STARTS."},
                                           {0, 0, 0},
                                           {"BACK ALREADY?", "YOU NEVER LEFT.", "STILL WARM."},
                                           {"THAT WAS QUICK.", "THE ROOM WAITED.", "NOTHING MOVED."},
                                           {"LIGHTS STAYED ON.", "SOMEONE SAT HERE.", "THE HUM KEPT ON."},
                                           {"THE DUST SETTLED.", "A DAY. OR TWO?", "IT KEPT HUMMING."},
                                           {"YOU WERE GONE.", "WE KEPT YOUR SEAT.", "DID YOU DREAM?"}};
static const char *const L_SPECIAL[CT_SP_COUNT] = {0,
                                                   "ALL IN A ROW.",
                                                   "AGAIN. AGAIN.",
                                                   "THE HUM STOPPED.",
                                                   "THE HALL IS LONGER",
                                                   "ALMOST.",
                                                   "MAKE A WISH.",
                                                   "IN ORDER.",
                                                   "ROOM NOT FOUND.",
                                                   "TEN, TEN.",
                                                   "NEW DAY. SAME ROOM",
                                                   "NO SHADOWS NOW.",
                                                   "BOTH HANDS UP.",
                                                   "IT TURNED BACK.",
                                                   "THE DOOR, TWICE."};
static const char *const L_MARATHON[6] = {"STILL HERE?",        "HAVE YOU EATEN?",    "HAVE YOU BLINKED?",
                                          "THE CARPET IS DAMP", "WE'RE ALL HERE NOW", "WHAT DAY IS IT?"};
static const char *const L_NIGHT[3] = {"IT'S LATE.", "THE HUM IS LOUDER.", "EVERYONE IS ASLEEP"};
static const char *const L_DAWN[2] = {"IT FEELS LIKE DAWN", "NO WINDOWS. STILL,"};
static uint8_t pick_(uint8_t n) {
  roll_ = (uint8_t)(roll_ * 5u + 1u);
  return (uint8_t)((roll_ >> 3) % n);
}
/* Called by feats_toast every pass with whether a card may show: a whisper waits for two calm seconds on the bench
 * (counted in VBlanks: a busy bench runs fewer passes than frames), and four after another one. A special minute's
 * whisper is dropped once its minute has passed. */
static uint8_t calm_need_ = 120u;
static uint16_t calm_at_;
uint8_t time_whisper_ready(uint8_t allowed) BANKED {
  time_poll();
  if ((pending_ & W_SPECIAL) && w_sp_mod_ != mod_) pending_ &= (uint8_t)~W_SPECIAL;
  if (!allowed) {
    calm_ = 0;
    return 0;
  }
  if (!calm_) {
    calm_ = 1;
    calm_at_ = sys_time;
  }
  return pending_ && (uint16_t)(sys_time - calm_at_) >= calm_need_;
}
uint8_t time_whisper(char *line1, char *line2) BANKED {
  const char *l = 0;
  uint8_t k;
  line1[0] = line2[0] = 0;
  if ((pending_ & W_SPECIAL) && w_sp_mod_ != mod_) pending_ &= (uint8_t)~W_SPECIAL;
  if (pending_ & W_SPECIAL) {
    pending_ &= (uint8_t)~W_SPECIAL;
    time_format(line1, 7u, hour_(), minute_of_());
    l = L_SPECIAL[w_sp_ < CT_SP_COUNT ? w_sp_ : 0u];
  } else if (pending_ & W_RETURN) {
    pending_ &= (uint8_t)~W_RETURN;
    k = w_ret_ < 8u ? w_ret_ : 0u;
    time_format(line1, ct_weekday(date_), hour_(), minute_of_());
    l = k == CT_AWAY_HOURS && streak_ >= 3u ? "YOU KEEP RETURNING" : L_RETURN[k][pick_(3u)];
  } else if (pending_ & W_MARATHON) {
    pending_ &= (uint8_t)~W_MARATHON;
    if (flags_ & CT_KNOWN) time_format(line1, 7u, hour_(), minute_of_());
    l = L_MARATHON[w_mar_ <= 1u ? pick_(2u)
                                : (uint8_t)(2u + (w_mar_ <= 2u ? pick_(2u) : (uint8_t)((w_mar_ - 1u) & 3u)))];
  } else if (pending_ & W_NIGHT) {
    pending_ &= (uint8_t)~W_NIGHT;
    time_format(line1, 7u, hour_(), minute_of_());
    l = L_NIGHT[pick_(3u)];
  } else if (pending_ & W_DAWN) {
    pending_ &= (uint8_t)~W_DAWN;
    time_format(line1, 7u, hour_(), minute_of_());
    l = L_DAWN[pick_(2u)];
  }
  if (!l) {
    line1[0] = 0;
    return 0;
  }
  strcpy(line2, l);
  calm_ = 0;
  calm_need_ = 240u;
  return 1;
}

/* ---- the menu at a special minute ---- */
/* where: 0 another menu page, 1 the main page, 2 the menu just opened (forget what was shown before) */
uint8_t time_menu(uint8_t where) BANKED {
  uint8_t sp, was = menu_sp_;
  crucible_story *s;
  time_poll();
  if (where == 2u) {
    menu_sp_ = 0;
    menu_mod_ = mod_;
    return CT_MENU_SAME;
  }
  sp = where && (flags_ & CT_KNOWN) && special_ != 0xffu ? special_ : 0u;
  if (sp != was) {
    menu_sp_ = sp;
    menu_mod_ = mod_;
    /* 3:33 in the night, paused: the dream notices (once a power-on) */
    if (sp == CT_SP_333 && ct_part(hour_()) == CT_NIGHT && !(once_ & O_PAUSE) && (s = saga_()) != 0) {
      once_ |= O_PAUSE;
      cru_story_act(s, CRU_ACT_EGG);
      cru_story_lucid(s, -6);
      story_save();
      if (!(pl.flags & PF_SHEET)) {
        pl.flags |= PF_SHEET;
        player_unlock(6);
        player_save();
      } /* the sheet ghost (8.4): never said */
    }
    return sp ? CT_MENU_ENTER : CT_MENU_LEAVE;
  }
  if (menu_mod_ != mod_) {
    menu_mod_ = mod_;
    return CT_MENU_MINUTE;
  }
  return CT_MENU_SAME;
}
/* the special minute the menu shows (0 none); bit 7: deep (at night: the music stops, RESUME reads WAKE) */
uint8_t time_menu_special(void) BANKED {
  return menu_sp_ ? (uint8_t)(menu_sp_ | (ct_part(hour_()) == CT_NIGHT ? 0x80u : 0u)) : 0u;
}

/* ---- story runs ---- */
/* A run opened. The absence reaches each slot once a power-on, and only a resumed run (a new one was not waiting).
 * Coming straight back leans "the same thing again"; a long time away leans idleness and clears the head a little. */
void time_story_opened(uint8_t slot, uint8_t resumed) BANKED {
  crucible_story *s = saga_();
  uint8_t bit = (uint8_t)(1u << (slot & 7u));
  if (!s || (story_mask_ & bit)) return;
  story_mask_ |= bit;
  if (!resumed) return;
  if (reported_ >= CT_AWAY_NOW) {
    cru_story_event(s, CRU_EV_MOVE, (uint16_t)(0xd0u | reported_), mod_);
    if (reported_ <= CT_AWAY_MINUTES)
      cru_story_act(s, CRU_ACT_LOOP);
    else {
      cru_story_act(s, CRU_ACT_IDLE);
      if (reported_ == CT_AWAY_DAY) cru_story_lucid(s, 8);
      if (reported_ == CT_AWAY_WEEK) {
        cru_story_act(s, CRU_ACT_IDLE);
        cru_story_lucid(s, 16);
      }
    }
  }
  if ((flags_ & CT_KNOWN) && ct_part(hour_()) == CT_NIGHT) cru_story_act(s, CRU_ACT_DEEP);
  story_save();
}

/* ---- the context other code reads ---- */
void crucible_time_context(crucible_time_ctx *out) BANKED {
  uint8_t h, sp;
  time_poll();
  civil_();
  h = hour_();
  sp = (flags_ & CT_KNOWN) ? ct_special(h, minute_of_()) : 0u;
  out->flags = (uint8_t)((flags_ & CT_KNOWN ? CT_F_KNOWN : 0u) | (reported_ ? CT_F_REPORTED : 0u) |
                         (reported_ && (uint16_t)(sess_min_ - report_min_) < CT_RETURNING ? CT_F_RETURNING : 0u) |
                         (sp && ct_part(h) == CT_NIGHT ? CT_F_DEEP : 0u) | (sp && menu_sp_ ? CT_F_PAUSED : 0u) |
                         (host_ ? CT_F_HOST : 0u) | ((flags_ & CT_KNOWN) && cm_ == cd_ ? CT_F_ANGEL_DATE : 0u));
  out->part = ct_part(h);
  out->weekday = ct_weekday(date_);
  out->hour = h;
  out->minute = minute_of_();
  out->away = reported_;
  out->marathon = sess_min_ >= 240u ? 2u : sess_min_ >= 120u ? 1u : 0u;
  out->special = sp;
  out->streak = streak_;
  out->session_min = sess_min_ > 255u ? 255u : (uint8_t)sess_min_;
  out->year = cy_;
  out->month = cm_;
  out->day = cd_;
  out->date = date_;
  out->mod = mod_;
  out->bmonth = bmon_;
  out->bday = bday_;
}
