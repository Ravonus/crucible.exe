/* The names the screens show: feats, their descriptions, trait tags, the title matrix's rows and columns.
 * Self-contained (no catalogue, no state), so on a banked cartridge it can live in any bank. */
#include "crucible_core.h"

static const char *const feat_names[CRU_FEATS] = {
    "COLLECTOR", "RECIPES", "QUICK HAND", "FLURRY",    "HOT STREAK", "CHAIN",   "EXPLORER",  "STUBBORN",
    "MIRROR",    "GILDED",  "NATURALIST", "GEOLOGIST", "STORMCALL",  "ARTISAN", "CONDUCTOR", "VOYAGER",
    "SCHOLAR",   "DEVOTED", "SPEEDRUN",   "OPENING",   "CLEAN RUN",  "HOARDER", "SWIFT",     "LINKED"};
static const char *const feat_descs[CRU_FEATS] = {
    "DISCOVER THINGS",    "DISTINCT RECIPES",  "TWO FINDS, QUICKLY", "FINDS IN 60 SEC",   "NEW PAIRS, NO MISS",
    "BUILD ON NEWEST",    "DIFFERENT PAIRS",   "FRESH PAIRS FAILED", "SAME + SAME",       "GILDED SPECIMENS",
    "LIFE FOUND",         "MATTER FOUND",      "WEATHER FOUND",      "CRAFTS FOUND",      "ENERGY FOUND",
    "PLACES FOUND",       "READ BOOK ENTRIES", "TIME AT THE BENCH",  "FIND ALL, FAST",    "5 FINDS FROM START",
    "NEW FINDS, NO MISS", "EARN POINTS",       "NEW PAIRS A MINUTE", "LINK WITH A FRIEND"};
static const char *const trait_names[CRU_TITLE_TRAITS] = {"SWIFT",  "DOGGED", "CURIOUS", "GILDED",
                                                          "FIERCE", "SOCIAL", "WISE",    "DEVOTED"};
static const char *const domain_names[CRU_TITLE_DOMAINS] = {"ADEPT", "MASON",   "RAINMAKER", "SPARKSMITH",
                                                            "DRUID", "ARTISAN", "VOYAGER"};
static const char *const tag_names[CRU_TRAITS] = {"HOT",   "COLD",  "WET",   "AIRY", "STONE", "SHINY",
                                                  "GLOWS", "ALIVE", "GREEN", "MADE", "BIG",   "MAGIC"};

static char *copy(char *out, const char *s) {
  while ((*out = *s) != 0) {
    out++;
    s++;
  }
  return out;
}
void cru_feat_name(uint8_t f, char *out) CORE_BANKED { copy(out, f < CRU_FEATS ? feat_names[f] : ""); }
void cru_feat_desc(uint8_t f, char *out) CORE_BANKED { copy(out, f < CRU_FEATS ? feat_descs[f] : ""); }
void cru_tag_name(uint8_t k, char *out) CORE_BANKED { copy(out, k < CRU_TRAITS ? tag_names[k] : ""); }
void cru_title_part(uint8_t axis, uint8_t i, char *out) CORE_BANKED {
  if (axis)
    copy(out, i < CRU_TITLE_DOMAINS ? domain_names[i] : "");
  else
    copy(out, i < CRU_TITLE_TRAITS ? trait_names[i] : "");
}
void cru_title_name(uint8_t t, char *out) CORE_BANKED {
  uint8_t r = 0;
  if (t >= CRU_TITLES) {
    *out = 0;
    return;
  }
  while (t >= CRU_TITLE_DOMAINS) {
    t = (uint8_t)(t - CRU_TITLE_DOMAINS);
    r++;
  }
  out = copy(out, trait_names[r]);
  *out++ = ' ';
  copy(out, domain_names[t]);
}
