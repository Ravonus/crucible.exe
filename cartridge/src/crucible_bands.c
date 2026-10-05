/* The title card's two cloud bands: columns of the menu scene with drifting cloud chunks,
 * scrolled by the LCD line events. */
#pragma bank 255
#include <gb/gb.h>
#include "crucible_data.h"
#include "crucible_scene.h"
#include "crucible_state.h"
#define NONE 255u
static uint16_t rng_ = 0x6b3du;
static uint8_t rnd8(void) {
  rng_ ^= (uint16_t)(rng_ << 7);
  rng_ ^= (uint16_t)(rng_ >> 9);
  rng_ ^= (uint16_t)(rng_ << 8);
  return (uint8_t)rng_;
}
static const uint8_t band_top[2] = {4, 7}, band_speed[2] = {3, 7}, band_chunks[2][3] = {{2, 1, 4}, {0, 3, 5}};
static uint16_t band_pos[2];
static uint8_t band_col[2], band_chunk[2], band_cx[2], band_row[2], band_gap[2];
static void band_column(uint8_t b, uint8_t col) {
  uint8_t r, y, tile, attr, box[4], in;
  if (band_chunk[b] == NONE) {
    if (band_gap[b])
      band_gap[b]--;
    else if ((rnd8() & 3u) == 0u) {
      band_chunk[b] = band_chunks[b][rnd8() % 3u];
      scene_cloud_chunk(band_chunk[b], box);
      band_cx[b] = 0;
      band_row[b] = band_top[b] + (uint8_t)(rnd8() % (4u - box[3]));
    }
  }
  if (band_chunk[b] != NONE) scene_cloud_chunk(band_chunk[b], box);
  for (r = 0; r < 3u; r++) {
    y = band_top[b] + r;
    in = band_chunk[b] != NONE && y >= band_row[b] && y < band_row[b] + box[3];
    if (in)
      scene_cell(SCENE_MENU, box[0] + band_cx[b], box[1] + (y - band_row[b]), &tile, &attr);
    else
      scene_cell(SCENE_MENU, col, y, &tile, &attr);
    VBK_REG = 1;
    set_bkg_tiles(col, y, 1, 1, &attr);
    VBK_REG = 0;
    set_bkg_tiles(col, y, 1, 1, &tile);
  }
  if (band_chunk[b] != NONE && ++band_cx[b] >= box[2]) {
    band_chunk[b] = NONE;
    band_gap[b] = 1u + (rnd8() & 3u);
  }
}
void bands_setup(void) BANKED {
  rng_ ^= DIV_REG;
  uint8_t b, c;
  for (b = 0; b < 2u; b++) {
    band_pos[b] = 0;
    band_col[b] = 0;
    band_chunk[b] = NONE;
    band_gap[b] = 0;
    for (c = 0; c < 32u; c++) band_column(b, c);
  }
}
void bands_tick(uint8_t dt) BANKED {
  uint8_t b, px;
  for (b = 0; b < 2u; b++) {
    band_pos[b] += (uint16_t)band_speed[b] * dt;
    px = (uint8_t)(band_pos[b] >> 4);
    while (band_col[b] != (px >> 3)) {
      band_col[b] = (band_col[b] + 1u) & 31u;
      band_column(b, (band_col[b] + 21u) & 31u);
    }
    lcd_value[b] = px;
  }
}
