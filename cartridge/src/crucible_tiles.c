/* Emit the frame just decoded into crucible_decoded: background or sprite tiles in either VRAM bank, a
 * revealed silhouette (light first, materials through an ordered mask), or a capture for the fusion. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include "crucible_data.h"
#include "crucible_codec.h"
#include "crucible_fusion.h"
static const uint8_t screen4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
/* Resolve the real current pose, preserving its gaps: pixels whose ordered threshold is not yet reached
 * show as the light silhouette, so the object resolves into its materials through a stable mask. */
static void form(uint8_t threshold) {
  uint8_t t, y, x, lo, hi, bit, px, py;
  uint8_t *data = crucible_decoded;
  for (t = 0; t < 16u; t++)
    for (y = 0; y < 8u; y++) {
      lo = data[t * 16u + y * 2u];
      hi = data[t * 16u + y * 2u + 1u];
      for (x = 0; x < 8u; x++) {
        bit = 0x80u >> x;
        if ((lo | hi) & bit) {
          px = (t & 3u) * 8u + x;
          py = (t >> 2) * 8u + y;
          if (screen4[(py & 3u) * 4u + (px & 3u)] >= threshold) {
            lo |= bit;
            hi |= bit;
          }
        }
      }
      data[t * 16u + y * 2u] = lo;
      data[t * 16u + y * 2u + 1u] = hi;
    }
}
void crucible_emit(uint8_t tile, uint8_t mode) BANKED {
  if (mode & CRUCIBLE_ART_CAPTURE) {
    crucible_capture_tiles(crucible_decoded, tile);
    return;
  }
  if (mode & CRUCIBLE_ART_FORM) form((mode & CRUCIBLE_ART_FORM) - 1u);
  if (mode & CRUCIBLE_ART_BANK1) VBK_REG = 1;
  if (mode & CRUCIBLE_ART_SPRITE)
    set_sprite_data(tile, 16, crucible_decoded);
  else
    set_bkg_data(tile, 16, crucible_decoded);
  VBK_REG = 0;
}
