/* The link cable's bytes, moved by interrupts (bank 0: they run with any ROM bank mapped). One byte crosses each way
 * every frame, however slow the game's loop is (a fight's loop can take six frames; a byte per loop
 * would make a versus setup take seconds and starve the line): the VBlank handler clocks the HOST's next byte (internal
 * clock), the serial interrupt takes the byte that came back into a ring and, on the GUEST, arms the next one (external
 * clock). crucible_link.c queues the packets and reads the ring; no packet logic lives here.
 *   - a host that reads FF heard a guest that was not armed: the same byte goes again next frame
 *   - a host transfer that never completes (no interrupt within 4 frames) is clocked again
 *   - a guest whose port went idle is armed again at VBlank */
#pragma bank 255
#include <gb/gb.h>
#include "crucible_link_io.h"
uint8_t lk_tx[LK_TXQ], lk_th, lk_tt, lk_rx[LK_RXQ], lk_rh, lk_rt, lk_on, lk_host, lk_cur, lk_busy, lk_wd, lk_rxlost;
void link_sio_isr(void) NONBANKED {
  uint8_t b, n;
  if (!lk_on) return;
  b = SB_REG;
  if (b == 0xFFu && lk_host) {
    lk_busy = 0;
    return;
  } /* not listening: the same byte again */
  if (b != 0xFFu) {
    n = (uint8_t)((lk_rh + 1u) & (LK_RXQ - 1u));
    if (n != lk_rt) {
      lk_rx[lk_rh] = b;
      lk_rh = n;
    } else
      lk_rxlost++;
  }
  if (lk_tt != lk_th) {
    lk_cur = lk_tx[lk_tt];
    lk_tt = (uint8_t)((lk_tt + 1u) & (LK_TXQ - 1u));
  } else
    lk_cur = 0;
  SB_REG = lk_cur;
  if (lk_host)
    lk_busy = 0;
  else
    SC_REG = 0x80u;
}
void link_vbl(void) NONBANKED {
  if (!lk_on) return;
  if (lk_host) {
    if (lk_busy && ++lk_wd < 4u) return;
    lk_wd = 0;
    lk_busy = 1;
    SB_REG = lk_cur;
    SC_REG = 0x81u;
  } else if (!(SC_REG & 0x80u)) {
    SB_REG = lk_cur;
    SC_REG = 0x80u;
  }
}
/* open (role: 1 host) and close; the interrupt is enabled only while a cable session is open */
static uint8_t installed_;
void link_io_open(uint8_t host) BANKED {
  CRITICAL {
    lk_on = 0;
    lk_th = lk_tt = lk_rh = lk_rt = 0;
    lk_cur = 0;
    lk_busy = 0;
    lk_wd = 0;
    lk_rxlost = 0;
    lk_host = host;
    if (!installed_) {
      add_SIO(link_sio_isr);
      installed_ = 1;
    }
    SB_REG = 0;
    SC_REG = host ? 0u : 0x80u;
    lk_on = 1;
  }
  set_interrupts(IE_REG | SIO_IFLAG);
}
void link_io_close(void) BANKED {
  lk_on = 0;
  SC_REG = 0;
  set_interrupts(IE_REG & (uint8_t)~SIO_IFLAG);
}
