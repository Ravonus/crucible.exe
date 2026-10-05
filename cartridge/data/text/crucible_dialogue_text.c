/* What speakers answer and how they remember: the reply table keyed by reply
 * class x speaker x attitude (plus the dream's rows), and the contextual
 * greeting hints. Own bank: crucible_dialogue.c keeps the selector and the
 * option labels, so neither module nears a 16 KiB bank. Slots are filled from
 * the conversation's frozen slots (name, item, category, trait, sector, player). */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_data.h"
#include "crucible_state.h"
#define KDC_REPLY_ONLY
#include "crucible_dialogue.h"
#define DIALOGUE_TEXT_ONLY
#define DIALOGUE_WITH_TEXT
#include "crucible_dialogue_data.h"
#define LINE_MAX 95u
static void fill(uint16_t at,const char*const*slots,char*out){
 const uint8_t*p=dialogue_text+at;uint8_t n=0,c;const char*v;
 while((c=*p++)!=0u&&n<LINE_MAX){
  if(c>=16u&&c<22u){v=slots?slots[c-16u]:0;if(v)while(*v&&n<LINE_MAX)out[n++]=*v++;}
  else out[n++]=(char)c;
 }
 out[n]=0;
}
uint8_t dialogue_reply_text(uint8_t cls,uint8_t who,uint8_t att,uint8_t deep,uint8_t avoid,uint16_t*r,const char*const*slots,char*out) BANKED{
 uint16_t i=kdc_reply_pick(dialogue_replies,DIALOGUE_REPLY_COUNT,cls,who,att,deep,avoid,r);
 if(i==0xffffu){out[0]=0;return 0;}
 fill(dialogue_replies[i].at,slots,out);return dialogue_replies[i].tag;
}
uint8_t dialogue_hint_text(uint8_t after,uint8_t same,uint16_t r,const char*const*slots,char*out) BANKED{
 uint8_t i,n=0,k;
 for(i=0;i<DIALOGUE_HINT_COUNT;i++)if(dialogue_hints[i].after==after&&(same||dialogue_hints[i].scope))n++;
 if(!n)return 0;
 k=(uint8_t)(r%n);
 for(i=0;i<DIALOGUE_HINT_COUNT;i++)if(dialogue_hints[i].after==after&&(same||dialogue_hints[i].scope)){if(!k){fill(dialogue_hints[i].at,slots,out);return 1;}k--;}
 return 0;
}
