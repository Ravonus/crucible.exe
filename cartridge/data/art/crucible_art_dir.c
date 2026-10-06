#pragma bank 255
#include <gb/gb.h>
#include "crucible_data.h"
#include "crucible_codec.h"
#define DIR_FIRST 510u
uint16_t crucible_art_locate(uint16_t id,const uint8_t**at) BANKED{uint8_t d[4];if(id>=CRUCIBLE_ITEMS)id=0;crucible_rom_copy(DIR_FIRST+(id>>12),(const uint8_t*)(0x4000u+((id&4095u)<<2)),d,4);*at=(const uint8_t*)(0x4000u+((uint16_t)d[3]<<8)+d[2]);return ((uint16_t)d[1]<<8)|d[0];}
uint8_t crucible_frame_ticks(uint16_t id) BANKED{const uint8_t*at;uint8_t n;uint16_t bank=crucible_art_locate(id,&at);crucible_rom_copy(bank,at+2,&n,1);return n;}

/* crucible-palette-table@1: 16 bytes per id (tints 0..3 x colours 2..3) in fixed banks, 1024 ids per bank. */
static const uint16_t palette_banks[6]={59,31,30,29,28,27};
void crucible_get_palette(uint16_t id,uint8_t v,uint16_t*out) BANKED{if(id>=CRUCIBLE_ITEMS)id=0;out[0]=32510u;out[1]=5220u;
 crucible_rom_copy(palette_banks[id>>10],(const uint8_t*)(0x4000u+((id&1023u)<<4)+((uint16_t)(v&3u)<<2)),(uint8_t*)(out+2),4);}

