#pragma bank 255
#include <gb/gb.h>
#include "crucible_data.h"
void crucible_bust_0(uint8_t,uint8_t,uint8_t,uint8_t) BANKED;
void crucible_bust_1(uint8_t,uint8_t,uint8_t,uint8_t) BANKED;
void crucible_bust_2(uint8_t,uint8_t,uint8_t,uint8_t) BANKED;
void crucible_bust_3(uint8_t,uint8_t,uint8_t,uint8_t) BANKED;
void crucible_bust_4(uint8_t,uint8_t,uint8_t,uint8_t) BANKED;
void crucible_bust_5(uint8_t,uint8_t,uint8_t,uint8_t) BANKED;
void crucible_bust_6(uint8_t,uint8_t,uint8_t,uint8_t) BANKED;
void crucible_bust_7(uint8_t,uint8_t,uint8_t,uint8_t) BANKED;
void crucible_bust_8(uint8_t,uint8_t,uint8_t,uint8_t) BANKED;
void crucible_bust_9(uint8_t,uint8_t,uint8_t,uint8_t) BANKED;
void crucible_bust_10(uint8_t,uint8_t,uint8_t,uint8_t) BANKED;
void crucible_bust_11(uint8_t,uint8_t,uint8_t,uint8_t) BANKED;
void crucible_bust_12(uint8_t,uint8_t,uint8_t,uint8_t) BANKED;
void crucible_bust_13(uint8_t,uint8_t,uint8_t,uint8_t) BANKED;
void crucible_bust_14(uint8_t,uint8_t,uint8_t,uint8_t) BANKED;
void crucible_bust_15(uint8_t,uint8_t,uint8_t,uint8_t) BANKED;
void crucible_bust_16(uint8_t,uint8_t,uint8_t,uint8_t) BANKED;
void crucible_bust_17(uint8_t,uint8_t,uint8_t,uint8_t) BANKED;
void crucible_bust_18(uint8_t,uint8_t,uint8_t,uint8_t) BANKED;
void crucible_bust_19(uint8_t,uint8_t,uint8_t,uint8_t) BANKED;
/* Decode one third (chunk 0..2) of a view of title bust b into sprite tiles tile.. of VRAM bank (0 or 1). */
void crucible_bust(uint8_t b,uint8_t view,uint8_t chunk,uint8_t tile,uint8_t bank) BANKED{switch(b){case 0:crucible_bust_0(view,chunk,tile,bank);break;case 1:crucible_bust_1(view,chunk,tile,bank);break;case 2:crucible_bust_2(view,chunk,tile,bank);break;case 3:crucible_bust_3(view,chunk,tile,bank);break;case 4:crucible_bust_4(view,chunk,tile,bank);break;case 5:crucible_bust_5(view,chunk,tile,bank);break;case 6:crucible_bust_6(view,chunk,tile,bank);break;case 7:crucible_bust_7(view,chunk,tile,bank);break;case 8:crucible_bust_8(view,chunk,tile,bank);break;case 9:crucible_bust_9(view,chunk,tile,bank);break;case 10:crucible_bust_10(view,chunk,tile,bank);break;case 11:crucible_bust_11(view,chunk,tile,bank);break;case 12:crucible_bust_12(view,chunk,tile,bank);break;case 13:crucible_bust_13(view,chunk,tile,bank);break;case 14:crucible_bust_14(view,chunk,tile,bank);break;case 15:crucible_bust_15(view,chunk,tile,bank);break;case 16:crucible_bust_16(view,chunk,tile,bank);break;case 17:crucible_bust_17(view,chunk,tile,bank);break;case 18:crucible_bust_18(view,chunk,tile,bank);break;case 19:crucible_bust_19(view,chunk,tile,bank);break;}}
/* each bust's sprite palette (colour 0 is clear): the marble heads share one stone ramp, the nostalgia icons keep their hue */
static const uint16_t pal[]={0,18537,17045,27548,0,18537,14803,23288,0,18537,20034,25364,0,18537,18956,25334,0,18537,17708,21043,0,18537,13656,22203,0,18537,24235,30650,0,18537,18963,29597,0,18537,14697,20049,0,18537,21194,28601,0,18537,20082,29596,0,18537,663,24478,0,18537,18775,24251,0,18537,12874,22294,0,18537,13728,20109,0,18537,21136,29627,0,0x5DB0u,0x76D8u,0x7FFFu,0,0x5DB0u,0x76D8u,0x7FFFu,0,0x5DB0u,0x76D8u,0x7FFFu,0,0x5DB0u,0x76D8u,0x7FFFu};void crucible_bust_palette(uint8_t b,uint16_t*out) BANKED{uint8_t i;for(i=0;i<4u;i++)out[i]=pal[b*4u+i];}
