#ifndef CRUCIBLE_LINK_IO_H
#define CRUCIBLE_LINK_IO_H
/* The link cable's bytes, interrupt driven (crucible_link_io.c). Rings: the game writes lk_tx at lk_th, the serial
 * interrupt reads at lk_tt; the interrupt writes lk_rx at lk_rh, the game reads at lk_rt. */
#define LK_TXQ 32u /* (64 before: a packet goes in whole or not at all; the periodic ones wait for an idle line) */
#define LK_RXQ                                                                                                         \
  64u /* a loop reads it within 64 frames (a reveal's slow frames overflowed 32); a byte lost to a longer one is a bad packet, sent again */
extern uint8_t lk_tx[LK_TXQ], lk_th, lk_tt, lk_rx[LK_RXQ], lk_rh, lk_rt, lk_on, lk_host, lk_cur, lk_busy, lk_wd,
    lk_rxlost;
void link_sio_isr(void) NONBANKED;
void link_vbl(void) NONBANKED; /* from the VBlank handler: the host's next byte */
void link_io_open(uint8_t host) BANKED;
void link_io_close(void) BANKED;
#endif
