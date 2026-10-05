#ifndef CRUCIBLE_OVERLAY_H
#define CRUCIBLE_OVERLAY_H
#include <gb/gb.h>
/* Material sprite overlay (crucible_overlay.c). cell: crucible.c cell index; frame: the cell's shown frame byte. */
void crucible_overlay_cell(uint8_t cell, uint16_t id, uint8_t frame, uint8_t x, uint8_t y) BANKED;
/* After the cells' VBlank bank swap: show prepared overlays, hide stale ones. */
void crucible_overlay_sync(void) BANKED;
/* Once per main-loop frame with crucible.c's screen: runs on the bench and the book; uploads, decodes. */
void crucible_overlay_tick(uint8_t screen, uint8_t load) BANKED; /* load: the focus loop's progress, 255 when ready */
/* The merge's result, ahead of its reveal: one decode step of its turntable overlay per call; 255 once the key and every
 * view are cached (or it has no overlay). The reveal then shows its overlay turning from the first frame. */
uint8_t crucible_overlay_ahead(uint16_t id) BANKED;
/* 1 while overlays own OAM 0..23: the bench keeps only its last sprite cloud (OAM 24..31). */
extern uint8_t crucible_overlay_cloud;
#endif
