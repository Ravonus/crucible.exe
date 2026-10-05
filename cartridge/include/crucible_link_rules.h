#ifndef CRUCIBLE_LINK_RULES_H
#define CRUCIBLE_LINK_RULES_H
/* The rules of a link FIGHT (crucible_link_rules.c, fight spec 9.2) */
extern uint8_t lr_at, lr_top;
void lr_preset(uint8_t p) BANKED;
uint8_t lr_suggest(uint8_t *who) BANKED;
uint8_t lr_edge_on(void) BANKED;
void lr_edge(uint8_t on) BANKED;
void lr_lobby_row(uint8_t row, char *label, char *value) BANKED;
uint8_t lr_lobby_change(uint8_t row, uint8_t pressed) BANKED;
void lr_page_draw(uint8_t row0) BANKED;
uint8_t lr_page(uint8_t pressed, uint8_t row0) BANKED;
void lr_load(void) BANKED;
void lr_save(void) BANKED;
#endif
