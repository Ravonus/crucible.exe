"""THE CRUCIBLE on screen: a story run, a few makes, then a duel, a fled duel, a boss and a gauntlet, each played with
real input, with a screenshot at each state.

From a blank save, headless. Fights are not a menu item: the encounter director (crucible_flow.c) brings them in play.
To have one now, this sets the director's telegraph in RAM (flow_force = a fight, flow_arg = what comes); the game then
announces the encounter and it comes as in play. Each fight is checked: it comes, its entrance names it, its bag shows
on the carousel, it plays to an end screen and back to the bench, and the bench after it is the bench before it (map,
attributes and font). Writes crux.json (a log of every capture) and exits 1 when any check fails.

    python test/harness/capture_crux.py ROM OUTDIR [--objects OBJDIR] [--boss F] [--turns N] [--seed S]
"""

from __future__ import annotations

import json
import random
import sys
from collections.abc import Callable, Iterable
from pathlib import Path
from typing import Any

from session import Screen, Session, argument_parser, rom_symbols

ADDRESS_MASK = 0xFFFF  # symbol values carry the bank in their upper bits
SCY = 0xFF42
LCDC = 0xFF40
LCDC_BG_MAP_HIGH = 0x08  # the background uses the map at 0x9C00 instead of 0x9800
BG_MAP_LOW = 0x9800
BG_MAP_HIGH = 0x9C00
BG_MAP_COLUMNS = 32
SCREEN_COLUMNS = 20
TILE_ID_BANK, ATTRIBUTE_BANK = 0, 1
ATTR_VRAM_BANK_BIT = 0x08  # which VRAM bank a tile's pixels come from: the font may sit in either
FONT_FIRST_TILE = 128  # the font's tiles 128..191 are ASCII 32..95
FONT_END_TILE = 192
FONT_FIRST_CHAR = 32
FONT_TILES = range(0x8800, 0x8C00)  # the font's 64 tiles

# Offsets into the `core` struct.
CORE_FOCUS = 0x1FC  # u16: the item under the bench cursor
CORE_SLOT_A = 0x1FE  # u16: the first ingredient picked
NO_ITEM = 0xFFFF

# THE CRUCIBLE's state `cx` (crucible_crux.h; SDCC packs structs): two 26-byte sides, then the pot (u16) at 52.
CX_POT = 52
EMPTY_POT = 0xFFFF
# The player record `pl` (player_rec in crucible_player.h): its flags byte. Until PF_FIRST_DUEL is set, any champion
# the director sends comes as the first contact duel instead.
PL_FLAGS = 78
PF_FIRST_DUEL = 0x01

FLOW_FIGHT = 2  # flow_force: a fight comes
# flow_arg for a fight: a faction (1 the daemons, 2 the ghosts), | FIGHT_DUEL for a duel with one of its figures
# instead of its champion.
FIGHT_DUEL = 8
DUEL = 1 | FIGHT_DUEL
FLEE_DUEL = 2 | FIGHT_DUEL
GAUNTLET = 2 | FIGHT_DUEL


class Ui:
    """Values of `fight_ui`."""

    INTRO = 0
    CHOOSE = 1
    WAIT = 2
    SHOW = 3
    END = 4
    LEVEL = 6  # a level gained, dismissed with A
    CHOICE = 9


FIGURE_WORDS = ("DAEMON", "PROGRAM", "GHOST", "AI", "OPERATOR", "RELIC")  # one names whoever enters
STORY_MAKES = ((0, 2), (0, 1), (3, 1), (3, 2), (1, 2), (1, 1), (0, 0), (3, 3), (2, 2), (0, 3))
# The six object cells on the bench (column, row of the top-left tile; 4x4 tiles each): they animate, so the bench
# compare leaves them out.
CELL_ORIGINS = ((1, 4), (8, 4), (15, 4), (2, 10), (8, 10), (14, 10))
CELL_TILES = 4
CELL = {
    (x, y) for left, top in CELL_ORIGINS for x in range(left, left + CELL_TILES) for y in range(top, top + CELL_TILES)
}
BENCH_ROWS = range(1, 16)  # row 0 is the story HUD's ticker, which moves on its own
LOGGED_ROWS = (0, 1, 2, 8, 15, 16, 17)
ENTRANCE_ROWS = (1, 15)
BAG_ROWS = (8, 16)
END_ROWS = (9, 14, 15, 16, 17)
CHOOSE_SHOTS = 6  # captures of the first turns' choices
SHOW_SHOTS = 12  # captures of the first moves' outcomes, with the choices
SHOWN_MOVES = 8


class Cartridge(Session):
    """The ROM headless from a blank save, with the moves a player makes in story mode."""

    def __init__(self, rom: Path, screen_address: int, symbols: dict[str, int]) -> None:
        super().__init__(rom, screen_address)
        self.symbols = symbols

    def sym(self, name: str) -> int:
        return self.symbols[name] & ADDRESS_MASK

    def r8(self, name: str) -> int:
        return self.peek(self.sym(name))

    def w8(self, name: str, value: int) -> None:
        self.poke(self.sym(name), value & 0xFF)

    def tap(self, button: str, after: int = 6) -> None:
        """Held 3 frames, then `after` frames released."""
        self.pyboy.button_press(button)
        self.step(3)
        self.pyboy.button_release(button)
        self.step(after)

    def until_true(self, condition: Callable[[], bool], limit: int = 3000) -> bool:
        """Step until `condition` holds, at most `limit` frames; whether it holds at the end."""
        for _ in range(limit):
            if condition():
                return True
            self.step(1)
        return condition()

    def row(self, y: int) -> str:
        """Background row y as text, read from VRAM bank 0: PyBoy's tilemap view follows VBK, so a decode that spans
        the frame's end would read the attribute bank instead."""
        base = (BG_MAP_HIGH if self.peek(LCDC) & LCDC_BG_MAP_HIGH else BG_MAP_LOW) + y * BG_MAP_COLUMNS
        tiles = [self.memory[TILE_ID_BANK, base + x] for x in range(SCREEN_COLUMNS)]
        return "".join(
            chr(t - FONT_FIRST_TILE + FONT_FIRST_CHAR) if FONT_FIRST_TILE <= t < FONT_END_TILE else "~" for t in tiles
        )

    def rows(self, ys: Iterable[int]) -> list[str]:
        return [self.row(y) for y in ys]

    def shot(self, path: Path) -> None:
        self.frame().save(path)

    def at_rest(self) -> bool:
        return self.screen == Screen.BENCH and self.peek(SCY) == 0

    # ---- getting there ----

    def boot(self) -> None:
        """To the title menu; the title card asks nothing."""
        self.until_true(lambda: self.screen == Screen.MENU, 6000)
        self.step(240)
        self.step(120)

    def story(self) -> bool:
        """PLAY > STORY > the first slot; through naming, the creator and the intro (A, every fifth press START) to
        the bench."""
        for _ in range(3):
            self.tap("a")
            self.step(60)
        for k in range(400):
            if self.screen == Screen.BENCH:
                return True
            self.tap("start" if k % 5 == 4 else "a", 40)
        return False

    # ---- the bench ----

    def focus(self) -> int:
        return self.peek16(self.sym("_core") + CORE_FOCUS)

    def slot_a(self) -> int:
        return self.peek16(self.sym("_core") + CORE_SLOT_A)

    def settle(self, limit: int = 400) -> bool:
        """Back to the bench through reveals and the machine's lines (A); False if a fight came."""
        for _ in range(limit):
            screen = self.screen
            if screen == Screen.BENCH and self.peek(SCY) == 0:
                return True
            if screen in (Screen.REVEAL, Screen.TALK):
                self.tap("a", 20)
            elif screen == Screen.FIGHT:
                return False
            else:
                self.step(10)
        return self.screen == Screen.BENCH

    def right(self) -> int:
        was = self.focus()
        self.tap("right", 2)
        self.until_true(lambda: self.focus() != was, 120)
        return self.focus()

    def goto(self, target: int, limit: int = 200) -> bool:
        for _ in range(limit):
            if self.focus() == target:
                return True
            self.right()
        return self.focus() == target

    def mix(self, a: int, b: int) -> bool:
        """Mix a with b on the bench; back on the bench (or in whatever scene came) after."""
        self.settle()
        if self.slot_a() != NO_ITEM:
            self.tap("b", 20)
        if not self.goto(a):
            return False
        self.tap("a", 20)
        if a != b and not self.goto(b):
            self.tap("b", 20)
            return False
        self.tap("a")
        self.until_true(lambda: self.screen in (Screen.MERGE, Screen.REVEAL), 300)
        after = (Screen.REVEAL, Screen.BENCH, Screen.TALK, Screen.FIGHT, Screen.LINKEND)
        self.until_true(lambda: self.screen in after, 4000)
        self.step(60)
        self.settle()
        return True

    def bench_state(self) -> tuple[list[tuple[int, int]], list[int]]:
        """The bench as drawn: every map tile and attribute outside the six cells (the attributes' VRAM bank bit
        aside), and the font's 64 tiles in both VRAM banks."""
        bg = [
            (
                self.memory[TILE_ID_BANK, BG_MAP_LOW + y * BG_MAP_COLUMNS + x],
                self.memory[ATTRIBUTE_BANK, BG_MAP_LOW + y * BG_MAP_COLUMNS + x] & ~ATTR_VRAM_BANK_BIT,
            )
            for y in BENCH_ROWS
            for x in range(SCREEN_COLUMNS)
            if (x, y) not in CELL
        ]
        font = [self.memory[bank, a] for bank in (TILE_ID_BANK, ATTRIBUTE_BANK) for a in FONT_TILES]
        return bg, font


class Crux:
    def __init__(self, game: Cartridge, out: Path, rng: random.Random, max_turns: int) -> None:
        self.g = game
        self.out = out
        self.rng = rng
        self.max_turns = max_turns
        self.log: list[dict[str, Any]] = []
        self.checks: list[bool] = []

    def check(self, name: str, ok: object, note: str = "") -> None:
        self.checks.append(bool(ok))
        print(("PASS " if ok else "FAIL ") + name + (" - " + note if note != "" else ""), flush=True)

    def shot(self, name: str) -> None:
        g = self.g
        g.shot(self.out / f"{name}.png")
        self.log.append({"shot": name, "ui": g.r8("_fight_ui"), "rows": g.rows(LOGGED_ROWS)})

    def settle_bench(self) -> None:
        """Through reveals, the machine's lines and any talk that came after the fight until the bench stays: a talk
        can still come a moment after the bench is back."""
        g = self.g
        for _ in range(4):
            for _ in range(60):
                if g.at_rest():
                    break
                if g.screen in (Screen.REVEAL, Screen.TALK):
                    g.tap("a", 30)
                else:
                    g.step(20)
            g.step(180)
            if g.at_rest():
                break

    def pot(self) -> int:
        g = self.g
        at = g.sym("_cx") + CX_POT
        return g.peek(at) | g.peek(at + 1) << 8

    def fight(self, tag: str, arg: int, *, flee: bool = False) -> None:
        """Bring a fight with flow_arg `arg`, play it with seeded input to its end (or flee it: B held on an empty
        pot) and back to the bench."""
        g = self.g
        self.settle_bench()
        before = g.bench_state()
        g.shot(self.out / f"{tag}-bench-before.png")
        g.w8("_flow_arg", arg)
        g.w8("_flow_force", FLOW_FIGHT)
        came = g.until_true(lambda: g.screen == Screen.FIGHT, 900)
        self.check(f"{tag}: it comes", came)
        g.step(30)
        self.shot(f"{tag}-00-intro")
        entrance = " ".join(g.rows(ENTRANCE_ROWS))
        self.check(
            f"{tag}: the entrance names it",
            any(word in entrance for word in FIGURE_WORDS),
            " / ".join(g.rows(ENTRANCE_ROWS)),
        )
        g.until_true(lambda: "THEIR BAG" in " ".join(g.rows(BAG_ROWS)) or g.r8("_fight_ui") != Ui.INTRO, 900)
        g.step(20)
        self.shot(f"{tag}-00-intro2")
        self.check(
            f"{tag}: then its bag shows on the carousel",
            g.r8("_fight_look") == 1 and "THEIR BAG" in " ".join(g.rows(BAG_ROWS)),
            " / ".join(g.rows(BAG_ROWS)),
        )
        g.until_true(lambda: g.r8("_fight_ui") in (Ui.CHOOSE, Ui.WAIT), 900)
        g.step(10)
        self.shot(f"{tag}-01-first")
        if flee:
            g.until_true(lambda: g.r8("_fight_ui") == Ui.CHOOSE and self.pot() == EMPTY_POT, 3000)
            g.pyboy.button_press("b")
            g.step(90)
            g.pyboy.button_release("b")
            g.step(20)
        moves, ends = self.play(tag)
        self.check(
            f"{tag}: an end screen (MADE IN THE FIGHT or NOTHING NEW MADE, the buttons)",
            ends and ("MADE IN THE FIGHT" in ends[0] or "NOTHING NEW MADE" in ends[0]),
            ends[0] if ends else "no end screen",
        )
        self.settle_bench()
        g.shot(self.out / f"{tag}-bench-after.png")
        after = g.bench_state()
        self.check(
            f"{tag}: played to the end and back to the bench",
            g.screen == Screen.BENCH,
            f"{moves} moves, screen {g.screen}",
        )
        differ = [i for i, (x, y) in enumerate(zip(before[0], after[0], strict=False)) if x != y]
        same_font = before[1] == after[1]
        self.check(
            f"{tag}: the bench after the fight is the bench before it (map, attributes, font)",
            not differ and same_font,
            f"{len(differ)} map cells differ; font {'same' if same_font else 'DIFFERS'}",
        )

    def play(self, tag: str) -> tuple[int, list[str]]:
        """Play on with seeded input until the fight is over; the moves made and each end screen's rows."""
        g, rng = self.g, self.rng
        moves = shots = 0
        ends: list[str] = []
        for _ in range(self.max_turns * 8):
            ui = g.r8("_fight_ui")
            if g.screen != Screen.FIGHT:
                break
            if ui == Ui.CHOOSE:
                for _ in range(rng.randrange(4)):
                    g.tap("right", 8)
                if shots < CHOOSE_SHOTS:
                    self.shot(f"{tag}-{10 + shots:02d}-choose")
                    shots += 1
                g.tap(rng.choice("aaab"), 6)
                if g.r8("_fight_ui") == Ui.CHOOSE:
                    g.tap("a", 6)
                moves += 1
            elif ui == Ui.SHOW:
                if shots < SHOW_SHOTS and moves < SHOWN_MOVES:
                    self.shot(f"{tag}-{10 + shots:02d}-show")
                    shots += 1
                g.step(30)
                g.tap("a", 4)
            elif ui in (Ui.END, Ui.CHOICE):
                ends.append(self.end_screen(tag, ui))
                if g.screen != Screen.FIGHT:
                    break
            elif ui == Ui.LEVEL:
                g.tap("a", 20)
            else:
                g.step(20)
        return moves, ends

    def end_screen(self, tag: str, ui: int) -> str:
        """Capture an end screen (or a choice before it) and press on; returns its rows."""
        g = self.g
        g.step(45)
        self.shot(f"{tag}-90-end")
        rows = " / ".join(g.rows(END_ROWS))
        g.tap("a", 30)
        g.step(20)
        if g.screen != Screen.FIGHT:
            return rows
        if g.r8("_fight_ui") == Ui.END and ui == Ui.CHOICE:
            self.shot(f"{tag}-91-chosen")
        if g.screen == Screen.FIGHT:  # never an A on the bench after it: that would be a mix
            g.tap("a", 30)
        return rows

    def run(self, boss: int) -> None:
        g = self.g
        g.boot()
        ok = g.story()
        g.step(300)
        self.check("story bench", ok and g.screen == Screen.BENCH)
        for a, b in STORY_MAKES:
            if g.screen != Screen.BENCH:
                g.settle()
            g.mix(a, b)
        g.shot(self.out / "00-bench.png")
        self.fight("duel", DUEL)
        self.fight("flee", FLEE_DUEL, flee=True)
        flags = g.sym("_pl") + PL_FLAGS
        g.poke(flags, g.peek(flags) | PF_FIRST_DUEL)
        self.fight("boss", boss)
        g.w8("_flow_gauntlet", 1)  # chapter 4's gauntlet: three figures, one after another
        self.fight("gauntlet", GAUNTLET)
        self.check("gauntlet: three doors (or fewer if it was lost)", True, f"fight_gauntlet {g.r8('_fight_gauntlet')}")


def main() -> int:
    parser = argument_parser(__doc__)
    parser.add_argument("--boss", type=int, default=1, help="flow_arg for the boss fight (the faction)")
    parser.add_argument("--turns", type=int, default=60, help="turns a fight may take (x8 polls)")
    parser.add_argument("--seed", type=int, default=7, help="seed of the fight input")
    args = parser.parse_args()
    out: Path = args.out
    out.mkdir(parents=True, exist_ok=True)
    symbols = rom_symbols(args.rom, args.objects)
    game = Cartridge(args.rom, symbols.pointer("crucible", "_crucible_run", "_screen"), symbols.symbols)
    run = Crux(game, out, random.Random(args.seed), args.turns)
    run.run(args.boss)
    game.stop()
    (out / "crux.json").write_text(json.dumps(run.log, indent=1))
    passed = sum(run.checks)
    print(f"{passed}/{len(run.checks)} checks passed", flush=True)
    return 0 if passed == len(run.checks) else 1


if __name__ == "__main__":
    sys.exit(main())
