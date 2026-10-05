/* Records (from the menu), four pages turned with left/right: ACHIEVEMENTS (every award with four tier pips and
 * the next target), TITLES (the trait-by-domain matrix of titles, wear one with A), STATS (personal records) and
 * the LEADERBOARD: this cartridge's best runs (points gained per power-on, by the player's initials and worn
 * title), plus linked friends' runs only once a link has actually exchanged them. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_data.h"
#include "crucible_ui.h"
#include "crucible_scene.h"
#include "crucible_state.h"
#define T_INK 0u
#define T_DIM 8u
#define T_CREAM 7u
#define T_BRASS 15u
#define ROWS 10u
/* Local text helpers: literals live in this bank, so they must not be passed to code in another bank. */
static void ui_put_(uint8_t x, uint8_t y, uint8_t tile, uint8_t attr) {
  VBK_REG = 1;
  set_bkg_tiles(x, y, 1, 1, &attr);
  VBK_REG = 0;
  set_bkg_tiles(x, y, 1, 1, &tile);
}
static void ui_text_(uint8_t x, uint8_t y, const char *s, uint8_t width, uint8_t attr) {
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
static void ui_field_(uint8_t x, uint8_t y, uint8_t width, const char *s, uint8_t attr) {
  uint8_t n = strlen(s);
  if (n > width) n = width;
  ui_text_(x, y, " ", width, attr);
  ui_text_(x + (width - n) / 2u, y, s, n, attr);
}
#define ui_put ui_put_
#define ui_text ui_text_
#define ui_field ui_field_
#define PAGES 4u
static uint8_t tab, at, top, tsel, rsel;
static char line[24], value[12];
static void cursor(uint8_t row) {
  set_sprite_tile(39, SPR_ARROW_R);
  set_sprite_prop(39, 0);
  if (row == 255u)
    move_sprite(39, 0, 0);
  else
    move_sprite(39, 8u + 2u, (uint8_t)((2u + row) * 8u + 16u));
}
static void box(uint8_t x, uint8_t y) {
  set_sprite_tile(39, SPR_BOX);
  set_sprite_prop(39, 0);
  move_sprite(39, (uint8_t)(x * 8u + 8u), (uint8_t)(y * 8u + 16u));
}
static void header(void) {
  uint8_t i;
  char n[8];
  ui_text(1, 0, " ", 18, T_CREAM);
  ui_text(1, 0,
          tab == 0u   ? "ACHIEVEMENTS"
          : tab == 1u ? "TITLES"
          : tab == 2u ? "STATS"
                      : "LEADERBOARD",
          tab == 0u   ? 12u
          : tab == 1u ? 6u
          : tab == 2u ? 5u
                      : 11u,
          T_CREAM);
  if (tab < 2u) {
    ui_number(n, tab ? cru_titles_earned(&core) : cru_feats_total(&core), 2);
    n[2] = '/';
    ui_number(n + 3, tab ? TITLES : FEATS * 4u, 2);
    ui_text(14, 0, n, 5, T_CREAM);
  }
  ui_text(1, 1, " ", 18, T_CREAM);
  ui_put(1, 1, UI_LEFT, T_CREAM);
  ui_put(18, 1, UI_RIGHT, T_CREAM);
  for (i = 0; i < PAGES; i++) ui_put(8u + i, 1, i == tab ? UI_PIP_ON : UI_PIP_OFF, T_CREAM);
}
static void cues(void) {
  ui_text(0, 16, " ", 20, T_CREAM);
  ui_put(1, 16, UI_LEFT, T_CREAM);
  ui_put(2, 16, UI_RIGHT, T_CREAM);
  ui_text(3, 16, "PAGE", 4, T_CREAM);
  if (tab == 1u) {
    ui_put(8, 16, UI_A, T_CREAM);
    ui_text(9, 16, "WEAR", 4, T_CREAM);
  }
  ui_put(14, 16, UI_B, T_CREAM);
  ui_text(15, 16, "BACK", 4, T_CREAM);
}
static void clear_page(void) {
  uint8_t y;
  for (y = 2; y < 15u; y++) ui_text(1, y, " ", 18, T_INK);
}
static void count(char *out, uint16_t v) {
  ui_number(out, v, v >= 10000u ? 5u : v >= 1000u ? 4u : v >= 100u ? 3u : v >= 10u ? 2u : 1u);
}
/* A value in its unit (CRU_UNIT_*): times as m:ss, minutes with M, percent, found/of, play time in hours. */
static void unit(uint16_t v, uint16_t of, uint8_t u, char *out) {
  if (u == CRU_UNIT_FRAMES || u == CRU_UNIT_SECONDS) {
    ui_time(out, u == CRU_UNIT_FRAMES ? v / 60u : v);
    return;
  }
  if (u == CRU_UNIT_PLAYTIME) {
    if (v < 60u)
      ui_time(out, v * 60u + of);
    else {
      count(out, v / 60u);
      strcat(out, "H");
      ui_number(out + strlen(out), v % 60u, 2);
    }
    return;
  }
  count(out, v);
  if (u == CRU_UNIT_MINUTES)
    strcat(out, "M");
  else if (u == CRU_UNIT_PERCENT)
    strcat(out, "%");
  else if (u == CRU_UNIT_OF) {
    strcat(out, "/");
    count(out + strlen(out), of);
  }
}
static void feat_detail(void) {
  uint8_t tier = core.feats[at], u = cru_feat_unit(&core, at);
  uint16_t v = cru_feat_value(&core, at);
  char a[12];
  cru_feat_desc(at, line);
  ui_field(1, 13, 18, line, T_DIM);
  if (tier >= 4u) {
    strcpy(line, "ALL TIERS ");
    unit(v, 0, u, a);
    strcat(line, a);
  } else {
    strcpy(line, "NOW ");
    if (cru_feat_lower(&core, at) && !v)
      strcat(line, "--");
    else {
      unit(v, 0, u, a);
      strcat(line, a);
    }
    strcat(line, "  NEXT ");
    unit(cru_feat_threshold(&core, at, tier), 0, u, a);
    strcat(line, a);
  }
  ui_field(1, 14, 18, line, T_INK);
}
static void feats_page(void) {
  uint8_t i, f, k, pips[4];
  for (i = 0; i < ROWS; i++) {
    f = top + i;
    ui_text(2, 2u + i, " ", 16, T_INK);
    if (f >= FEATS) continue;
    cru_feat_name(f, line);
    ui_text(2, 2u + i, line, (uint8_t)strlen(line), core.feats[f] ? T_INK : T_DIM);
    for (k = 0; k < 4u; k++) pips[k] = k < core.feats[f] ? UI_PIP_ON : UI_PIP_OFF;
    for (k = 0; k < 4u; k++) ui_put(14u + k, 2u + i, pips[k], T_INK);
  }
  ui_text(1, 12, " ", 18, T_INK);
  feat_detail();
  cursor(at - top);
}
static void best(uint8_t row, const char *label, const char *v) {
  ui_text(2, row, label, (uint8_t)strlen(label), T_DIM);
  ui_text((uint8_t)(18u - strlen(v)), row, v, (uint8_t)strlen(v), T_INK);
}
/* STATS: the core's thirteen rows (cru_stat), labelled here */
static const char stat_label[13][14] = {"FOUND",        "RECIPES",     "PAIRS TRIED", "POINTS",        "FASTEST PAIR",
                                        "FIVE FINDS",   "BEST FLURRY", "BEST STREAK", "LONGEST CHAIN", "CLEAN RUN",
                                        "PAIRS/MINUTE", "PLAY TIME",   "COMPLETED IN"};
static void bests_page(void) {
  uint8_t i;
  crucible_stat st;
  for (i = 0; i < 13u; i++) {
    cru_stat(&core, i, &st);
    if (st.known)
      unit(st.value, st.of, st.unit, value);
    else
      strcpy(value, "--");
    best(2u + i, stat_label[i], value);
  }
  cursor(255u);
}
/* ---- titles: rows are traits, columns domains; a cell shows its grade (1..4), a star when worn ---- */
static void title_cell(uint8_t t) {
  uint8_t r = 0, k = t, g;
  while (k >= TITLE_DOMAINS) {
    k -= TITLE_DOMAINS;
    r++;
  }
  g = cru_title_grade(&core, t);
  if (core.title == t + 1u)
    ui_put(10u + k, 3u + r, UI_STAR, T_INK);
  else {
    line[0] = g ? '0' + g : '.';
    line[1] = 0;
    ui_text(10u + k, 3u + r, line, 1, g ? T_INK : T_DIM);
  }
}
static void title_detail(void) {
  uint8_t g = cru_title_grade(&core, tsel), need = cru_title_needs(&core, tsel);
  char a[12];
  cru_title_name(tsel, line);
  ui_field(1, 12, 18, line, g ? T_INK : T_DIM);
  if (g) {
    strcpy(line, "GRADE ");
    strcat(line, g == 1u ? "I" : g == 2u ? "II" : g == 3u ? "III" : "IV");
    if (core.title == tsel + 1u) strcat(line, "  WORN");
  } else if (need == 3u)
    strcpy(line, "NEEDS TWO AWARDS");
  else {
    strcpy(line, "NEEDS ");
    cru_feat_name(cru_title_feat(&core, tsel, need == 1u ? 0u : 1u), a);
    strcat(line, a);
  }
  ui_field(1, 13, 18, line, T_DIM);
  if (g)
    ui_field(1, 14, 18, core.title == tsel + 1u ? "A TO TAKE OFF" : "A TO WEAR", T_DIM);
  else
    ui_text(1, 14, " ", 18, T_DIM);
  {
    uint8_t r = 0, k = tsel;
    while (k >= TITLE_DOMAINS) {
      k -= TITLE_DOMAINS;
      r++;
    }
    box(10u + k, 3u + r);
  }
}
static void titles_page(void) {
  uint8_t r, k, t;
  char a[12];
  for (k = 0; k < TITLE_DOMAINS; k++) {
    cru_title_part(1, k, a);
    a[1] = 0;
    ui_text(10u + k, 2, a, 1, T_DIM);
  }
  for (r = 0, t = 0; r < TITLE_TRAITS; r++) {
    uint8_t any = 0;
    for (k = 0; k < TITLE_DOMAINS; k++, t++) {
      title_cell(t);
      if (cru_title_grade(&core, t)) any = 1;
    }
    cru_title_part(0, r, a);
    ui_text(2, 3u + r, a, (uint8_t)strlen(a), any ? T_INK : T_DIM);
  }
  title_detail();
}
/* ---- leaderboard ---- */
/* One leaderboard row: rank, a friend's link mark, name (8 letters), points gained in that run, new finds. */
static void rank_row(uint8_t i) {
  crucible_board_row r;
  uint8_t y = 3u + i, attr;
  cru_board_row(&core, i, &r);
  ui_text(2, y, " ", 16, T_INK);
  line[0] = '1' + i;
  line[1] = 0;
  ui_text(2, y, line, 1, T_DIM);
  if (!r.points) {
    ui_text(4, y, "---", 3, T_DIM);
    return;
  }
  attr = r.mine ? T_INK : T_DIM;
  ui_text(4, y, r.name, (uint8_t)strlen(r.name), attr);
  ui_number(value, r.points, 4);
  ui_text(13, y, value, 4, attr);
  ui_number(value, r.finds, 2);
  ui_text(17, y, value, 2, attr);
  if (r.session == BOARD_FRIEND) ui_put(3, y, UI_LINK, T_INK);
}
static void rank_detail(void) {
  crucible_board_row r;
  cru_board_row(&core, rsel, &r);
  if (r.points && r.title && r.title <= TITLES)
    cru_title_name(r.title - 1u, line);
  else
    strcpy(line, r.points ? "NO TITLE" : "");
  ui_field(1, 12, 18, line, T_INK);
  cursor(1u + rsel);
}
static void ranks_page(void) {
  uint8_t i, me = cru_board_current(&core);
  ui_text(2, 2, "#", 1, T_DIM);
  ui_text(4, 2, "NAME", 4, T_DIM);
  ui_text(13, 2, "PTS", 3, T_DIM);
  ui_text(17, 2, "NEW", 2, T_DIM);
  for (i = 0; i < BOARD_ROWS; i++) rank_row(i);
  strcpy(line, "THIS RUN ");
  ui_number(value, cru_board_run_points(&core), 4);
  strcat(line, value);
  ui_field(1, 13, 18, line, T_DIM);
  ui_field(1, 14, 18, core.links ? "LINK MARK: A FRIEND" : "LINK A FRIEND TO ADD", T_DIM);
  rsel = me == CRU_NONE8 ? 0u : me;
  rank_detail();
}
static void page(void) {
  clear_page();
  header();
  cues();
  cursor(255u);
  if (tab == 0u)
    feats_page();
  else if (tab == 1u)
    titles_page();
  else if (tab == 2u)
    bests_page();
  else
    ranks_page();
}
void records_open(uint8_t first) BANKED {
  scene_draw(SCENE_RECORDS);
  tab = first >= PAGES ? 0u : first;
  at = 0;
  top = 0;
  tsel = core.title ? core.title - 1u : 0u;
  page();
}
static void turn(int8_t d) {
  tab = (uint8_t)((tab + (d > 0 ? 1u : PAGES - 1u)) % PAGES);
  sound_play(SFX_MOVE);
  page();
}
/* Returns 1 when the ledger closes. */
uint8_t records_tick(uint8_t pressed) BANKED {
  if (pressed & (J_B | J_SELECT | J_START)) {
    cursor(255u);
    sound_play(SFX_CLOSE);
    return 1;
  }
  if (tab == 1u) {
    uint8_t r = 0, k = tsel;
    while (k >= TITLE_DOMAINS) {
      k -= TITLE_DOMAINS;
      r++;
    }
    if (pressed & J_LEFT) {
      if (!k) {
        turn(-1);
        return 0;
      }
      tsel--;
    } else if (pressed & J_RIGHT) {
      if (k + 1u >= TITLE_DOMAINS) {
        turn(1);
        return 0;
      }
      tsel++;
    } else if (pressed & J_UP) {
      if (!r) return 0;
      tsel -= TITLE_DOMAINS;
    } else if (pressed & J_DOWN) {
      if (r + 1u >= TITLE_TRAITS) return 0;
      tsel += TITLE_DOMAINS;
    } else if (pressed & J_A) {
      uint8_t old = core.title;
      if (!cru_title_wear(&core, tsel)) {
        sound_play(SFX_DENY);
        return 0;
      }
      if (old) title_cell(old - 1u);
      title_cell(tsel);
      sound_play(SFX_OPEN);
      title_detail();
      return 0;
    } else
      return 0;
    sound_play(SFX_MOVE);
    title_detail();
    return 0;
  }
  if (pressed & J_LEFT)
    turn(-1);
  else if (pressed & J_RIGHT)
    turn(1);
  else if (tab == 0u && (pressed & (J_UP | J_DOWN))) {
    uint8_t was = top;
    if ((pressed & J_UP) && at)
      at--;
    else if ((pressed & J_DOWN) && at + 1u < FEATS)
      at++;
    else
      return 0;
    if (at < top) top = at;
    if (at >= top + ROWS) top = at - ROWS + 1u;
    sound_play(SFX_MOVE);
    if (top != was)
      feats_page();
    else {
      feat_detail();
      cursor(at - top);
    }
  } else if (tab == 3u && (pressed & (J_UP | J_DOWN))) {
    if ((pressed & J_UP) && rsel)
      rsel--;
    else if ((pressed & J_DOWN) && rsel + 1u < BOARD_ROWS)
      rsel++;
    else
      return 0;
    sound_play(SFX_MOVE);
    rank_detail();
  }
  return 0;
}
