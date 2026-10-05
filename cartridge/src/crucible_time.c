/* Time awareness without a clock chip (see crucible_time.h and docs/time-awareness.md).
 *
 * The player tells the cartridge what time it is; VBlanks keep it going while it runs. What the player reports at
 * power-on is all the cartridge knows about the time away, so an answer of "NOT SURE" (or a small step backwards,
 * which is a correction) gives CT_AWAY_UNKNOWN and nothing reacts to it. Effects are whispers (a card on the bench,
 * drawn by crucible_feats.c), the pause menu at a special minute (crucible_menu.c), and quiet leanings in a story
 * run's saga (lucidity and the truth matrix), never explained.
 *
 * SRAM: two 32-byte records in bank 15 at 0x1F80 (A) and 0x1FA0 (B), linear 0x1FF80 / 0x1FFA0, above the eggs
 * (0x1F00..0x1F06) and the story record (0x1D00..0x1DB7). Neither save, the slot wipes nor RESET GAME write there.
 * Each write invalidates the commit byte first and sets it last; the newer valid record by wrapping serial wins. */
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
#define CT_VERSION 1u
#define CT_WEEK 10080u /* minutes in a week */
#define CT_DEFAULT 600u /* SUN 10:00 AM: the guess before the player ever answers (not a special minute) */
#define CT_FRAME 4389u /* one VBlank in 1/262144 s (70224 clocks at 4194304 Hz) */
#define CT_CORRECTION 360u /* stepping back up to six hours is a correction, not a week away */
#define CT_RETURNING 10u /* minutes the return colours things ("for a bit") */
#define CT_KNOWN 1u /* persisted flag: the clock was set by the player */

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

static uint8_t loaded_, at_, flags_, sec_, reported_, streak_, best_, quick_, long_, prev_part_, special_;
static uint8_t pending_, w_ret_, w_sp_, w_mar_, once_, story_mask_, menu_sp_, calm_, roll_;
static uint16_t serial_, mow_, week_, sess_min_, last_day_, returns_, last_vbl_, report_min_, next_mar_, w_sp_mow_,
    menu_mow_;
static uint32_t frac_, total_;

/* ---- portable rules (pure C, testable on a host) ---- */
static uint8_t ct_part(uint8_t hour) {
  return hour >= 22u || hour < 5u ? CT_NIGHT : hour < 8u ? CT_DAWN : hour < 18u ? CT_DAY : CT_DUSK;
}
static const uint8_t SP_H[11] = {1, 2, 3, 4, 5, 11, 12, 4, 10, 0, 12};
static const uint8_t SP_M[11] = {11, 22, 33, 44, 55, 11, 34, 4, 10, 0, 0};
static uint8_t ct_special(uint8_t hour, uint8_t minute) {
  uint8_t h = (uint8_t)(hour % 12u), i;
  if (!minute && !hour) return CT_SP_MIDNIGHT;
  if (!minute && hour == 12u) return CT_SP_NOON;
  if (!h) h = 12u;
  for (i = 0; i < 9u; i++)
    if (SP_H[i] == h && SP_M[i] == minute) return (uint8_t)(i + 1u);
  return CT_SP_NONE;
}
static uint8_t ct_bucket(uint32_t away) {
  return away < 5ul                 ? CT_AWAY_NOW
         : away < 60ul              ? CT_AWAY_MINUTES
         : away < 1440ul            ? CT_AWAY_HOURS
         : away < (uint32_t)CT_WEEK ? CT_AWAY_DAY
                                    : CT_AWAY_WEEK;
}
/* The report: last-known minute of the week, the player's, and whole weeks they add. Forward is time away; a small
 * step back is a correction (unknown), never six days and some hours. */
static uint8_t ct_report(uint16_t was, uint16_t now, uint8_t weeks, uint32_t *away) {
  uint16_t d = (uint16_t)((now + CT_WEEK - was) % CT_WEEK);
  *away = 0;
  if (!weeks && d >= CT_WEEK - CT_CORRECTION) return CT_AWAY_UNKNOWN;
  *away = (uint32_t)weeks * CT_WEEK + d;
  return ct_bucket(*away);
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
  return r[0] == 'C' && r[1] == 'T' && r[2] == CT_VERSION && r[CT_COMMIT] == CT_COMMITTED &&
         ct_crc(r, 30u) == u16_(r + 30) && u16_(r + 6) < CT_WEEK && r[8] < 60u;
}
/* 0 A, 1 B, 255 neither */
static uint8_t ct_latest(const uint8_t *a, const uint8_t *b) {
  uint8_t va = ct_valid(a), vb = ct_valid(b);
  if (va && vb) return (int16_t)(u16_(b + 4) - u16_(a + 4)) > 0 ? 1u : 0u;
  return va ? 0u : vb ? 1u : 255u;
}

/* ---- the record ---- */
static void read_(uint32_t at, uint8_t *r) {
  uint8_t i;
  for (i = 0; i < CT_REC_BYTES; i++) r[i] = crucible_sram_read(0, at + i);
  SWITCH_RAM(0);
  DISABLE_RAM;
}
static void load_(void) {
  uint8_t a[CT_REC_BYTES], b[CT_REC_BYTES], *r;
  loaded_ = 1;
  next_mar_ = 120u;
  read_(CT_REC_A, a);
  read_(CT_REC_B, b);
  at_ = ct_latest(a, b);
  if (at_ == 255u) {
    mow_ = CT_DEFAULT;
    return;
  } /* a fresh cartridge or an old save: nothing was ever said */
  r = at_ ? b : a;
  flags_ = r[3];
  serial_ = u16_(r + 4);
  mow_ = u16_(r + 6);
  sec_ = r[8];
  week_ = u16_(r + 10);
  total_ = (uint32_t)u16_(r + 12) | ((uint32_t)u16_(r + 14) << 16);
  last_day_ = u16_(r + 18);
  streak_ = r[20];
  best_ = r[21];
  returns_ = u16_(r + 22);
  quick_ = r[24];
  long_ = r[25];
  roll_ = (uint8_t)(serial_ ^ (serial_ >> 8) ^ (uint8_t)total_);
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
  put16_(r + 6, mow_);
  r[8] = sec_;
  r[9] = reported_;
  put16_(r + 10, week_);
  put16_(r + 12, (uint16_t)total_);
  put16_(r + 14, (uint16_t)(total_ >> 16));
  put16_(r + 16, sess_min_);
  put16_(r + 18, last_day_);
  r[20] = streak_;
  r[21] = best_;
  put16_(r + 22, returns_);
  r[24] = quick_;
  r[25] = long_;
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
static uint8_t hour_(void) { return (uint8_t)((mow_ % 1440u) / 60u); }
static uint8_t minute_of_(void) { return (uint8_t)(mow_ % 60u); }
static uint16_t today_(void) { return (uint16_t)(week_ * 7u + mow_ / 1440u); }
static void day_seen_(void) {
  uint16_t d = today_();
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
      w_sp_mow_ = mow_;
      pending_ |= W_SPECIAL;
      if ((s = saga_()) != 0) {
        cru_story_event(s, CRU_EV_FLAG, (uint16_t)(0xe0u | sp), mow_);
        story_save();
      }
    }
  }
}
static void minutes_(uint16_t n) {
  while (n--) {
    if (++mow_ >= CT_WEEK) {
      mow_ = 0;
      week_++;
    }
    if (sess_min_ < 0xffffu) sess_min_++;
    if (!(mow_ % 1440u) && (flags_ & CT_KNOWN)) day_seen_();
  }
  minute_events_();
  persist_();
}
void time_poll(void) BANKED {
  uint16_t now = sys_time, n, s;
  if (!loaded_) {
    load_();
    last_vbl_ = now;
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

/* ---- the ask ---- */
uint8_t time_ask_mode(void) BANKED {
  time_poll();
  return reported_ ? CT_ASK_DONE : (flags_ & CT_KNOWN) ? CT_ASK_RETURN : CT_ASK_FIRST;
}
void time_ask_prefill(uint8_t *day, uint8_t *hour, uint8_t *minute) BANKED {
  time_poll();
  *day = (uint8_t)(mow_ / 1440u);
  *hour = hour_();
  *minute = minute_of_();
}
/* set the clock to the nearer of forward or back (a correction keeps the week count honest) */
static void set_near_(uint16_t now) {
  uint16_t d = (uint16_t)((now + CT_WEEK - mow_) % CT_WEEK);
  if (d && d <= CT_WEEK / 2u) {
    if (now < mow_) week_++;
  } else if (d && now > mow_ && week_)
    week_--;
  mow_ = now;
  sec_ = 0;
  frac_ = 0;
}
void time_answer(uint8_t mode, uint8_t answer, uint8_t day, uint8_t hour, uint8_t minute, uint8_t weeks) BANKED {
  uint16_t now = (uint16_t)((uint16_t)(day % 7u) * 1440u + (uint16_t)(hour % 24u) * 60u + (minute % 60u));
  uint32_t away;
  uint8_t b;
  time_poll();
  if (mode == CT_ASK_ADJUST) { /* SETUP: a correction, whichever way */
    if (answer != CT_ANSWER_OK) return;
    set_near_(now);
    flags_ |= CT_KNOWN;
    day_seen_();
    special_ = 0xffu;
    minute_events_();
    persist_();
    return;
  }
  if (answer != CT_ANSWER_OK)
    b = CT_AWAY_UNKNOWN; /* the estimate stands; nothing is inferred */
  else if (!(flags_ & CT_KNOWN)) {
    b = CT_AWAY_FIRST;
    mow_ = now;
    sec_ = 0;
    frac_ = 0;
    flags_ |= CT_KNOWN;
  } else {
    b = ct_report(mow_, now, weeks, &away);
    if (b == CT_AWAY_UNKNOWN)
      set_near_(now);
    else {
      if (now < mow_) week_++;
      week_ = (uint16_t)(week_ + weeks);
      mow_ = now;
      sec_ = 0;
      frac_ = 0;
    }
    if (b >= CT_AWAY_NOW) {
      if (returns_ < 0xffffu) returns_++;
      if (b <= CT_AWAY_MINUTES && quick_ < 255u) quick_++;
      if (b >= CT_AWAY_DAY && long_ < 255u) long_++;
    }
  }
  if (flags_ & CT_KNOWN) day_seen_();
  reported_ = b;
  report_min_ = sess_min_;
  if (b != CT_AWAY_UNKNOWN) {
    w_ret_ = b;
    pending_ |= W_RETURN;
  }
  prev_part_ = ct_part(hour_());
  special_ = 0xffu;
  minute_events_();
  persist_();
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
  time_format(out, with_day ? (uint8_t)(mow_ / 1440u) : 7u, hour_(), minute_of_());
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
static const char *const L_SPECIAL[12] = {0,
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
                                          "NO SHADOWS NOW."};
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
  if ((pending_ & W_SPECIAL) && w_sp_mow_ != mow_) pending_ &= (uint8_t)~W_SPECIAL;
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
  if ((pending_ & W_SPECIAL) && w_sp_mow_ != mow_) pending_ &= (uint8_t)~W_SPECIAL;
  if (pending_ & W_SPECIAL) {
    pending_ &= (uint8_t)~W_SPECIAL;
    time_format(line1, 7u, hour_(), minute_of_());
    l = L_SPECIAL[w_sp_ < 12u ? w_sp_ : 0u];
  } else if (pending_ & W_RETURN) {
    pending_ &= (uint8_t)~W_RETURN;
    k = w_ret_ < 8u ? w_ret_ : 0u;
    time_format(line1, (uint8_t)(mow_ / 1440u), hour_(), minute_of_());
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
    menu_mow_ = mow_;
    return CT_MENU_SAME;
  }
  sp = where && (flags_ & CT_KNOWN) && special_ != 0xffu ? special_ : 0u;
  if (sp != was) {
    menu_sp_ = sp;
    menu_mow_ = mow_;
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
  if (menu_mow_ != mow_) {
    menu_mow_ = mow_;
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
    cru_story_event(s, CRU_EV_MOVE, (uint16_t)(0xd0u | reported_), mow_);
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
  h = hour_();
  sp = (flags_ & CT_KNOWN) ? ct_special(h, minute_of_()) : 0u;
  out->flags = (uint8_t)((flags_ & CT_KNOWN ? CT_F_KNOWN : 0u) | (reported_ ? CT_F_REPORTED : 0u) |
                         (reported_ && (uint16_t)(sess_min_ - report_min_) < CT_RETURNING ? CT_F_RETURNING : 0u) |
                         (sp && ct_part(h) == CT_NIGHT ? CT_F_DEEP : 0u) | (sp && menu_sp_ ? CT_F_PAUSED : 0u));
  out->part = ct_part(h);
  out->weekday = (uint8_t)(mow_ / 1440u);
  out->hour = h;
  out->minute = minute_of_();
  out->away = reported_;
  out->marathon = sess_min_ >= 240u ? 2u : sess_min_ >= 120u ? 1u : 0u;
  out->special = sp;
  out->streak = streak_;
  out->session_min = sess_min_ > 255u ? 255u : (uint8_t)sess_min_;
}
