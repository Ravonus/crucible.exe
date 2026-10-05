/* The menu: a vaporwave title card at power-on and a pause menu on Start: PLAY (title) or RESUME (pause), the recipe
 * book, TITLES and STATS (the records ledger; LEFT/RIGHT also turn to awards and ranks), SETUP (music, sound, turn
 * speed, the clock, how to play, reset game), and a sixth item only when it means something: SEND while linked, LEAVE
 * on the pause menu (the run is saved; back to the title card, where PLAY picks story, free play or link).
 * Talks and fights are never menu items: they come to the bench in play (crucible_flow.c).
 * Settings live in the save (the core's options byte) and apply at once; leaving settings saves them. RESET GAME asks
 * for A to be held two seconds, erases progress (cru_reset) and restarts the cartridge.
 * Names live with the three story slots: given when a run wakes, changed with SELECT on the slot list.
 * Each new story game asks the date and time (crucible_time.c: no clock chip) and the player's birthday.
 * Free play, link sessions and resumed runs never ask. Each slot keeps its own answers, then a brief
 * loader shows the player's sign mark (crucible_sky.c, run by crucible.c). Never on the title card. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_ui.h"
#include "crucible_scene.h"
#include "crucible_state.h"
#include "crucible_storyrun.h"
#include "crucible_dialogue.h"
#include "crucible_link.h"
#include "crucible_link_rules.h"
#include "crucible_time.h"
#include "crucible_volume.h"
#include "crucible_flow.h"
#define T_CREAM 7u
#define T_BRASS 15u
#define ROW0 9u
#define ITEMS 6u
/* Local text helpers: literals live in this bank, so they must not be passed to code in another bank. */
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
static void field_(uint8_t x, uint8_t y, uint8_t width, const char *s, uint8_t attr) {
  uint8_t n = strlen(s);
  if (n > width) n = width;
  text_(x, y, " ", width, attr);
  text_(x + (width - n) / 2u, y, s, n, attr);
}
enum {
  PAGE_MAIN,
  PAGE_SETTINGS,
  PAGE_NAME,
  PAGE_HELP,
  PAGE_RESET,
  PAGE_PLAY,
  PAGE_SLOTS,
  PAGE_LINK,
  PAGE_LOBBY,
  PAGE_TIME,
  PAGE_RULES,
  PAGE_BRING,
  PAGE_BIRTH
};
static uint8_t bring_role; /* the page that asks which save to bring (fight spec 9.3): HOST or JOIN chosen */
/* the keyboard edits the link seed, or a story slot's name (rename_slot < 3) */
static uint8_t seed_edit, rename_slot = 255u;
/* the date and time page: CT_ASK_* mode and its fields (crucible_time.h CT_FY..CT_FMIN); the birthday page's month
 * and day; the game start waiting behind the asks (a MENU_* action, 0 none) */
static uint8_t tmode, tf[5], bf[2], pend;
/* the fields in the order they show: month, day, year, hour, minute */
static const uint8_t FORD[5] = {CT_FM, CT_FD, CT_FY, CT_FH, CT_FMIN};
static char seed_word[CRU_NAME + 1u] = "DREAM   ";
uint8_t menu_slot;
#define SETTINGS 6u
#define HOLD_FRAMES 120u
static uint8_t page, at, title, help, letter, changed, kx, ky, hold, armed;
static uint16_t hold_t0;
/* the name being typed: CRU_NAME letters, blank-padded */
static char name[CRU_NAME + 1u];
/* Keyboard: five rows of seven keys; '_' is a space, '<' deletes, '#' is OK (two keys wide). */
static const char keys[5][8] = {"ABCDEFG", "HIJKLMN", "OPQRSTU", "VWXYZ-.", "_!?*<##"};
/* the main page is a compact grid, two columns of three (short labels), so the title bust has the sky to itself */
static const char *const main_items[ITEMS] = {0, "BOOK", "TITLES", "STATS", "SETUP", 0};
static const uint8_t main_action[ITEMS] = {MENU_RESUME, MENU_BOOK, MENU_TITLES, MENU_STATS, MENU_STAY, MENU_STAY};
#define SETUP_AT 4u
/* the sixth cell: SEND while linked, LEAVE on the pause menu, nothing on the title card */
static const char *sixth(void) { return link_started ? "SEND" : title ? 0 : "LEAVE"; }
static uint8_t items(void) { return sixth() ? ITEMS : ITEMS - 1u; }
#define GRID_ROW(i) (ROW0 + 2u + ((i) >> 1))
#define GRID_X(i) (((i) & 1u) ? 11u : 4u)
static void grid_cursor(uint8_t i) {
  set_sprite_tile(39, SPR_ARROW_R);
  set_sprite_prop(39, 0);
  move_sprite(39, (uint8_t)((GRID_X(i) - 1u) * 8u + 8u), (uint8_t)(GRID_ROW(i) * 8u + 16u));
}
static const char *const turn_names[3] = {"NORMAL", "SLOW", "FAST"};
/* a volume 0..8 as a pixel bar: eight cells, filled up to the level */
static void vol_bar(char *out, uint8_t v) {
  uint8_t i;
  for (i = 0; i < VOL_STEPS; i++) out[i] = i < v ? '#' : '.';
  out[VOL_STEPS] = 0;
}
static const char *const help_lines[3][6] = {
    {"PICK A THING", "A ADDS IT", "PICK ANOTHER", "A MIXES THEM", "B PUTS IT BACK", "1/3"},
    {"ARROWS TURN", "THE SHELF", "UP/DOWN: GROUP", "SELECT: FILTER", "START: MENU", "2/3"},
    {"NOT ALONE.", "A FACE ABOVE?", "PRESS UP.", "EYES BELOW?", "PRESS DOWN.", "3/3"}};
static const char *const reset_lines[5] = {"ERASE GAME?", "THINGS, BOOK,", "AWARDS, TITLES", "AND RUNS GO.",
                                           "SETTINGS STAY."};
static void cursor(uint8_t row) {
  set_sprite_tile(39, SPR_ARROW_R);
  set_sprite_prop(39, 0);
  if (row == 255u)
    move_sprite(39, 0, 0);
  else
    move_sprite(39, 3u * 8u + 8u, (uint8_t)((ROW0 + row) * 8u + 16u));
}
static void cues(const char *a, const char *b) {
  text_(0, 17, " ", 20, T_CREAM);
  put_(1, 17, UI_A, T_CREAM);
  text_(2, 17, a, (uint8_t)strlen(a), T_CREAM);
  if (b) {
    put_(10, 17, UI_B, T_CREAM);
    text_(11, 17, b, (uint8_t)strlen(b), T_CREAM);
  }
}
static void keyboard(void);
static void lobby_cable(void);
static void cursor_at(uint8_t x, uint8_t y) {
  set_sprite_tile(39, SPR_ARROW_R);
  set_sprite_prop(39, 0);
  move_sprite(39, (uint8_t)(x * 8u + 8u), (uint8_t)(y * 8u + 16u));
}
/* hold bar: ten cells that fill while A is held on the reset page */
static void reset_bar(void) {
  uint8_t i, n = (uint8_t)((uint16_t)hold * 10u / HOLD_FRAMES);
  for (i = 0; i < 10u; i++) put_((uint8_t)(5u + i), ROW0 + 6u, GLYPH(i < n ? '#' : '.'), i < n ? T_CREAM : T_BRASS);
}
static void clear(void) {
  uint8_t y;
  for (y = ROW0; y < ROW0 + 7u; y++) text_(3, y, " ", 14, T_CREAM);
}
/* label on the left, value on the right of a 13-column row */
static void setting(uint8_t row, const char *label, const char *value) {
  text_(4, ROW0 + row, " ", 13, T_CREAM);
  text_(4, ROW0 + row, label, (uint8_t)strlen(label), T_CREAM);
  text_((uint8_t)(17u - strlen(value)), ROW0 + row, value, (uint8_t)strlen(value), T_BRASS);
}
static void box(uint8_t x, uint8_t y) {
  set_sprite_tile(39, SPR_BOX);
  set_sprite_prop(39, 0);
  move_sprite(39, (uint8_t)(x * 8u + 8u), (uint8_t)(y * 8u + 16u));
}
static void name_line(void) {
  uint8_t i;
  char c;
  text_(3, ROW0, " ", 14, T_CREAM);
  text_(3, ROW0, "NAME", 4, T_BRASS);
  for (i = 0; i < CRU_NAME; i++) {
    c = name[i];
    put_((uint8_t)(8u + i), ROW0, GLYPH(i < letter && c != ' ' ? c : '_'), i < letter ? T_CREAM : T_BRASS);
  }
}
static void keyboard(void) {
  uint8_t y, x;
  char c;
  name_line();
  for (y = 0; y < 5u; y++) {
    text_(3, ROW0 + 1u + y, " ", 14, T_CREAM);
    for (x = 0; x < 7u; x++) {
      c = keys[y][x];
      if (c == '#') {
        if (x == 5u) text_(14, ROW0 + 5u, "OK", 2, T_CREAM);
      } else if (c == '<')
        put_((uint8_t)(4u + x * 2u), ROW0 + 1u + y, UI_LEFT, T_CREAM);
      else
        put_((uint8_t)(4u + x * 2u), ROW0 + 1u + y, GLYPH(c), T_CREAM);
    }
  }
  text_(3, ROW0 + 6u, " ", 14, T_BRASS);
  text_(5, ROW0 + 6u, "START: DONE", 11, T_BRASS);
  box((uint8_t)(4u + (keys[ky][kx] == '#' ? 5u : kx) * 2u), (uint8_t)(ROW0 + 1u + ky));
  cues("TYPE", "DELETE");
}
static void draw(void) {
  uint8_t i;
  char shown[13];
  clear();
  if (page == PAGE_MAIN) {
    uint8_t sp = time_menu_special();
    for (i = 0; i < ITEMS; i++) {
      const char *l = i == 5u ? sixth() : i ? main_items[i] : (sp & 0x80u) ? "WAKE" : title ? "PLAY" : "RESUME";
      text_(GRID_X(i), GRID_ROW(i), "      ", 6, T_BRASS);
      if (l) text_(GRID_X(i), GRID_ROW(i), l, (uint8_t)strlen(l), i == at ? T_CREAM : T_BRASS);
    }
    grid_cursor(at);
    /* a special minute while the menu is open: the cues give way to the time */
    if (sp) {
      char v[13];
      time_now_text(v, 0);
      text_(0, 17, " ", 20, T_BRASS);
      field_(3, 17, 14, v, T_BRASS);
    } else
      cues("CHOOSE", title ? 0 : "BACK");
  } else if (page == PAGE_SETTINGS) {
    /* six rows inside the panel (the top row is the sky's, where the cloud bands scroll); B goes back */
    vol_bar(shown, vol_music(core.options));
    setting(1, "MUSIC", shown);
    vol_bar(shown, vol_sfx(core.options, core.flags));
    setting(2, "SFX", shown);
    setting(3, "TURN", turn_names[(core.options >> 3) & 3u]);
    {
      crucible_time_ctx c;
      crucible_time_context(&c);
      if (c.flags & CT_F_KNOWN)
        time_now_text(shown, 0);
      else
        strcpy(shown, "NOT SET");
    }
    setting(4, "TIME", shown);
    setting(5, "HOW TO PLAY", "");
    setting(6, "RESET GAME", "");
    cursor((uint8_t)(at + 1u));
    cues("CHANGE", "BACK");
  } else if (page == PAGE_NAME) {
    cursor(255u);
    keyboard();
  } else if (page == PAGE_PLAY) {
    field_(3, ROW0 + 1u, 14, "PLAY", T_BRASS);
    text_(5, ROW0 + 3u, "STORY", 5, T_CREAM);
    text_(5, ROW0 + 4u, "FREE PLAY", 9, T_CREAM);
    text_(5, ROW0 + 5u, "LINK", 4, T_CREAM);
    text_(5, ROW0 + 6u, "BACK", 4, T_CREAM);
    cursor_at(4u, (uint8_t)(ROW0 + 3u + at));
    cues("CHOOSE", "BACK");
  } else if (page == PAGE_LINK) {
    field_(3, ROW0 + 1u, 14, "LINK CABLE", T_BRASS);
    text_(5, ROW0 + 3u, "HOST", 4, T_CREAM);
    text_(5, ROW0 + 4u, "JOIN", 4, T_CREAM);
    text_(5, ROW0 + 6u, "BACK", 4, T_CREAM);
    cursor_at(4u, at < 2u ? (uint8_t)(ROW0 + 3u + at) : ROW0 + 6u);
    cues("CHOOSE", "BACK");
  } else if (page == PAGE_LOBBY) {
    char v[9];
    uint8_t host = link_role == LINK_HOST;
    text_(3, ROW0, " ", 14, T_CREAM);
    lobby_cable(); /* the status lives in the cable scene (ROW0 is the sky band's: it scrolled) */
    link_label(0, v);
    setting(1, "MODE", v);
    if (link_mode == LINK_FIGHT) {
      char l[9];
      lr_lobby_row(2, l, v);
      setting(2, l, v);
      lr_lobby_row(3, l, v);
      setting(3, l, v);
    } /* a FIGHT's rules and edge (crucible_link_rules.c) */
    else {
      link_label(1, v);
      setting(2, "TIME", v);
      link_label(2, v);
      setting(3, "GOAL", v);
    }
    memcpy(v, seed_word, 8);
    v[8] = 0;
    setting(4, "SEED", host ? v : "HOST'S");
    text_(4, ROW0 + 6u, " ", 13, T_CREAM);
    text_(4, ROW0 + 6u, host ? (link_linked ? "START" : "START (WAIT)") : "WAIT FOR HOST",
          host ? (link_linked ? 5u : 12u) : 13u, host && link_linked ? T_CREAM : T_BRASS);
    if (host)
      cursor(at == 4u ? 6u : (uint8_t)(at + 1u));
    else
      cursor(255u);
    cues(host ? "CHANGE" : "", host ? "LEAVE" : "LEAVE");
  } else if (page == PAGE_RULES) {
    cursor(255u);
    lr_page_draw(ROW0);
    cues("CHANGE", "BACK");
  } else if (page == PAGE_BRING) {
    crucible_story st;
    char n[9];
    field_(3, ROW0 + 1u, 14, "BRING WHICH?", T_BRASS);
    text_(6, ROW0 + 2u, "FREE PLAY", 9, at == 0u ? T_CREAM : T_BRASS);
    for (i = 0; i < 3u; i++) {
      text_(4, (uint8_t)(ROW0 + 3u + i), "            ", 13, T_CREAM);
      put_(4, (uint8_t)(ROW0 + 3u + i), GLYPH((char)('1' + i)), T_BRASS);
      if (story_peek(i, &st)) {
        memcpy(n, st.name, 8);
        n[8] = 0;
        text_(6, (uint8_t)(ROW0 + 3u + i), n, 8, at == i + 1u ? T_CREAM : T_BRASS);
      } else
        text_(6, (uint8_t)(ROW0 + 3u + i), "---", 3, T_BRASS);
    }
    cursor_at(3u, (uint8_t)(ROW0 + 2u + at));
    cues("CHOOSE", "BACK");
  } else if (page == PAGE_SLOTS) {
    crucible_story st;
    char n[9];
    field_(3, ROW0 + 1u, 14, "STORY", T_BRASS);
    for (i = 0; i < 3u; i++) {
      text_(4, (uint8_t)(ROW0 + 2u + i), "            ", 13, T_CREAM);
      put_(4, (uint8_t)(ROW0 + 2u + i), GLYPH((char)('1' + i)), T_BRASS);
      if (story_peek(i, &st)) {
        memcpy(n, st.name, 8);
        n[8] = 0;
        text_(6, (uint8_t)(ROW0 + 2u + i), n, 8, i == at ? T_CREAM : T_BRASS);
        put_(15, (uint8_t)(ROW0 + 2u + i), GLYPH((char)('1' + (st.chapter > 8u ? 8u : st.chapter))), T_BRASS);
        put_(16, (uint8_t)(ROW0 + 2u + i), GLYPH(st.scale == 0u ? 'G' : st.scale == 1u ? 'N' : 'H'), T_BRASS);
      } else
        text_(6, (uint8_t)(ROW0 + 2u + i), "NEW", 3, i == at ? T_CREAM : T_BRASS);
    }
    field_(3, ROW0 + 5u, 14, "SELECT: RENAME", T_BRASS);
    text_(5, ROW0 + 6u, "BACK", 4, T_CREAM);
    cursor_at(3u, at < 3u ? (uint8_t)(ROW0 + 2u + at) : ROW0 + 6u);
    cues("CHOOSE", "BACK");
  } else if (page == PAGE_TIME) {
    char v[16], w[14];
    uint8_t k;
    static const uint8_t fx[5] = {11, 4, 8, 8, 11}, fw[5] = {4, 3, 2, 2, 2}, fy[5] = {3, 3, 3, 4, 4};
    cursor(255u);
    field_(3, ROW0 + 1u, 14, tmode == CT_ASK_ADJUST ? "SET THE CLOCK" : "WHAT IS TODAY?", T_BRASS);
    time_date_text(v, tf);
    text_(4, ROW0 + 3u, v + 4, 11, T_BRASS); /* OCT 05 2026 */
    memcpy(w, v, 4);
    time_format(w + 4, 7u | 0x80u, tf[CT_FH], tf[CT_FMIN]);
    text_(4, ROW0 + 4u, w, 12, T_BRASS); /* MON 10:04 AM */
    k = FORD[at < 5u ? at : 0u];
    text_(fx[k], (uint8_t)(ROW0 + fy[k]), fy[k] == 3u ? v + fx[k] : w + fx[k] - 4u, fw[k], T_CREAM);
    if (k == 3u) text_(14, ROW0 + 4u, w + 10, 2, T_CREAM);
    cues("OK", tmode == CT_ASK_ADJUST ? "BACK" : "SKIP");
  } else if (page == PAGE_BIRTH) {
    char v[16];
    uint8_t f[5];
    cursor(255u);
    field_(3, ROW0 + 1u, 14, "YOUR BIRTHDAY?", T_BRASS);
    field_(3, ROW0 + 2u, 14, "(NO YEAR)", T_BRASS);
    f[CT_FY] = 0;
    f[CT_FM] = bf[0];
    f[CT_FD] = bf[1];
    time_date_text(v, f);
    v[10] = 0;
    text_(7, ROW0 + 4u, v + 4, 6, T_BRASS);
    text_(at ? 11u : 7u, ROW0 + 4u, v + (at ? 8u : 4u), at ? 2u : 3u, T_CREAM);
    cues("OK", "SKIP");
  } else if (page == PAGE_RESET) {
    for (i = 0; i < 5u; i++) field_(3, ROW0 + i, 14, reset_lines[i], i ? T_BRASS : T_CREAM);
    text_(3, ROW0 + 6u, " ", 14, T_BRASS);
    reset_bar();
    cursor(255u);
    cues("HOLD", "KEEP");
  } else {
    for (i = 0; i < 6u; i++) field_(3, ROW0 + i, 14, help_lines[help][i], i == 5u ? T_BRASS : T_CREAM);
    cursor(255u);
    cues("NEXT", "BACK");
  }
}
/* The link room's cable, row ROW0+5: your handheld, the cable, and theirs (their name once it crossed). Waiting, a
 * spark runs down a dashed cable to an empty slot; linked, the cable is solid and hums. */
static void lobby_cable(void) {
  char s[15], n[CRU_NAME + 1u];
  uint8_t i, k = (uint8_t)((sys_time >> 3) & 3u);
  strcpy(s, "[#]");
  if (link_linked) {
    for (i = 0; i < 3u; i++) s[3u + i] = (char)(((i + k) & 1u) ? '=' : '-');
    link_peer(n);
    if (!n[0]) strcpy(n, link_role == LINK_HOST ? "GUEST" : "HOST");
    s[6] = 0;
    strcat(s, n);
    if (link_ask && link_role == LINK_HOST) strcat(s, "?");
  } /* ?: the guest asks for other rules */
  else {
    for (i = 0; i < 3u; i++) s[3u + i] = (char)(i == (k % 3u) ? '*' : '-');
    s[6] = 0;
    strcat(s, " [ ]");
  }
  text_(3, ROW0 + 5u, " ", 14, link_linked ? T_CREAM : T_BRASS);
  text_(4, ROW0 + 5u, s, (uint8_t)strlen(s), link_linked ? T_CREAM : T_BRASS);
}
/* the link moved (linked, rules): the lobby redraws */
void menu_link_refresh(void) BANKED {
  if (page == PAGE_LOBBY) draw();
}
static void time_page(uint8_t mode) {
  tmode = mode;
  time_ask_prefill(tf);
  page = PAGE_TIME;
  at = 0;
}
/* A new story game asks its date and birthday; loading and free play enter directly. */
static uint8_t start(uint8_t k) {
  /* Loading and free play have no asks. Re-entry from an ask keeps this new game's clock. */
  if (k != MENU_STORY_NEW) {
    pend = 0;
    cursor(255u);
    return k;
  }
  if (pend != k) time_new_game(menu_slot);
  if (time_ask_mode() != CT_ASK_DONE) {
    pend = k;
    time_page(CT_ASK_FIRST);
  } else if (time_birthday_due()) {
    pend = k;
    page = PAGE_BIRTH;
    bf[0] = 1;
    bf[1] = 1;
    at = 0;
  } else {
    pend = 0;
    cursor(255u);
    return k;
  }
  sound_play(SFX_OPEN);
  draw();
  return MENU_STAY;
}
void menu_open(uint8_t is_title) BANKED {
  flow_pending = 0;
  scene_draw(SCENE_MENU);
  title = is_title;
  page = PAGE_MAIN;
  at = 0;
  changed = 0;
  pend = 0;
  time_menu(2);
  draw();
}
/* the date and time page: LEFT/RIGHT a field, UP/DOWN its value, A sets it, B skips (or back, from SETUP) */
static uint8_t time_tick(uint8_t pressed) {
  static const uint8_t lo[5] = {0, 1, 1, 0, 0}, span[5] = {100, 12, 31, 24, 60};
  uint8_t n;
  n = time_month_days(tf[CT_FY], tf[CT_FM]);
  if (pressed & J_LEFT) {
    at = at ? at - 1u : 4u;
    sound_play(SFX_MOVE);
    draw();
  } else if (pressed & J_RIGHT) {
    at = (uint8_t)((at + 1u) % 5u);
    sound_play(SFX_MOVE);
    draw();
  } else if (pressed & (J_UP | J_DOWN)) {
    uint8_t k = FORD[at], s = k == CT_FD ? n : span[k];
    tf[k] = (uint8_t)(lo[k] + (uint8_t)((tf[k] - lo[k] + ((pressed & J_UP) ? 1u : s - 1u)) % s));
    n = time_month_days(tf[CT_FY], tf[CT_FM]);
    if (tf[CT_FD] > n) tf[CT_FD] = n;
    sound_play(SFX_MOVE);
    draw();
  } else if (pressed & (J_A | J_START | J_B)) {
    uint8_t ok = !(pressed & J_B);
    time_answer(tmode, ok ? CT_ANSWER_OK : CT_ANSWER_SKIP, tf);
    sound_play(ok ? SFX_PICK : SFX_CLOSE);
    if (tmode == CT_ASK_ADJUST) {
      page = PAGE_SETTINGS;
      at = 3u;
    } else if (pend)
      return start(pend);
    else {
      page = PAGE_MAIN;
      at = 0;
    }
    draw();
  }
  return MENU_STAY;
}
/* the birthday page: LEFT/RIGHT month or day, UP/DOWN the value, A keeps it, B skips (no sign); then the game */
static uint8_t birth_tick(uint8_t pressed) {
  uint8_t n = time_month_days(0, bf[0]); /* 2000 leaps: FEB 29 is a birthday */
  if (pressed & (J_LEFT | J_RIGHT)) {
    at ^= 1u;
    sound_play(SFX_MOVE);
    draw();
  } else if (pressed & (J_UP | J_DOWN)) {
    uint8_t s = at ? n : 12u;
    bf[at] = (uint8_t)(1u + (uint8_t)((bf[at] - 1u + ((pressed & J_UP) ? 1u : s - 1u)) % s));
    if (bf[1] > time_month_days(0, bf[0])) bf[1] = time_month_days(0, bf[0]);
    sound_play(SFX_MOVE);
    draw();
  } else if (pressed & (J_A | J_START | J_B)) {
    if (pressed & J_B)
      time_birthday_set(0, 0);
    else
      time_birthday_set(bf[0], bf[1]);
    sound_play((pressed & J_B) ? SFX_CLOSE : SFX_PICK);
    return start(pend);
  }
  return MENU_STAY;
}
static void cycle(uint8_t row, int8_t d) {
  uint8_t v;
  /* volumes: LEFT/RIGHT step 0..8, A steps up and wraps to 0; the change plays at once (the tick is its preview) */
  if (row < 2u) {
    uint8_t f = core.flags;
    v = row ? vol_sfx(core.options, f) : vol_music(core.options);
    v = d < 0 ? (v ? v - 1u : 0u) : d > 1 ? (v >= VOL_STEPS ? 0u : v + 1u) : (v < VOL_STEPS ? v + 1u : v);
    if (row)
      core.options = vol_sfx_set(core.options, &f, v);
    else
      core.options = vol_music_set(core.options, v);
    core.flags = f;
  } else if (row == 2u) {
    v = (uint8_t)(((core.options >> 3) & 3u) + (d > 0 ? 1u : 2u)) % 3u;
    core.options = (core.options & ~(3u << 3)) | (uint8_t)(v << 3);
  } else
    return;
  changed = 1;
  sound_options(core.options);
  sound_play(SFX_MOVE);
  draw();
}
/* Returns a MENU_* action for the cartridge (MENU_STAY while the menu stays open). */
uint8_t menu_tick(uint8_t pressed) BANKED {
  {
    uint8_t e = time_menu(page == PAGE_MAIN); /* a special minute on the open menu; at night the music stops */
    if (e == CT_MENU_ENTER || e == CT_MENU_LEAVE) {
      if (page == PAGE_MAIN) draw();
      music_mood(e == CT_MENU_ENTER && (time_menu_special() & 0x80u) ? 0u : 2u);
    } else if (e == CT_MENU_MINUTE && page == PAGE_SETTINGS)
      draw();
  }
  if (page == PAGE_TIME) return time_tick(pressed);
  if (page == PAGE_BIRTH) return birth_tick(pressed);
  if (page == PAGE_MAIN) {
    uint8_t n = items();
    if (at >= n) at = 0;
    if (pressed & J_UP) {
      at = (uint8_t)((at + ITEMS - 2u) % ITEMS);
      if (at >= n) at -= 2u;
      sound_play(SFX_MOVE);
      draw();
    } else if (pressed & J_DOWN) {
      at = (uint8_t)((at + 2u) % ITEMS);
      if (at >= n) at &= 1u;
      sound_play(SFX_MOVE);
      draw();
    } else if (pressed & (J_LEFT | J_RIGHT)) {
      at ^= 1u;
      if (at >= n) at ^= 1u;
      sound_play(SFX_MOVE);
      draw();
    } else if (pressed & (J_A | J_START)) {
      if (at == SETUP_AT) {
        page = PAGE_SETTINGS;
        at = 0;
        sound_play(SFX_OPEN);
        draw();
        return MENU_STAY;
      }
      if (at == 0u && title) {
        page = PAGE_PLAY;
        at = 0;
        sound_play(SFX_OPEN);
        draw();
        return MENU_STAY;
      } /* PLAY: a story, or free play */
      if (at == 5u && !link_started) {
        story_leave();
        sound_play(SFX_CLOSE);
        menu_open(1);
        return MENU_STAY;
      } /* LEAVE: the run is saved; the title card */
      cursor(255u);
      sound_play(at ? SFX_OPEN : SFX_CLOSE);
      return at == 5u ? MENU_SEND : main_action[at];
    } else if ((pressed & J_B) && !title) {
      cursor(255u);
      sound_play(SFX_CLOSE);
      return MENU_RESUME;
    }
    return MENU_STAY;
  }
  if (page == PAGE_PLAY) {
    if (pressed & J_UP) {
      at = at ? at - 1u : 3u;
      sound_play(SFX_MOVE);
      draw();
    } else if (pressed & J_DOWN) {
      at = (uint8_t)((at + 1u) & 3u);
      sound_play(SFX_MOVE);
      draw();
    } else if (pressed & J_B || ((pressed & J_A) && at == 3u)) {
      page = PAGE_MAIN;
      at = 0;
      sound_play(SFX_CLOSE);
      draw();
    } else if (pressed & J_A) {
      if (at == 1u) {
        sound_play(SFX_CLOSE);
        return start(MENU_FREE);
      }
      page = at == 2u ? PAGE_LINK : PAGE_SLOTS;
      at = 0;
      sound_play(SFX_OPEN);
      draw();
    }
    return MENU_STAY;
  }
  if (page == PAGE_LINK) {
    if (pressed & J_UP) {
      at = at ? at - 1u : 2u;
      sound_play(SFX_MOVE);
      draw();
    } else if (pressed & J_DOWN) {
      at = (uint8_t)((at + 1u) % 3u);
      sound_play(SFX_MOVE);
      draw();
    } else if (pressed & J_B || ((pressed & J_A) && at == 2u)) {
      page = PAGE_PLAY;
      at = 2u;
      sound_play(SFX_CLOSE);
      draw();
    } else if (pressed & J_A) {
      bring_role = at;
      page = PAGE_BRING;
      at = 0;
      sound_play(SFX_OPEN);
      draw();
    }
    return MENU_STAY;
  }
  if (page == PAGE_LOBBY) {
    if (!((uint8_t)sys_time & 7u)) lobby_cable();
    uint8_t host = link_role == LINK_HOST;
    if (pressed & J_B) {
      link_close();
      page = PAGE_LINK;
      at = 0;
      sound_play(SFX_CLOSE);
      draw();
      return MENU_STAY;
    }
    if (!host) {
      if ((pressed & J_SELECT) && link_linked && link_mode == LINK_FIGHT) {
        (void)link_send(P_READY, 0xffu, 2u);
        sound_play(SFX_MOVE);
      }
      return MENU_STAY;
    } /* the guest asks for other rules */
    if (pressed & J_UP) {
      at = at ? at - 1u : 4u;
      sound_play(SFX_MOVE);
      draw();
    } else if (pressed & J_DOWN) {
      at = (uint8_t)((at + 1u) % 5u);
      sound_play(SFX_MOVE);
      draw();
    } else if (pressed & (J_A | J_LEFT | J_RIGHT)) {
      link_ask = 0;
      if (at == 0u)
        link_mode = (uint8_t)((link_mode + ((pressed & J_LEFT) ? 2u : 1u)) % 3u); /* CO-OP, RACE, FIGHT */
      else if ((at == 1u || at == 2u) && link_mode == LINK_FIGHT) {
        if (lr_lobby_change((uint8_t)(at + 1u), pressed)) {
          page = PAGE_RULES;
          sound_play(SFX_OPEN);
          draw();
          return MENU_STAY;
        }
      } else if (at == 1u)
        link_time = (uint8_t)((link_time + ((pressed & J_LEFT) ? 3u : 1u)) & 3u);
      else if (at == 2u)
        link_goal = (uint8_t)((link_goal + ((pressed & J_LEFT) ? 2u : 1u)) % 3u);
      else if (at == 3u && (pressed & J_A)) {
        seed_edit = 1;
        memcpy(name, seed_word, CRU_NAME);
        name[CRU_NAME] = 0;
        letter = 0;
        while (letter < CRU_NAME && name[letter] != ' ') letter++;
        kx = ky = 0;
        page = PAGE_NAME;
        clear();
        draw();
        return MENU_STAY;
      } else if (at == 4u && (pressed & J_A)) {
        if (!link_linked) {
          sound_play(SFX_DENY);
          return MENU_STAY;
        }
        cursor(255u);
        sound_play(SFX_OPEN);
        return MENU_LINK_GO;
      }
      sound_play(SFX_MOVE);
      draw();
    }
    return MENU_STAY;
  }
  if (page == PAGE_BRING) {
    crucible_story st;
    if (pressed & J_UP) {
      at = at ? at - 1u : 3u;
      sound_play(SFX_MOVE);
      draw();
    } else if (pressed & J_DOWN) {
      at = (uint8_t)((at + 1u) & 3u);
      sound_play(SFX_MOVE);
      draw();
    } else if (pressed & J_B) {
      page = PAGE_LINK;
      at = bring_role;
      sound_play(SFX_CLOSE);
      draw();
    } else if (pressed & J_A) {
      if (at && !story_peek((uint8_t)(at - 1u), &st)) {
        sound_play(SFX_DENY);
        return MENU_STAY;
      }
      link_save = at;
      link_open(bring_role == 0u ? LINK_HOST : LINK_GUEST);
      if (bring_role == 0u) lr_load();
      page = PAGE_LOBBY;
      at = 0;
      sound_play(SFX_OPEN);
      draw();
    }
    return MENU_STAY;
  }
  if (page == PAGE_RULES) {
    if (lr_page(pressed, ROW0)) {
      page = PAGE_LOBBY;
      at = 1;
      sound_play(SFX_CLOSE);
      draw();
    } else if (pressed)
      link_ask = 0;
    return MENU_STAY;
  }
  if (page == PAGE_SLOTS) {
    if (pressed & J_UP) {
      at = at ? at - 1u : 3u;
      sound_play(SFX_MOVE);
      draw();
    } else if (pressed & J_DOWN) {
      at = (uint8_t)((at + 1u) % 4u);
      sound_play(SFX_MOVE);
      draw();
    } else if (pressed & J_B || ((pressed & J_A) && at == 3u)) {
      page = PAGE_PLAY;
      at = 0;
      sound_play(SFX_CLOSE);
      draw();
    } else if (pressed & J_A) {
      crucible_story st;
      menu_slot = at;
      sound_play(SFX_OPEN);
      return start(story_peek(at, &st) ? MENU_STORY_LOAD : MENU_STORY_NEW);
    } else if (pressed & J_SELECT) {
      crucible_story st;
      if (at >= 3u || !story_peek(at, &st)) {
        sound_play(SFX_DENY);
        return MENU_STAY;
      }
      rename_slot = at;
      memcpy(name, st.name, CRU_NAME);
      name[CRU_NAME] = 0;
      letter = CRU_NAME;
      while (letter && name[letter - 1u] == ' ') letter--;
      kx = ky = 0;
      page = PAGE_NAME;
      clear();
      sound_play(SFX_OPEN);
      draw();
    }
    return MENU_STAY;
  }
  if (page == PAGE_SETTINGS) {
    if (pressed & J_UP) {
      at = at ? at - 1u : SETTINGS - 1u;
      sound_play(SFX_MOVE);
      draw();
    } else if (pressed & J_DOWN) {
      at = (at + 1u) % SETTINGS;
      sound_play(SFX_MOVE);
      draw();
    } else if (pressed & (J_LEFT | J_RIGHT))
      cycle(at, (pressed & J_LEFT) ? -1 : 1);
    else if (pressed & J_A) {
      if (at < 3u)
        cycle(at, at < 2u ? 2 : 1);
      else if (at == 3u) {
        time_page(CT_ASK_ADJUST);
        sound_play(SFX_OPEN);
        draw();
      } else if (at == 4u) {
        page = PAGE_HELP;
        help = 0;
        sound_play(SFX_OPEN);
        draw();
      } else if (at == 5u) {
        page = PAGE_RESET;
        hold = 0;
        armed = 0;
        sound_play(SFX_OPEN);
        draw();
        return MENU_STAY;
      }
    }
    if (pressed & J_B) {
      if (changed) {
        cru_save(&core);
        changed = 0;
      }
      page = PAGE_MAIN;
      at = SETUP_AT;
      sound_play(SFX_CLOSE);
      draw();
    }
    return MENU_STAY;
  }
  /* Reset: a fresh A press arms the hold (the A that opened the page does not count); letting go or B keeps the game. */
  if (page == PAGE_RESET) {
    if (pressed & J_B) {
      page = PAGE_SETTINGS;
      at = 5u;
      sound_play(SFX_CLOSE);
      draw();
      return MENU_STAY;
    }
    /* timed on the VBlank clock, so the hold is two real seconds however busy the title card is */
    if (pressed & J_A) {
      armed = 1;
      hold_t0 = sys_time;
    }
    if (armed && (joypad() & J_A)) {
      uint16_t t = sys_time - hold_t0;
      if (t >= HOLD_FRAMES) {
        dialogue_reset_free();
        talk_saga_reset();
        cru_reset(&core, DIV_REG);
        reset();
      }
      if ((uint8_t)t != hold) {
        hold = (uint8_t)t;
        reset_bar();
      }
    } else if (armed) {
      hold = 0;
      armed = 0;
      reset_bar();
    }
    return MENU_STAY;
  }
  if (page == PAGE_NAME) {
    char c = keys[ky][kx];
    if (pressed & J_UP) {
      ky = ky ? ky - 1u : 4u;
    } else if (pressed & J_DOWN) {
      ky = (ky + 1u) % 5u;
    } else if (pressed & J_LEFT) {
      kx = kx ? kx - 1u : 6u;
      if (keys[ky][kx] == '#' && kx == 6u) kx = 5u;
    } else if (pressed & J_RIGHT) {
      kx = (kx + 1u) % 7u;
      if (keys[ky][kx] == '#' && kx == 6u) kx = 0u;
    } else if (pressed & J_B) {
      if (letter) letter--;
      name[letter] = ' ';
      sound_play(SFX_UNDO);
    } else if ((pressed & J_START) || ((pressed & J_A) && c == '#')) {
      if (seed_edit) {
        uint8_t i;
        uint16_t h = 0x1d2bu;
        seed_edit = 0;
        memcpy(seed_word, name, CRU_NAME);
        for (i = 0; i < CRU_NAME; i++) h = (uint16_t)((h << 5) + h + (uint8_t)name[i]);
        link_seed = h;
        page = PAGE_LOBBY;
        at = 3u;
        sound_play(SFX_CLOSE);
        clear();
        draw();
        return MENU_STAY;
      }
      if (!letter) {
        sound_play(SFX_DENY);
        return MENU_STAY;
      } /* a slot keeps a name */
      story_rename(rename_slot, name);
      page = PAGE_SLOTS;
      at = rename_slot;
      rename_slot = 255u;
      sound_play(SFX_CLOSE);
      clear();
      draw();
      return MENU_STAY;
    } else if (pressed & J_A) {
      if (c == '<') {
        if (letter) letter--;
        name[letter] = ' ';
        sound_play(SFX_UNDO);
      } else if (letter < CRU_NAME) {
        name[letter++] = c == '_' ? ' ' : c;
        sound_play(SFX_MOVE);
      } else
        sound_play(SFX_DENY);
    } else
      return MENU_STAY;
    if (!(pressed & (J_A | J_B))) sound_play(SFX_MOVE);
    keyboard();
    return MENU_STAY;
  }
  if (pressed & J_A) {
    help++;
    if (help >= 3u) {
      page = PAGE_SETTINGS;
      at = 4u;
    }
    sound_play(SFX_MOVE);
    draw();
  } else if (pressed & J_B) {
    page = PAGE_SETTINGS;
    at = 4u;
    sound_play(SFX_CLOSE);
    draw();
  }
  return MENU_STAY;
}
