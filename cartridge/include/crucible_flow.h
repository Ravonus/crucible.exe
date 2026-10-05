#ifndef CRUCIBLE_FLOW_H
#define CRUCIBLE_FLOW_H
/* The encounter director (crucible_flow.c): talks and fights come to the bench during play, never from a menu.
 * Some force themselves in (telegraphed on the sign row, then the view pans away), others wait at the edge of the
 * bench as a hint (a face peeking in above: someone to talk to; eyes at the floor's edge below: something to fight)
 * until you approach with UP or DOWN, or they fade. The view pans (SCY) to the encounter and back to the exact bench.
 * It also draws the bench, book and reveal cue rows (the button grammar: A choose, B back, START menu, SELECT filter)
 * and the bench's sign row. See docs/game-flow.md. */
#include <gb/gb.h>
#define FLOW_NONE 0u
#define FLOW_TALK 1u /* a visitor: talk_open */
#define FLOW_FIGHT 2u /* a champion: flow_arg = faction | 0x80 for the nemesis */
#define FLOW_UP 1u /* where an encounter waits: above (a talker) */
#define FLOW_DOWN 2u /* below (a fighter) */
#define FLOW_OAM 38u /* the hint sprite (free between the loading spark, 37, and the cursor, 39) */
#define FLOW_TILE 93u /* two hint tiles, OBJ tiles 93..94 in VRAM bank 0 (UI sprites end at 92, rooms start at 112) */
/* state (globals so a test harness can read them from the .noi) */
extern uint8_t flow_hint; /* FLOW_NONE, FLOW_TALK (waits above) or FLOW_FIGHT (waits below) */
extern uint8_t flow_force; /* a telegraphed encounter about to force itself in (FLOW_*), or FLOW_NONE */
extern uint8_t flow_arg; /* the fight: faction | 0x80 nemesis */
extern uint8_t flow_pending; /* the view went this way (FLOW_UP / FLOW_DOWN) and pans back on the next bench */
extern uint8_t flow_since; /* mixes since the last encounter */
extern uint8_t flow_count; /* encounters this power-on (talks + fights), for tests and pacing */
extern uint8_t flow_fgap, flow_tgap; /* mixes since the last fight / talk */
/* After a mix's story reaction (STORY_*): returns STORY_LOSS / STORY_OVER for the cartridge to act on at once (the
 * machine's own lines), and queues everything else (visits, champions, chapter beats, rivals) as a hint or a force. */
uint8_t flow_after_mix(uint8_t story) BANKED;
/* Every frame on every screen, before the room's tick: on the bench it runs hints, telegraphs and approaches (the
 * hinted direction is taken out of *pressed); elsewhere it hides the hint. Returns FLOW_TALK / FLOW_FIGHT when an
 * encounter starts now (the pan has played; the cartridge opens the scene and sets SCY back to 0). */
uint8_t flow_tick(uint8_t screen, uint8_t *pressed) BANKED;
/* to_bench: phase 0 before the bench is drawn, 1 after; pans the view back from a pending encounter. */
void flow_back(uint8_t phase) BANKED;
/* B on the bench with nothing held: clears the filter (1 when it did). */
uint8_t flow_unfilter(void) BANKED;
/* Cue rows: screen is crucible.c's BENCH (0), REVEAL (2) or BOOK (3). */
void flow_cues(uint8_t screen) BANKED;
/* The bench's sign row (17): NO RESULT, =RESULT, or, with nothing held, a waiting encounter or START's hint. */
void flow_sign(uint8_t message, uint16_t sign) BANKED;
#endif
