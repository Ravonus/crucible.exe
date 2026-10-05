/* Feats: twenty-four personal achievements with four tiers each, and the 56 titles they open, earned offline. The
 * rules, personal bests and the toast queue are the CRUCIBLE core's (core cru_feats.c); this file draws
 * the toast: an unlock slides a card down under the header, chimes, and names the tier points. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "ui.h"
#include "crucible_data.h"
#include "crucible_ui.h"
#include "crucible_state.h"
#include "crucible_lines.h"
#include "crucible_truths.h"
#include "crucible_time.h"
#include "crucible_link.h"
static const uint8_t tier_points[4] = {5, 10, 20, 40};
static uint8_t toast_t, toast, whisper_, partner_, pfl_, pmore_;
static uint16_t pid_; /* partner_: a link partner's make (crucible_link_coop.c) */
/* Toast card: the window holds a white pixel-art card (rounded corners, one-pixel lilac border, dithered inner
 * edge) just under the header with a strip of sky above it, so the counters stay visible. It unrolls (three
 * lines a frame), holds, and rolls back; the cartridge ends the window at the returned height. */
static void toast_draw(void) {
  uint8_t row[20], attr[20], tier = toast & 3u, i, y;
  char line[21], n[6];
  /* frame rows 0 and 3: corners and edges in the sky palette, mirrored by attribute flips */
  for (y = 0; y < 4u; y += 3u) {
    memset(row, UI_TOAST_EDGE, 20);
    row[0] = row[19] = UI_TOAST_CORNER;
    memset(attr, y ? 0x42u : 0x02u, 20);
    attr[19] = y ? 0x62u : 0x22u;
    VBK_REG = 1;
    set_win_tiles(0, y, 20, 1, attr);
    VBK_REG = 0;
    set_win_tiles(0, y, 20, 1, row);
  }
  memset(attr, 7, 20);
  attr[0] = 0x02u;
  attr[19] = 0x22u;
  VBK_REG = 1;
  set_win_tiles(0, 1, 20, 1, attr);
  set_win_tiles(0, 2, 20, 1, attr);
  VBK_REG = 0;
  memset(row, UI_FONT, 20);
  row[0] = row[19] = UI_TOAST_SIDE;
  row[1] = UI_STAR;
  /* a time whisper (crucible_time.c): an hourglass, the clock, and one line */
  if (partner_) {
    char n[14];
    row[1] = UI_LINK;
    crucible_text_line(pfl_ & 0x80u   ? EV_ELSEWHERE_FIRST
                       : pfl_ & 0x40u ? EV_THEYMADE_FIRST
                                      : EV_PARTNER_FIRST,
                       line, 17, 0);
    for (i = 0; line[i] && i < 16u; i++) row[3u + i] = GLYPH(line[i]);
    set_win_tiles(0, 1, 20, 1, row);
    memset(row, UI_FONT, 20);
    row[0] = row[19] = UI_TOAST_SIDE;
    if (pfl_ & 0x80u)
      crucible_text_line(EV_ELSEWHERE_FIRST + 1u, line, 19, 0);
    else {
      crucible_get_name(pid_, n);
      strcpy(line, n);
    }
    if (pfl_ & 0x80u) {
    } else if (pfl_ & 0x40u) {
      strcat(line, " ");
      ui_number(n, pfl_ & 0x3fu, (pfl_ & 0x3fu) >= 10u ? 2u : 1u);
      strcat(line, n);
    } else if (pfl_ & 1u)
      strcat(line, " NEW");
    if (pmore_) {
      strcat(line, " +");
      ui_number(n, pmore_, pmore_ >= 10u ? 2u : 1u);
      strcat(line, n);
    }
    for (i = 0; line[i] && i < 18u; i++) row[1u + i] = GLYPH(line[i]);
    set_win_tiles(0, 2, 20, 1, row);
    return;
  }
  if (whisper_) {
    char said[19];
    time_whisper(line, said);
    row[1] = UI_HOURGLASS;
    for (i = 0; line[i] && i < 16u; i++) row[3u + i] = GLYPH(line[i]);
    set_win_tiles(0, 1, 20, 1, row);
    memset(row, UI_FONT, 20);
    row[0] = row[19] = UI_TOAST_SIDE;
    for (i = 0; said[i] && i < 18u; i++) row[1u + i] = GLYPH(said[i]);
    set_win_tiles(0, 2, 20, 1, row);
    return;
  }
  if (toast & CRU_TOAST_TITLE)
    strcpy(line, "NEW TITLE");
  else {
    cru_feat_name(toast >> 2, line);
    strcat(line, " ");
    strcat(line, tier == 0u ? "I" : tier == 1u ? "II" : tier == 2u ? "III" : "IV");
  }
  for (i = 0; line[i] && i < 16u; i++) row[3u + i] = GLYPH(line[i]);
  set_win_tiles(0, 1, 20, 1, row);
  memset(row, UI_FONT, 20);
  row[0] = row[19] = UI_TOAST_SIDE;
  if (toast & CRU_TOAST_TITLE)
    cru_title_name(toast & 0x7fu, line);
  else {
    line[0] = '+';
    ui_number(n, tier_points[tier], tier_points[tier] >= 10u ? 2u : 1u);
    strcpy(line + 1, n);
    strcat(line, " POINTS");
  }
  for (i = 0; line[i] && i < 18u; i++) row[1u + i] = GLYPH(line[i]);
  set_win_tiles(0, 2, 20, 1, row);
}
/* Visible window lines: three of sky above the card's border, the card, and its bottom border (29 lines). */
#define TOAST_LINES 29u
/* frames after which a cut-off card counts as read: unrolled (10) plus a third of a second open */
#define TOAST_SEEN 30u
uint8_t feats_toast(uint8_t allowed) BANKED {
  uint8_t h, w = time_whisper_ready(allowed && !toast_t); /* the clock counts VBlanks here, on every screen */
  /* Cut off (a mix, a menu, a result's glitch): a card that was open long enough to read counts as shown, so the
  * same award never comes back after every step; one cut while still unrolling plays again later. */
  if (toast_t && !allowed) {
    win_pos_y = MENU_CLOSED_Y;
    if (toast_t >= TOAST_SEEN && !whisper_ && !partner_) cru_toast_done(&core);
    toast_t = 0;
    return 0;
  }
  if (!toast_t) {
    if (!allowed) return 0;
    toast = cru_toast_peek(&core);
    whisper_ = toast == CRU_TOAST_NONE;
    partner_ = 0;
    if (whisper_ && !w) {
      if (!link_on || !link_toast_take(&pid_, &pfl_, &pmore_)) return 0;
      partner_ = 1;
      whisper_ = 0;
    } /* the core's own first, then a partner's make */
    toast_draw();
    if (partner_)
      sound_play(SFX_PMADE);
    else if (!whisper_)
      sound_voice(0, 3, 6, (toast & CRU_TOAST_TITLE) ? 3u : (toast & 3u));
    toast_t = 1;
  }
  toast_t++;
  h = toast_t < 11u    ? (uint8_t)((toast_t - 1u) * 3u)
      : toast_t < 130u ? TOAST_LINES
      : toast_t < 140u ? (uint8_t)(TOAST_LINES - (toast_t - 130u) * 3u)
                       : 0u;
  if (h > TOAST_LINES) h = TOAST_LINES;
  win_pos_x = 0;
  win_pos_y = h ? 8u : MENU_CLOSED_Y;
  if (toast_t >= 140u) {
    toast_t = 0;
    if (!whisper_ && !partner_) cru_toast_done(&core);
  }
  return h;
}
