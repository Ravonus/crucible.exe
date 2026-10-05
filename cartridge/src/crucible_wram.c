#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "crucible_codec.h"
/* CGB work RAM banks 2..7 as a frame cache. GBDK keeps the stack in D000-DFFF, so the bank is switched only with
 * interrupts off and only around eight-byte register-only bursts (no stack use while switched, and an LCD
 * interrupt waits at most a third of a scanline). Copies 256 bytes between wram_at (D000-DFFF of wram_bank) and
 * wram_buf, which must lie in C000-CFFF; wram_dir 0 = bank to buffer, 1 = buffer to bank. */
uint8_t wram_bank, wram_dir;
uint8_t *wram_at;
uint8_t *wram_buf;
#ifdef __SDCC
// clang-format off
void crucible_wram_copy(void) BANKED __naked { __asm
	ld	hl, #_wram_at
	ld	a, (hl+)
	ld	e, a
	ld	d, (hl)
	ld	hl, #_wram_buf
	ld	a, (hl+)
	ld	h, (hl)
	ld	l, a
	ld	a, (#_wram_dir)
	or	a, a
	jr	z, 00302$
	push	hl
	ld	h, d
	ld	l, e
	pop	de
00302$:
	ld	a, (#_wram_bank)
	ld	b, a
	ld	c, #32
00301$:
	di
	ld	a, b
	ld	(#0xFF70), a
	ld	a, (de)
	ld	(hl+), a
	inc	de
	ld	a, (de)
	ld	(hl+), a
	inc	de
	ld	a, (de)
	ld	(hl+), a
	inc	de
	ld	a, (de)
	ld	(hl+), a
	inc	de
	ld	a, (de)
	ld	(hl+), a
	inc	de
	ld	a, (de)
	ld	(hl+), a
	inc	de
	ld	a, (de)
	ld	(hl+), a
	inc	de
	ld	a, (de)
	ld	(hl+), a
	inc	de
	ld	a, #1
	ld	(#0xFF70), a
	ei
	dec	c
	jr	nz, 00301$
	ret
__endasm; }
// clang-format on
#else
#include <stdint.h>
/* Host builds: six 4 KB arrays stand in for CGB WRAM banks 2..7 (wram_at only carries the offset 0xD000+n). */
static uint8_t wram_banks[6][4096];
void crucible_wram_copy(void) {
  uint8_t *b = wram_banks[(wram_bank - 2u) % 6u] + (((uintptr_t)wram_at - 0xD000u) & 0x0f00u);
  if (wram_dir)
    memcpy(b, wram_buf, 256);
  else
    memcpy(wram_buf, b, 256);
}
#endif
