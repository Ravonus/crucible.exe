#ifndef CRUCIBLE_DIALOGUE_H
#define CRUCIBLE_DIALOGUE_H
/* Cartridge profile: flags[16..31] hold the portable 16-byte dialogue state.
 * flags[0..15] and the v5 story record's size and CRC are not affected. */
#define KDC_TYPES_ONLY
#include "keel_dialogue_choice.h"
#undef KDC_TYPES_ONLY
/* crucible_dialogue.c: selection, effects, the alignment matrix (never shown) */
void dialogue_init(crucible_story *) BANKED;
uint16_t dialogue_open(crucible_story *, uint16_t) BANKED;
uint8_t dialogue_cadence(crucible_story *) BANKED;
uint16_t dialogue_ask(uint8_t, uint16_t *) BANKED;
void dialogue_prepare(crucible_story *, uint8_t, uint16_t, uint16_t, uint16_t, uint16_t *) BANKED;
void dialogue_label(uint8_t, char *) BANKED;
/* the answer: effects, then the speaker's reply (slots filled) into out; CRU_REACT_* */
uint8_t dialogue_answer(crucible_story *, uint8_t, uint8_t, const char *const *, char *) BANKED;
/* a greeting that answers to the last act (1: written to out) */
uint8_t dialogue_hint(crucible_story *, uint8_t, uint16_t, const char *const *, char *) BANKED;
/* subtle cues: the option cursor glyph; after an answer 0 none, 1 one-frame glyph swap,
 * 2 a stutter, 4 the room flickers (consumed when read) */
uint8_t dialogue_cursor(void) BANKED;
uint8_t dialogue_cue(void) BANKED;
/* typing: a speaker's temperament, base quarter-frames per letter, and each letter's cost */
uint8_t dialogue_temper(crucible_story *, uint8_t) BANKED;
uint8_t dialogue_type_base(uint8_t, uint16_t) BANKED;
uint8_t dialogue_type_cost(uint8_t, uint8_t, char, uint8_t, uint16_t) BANKED;
void dialogue_save(crucible_story *) BANKED;
void dialogue_reset_free(void) BANKED;
/* crucible_dialogue_labels.c: option labels; crucible_dialogue_text.c: reply and hint prose */
void dialogue_label_text(uint8_t, char *) BANKED;
uint8_t dialogue_reply_text(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint16_t *, const char *const *,
                            char *) BANKED;
uint8_t dialogue_hint_text(uint8_t, uint8_t, uint16_t, const char *const *, char *) BANKED;
#endif
