#ifndef CRUCIBLE_STANDALONE_INTERRUPTS_H
#define CRUCIBLE_STANDALONE_INTERRUPTS_H
#include <gb/gb.h>
extern uint8_t hide_sprites;
/* This cartridge owns its LCD handler: there is no shared ISR list to clear. */
#define remove_LCD_ISRs() ((void)0)
#endif
