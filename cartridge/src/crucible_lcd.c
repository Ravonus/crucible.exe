/* Scanline effects: a short list of events, each fired by LYC at a line and applied in that line's HBlank:
 * set the horizontal scroll (parallax sky bands), turn the window off (a toast card that ends mid-screen) and
 * turn sprites off or on (no cloud drifts over a toast). The list is the only LCD handler; main.c's VBlank
 * handler positions and shows the window each frame. */
#pragma bank 255
#include <gb/gb.h>
#include "interrupts.h"
#include "crucible_state.h"
#include "crucible_link_io.h"
uint8_t lcd_line[LCD_EVENTS], lcd_kind[LCD_EVENTS], lcd_value[LCD_EVENTS];
static uint8_t lcd_n, lcd_i;
/* Bank 0: runs from the interrupt with any ROM bank mapped. */
void crucible_lcd_isr(void) NONBANKED {
  uint8_t i = lcd_i, k = lcd_kind[i];
  if (lcd_line[i] < 144u)
    while (STAT_REG & STATF_BUSY);
  if (k & LCD_SCX) SCX_REG = lcd_value[i];
  if (k & LCD_WIN_OFF) LCDC_REG &= ~LCDCF_WINON;
  if (k & LCD_OBJ_OFF) LCDC_REG &= ~LCDCF_OBJON;
  if (k & LCD_OBJ_ON) LCDC_REG |= LCDCF_OBJON;
  if (++i >= lcd_n) i = 0;
  lcd_i = i;
  LYC_REG = lcd_line[i];
}
/* Replace the event list (sorted by line, ending with a VBlank line >= 144). */
void lcd_events(const uint8_t *lines, const uint8_t *kinds, uint8_t n) BANKED {
  uint8_t i;
  CRITICAL {
    for (i = 0; i < n && i < LCD_EVENTS; i++) {
      lcd_line[i] = lines[i];
      lcd_kind[i] = kinds[i];
      lcd_value[i] = 0;
    }
    lcd_n = n;
    lcd_i = 0;
    LYC_REG = lcd_line[0];
    STAT_REG |= STATF_LYC;
  }
}
/* Button presses latched every VBlank (new presses since the last VBlank), taken by the game loop. */
static uint8_t input_latch, input_last;
/* also re-arms the LYC interrupt every frame: nothing else may switch it off for good (a toast's window cut
 * depends on it; with it off the window would cover the whole screen) */
void crucible_vbl_input(void) NONBANKED {
  uint8_t j = joypad();
  STAT_REG |= STATF_LYC;
  input_latch |= j & (uint8_t)~input_last;
  input_last = j;
  link_vbl();
} /* and the link cable's byte for this frame */
uint8_t input_take(void) BANKED {
  uint8_t p;
  CRITICAL {
    p = input_latch;
    input_latch = 0;
  }
  return p;
}
void lcd_install(void) BANKED {
  static const uint8_t line[1] = {150}, kind[1] = {LCD_SCX};
  CRITICAL {
    remove_LCD_ISRs();
    add_LCD(crucible_lcd_isr);
    add_VBL(crucible_vbl_input);
  }
  lcd_events(line, kind, 1);
  SCX_REG = 0;
}
