#ifndef CRUCIBLE_STORYRUN_H
#define CRUCIBLE_STORYRUN_H
/* Story runs (crucible_storyrun.c): three slots beside free play. */
#define STORY_NONE 0u
#define STORY_LOSS 1u /* a miss destroyed an element (story_lost); or a lost piece's first glitch (talk_lost_arm) */
#define STORY_OVER 2u /* the run is lost for good */
#define STORY_VISIT 3u /* someone steps in */
#define STORY_BOSS 4u /* a hostile faction's champion comes for you */
extern uint8_t story_on, story_slot_at;
uint8_t story_peek(uint8_t slot, crucible_story *out) BANKED;
void story_begin(uint8_t slot, uint8_t scale, uint32_t hash, const char *name) BANKED;
uint8_t story_resume(uint8_t slot) BANKED; /* 1 resumed; 0 the slot was empty */
void story_rename(uint8_t slot, const char *name) BANKED; /* CRU_NAME letters, blank-padded */
void story_save(void) BANKED;
void story_leave(void) BANKED;
void story_end(void) BANKED;
uint16_t story_lost(void) BANKED;
uint8_t story_after_mix(uint16_t a, uint16_t b, uint8_t made) BANKED;
void story_hud(uint8_t dt, uint8_t force) BANKED; /* the bench's top row during a run */
/* the saga lives with the TALK screen (crucible_talk.c) */
crucible_story *talk_saga(void) BANKED;
void talk_saga_ready(void) BANKED; /* the saga was loaded from a run: do not re-initialize it */
void talk_saga_reset(void) BANKED; /* back to free play: a fresh dream for this power-on */
/* The next talk_event is a lost piece's first glitch (id): one short line from the machine, gone by itself. */
void talk_lost_arm(uint16_t id) BANKED;
#endif
