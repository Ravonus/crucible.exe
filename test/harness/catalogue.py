"""The catalogue the ROM carries (catalogue/items.json and recipes.json), with ids resolved."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DEFAULT_DIR = REPO / "catalogue"

# Shelf order of the categories, as the cartridge sorts them.
CATEGORIES = ("element", "matter", "weather", "energy", "life", "craft", "place")


@dataclass(frozen=True)
class Item:
    id: int
    concept: str
    category: str


@dataclass(frozen=True)
class Catalogue:
    items: list[Item]
    recipes: list[tuple[int, int, int]]  # (ingredient, ingredient, result)

    @property
    def ids(self) -> dict[str, int]:
        return {item.concept: item.id for item in self.items}

    def category_index(self, item_id: int) -> int:
        return CATEGORIES.index(self.items[item_id].category)


def load(directory: Path = DEFAULT_DIR) -> Catalogue:
    raw_items = json.loads((directory / "items.json").read_text())
    items = [Item(id=i["id"], concept=i["c"], category=i["cat"]) for i in raw_items]
    ids = {item.concept: item.id for item in items}
    raw_recipes = json.loads((directory / "recipes.json").read_text())
    recipes = [(ids[a], ids[b], ids[r]) for a, b, r in raw_recipes]
    return Catalogue(items=items, recipes=recipes)
