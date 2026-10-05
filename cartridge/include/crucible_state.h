#ifndef CRUCIBLE_STATE_H
#define CRUCIBLE_STATE_H
#include <gb/gb.h>
/* Shared game state. The rules, the catalogue lookups and the battery save (two CRC records, a journal and the
 * play-memory areas; docs/save-structures.md) live in the CRUCIBLE core (core/, portable C that host builds also compile); the
 * cartridge keeps one crucible_core and draws, plays sound and animates around it. SRAM banks 1 and 3 cache
 * downloaded artwork; bank 2 is fusion and palette scratch. */
/* the core is banked on the cartridge: its prototypes must say so here too, or calls into it are plain calls */
#ifndef CORE_BANKED
#define CORE_BANKED BANKED
#define CORE_LOCAL BANKED
#endif
#include "crucible_core.h"
#define NONE 255u
#define FEATS CRU_FEATS
#define BOARD_ROWS CRU_BOARD_ROWS
#define TITLE_TRAITS CRU_TITLE_TRAITS
#define TITLE_DOMAINS CRU_TITLE_DOMAINS
#define TITLES CRU_TITLES
#define OPT_SFX_OFF CRU_OPT_SFX_OFF
#define BOARD_FRIEND CRU_FRIEND
extern crucible_core core;
#define OWNED(id) cru_owned(&core, (id))
#define VARIANT(id) cru_variant(&core, (id))
/* power-on: the core over the cartridge's SRAM (crucible_codec.c) */
void save_boot(void) BANKED;
/* Toast card under the header: returns its visible height in lines (0 when hidden). */
uint8_t feats_toast(uint8_t allowed) BANKED;
/* Shared text helpers (crucible_text.c): attr is palette | 8 for the dim font */
void ui_put(uint8_t x, uint8_t y, uint8_t tile, uint8_t attr) BANKED;
void ui_text(uint8_t x, uint8_t y, const char *s, uint8_t width, uint8_t attr) BANKED;
void ui_field(uint8_t x, uint8_t y, uint8_t width, const char *s, uint8_t attr) BANKED;
void ui_number(char *out, uint16_t n, uint8_t width) BANKED;
void ui_time(char *out, uint16_t seconds) BANKED;
/* Records screen (crucible_records.c): pages 0 achievements, 1 titles, 2 stats, 3 leaderboard */
void records_open(uint8_t tab) BANKED;
uint8_t records_tick(uint8_t pressed) BANKED;
/* Sound effects (crucible_sound.c) */
enum {
  SFX_MOVE = 1,
  SFX_PICK,
  SFX_UNDO,
  SFX_MIX,
  SFX_BUBBLE,
  SFX_SWAP,
  SFX_FLASH,
  SFX_NEW,
  SFX_ROUTE,
  SFX_KNOWN,
  SFX_NOTHING,
  SFX_OPEN,
  SFX_CLOSE,
  SFX_DENY,
  SFX_LINK
};
#define SFX_PMADE SFX_ROUTE /* rows 9 and 15 were silent: the partner's make and miss (fight spec 9.5) */
#define SFX_PMISS SFX_LINK
void sound_init(void) BANKED;
void sound_play(uint8_t) BANKED;
void sound_pitch(uint8_t) BANKED;
void sound_tick(void) BANKED;
/* Generative layer: moods 0 off, 1 bench, 2 quiet (book, records), 3 merge; voice kinds in crucible_sound.c */
void music_mood(uint8_t) BANKED;
void music_scene(uint8_t) BANKED;
void sound_voice(uint16_t id, uint8_t category, uint8_t kind, uint8_t from) BANKED;
void sound_drone(uint8_t step) BANKED;
void sound_options(uint8_t options) BANKED;
/* Filters (crucible_filter.c): 0 all, 1..7 types, 8..19 traits */
#define FILTERS CRU_FILTERS
void filter_label(uint8_t f, char *out) BANKED;
void filter_open(void) BANKED;
uint8_t filter_tick(uint8_t pressed) BANKED;
/* Scanline events (crucible_lcd.c): at LYC line, in HBlank, apply kind bits; values are written directly. */
#define LCD_EVENTS 7u
#define LCD_SCX 1u
#define LCD_WIN_OFF 2u
#define LCD_OBJ_OFF 4u
#define LCD_OBJ_ON 8u
extern uint8_t lcd_line[LCD_EVENTS], lcd_kind[LCD_EVENTS], lcd_value[LCD_EVENTS];
void lcd_install(void) BANKED;
uint8_t input_take(void) BANKED;
void lcd_events(const uint8_t *lines, const uint8_t *kinds, uint8_t n) BANKED;
/* Menu (crucible_menu.c): title card at power-on, pause menu on Start */
/* talks and fights are never menu actions: they come to the bench in play (crucible_flow.c) */
enum {
  MENU_STAY,
  MENU_RESUME,
  MENU_BOOK,
  MENU_TITLES,
  MENU_STATS,
  MENU_FREE,
  MENU_STORY_NEW,
  MENU_STORY_LOAD,
  MENU_LINK_GO,
  MENU_SEND
};
void menu_link_refresh(void) BANKED;
void bands_setup(void) BANKED; /* the title card's cloud bands (crucible_bands.c) */
void bands_tick(uint8_t dt) BANKED;
extern uint8_t menu_slot; /* the story slot MENU_STORY_NEW / _LOAD chose */
/* TALK (crucible_talk.c): a seeded character's conversation */
void talk_open(uint16_t seed, uint8_t sector) BANKED;
uint8_t talk_tick(uint8_t pressed) BANKED;
uint8_t talk_bust(void) BANKED;
uint8_t talk_tear(void) BANKED; /* 1 once when a line tears the scene */
void talk_egg(uint8_t k) BANKED; /* an easter egg's visitor */
void talk_intro(uint16_t seed) BANKED; /* a new story run wakes */
uint8_t talk_intro_scale(void) BANKED;
void talk_wait(void) BANKED; /* the text box while a new run is built */
uint32_t talk_intro_hash(void) BANKED;
const char *talk_intro_name(void) BANKED;
void talk_event(uint8_t kind, uint16_t item) BANKED; /* the machine at a turning point (STORY_LOSS / STORY_OVER) */
void talk_notice(uint16_t id) BANKED; /* an element made: factions and the truth matrix notice */
void menu_open(uint8_t title) BANKED;
uint8_t menu_tick(uint8_t pressed) BANKED;
#endif
