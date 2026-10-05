#ifndef CRUCIBLE_PREDICT_H
#define CRUCIBLE_PREDICT_H
#include <gb/gb.h>
void crucible_art_cursor(uint16_t id, uint8_t book) BANKED;
/* the on-screen objects, whose motion frames stay cached */
void crucible_art_visible(const uint16_t *ids, uint8_t n) BANKED;
/* 255: the object's whole loop (turn 0 resting poses, 1 turntable) is cached; else progress 0..254, chain queued */
uint8_t crucible_art_ready(uint16_t id, uint8_t turn, uint8_t priority) BANKED;
extern uint8_t crucible_art_urgent;
extern uint16_t crucible_cache_min, crucible_cache_max;
extern uint16_t crucible_gen_jobs, crucible_cache_hits;
extern uint8_t crucible_gen_last_frames, crucible_gen_max_frames;
#endif
