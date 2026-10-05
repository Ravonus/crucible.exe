#ifndef CRUCIBLE_LINK_H
#define CRUCIBLE_LINK_H
/* Link cable play (crucible_link.c). */
#define LINK_HOST 0u
#define LINK_GUEST 1u
#define LINK_COOP 0u
#define LINK_VERSUS 1u /* RACE: to GOAL new finds within TIME */
#define LINK_RACE LINK_VERSUS
#define LINK_FIGHT 2u /* a bout, each with their own things: lockstep turns (crucible_fight.c) */
#define LINK_EV_NONE 0u
#define LINK_EV_LINKED 1u
#define LINK_EV_UNLINKED 2u
#define LINK_EV_RULES 3u /* the guest got the host's rules */
#define LINK_EV_GO 4u /* the guest: the host started */
#define LINK_EV_GIFT 5u /* an element arrived (link_inbox) */
#define LINK_EV_SCORE 6u
#define LINK_EV_END 7u /* the link game ended (link_result) */
#define LINK_WON 1u
#define LINK_LOSTRACE 2u
#define LINK_DRAW 3u
#define LINK_LOST 4u /* the cable went quiet */
extern uint8_t link_on, link_role, link_mode, link_time, link_goal, link_linked, link_started;
extern uint16_t link_seed;
extern uint8_t link_ask; /* the host: the guest asked for other rules (P_READY 0xff) */
uint8_t link_partner_attrs(void) BANKED; /* GRIT:3 | FOCUS:2 | REACH:2 | slot2 (from P_AVATAR2, in the lobby too) */
void link_open(uint8_t role) BANKED;
void link_close(void) BANKED;
uint8_t link_poll(uint8_t dt) BANKED;
void link_go(void) BANKED;
void link_begin(void) BANKED;
void link_found(uint16_t id) BANKED;
uint8_t link_result(void) BANKED;
void link_gift(uint16_t id) BANKED;
uint16_t link_inbox(void) BANKED;
void link_hud(void) BANKED;
void link_result_draw(void) BANKED;
void link_label(uint8_t which, char *out) BANKED; /* 0 mode, 1 time, 2 goal; out[8] */
void link_end(
    uint8_t result) BANKED; /* the link game ends here with LINK_WON / LINK_LOSTRACE / LINK_DRAW / LINK_LOST */
/* packets: [A5 type a.lo a.hi b.lo b.hi sum] (crucible_link.c); 1..10 the session's, 11.. link play's (fight spec 9.6;
 * the spec's 8..10 were taken by TURN and the names, so its numbers move up) */
#define P_HELLO 1u
#define P_RULES 2u /* a: R0 (mode | time << 4) | R1 << 8; b: R2 (goal) | R3 << 8 */
#define P_GO 3u
#define P_FIND 4u
#define P_GIFT 5u
#define P_SCORE 6u
#define P_END 7u
#define P_TURN 8u /* the first link fight's input (retired) */
#define P_NAME 9u
#define P_NAME2 10u
#define P_FTURN 11u /* a: action | turn << 8; b: hash | (pip | ack << 7) << 8 */
#define P_FSYNC 12u /* a: chunk | byte << 8; b: two bytes (chunk 0xff: the guest asks) */
#define P_MADE 13u /* a: item; b: flags | serial << 8 */
#define P_MISS 14u /* a: serial | streak << 8 */
#define P_CUE 15u /* a: scene; b: seed */
#define P_READY 16u /* a: scene; b: 0 busy, 1 ready, 2 declined/ask */
#define P_BEAT 17u /* a: beat | choice << 8; b: veto | active << 8 */
#define P_CURSOR 18u /* a: beat | cursor << 8; b: nudge */
#define P_CAT 19u /* a: items; b: recipes */
#define P_DECK 20u /* a, b: id | slot << 13 */
#define P_LEAVE 21u
#define P_RULES2 22u /* a: R4 | R5 << 8; b: R6 | R7 << 8 */
#define P_AVATAR 23u /* genome 0..3 */
#define P_AVATAR2 24u /* genome 4..5; level | attributes << 8 */
#define P_PAS 25u /* a: passives (two nibbles) | (cell | got << 7) << 8; b: seed */
#define P_SAVE 26u /* a: which save the partner brought (0 free, 1..3 a story slot) | chapter << 8 */
#define P_SEED                                                                                                         \
  27u /* a: the run's seed (16 bits); b: order | heart << 8 (the alignment axes, signed): seeds grow together (9.4) */
extern uint8_t link_rules[8]; /* R0..R7 (fight spec 9.2): the host's */
uint8_t link_rule(uint8_t k) BANKED;
uint8_t link_send(uint8_t type, uint16_t a, uint16_t b) BANKED;
uint8_t link_idle(void) BANKED;
uint16_t link_quiet(void) BANKED;
/* link play (crucible_link_play.c, crucible_link_coop.c) */
void link_play_begin(void) BANKED;
void link_play_tick(uint8_t dt) BANKED;
uint8_t link_play_packet(uint8_t type, uint16_t a, uint16_t b) BANKED;
uint8_t link_coop_packet(uint8_t type, uint16_t a, uint16_t b) BANKED;
uint8_t link_fight_ready(void) BANKED;
uint16_t link_fight_seed(void) BANKED;
uint8_t link_fight_arena(void) BANKED;
uint8_t link_fight_rules(void) BANKED;
uint8_t link_fight_hp(uint8_t side) BANKED;
uint8_t link_fight_focus(uint8_t side) BANKED;
void link_kit_mine(uint16_t *kit, uint8_t *pas) BANKED;
void link_kit_theirs(uint16_t *kit, uint8_t *pas) BANKED;
void link_partner_genome(uint8_t *g) BANKED;
uint8_t link_partner_level(void) BANKED;
void link_fturn(uint8_t act, uint8_t turn, uint8_t hash, uint8_t pip) BANKED;
uint8_t link_fturn_in(uint8_t *act, uint8_t *turn, uint8_t *hash, uint8_t *pip) BANKED;
uint8_t link_partner_locked(uint8_t turn) BANKED;
void link_fsync_ask(void) BANKED;
uint8_t link_fight_over(uint8_t r) BANKED; /* a bout ended; 1: the match goes on (best of 3 or 5) */
uint8_t link_fight_pips(void) BANKED;
extern uint8_t lp_wins[2], lp_bout;
/* co-op and race (crucible_link_coop.c) */
extern uint8_t link_save; /* the save brought to the session: 0 free play, 1..3 a story slot */
void link_bring(void) BANKED; /* at GO: open the save brought (free play or the story slot) */
void link_inbox_put(uint16_t id) BANKED;
void link_coop_begin(void) BANKED;
void link_made(uint16_t id, uint8_t is_new) BANKED;
void link_missed(void) BANKED;
void link_coop_tick(uint8_t dt) BANKED;
void link_coop_race(uint16_t id, uint8_t score) BANKED;
uint8_t link_toast_take(uint16_t *id, uint8_t *fl, uint8_t *more) BANKED;
void link_coop_end(void) BANKED;
void link_coop_elsewhere(void) BANKED;
void link_seed_scene(void) BANKED; /* a shared scene: the bounded tint and the seed drift again */
void link_seed_session(uint16_t seed, int8_t order,
                       int8_t heart) BANKED; /* the partner's seed and axes arrived (the session's start) */
extern uint8_t lc_seed_got;
extern int8_t lc_their[2];
extern uint8_t lc_out_n, lc_serial, lc_in, lc_toast_n, lc_more, lc_notified, lc_kept;
/* the partner's name (CRU_NAME letters, blank-padded; "" until it has crossed), into out[CRU_NAME+1] */
void link_peer(char *out) BANKED;
#endif
