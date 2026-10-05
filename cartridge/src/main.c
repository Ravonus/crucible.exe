#include <gb/gb.h>
#include <gb/cgb.h>
uint8_t hide_sprites;
uint8_t win_pos_x, win_pos_y = 144u;
static void window_vblank(void) NONBANKED {
  WX_REG = win_pos_x + 7u;
  WY_REG = win_pos_y;
  if (win_pos_y < 144u)
    LCDC_REG |= LCDCF_WINON;
  else
    LCDC_REG &= ~LCDCF_WINON;
}
void crucible_run(void) BANKED;
void main(void) {
  if (_cpu != CGB_TYPE) {
    DISPLAY_ON;
    return;
  }
  cpu_fast();
  add_VBL(window_vblank);
  set_interrupts(VBL_IFLAG | LCD_IFLAG);
  crucible_run();
}
