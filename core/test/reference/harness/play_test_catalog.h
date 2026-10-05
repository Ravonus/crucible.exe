/* Catalogue accessors for the tests (the game reads its own from the baked catalogue). */
#ifndef PLAY_TEST_CATALOG_H
#define PLAY_TEST_CATALOG_H
#include <stdint.h>
extern uint16_t play_cat_items, play_cat_recipes;
uint8_t crucible_depth(uint16_t id);
uint16_t crucible_route(uint16_t id, uint8_t k);
uint16_t crucible_recipe_at(uint16_t ri, uint16_t *ab);
uint16_t crucible_recipe_find(uint16_t a, uint16_t b);
#define PLAY_ITEMS play_cat_items
#define PLAY_RECIPES play_cat_recipes
#endif
