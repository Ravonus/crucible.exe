#ifndef CRUCIBLE_PLAYER_H
#define CRUCIBLE_PLAYER_H
/* The player record (crucible_player.c; docs/fight-system.md 8 and 10.4): avatar genome, level, XP, attributes,
 * cosmetic unlocks, secrets, passives, the persistent kit, nemesis memory, the last match rules and the link record.
 * One per story slot (128 B at 0x1DC0 in the slot's SRAM bank, its own CRC) and one for free play (its first 82 bytes
 * are the Classic record's bytes 264..345, under that record's CRC: core.card). Only this cartridge ever writes it. */
#include <gb/gb.h>
#define PL_BYTES 128u
#define PL_CARD 82u /* the free-play part (the Classic record holds this much) */
#define PL_VERSION 1u
#define PL_AT 0x1DC0u /* in a story slot's SRAM bank */
/* cosmetic option rows (8.2): STYLE HEAD EYES MOUTH CROWN MARK HUE GRAIN AURA; 81 options in all */
#define AV_ROWS 9u
#define AV_OPTIONS 81u
uint8_t player_row_count(uint8_t row) BANKED; /* options in a row (the tables live in the player's bank) */
uint8_t player_row_base(uint8_t row) BANKED;
/* attributes (8.3): GRIT +1 HP (cap 4), FOCUS +1 starting focus (cap 2), REACH +1 kit budget (cap 3) */
#define PA_GRIT 0u
#define PA_FOCUS 1u
#define PA_REACH 2u
/* flags (pl.flags) */
#define PF_FIRST_DUEL 1u /* the first contact duel is done */
#define PF_CREATED 2u /* the creator ran for this save */
#define PF_SHEET 4u /* the 3:33 sheet ghost was seen */
#define PF_GLITCH 8u /* survived lucid 0 */
#define PF_AI_EYE 16u
#define PF_DMG 32u /* the DMG-green world was seen: the hue row's 17th swatch */
typedef struct {
  uint8_t magic[2], version;
  uint8_t genome[6];
  uint8_t level;
  uint16_t xp;
  uint8_t attrs; /* GRIT:3 | FOCUS:2 << 3 | REACH:2 << 5 | slot2 << 7 */
  uint8_t unlock[11]; /* 81 option bits */
  uint8_t secret[2]; /* spared:3 | taken:3 << 3 | ai wins:2 << 6, flags */
  uint16_t known; /* passives known (12 bits) */
  uint16_t kit[6]; /* the persistent kit (CRU_NONE: empty) */
  uint8_t equip; /* two passive slots, 4 bits each (15: none) */
  uint8_t nemesis[2]; /* the stance it was beaten with most, how (4 bits) | met */
  uint8_t rules[8]; /* the last match rules R0..R7 */
  /* the link record (9.3, 9.4): the last partner and this session's bounded traces */
  uint8_t pgenome[6];
  char pname[8];
  uint8_t sessions;
  uint16_t session;
  uint8_t kept_s, kept_ch;
  int8_t tint[2];
  uint16_t plast;
  uint8_t pts; /* attribute points to spend */
  uint8_t remake; /* known recipes remade for XP this chapter (max 20) */
  uint8_t chapter; /* the chapter these counters belong to */
  uint8_t cell; /* alignment cell the chapter has stayed in (low 4 bits; 15 changed), checked at its end */
  uint8_t flags;
  uint8_t duels, bosses, order; /* order: the seed of its level order (8.3) */
} player_rec; /* in RAM: the 82 bytes free play keeps too; a story slot's bytes 82..125 stay in SRAM, 126..127 its CRC */
typedef char player_rec_is_82[sizeof(player_rec) == PL_CARD ? 1 : -1];
extern player_rec pl;
extern uint8_t pl_slot; /* 0xff: free play; else the story slot */
void player_classic_attach(void) BANKED; /* before cru_load of the Classic save: core.card = pl */
void player_classic_loaded(void) BANKED; /* after it: validate the card, defaults if missing */
void player_story_load(uint8_t slot, uint32_t seed) BANKED; /* defaults (an old save's face from the seed) if missing */
void player_story_new(uint8_t slot, const uint8_t *genome) BANKED;
void player_save(void) BANKED;
void player_defaults(uint32_t seed) BANKED;
uint8_t player_attr(uint8_t which) BANKED;
uint8_t player_slots(void) BANKED; /* passive slots: 1, 2 from level 6 */
uint8_t player_xp(uint16_t xp) BANKED; /* adds XP; returns the levels gained */
uint16_t player_next(void) BANKED; /* XP still to the next level */
uint8_t player_spend(uint8_t which) BANKED; /* one point into GRIT/FOCUS/REACH; 0 when capped or none */
uint8_t player_unlocked(uint8_t option) BANKED;
uint16_t player_bit(uint8_t k) BANKED; /* 1 << k for k < 16 (a table: sdcc and variable shifts disagree) */
void player_unlock(uint8_t option) BANKED;
uint8_t player_kit(uint16_t *kit) BANKED; /* the kit to fight with (owned, within budget; auto-kit otherwise) */
void player_autokit(uint16_t *kit, uint8_t budget) BANKED;
uint16_t player_owned_next(uint16_t id) BANKED; /* the next owned element on the shelf, any filter; wraps */
uint8_t player_budget(void) BANKED;
void player_genome_roll(uint8_t *g, uint16_t seed) BANKED; /* a face within the unlocked options (SELECT: dream it) */
void player_chapter(uint8_t chapter, uint8_t cell) BANKED; /* the story moved: chapter counters, cell streak unlocks */
#endif
