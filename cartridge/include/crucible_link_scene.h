#ifndef CRUCIBLE_LINK_SCENE_H
#define CRUCIBLE_LINK_SCENE_H
/* Shared scenes over the link (crucible_link_scene.c, fight spec 9.4): a co-op boss on both screens. */
#define LS_NONE 0u
#define LS_OWNER 1u
#define LS_WATCHER 2u
/* states */
#define LS_CUE 1u /* the owner waits for the partner (4 s) */
#define LS_COLLECT 2u /* the watcher gathers the setup */
#define LS_PULL 3u /* the watcher: pulled in at its next safe point */
#define LS_SHARED 4u /* both in the scene */
#define LS_SOLO 5u /* given up: the owner plays alone */
/* the setup: 0 seed; 1 faction | tier << 4 | memory << 8 | pip adjust (+2) << 12; 2..7 the owner's kit; 8..11 the
 * boss's hand; 12 passives | HP << 8; 13 focus | level << 8 */
#define LS_WORDS 14u
extern uint8_t ls_role, ls_state, ls_beat, ls_active, ls_veto, ls_nudge, ls_their_hp, ls_quiet, ls_nudges;
extern uint16_t ls_seed;
uint16_t ls_word(uint8_t k) BANKED;
void link_scene_reset(void) BANKED;
void link_scene_owner(const uint16_t *w) BANKED;
uint8_t link_scene_wait(void) BANKED;
void link_scene_round(uint8_t beat) BANKED;
uint8_t link_scene_shared(void) BANKED;
uint8_t link_scene_mine(void) BANKED;
void link_scene_send(uint8_t p0, uint8_t p1, uint8_t close) BANKED;
uint8_t link_scene_take(uint8_t *p0, uint8_t *p1, uint8_t *close) BANKED;
void link_scene_nudge(uint8_t cursor) BANKED;
uint8_t link_scene_veto(void) BANKED;
void link_scene_phase(void) BANKED;
void link_scene_end(void) BANKED;
uint8_t link_scene_packet(uint8_t type, uint16_t a, uint16_t b) BANKED;
uint8_t link_scene_pull(void) BANKED;
uint8_t link_scene_watching(void) BANKED;
void link_scene_joined(uint8_t my_hp) BANKED;
void link_scene_tick(uint8_t dt) BANKED;
#endif
