/* The option labels (what the player can say), in their own bank beside the
 * selector's compact metadata in crucible_dialogue.c. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "crucible_data.h"
#include "crucible_state.h"
#include "crucible_dialogue.h"
#define DIALOGUE_TEXT_ONLY
#define DIALOGUE_WITH_LABELS
#include "crucible_dialogue_data.h"
void dialogue_label_text(uint8_t variant,char*out) BANKED{if(variant<DIALOGUE_VARIANT_COUNT)strcpy(out,dialogue_labels[variant]);else out[0]=0;}
