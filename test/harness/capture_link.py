"""Two cartridges on a link cable, headless: two PyBoy instances with separate blank saves, the cable played by
linkbridge.py. Every step is captured as a side-by-side pair (host left, guest right) and checked on both screens.

Phases (the flow through the menus is described in docs/game-flow.md):

  bridge  raw bytes cross both ways before anything else
  coop    PLAY > LINK on both: host and guest find each other (LINKED), the host's rules arrive (RULES), GO, the host's
          discovery lands in the guest's book (FIND), the session ends at its time (TIME UP), both saves reboot valid.
          The guest comes from a story run (LEAVE): the run is kept and resumes unchanged afterwards.
  race    RACE to 10 finds in 3 minutes, the same seed on both: the guest SENDs a find (GIFT), scores travel (SCORE),
          the host wins (YOU WIN / THEY WIN), the HUD's clock reads M:SS
  draw    RACE where nobody finds anything: the clock runs out on both (A DRAW)
  lost    the cable is pulled mid-session: LINK LOST on both
  fight   FIGHT (THE CRUCIBLE): both carts make things first (LIFE among them); a best-of-3 match driven move by move
          toward every kind of event (START, FORGE, HIJACK and a chain, SPLIT, BREAK, POUR, WAIT; PLACE, LIFE and
          WEATHER pots), both engines compared after every move (the guest's is swapped); the results mirror; AGAIN
          starts a rematch; then a second match with the cable pulled (NO CONTEST); both saves reboot
  fightnoise  the versus on a bad wire (1% of bytes flipped, 0..6 frames of delay) with the guest's engine corrupted
          once: the guest is resynced from the host and the match plays through, the same on both

The moves are chosen with the fight engine's Python reference, docs/fight-system/sim/crux.py.

Writes OUTDIR/<step>.png for every pair and OUTDIR/link.json (ROM hash, checks, per-pair screen rows). Exits 1 when any
check fails.

Usage: python test/harness/capture_link.py ROM OUTDIR [--objects OBJDIR] [--phases bridge,coop,...,fightnoise]
       [--catalogue DIR] [--noise-seed N]
"""

from __future__ import annotations

import argparse
import functools
import hashlib
import importlib.util
import json
import sys
from collections.abc import Callable, Iterable
from dataclasses import dataclass, field
from enum import IntEnum
from pathlib import Path
from types import ModuleType
from typing import Any, TypedDict

from linkbridge import ADDRESS_MASK, Cable, LinkHooks, Side, link_hooks
from PIL import Image
from romsym import RomSymbols

import catalogue

PHASES = ("bridge", "coop", "race", "draw", "lost", "fight", "fightnoise")
FIGHT_REFERENCE = Path(__file__).resolve().parents[2] / "docs" / "fight-system" / "sim" / "crux.py"


class Screen(IntEnum):
    """Values of crucible_run's `screen` static."""

    BENCH = 0
    MERGE = 1
    REVEAL = 2
    BOOK = 3
    RECORDS = 4
    MENU = 5
    FILTER = 6
    TALK = 7
    FIGHT = 8
    LINKEND = 9


class LinkMode(IntEnum):
    """The host's MODE rule (`link_mode`)."""

    COOP = 0
    RACE = 1
    FIGHT = 2


# Offsets into the `core` struct.
CORE_FOCUS = 0x1FC  # u16: the item under the bench cursor
CORE_SLOT_A = 0x1FE  # u16: the first ingredient picked, 0xFFFF when none
NO_ITEM = 0xFFFF

SCY = 0xFF42  # background scroll Y: the bench counts as settled only once it is back at 0

# Background map rows recorded for every pair (0 is the HUD/header, 4 the result title on LINKEND).
LOGGED_ROWS = (0, 4, 8, 9, 10, 11, 12, 13, 17)
HUD_ROW = 0
RESULT_ROW_INDEX = 1  # LOGGED_ROWS[1], row 4
LOBBY_CABLE_ROW = 14
TITLE_PLAY_ROW = 11
SCREEN_COLUMNS = 20
SCREEN_ROWS = 18
END_MADE_ROW = 9  # the versus end screen: MADE IN THE FIGHT
END_SCORE_ROW = 15  # ROUNDS n TO m
END_BUTTONS_ROW = 17  # AGAIN / LEAVE

LCDC = 0xFF40
LCDC_BG_MAP_HIGH = 0x08  # the background uses the map at 0x9C00 instead of 0x9800
BG_MAP_LOW = 0x9800
BG_MAP_HIGH = 0x9C00
BG_MAP_COLUMNS = 32
TILE_ID_VRAM_BANK = 0  # bank 1 holds the attributes

# The font's printable tiles: tile 128 + n is ASCII 32 + n.
FONT_FIRST_TILE = 128
FONT_END_TILE = 192
FONT_TILE_TO_ASCII = 96

# THE CRUCIBLE's engine state `cx` (crucible_crux.h; SDCC packs structs). Two 26-byte sides: bag[10] (u16 ids),
# the bag's count, the four (bit e: element e still held), the twist (a boss's style), made, hp (int8), max.
CX_SIDE_SIZE = 26
CX_BAG_COUNT = 20
CX_FOUR = 21
CX_TWIST = 22
CX_HP = 24
CX_MAX = 25
# Then the shared fields: pot and sky (u16), owner, heat, turn, the side to act, the hijack chain, passes, the shield,
# and the last move's event, damage and the ids it touched (u16 each).
CX_POT = 52
CX_SKY = 54
CX_OWNER = 56
CX_HEAT = 57
CX_TURN = 58
CX_ACT = 59
CX_CHAIN = 60
CX_PASSES = 61
CX_SHIELD = 62
CX_EVENT = 63
CX_DAMAGE = 64
CX_EVENT_X = 65
CX_EVENT_R = 67
EMPTY_POT = 0xFFFF
SHIELD_SIDES = 3  # the shield names side 1 or 2 (0: none); swapping the sides turns one into the other
EVENTS = ("START", "BUILD", "HIJACK", "SPLIT", "BREAK", "MISS", "FORGE", "POUR", "PASS")
FOUR_TOOLS = 12  # tool index 12 + e is element e of the four; 0..9 are bag slots
COMPARED = ("s", "pot", "sky", "owner", "heat", "turn", "act", "chain")  # what both engines must agree on
FIGHT_EVENTS = ("START", "FORGE", "HIJACK", "SPLIT", "BREAK", "POUR", "PASS")


class FightUi(IntEnum):
    """Values of `fight_ui`."""

    CHOOSE = 1
    WAIT = 2
    END = 4
    ROUND = 7  # the card between rounds, dismissed with A


MAX_BOUT_MOVES = 900
POKED_HP = 3  # fightnoise: the guest's own HP is raised by this much once, so its engine disagrees with the host's
NOISE = 0.01
NOISE_DELAY = 6
NOISE_SEED = 18
NOISE_MIXES = 8
NOISE_POKE_AT = 12  # moves into the noisy match
FIGHT_PULL_AT = 3  # moves into the second match when the cable is pulled
# Results that grow (LIFE) in both bags: ENERGY, LIFE, RAIN, PLANT.
LIFE_RECIPES = (("air", "fire"), ("water", "energy"), ("air", "water"), ("earth", "rain"))
FIGHT_MIXES_HOST = 16
FIGHT_MIXES_GUEST = 14

SAVE_BANKS = 16
SRAM_START = 0xA000
SRAM_LAST = 0xBFFF

PAIR_GAP = 8
PAIR_BACKGROUND = (40, 40, 40)
PAIR_SCALE = 2

SESSION_FRAMES = 3 * 3600  # a 3-minute session at 60 frames a second
MAX_RACE_MIXES = 14
ASK_PRESSES = 12  # A presses through a game start's asks (date and time, birthday) and the sign loader


def screen_text(side: Side, y: int) -> str:
    """Background row y as text; tiles outside the font read as '~'. The tile ids are read from VRAM bank 0 directly:
    PyBoy's tilemap view follows VBK, so a decode that spans the frame's end would read the attribute bank instead."""
    base = (BG_MAP_HIGH if side.m[LCDC] & LCDC_BG_MAP_HIGH else BG_MAP_LOW) + y * BG_MAP_COLUMNS
    tiles = [side.pb.memory[TILE_ID_VRAM_BANK, base + x] for x in range(SCREEN_COLUMNS)]
    return "".join(chr(t - FONT_TILE_TO_ASCII) if FONT_FIRST_TILE <= t < FONT_END_TILE else "~" for t in tiles)


def until(cable: Cable, predicate: Callable[[], object], limit: int = 3000) -> bool:
    """Step the cable a frame at a time until `predicate` holds, at most `limit` frames."""
    for _ in range(limit):
        if predicate():
            return True
        cable.step(1)
    return bool(predicate())


def found(side: Side, cable: Cable | None = None) -> int:
    """The finds count in the HUD header, -1 when it shows none. With a cable, waits out a header mid-redraw."""
    for _ in range(60):
        header = screen_text(side, HUD_ROW)
        digits = "".join(ch for ch in header[1:6] if ch.isdigit())
        if digits or cable is None:
            return int(digits) if digits else -1
        cable.step(2)
    return -1


def save_ram(side: Side) -> bytes:
    """All 16 cartridge RAM banks."""
    memory = side.pb.memory
    # PyBoy's banked slice excludes its end address, so the last byte of each bank is read on its own.
    return b"".join(
        bytes(memory[bank, SRAM_START:SRAM_LAST]) + bytes([memory[bank, SRAM_LAST]]) for bank in range(SAVE_BANKS)
    )


def stop(*sides: Side) -> None:
    for side in sides:
        side.pb.stop(save=False)


def load_fight_reference() -> ModuleType:
    """The fight engine's Python reference (it reads the catalogue as it loads)."""
    spec = importlib.util.spec_from_file_location("crux", FIGHT_REFERENCE)
    assert spec and spec.loader, f"no fight reference at {FIGHT_REFERENCE}"
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class CxSide(TypedDict):
    bag: list[int]
    four: int
    tw: int
    hp: int
    max: int


class CxState(TypedDict):
    """One cart's engine state; its own player is side 0 (the guest's engine is swapped)."""

    s: list[CxSide]
    pot: int
    sky: int
    owner: int
    heat: int
    turn: int
    act: int
    chain: int
    passes: int
    shield: int
    ev: int
    dmg: int
    ex: int
    er: int


@dataclass
class Match:
    """A match as the harness saw it."""

    moves: int = 0
    agree: bool = True
    bad: list[tuple[int, ...]] = field(default_factory=list)  # (move, turns, own HPs, pots) where the engines differ
    seen: set[str] = field(default_factory=set)  # events, CHAIN, CATn for the pots' categories, GROWING
    rounds: set[int] = field(default_factory=set)


def host_order(state: CxState, swapped: bool) -> CxState:
    """A cart's engine state in the host's side order."""
    if not swapped:
        return state
    turned = state.copy()
    turned["s"] = [state["s"][1], state["s"][0]]
    turned["owner"] ^= 1
    turned["act"] ^= 1
    turned["shield"] = SHIELD_SIDES - state["shield"] if state["shield"] else 0
    return turned


def tools_of(state: CxState, k: int) -> list[int]:
    """The tools side k can use, in the order the fight's cursor steps through them: its bag, then the four left."""
    side = state["s"][k]
    return list(range(len(side["bag"]))) + [FOUR_TOOLS + e for e in range(4) if side["four"] >> e & 1]


class LinkCapture:
    def __init__(self, rom: Path, out: Path, symbols: RomSymbols, items: catalogue.Catalogue, noise_seed: int) -> None:
        self.rom = rom
        self.out = out
        self.symbols = symbols.symbols
        self.hooks: LinkHooks = link_hooks(symbols)
        self.screen_at = symbols.pointer("crucible", "_crucible_run", "_screen")
        self.core = self.symbols["_core"]
        self.cx = self.symbols["_cx"] & ADDRESS_MASK
        self.fight_cursor = self.symbols["_fight_cursor"] & ADDRESS_MASK
        self.items = items
        self.recipes = items.recipes
        self.noise_seed = noise_seed
        self.reference = load_fight_reference()
        self.mismatches: list[tuple[int, ...]] = []  # the last match's disagreements, for the notes
        self.checks: list[dict[str, Any]] = []
        self.log: list[dict[str, Any]] = []

    # --- reporting -------------------------------------------------------------------------------------------------

    def check(self, name: str, ok: object, note: str = "") -> None:
        self.checks.append({"check": name, "ok": bool(ok), "note": note})
        print(("PASS " if ok else "FAIL ") + name + (" - " + note if note else ""), flush=True)

    def side_entry(self, side: Side) -> dict[str, Any]:
        return {"screen": Screen(self.screen(side)).name, "rows": [screen_text(side, y) for y in LOGGED_ROWS]}

    def pair(self, cable: Cable, name: str, note: str = "") -> dict[str, Any]:
        """Run a frame, save both screens side by side as NAME.png and log both screens' state."""
        cable.step(1)
        host = cable.host.pb.screen.image.convert("RGB")
        guest = cable.guest.pb.screen.image.convert("RGB")
        image = Image.new("RGB", (host.width * 2 + PAIR_GAP, host.height), PAIR_BACKGROUND)
        image.paste(host, (0, 0))
        image.paste(guest, (host.width + PAIR_GAP, 0))
        scaled = image.resize((image.width * PAIR_SCALE, image.height * PAIR_SCALE), Image.Resampling.NEAREST)
        scaled.save(self.out / f"{name}.png")
        entry = {"pair": name, "note": note, "host": self.side_entry(cable.host), "guest": self.side_entry(cable.guest)}
        self.log.append(entry)
        return entry

    # --- reading the game ------------------------------------------------------------------------------------------

    def screen(self, side: Side) -> int:
        return side.m[self.screen_at]

    def var(self, side: Side, name: str) -> int:
        return side.m[self.symbols[name]]

    def word(self, side: Side, address: int) -> int:
        return side.m[address] | side.m[address + 1] << 8

    def focus(self, side: Side) -> int:
        return self.word(side, self.core + CORE_FOCUS)

    def slot_a(self, side: Side) -> int:
        return self.word(side, self.core + CORE_SLOT_A)

    def linked(self, cable: Cable) -> bool:
        return bool(self.var(cable.host, "_link_linked") and self.var(cable.guest, "_link_linked"))

    def both_on(self, cable: Cable, screen: Screen) -> bool:
        return self.screen(cable.host) == screen and self.screen(cable.guest) == screen

    def text(self, side: Side, name: str, length: int = 20) -> str:
        """A NUL-terminated string variable."""
        raw = bytes(side.m[self.symbols[name] + i] for i in range(length))
        return raw.split(b"\0")[0].decode("latin-1")

    def big_title(self, side: Side) -> str:
        """The end card's big title: pixel art, so it is read from the string it is drawn from."""
        return self.text(side, "_fe_big", 14)

    # --- THE CRUCIBLE ----------------------------------------------------------------------------------------------

    def cx_read(self, side: Side) -> CxState:
        def r8(offset: int) -> int:
            return side.m[self.cx + offset]

        def r16(offset: int) -> int:
            return self.word(side, self.cx + offset)

        def signed(value: int) -> int:
            return value - 256 if value > 127 else value

        def cx_side(k: int) -> CxSide:
            at = k * CX_SIDE_SIZE
            return {
                "bag": [r16(at + 2 * i) for i in range(r8(at + CX_BAG_COUNT))],
                "four": r8(at + CX_FOUR),
                "tw": r8(at + CX_TWIST),
                "hp": signed(r8(at + CX_HP)),
                "max": r8(at + CX_MAX),
            }

        return {
            "s": [cx_side(0), cx_side(1)],
            "pot": r16(CX_POT),
            "sky": r16(CX_SKY),
            "owner": r8(CX_OWNER),
            "heat": r8(CX_HEAT),
            "turn": r8(CX_TURN),
            "act": r8(CX_ACT),
            "chain": r8(CX_CHAIN),
            "passes": r8(CX_PASSES),
            "shield": r8(CX_SHIELD),
            "ev": r8(CX_EVENT),
            "dmg": r8(CX_DAMAGE),
            "ex": r16(CX_EVENT_X),
            "er": r16(CX_EVENT_R),
        }

    def reference_state(self, state: CxState) -> Any:  # noqa: ANN401 - the reference module is untyped
        """The reference's State for a cart's engine state, to see what each move would do."""
        x = self.reference
        sides = []
        for p in state["s"]:
            side = x.Side(p["bag"], p["hp"], p["tw"])
            side.four = p["four"]
            side.max = p["max"]
            sides.append(side)
        ref = x.State.__new__(x.State)
        ref.s = sides
        for key in ("pot", "sky", "owner", "heat", "turn", "act", "chain", "passes", "shield"):
            setattr(ref, key, state[key])
        ref.last = None
        ref.dmg = 0
        return ref

    def pick(self, state: CxState, seen: set[str]) -> int:
        """The local side's next move: one that makes an event not seen yet (a chain counts), else a sensible one."""
        x = self.reference
        ref = self.reference_state(state)
        best, best_score = None, -1
        for move in ref.legal(0):
            after = ref.clone()
            after.do(move)
            event = after.last[0]
            score = 0
            if event not in seen:
                score += 10
            if event == "HIJACK" and after.chain >= 2 and "CHAIN" not in seen:
                score += 12
            if event == "POUR":
                score += 3
            if event in ("HIJACK", "SPLIT", "BREAK"):
                score += 2
            if event == "MISS":
                score -= 3
            if (
                after.pot != x.NONE
                and x.CAT[after.pot] in (x.C_PLACE, x.C_LIFE, x.C_WEATHER)
                and f"CAT{x.CAT[after.pot]}" not in seen
            ):
                score += 6
            if score > best_score:
                best, best_score = move, score
        assert best is not None, "no legal move"
        return int(best)

    def drive(self, cable: Cable, side: Side, move: int) -> None:
        """Play the move on screen: the cursor onto its thing, then A (mark, add, forge) or B (pour, wait)."""
        x = self.reference
        state = self.cx_read(side)
        kind = move >> 8
        tools = tools_of(state, 0)

        def cursor_to(tool: int) -> None:
            side.m[self.fight_cursor] = tools.index(tool)
            cable.step(2)

        if kind in (x.POUR, x.PASS):
            cable.press(side, "b", 6)
            return
        if kind == x.FORGE:
            cursor_to((move >> 4) & 15)
            cable.press(side, "a", 6)
            cursor_to(move & 15)
            cable.press(side, "a", 6)
            return
        cursor_to((move >> 4) & 15)
        cable.press(side, "a", 6)
        if state["pot"] == EMPTY_POT:  # an empty pot: A marks it, A again sets it alone
            cable.press(side, "a", 6)

    def ui(self, side: Side) -> int:
        return self.var(side, "_fight_ui")

    # --- driving the game ------------------------------------------------------------------------------------------

    def new_cable(self, saves: tuple[bytes, bytes] | None = None) -> Cable:
        host_ram, guest_ram = saves if saves else (None, None)
        host = Side(self.rom, "host", self.hooks, host_ram)
        guest = Side(self.rom, "guest", self.hooks, guest_ram)
        return Cable(host, guest)

    def wait_screen(self, cable: Cable, side: Side, screen: Screen, limit: int) -> bool:
        return until(cable, lambda: self.screen(side) == screen, limit)

    def wait_linked(self, cable: Cable) -> None:
        until(cable, lambda: self.linked(cable), 600)

    def boot(self, cable: Cable) -> None:
        """Power on both to the title card's menu. The title card asks nothing: a game start asks the date and time."""
        until(cable, lambda: self.both_on(cable, Screen.MENU), 6000)
        cable.step(360)

    def asks(self, cable: Cable, side: Side) -> None:
        """A fresh cartridge's first story or free play start asks the date and time, then the birthday (A keeps what
        is shown), then the sign loader opens the game: A until the menu is gone."""
        for _ in range(ASK_PRESSES):
            if self.screen(side) != Screen.MENU:
                return
            cable.press(side, "a", 40)

    def to_lobby(self, cable: Cable) -> None:
        """PLAY > LINK on both, HOST on the host and JOIN on the guest, then FREE PLAY as the save each brings."""
        for side in (cable.host, cable.guest):
            cable.press(side, "a", 30)  # PLAY
        for side in (cable.host, cable.guest):
            cable.press(side, "down", 10)
            cable.press(side, "down", 10)
            cable.press(side, "a", 30)  # LINK
        cable.press(cable.guest, "down", 10)
        cable.press(cable.host, "a", 30)  # HOST
        cable.press(cable.guest, "a", 30)  # JOIN
        cable.press(cable.host, "a", 30)  # FREE PLAY
        cable.press(cable.guest, "a", 30)

    def lobby_set(self, cable: Cable, mode: int, time: int, goal: int = 0) -> None:
        """Set the host's rules (rows: 0 MODE, 1 TIME, 2 GOAL, 4 START) and leave the cursor on START."""
        host = cable.host
        while self.var(host, "_link_mode") != mode:
            cable.press(host, "a", 20)
        cable.press(host, "down", 10)
        while self.var(host, "_link_time") != time:
            cable.press(host, "a", 20)
        cable.press(host, "down", 10)
        while self.var(host, "_link_goal") != goal:
            cable.press(host, "a", 20)
        cable.press(host, "down", 10)
        cable.press(host, "down", 10)

    def start(self, cable: Cable) -> bool:
        """The host presses START; true once both are on their benches."""
        cable.press(cable.host, "a", 30)
        return until(cable, lambda: self.both_on(cable, Screen.BENCH), 1200)

    def start_free_play(self, cable: Cable) -> None:
        """PLAY > FREE on both (through the game start's asks), then let both benches settle."""
        for side in (cable.host, cable.guest):
            cable.press(side, "a", 30)
            cable.press(side, "down", 10)
            cable.press(side, "a", 30)
            self.asks(cable, side)
        until(cable, lambda: self.both_on(cable, Screen.BENCH), 1500)
        cable.step(300)

    def point_pause_menu(self, cable: Cable, side: Side) -> None:
        """Pause and move to the slot that is LEAVE in a story run and SEND in a link session."""
        cable.press(side, "start", 30)
        self.wait_screen(cable, side, Screen.MENU, 300)
        for button in ("down", "down", "right"):
            cable.press(side, button, 10)

    def leave_run(self, cable: Cable, side: Side) -> None:
        """LEAVE the run (it is saved) for the title card, allowing it a fixed 120 frames."""
        self.point_pause_menu(cable, side)
        cable.press(side, "a", 120)

    def leave_run_until_title(self, cable: Cable, side: Side) -> None:
        """LEAVE the run (it is saved), waiting until the title card shows PLAY: saving a story run can take longer."""
        self.point_pause_menu(cable, side)
        cable.press(side, "a", 30)
        until(cable, lambda: "PLAY" in screen_text(side, TITLE_PLAY_ROW), 600)

    def settle(self, cable: Cable, side: Side, limit: int = 200) -> bool:
        """Dismiss reveals and talk until the bench is up and still."""
        for _ in range(limit):
            if self.screen(side) == Screen.BENCH and side.m[SCY] == 0:
                return True
            if self.screen(side) in (Screen.REVEAL, Screen.TALK):
                cable.press(side, "a", 20)
            else:
                cable.step(10)
        return self.screen(side) == Screen.BENCH

    def right(self, cable: Cable, side: Side) -> int:
        """Move the bench cursor one item right; return the new focus."""
        was = self.focus(side)
        cable.press(side, "right", 2)
        until(cable, lambda: self.focus(side) != was, 120)
        return self.focus(side)

    def owned(self, cable: Cable, side: Side) -> set[int]:
        """The items on the bench, found by walking the cursor all the way round."""
        self.settle(cable, side)
        first = self.focus(side)
        seen = [first]
        for _ in range(2000):
            focus = self.right(cable, side)
            if focus == first and len(seen) == 1:
                continue
            if focus == first:
                break
            seen.append(focus)
        return set(seen)

    def goto(self, cable: Cable, side: Side, target: int, count: int) -> bool:
        for _ in range(count + 2):
            if self.focus(side) == target:
                return True
            self.right(cable, side)
        return self.focus(side) == target

    def mix(self, cable: Cable, side: Side, have: set[int], avoid: Iterable[int] = ()) -> int | None:
        """Make the first recipe (in id order) whose ingredients are owned and whose result is new; add it to `have`."""
        todo = sorted(
            (x, y, r) for x, y, r in self.recipes if x in have and y in have and r not in have and r not in avoid
        )
        if not todo:
            return None
        x, y, result = todo[0]
        if self.slot_a(side) != NO_ITEM:
            cable.press(side, "b", 20)
        if not self.goto(cable, side, x, len(have)):
            return None
        cable.press(side, "a", 20)
        if x == y:
            cable.press(side, "a")
        else:
            if not self.goto(cable, side, y, len(have)):
                cable.press(side, "b", 20)
                return None
            cable.press(side, "a")
        until(cable, lambda: self.screen(side) in (Screen.MERGE, Screen.REVEAL), 300)
        until(cable, lambda: self.screen(side) in (Screen.REVEAL, Screen.BENCH, Screen.LINKEND), 4000)
        cable.step(60)
        self.settle(cable, side)
        have.add(result)
        return result

    def end_both(self, cable: Cable, name: str, want_host: str, want_guest: str, limit: int = 4000) -> dict[str, Any]:
        """Wait for LINKEND on both and check each shows its expected result."""
        until(cable, lambda: self.both_on(cable, Screen.LINKEND), limit)
        cable.step(60)
        entry = self.pair(cable, name, "the result on both")
        host_big, guest_big = self.big_title(cable.host), self.big_title(cable.guest)
        host_rows = " ".join(entry["host"]["rows"]) + " " + host_big
        guest_rows = " ".join(entry["guest"]["rows"]) + " " + guest_big
        entry["host"]["rows"][RESULT_ROW_INDEX] += " " + host_big
        entry["guest"]["rows"][RESULT_ROW_INDEX] += " " + guest_big
        self.check(f"{name}: host shows {want_host}", want_host in host_rows, entry["host"]["rows"][1].strip())
        self.check(f"{name}: guest shows {want_guest}", want_guest in guest_rows, entry["guest"]["rows"][1].strip())
        return entry

    # --- phases ----------------------------------------------------------------------------------------------------

    def phase_bridge(self) -> None:
        cable = self.new_cable()
        host, guest = cable.host, cable.guest
        self.boot(cable)
        self.to_lobby(cable)
        self.wait_linked(cable)
        self.check(
            "bridge: bytes cross both ways",
            cable.bytes > 50 and host.got and guest.got,
            f"{cable.bytes} transfers; host heard {len(host.got)} non-idle reads, guest {len(guest.got)}",
        )
        stop(host, guest)

    def story_then_leave(self, cable: Cable, side: Side) -> tuple[set[int], int]:
        """Start a story run, make a few things, then LEAVE (the run is saved) to the title card.

        Returns the items the run owns and its finds count when it was left.
        """
        for _ in range(3):
            cable.press(side, "a", 60)
        for k in range(300):
            if self.screen(side) == Screen.BENCH:
                break
            cable.press(side, "start" if k % 5 == 4 else "a", 40)
        cable.step(300)
        have = self.owned(cable, side)
        for _ in range(3):
            self.mix(cable, side, have)
        finds = found(side, cable)
        self.leave_run_until_title(cable, side)
        return have, finds

    def phase_coop(self) -> None:
        cable = self.new_cable()
        host, guest = cable.host, cable.guest
        self.boot(cable)
        # The guest comes to the session from a story run.
        story_have, story_before = self.story_then_leave(cable, guest)
        self.check(
            "coop: the guest left its run for the title card",
            self.var(guest, "_story_on") == 0 and "PLAY" in screen_text(guest, TITLE_PLAY_ROW),
        )

        self.to_lobby(cable)
        self.wait_linked(cable)
        cable.step(60)
        cable.step(400)
        self.pair(cable, "coop-1-linked", "LINKED: the link room shows the cable and the partner's name")
        host_row = screen_text(host, LOBBY_CABLE_ROW)
        guest_row = screen_text(guest, LOBBY_CABLE_ROW)
        # The guest's panel redraws as the host's rules arrive (cleared, then written): read the cable row a while.
        for _ in range(30):
            if "LINK" in host_row and "LINK" in guest_row:
                break
            cable.step(2)
            host_row = screen_text(host, LOBBY_CABLE_ROW)
            guest_row = screen_text(guest, LOBBY_CABLE_ROW)
        # A default name (YOU) reads as GUEST on the host and HOST on the guest.
        self.check(
            "coop: LINKED on both (the cable row, the partner's name)",
            self.linked(cable)
            and "LINK" in host_row
            and "LINK" in guest_row
            and "GUEST" in host_row
            and "HOST" in guest_row,
            f"{host_row.strip()} / {guest_row.strip()}",
        )

        self.lobby_set(cable, LinkMode.COOP, 1)
        cable.step(150)
        self.pair(cable, "coop-2-rules", "RULES: the guest shows the host's mode and time")
        self.check(
            "coop: the rules arrived",
            self.var(guest, "_link_time") == 1
            and self.var(guest, "_link_mode") == LinkMode.COOP
            and self.var(host, "_link_seed") == self.var(guest, "_link_seed"),
        )

        ok = self.start(cable)
        cable.step(120)
        self.pair(cable, "coop-3-go", "GO: both on their benches")
        self.check("coop: GO on both", ok)

        host_have = self.owned(cable, host)
        guest_found = found(guest, cable)
        made = self.mix(cable, host, host_have, avoid=story_have)
        cable.step(240)
        self.pair(cable, "coop-4-find", f"the host made {made}; it lands in the guest's book")
        # found() may step the cable while the header redraws, so it is read afresh for the check and for the note.
        self.check(
            "coop: the partner's find is granted",
            found(guest, cable) == guest_found + 1,
            f"guest {guest_found} -> {found(guest, cable)}",
        )

        self.end_both(cable, "coop-5-end", "TIME UP", "TIME UP", SESSION_FRAMES + 1200)
        for side in (host, guest):
            cable.press(side, "a", 120)
        self.check(
            "coop: back to the title on both",
            self.screen(host) == Screen.MENU and self.screen(guest) == Screen.MENU,
        )

        # The guest's story run is still there: resume it.
        for _ in range(3):
            cable.press(guest, "a", 60)
        self.wait_screen(cable, guest, Screen.BENCH, 1200)
        cable.step(300)
        self.pair(cable, "coop-6-run-resumed", "the guest resumes its story run")
        self.check(
            "coop: the story run is kept",
            self.var(guest, "_story_on") == 1 and found(guest, cable) == story_before,
            f"{story_before} -> {found(guest, cable)}",
        )
        saves = (save_ram(host), save_ram(guest))
        stop(host, guest)

        # Both saves reboot valid: the guest's free play has the host's find.
        cable = self.new_cable(saves)
        host, guest = cable.host, cable.guest
        self.boot(cable)
        self.start_free_play(cable)
        self.pair(cable, "coop-7-reboot", "both saves after the session, rebooted: free play")
        self.check(
            "coop: both saves load",
            self.screen(host) == Screen.BENCH
            and self.screen(guest) == Screen.BENCH
            and found(host, cable) >= 5
            and found(guest, cable) >= 5,
            f"host {found(host, cable)} guest {found(guest, cable)}",
        )
        stop(host, guest)

    def race(self, cable: Cable, name: str, time: int) -> None:
        """Into a RACE session with the given TIME rule, as far as GO."""
        host, guest = cable.host, cable.guest
        self.to_lobby(cable)
        self.wait_linked(cable)
        self.lobby_set(cable, LinkMode.RACE, time)
        cable.step(150)
        self.pair(cable, f"{name}-1-rules", "RACE rules on both")
        self.check(
            f"{name}: the same seed and rules",
            self.var(host, "_link_seed") == self.var(guest, "_link_seed")
            and self.var(guest, "_link_mode") == LinkMode.RACE
            and self.var(guest, "_link_time") == time,
        )
        ok = self.start(cable)
        cable.step(120)
        self.pair(cable, f"{name}-2-go", "GO")
        self.check(f"{name}: GO on both", ok)

    def phase_race(self) -> None:
        cable = self.new_cable()
        host, guest = cable.host, cable.guest
        self.boot(cable)
        self.race(cable, "race", 1)
        hud = screen_text(host, HUD_ROW)
        self.check(
            "race: the HUD clock reads M:SS",
            ":" in hud[7:19] and hud[13:17].count(":") + hud[14:18].count(":") >= 1,
            hud,
        )

        # The guest makes a find and SENDs it: it lands on the host (RACE shares finds only as scores).
        guest_have = self.owned(cable, guest)
        sent = self.mix(cable, guest, guest_have)
        host_found = found(host, cable)
        self.point_pause_menu(cable, guest)
        menu = " ".join(screen_text(guest, y) for y in range(10, 15))
        self.check("race: SEND on the guest's pause menu", "SEND" in menu, " ".join(menu.split()))
        cable.press(guest, "a", 60)
        self.settle(cable, guest)
        cable.step(240)
        self.pair(cable, "race-3-gift", f"GIFT: the guest sent {sent}; the host has it")
        self.check(
            "race: the gift is granted on the host",
            found(host, cable) == host_found + 1,
            f"host {host_found} -> {found(host, cable)}",
        )

        host_have = self.owned(cable, host)
        made = 0
        while self.screen(host) == Screen.BENCH and made < MAX_RACE_MIXES:
            if self.mix(cable, host, host_have) is None:
                break
            made += 1
            if made == 3:
                cable.step(120)
                self.pair(cable, "race-4-score", "SCORE: the host leads on both HUDs")
                host_score = screen_text(host, HUD_ROW)[7:12]
                guest_score = screen_text(guest, HUD_ROW)[7:12]
                self.check(
                    "race: the score travels",
                    self.var(guest, "_link_started") and guest_score != host_score,
                    f"{host_score} / {guest_score}",
                )
        self.end_both(cable, "race-5-end", "YOU WIN", "THEY WIN", 1200)
        stop(host, guest)

    def phase_draw(self) -> None:
        cable = self.new_cable()
        self.boot(cable)
        self.race(cable, "draw", 1)
        self.end_both(cable, "draw-3-end", "A DRAW", "A DRAW", SESSION_FRAMES + 1200)
        stop(cable.host, cable.guest)

    def phase_lost(self) -> None:
        cable = self.new_cable()
        self.boot(cable)
        self.race(cable, "lost", 2)
        cable.step(300)
        cable.connected = False
        self.pair(cable, "lost-3-pulled", "the cable is pulled")
        self.end_both(cable, "lost-4-end", "LINK LOST", "LINK LOST", 900)
        stop(cable.host, cable.guest)

    def vs_end(self, cable: Cable, name: str) -> None:
        """Both carts on the versus end screen: the big result mirrored, what each made, the score, AGAIN / LEAVE."""
        host, guest = cable.host, cable.guest
        ok = until(
            cable,
            lambda: (
                self.ui(host) == FightUi.END and self.ui(guest) == FightUi.END and self.both_on(cable, Screen.FIGHT)
            ),
            4000,
        )
        cable.step(60)
        self.pair(cable, name, "the end screen on both")
        host_big, guest_big = self.big_title(host), self.big_title(guest)
        self.check(f"{name}: both on the end screen", ok)
        both = host_big + guest_big
        mirrored = (
            (host_big == "YOU WIN") != (guest_big == "YOU WIN")
            and ("WINS" in both or "THEY WIN" in (host_big, guest_big))
            and "  " not in both
            and "YOU WINS" not in both
            and "THEM WINS" not in both
        ) or host_big == guest_big == "NEITHER FALLS"
        self.check(f"{name}: the big result mirrors", mirrored, f"{host_big} / {guest_big}")
        host_rows = [screen_text(host, y) for y in range(SCREEN_ROWS)]
        guest_rows = [screen_text(guest, y) for y in range(SCREEN_ROWS)]
        self.check(
            f"{name}: MADE IN THE FIGHT, the score and AGAIN / LEAVE on both",
            all(
                "MADE" in rows[END_MADE_ROW]
                and "ROUNDS" in rows[END_SCORE_ROW]
                and "AGAIN" in rows[END_BUTTONS_ROW]
                and "LEAVE" in rows[END_BUTTONS_ROW]
                for rows in (host_rows, guest_rows)
            ),
            f"{host_rows[END_MADE_ROW].strip()} | {host_rows[END_SCORE_ROW].strip()} | "
            f"{host_rows[END_BUTTONS_ROW].strip()} // {guest_rows[END_SCORE_ROW].strip()}",
        )

    def match_over(self, cable: Cable) -> bool:
        host, guest = cable.host, cable.guest
        return (
            self.screen(host) != Screen.FIGHT
            or self.screen(guest) != Screen.FIGHT
            or (self.ui(host) == FightUi.END and self.ui(guest) == FightUi.END)
        )

    def fight_round(self, side: Side) -> int:
        return self.var(side, "_fight_round")

    def someone_to_move(self, cable: Cable) -> bool:
        return self.ui(cable.host) == FightUi.CHOOSE or self.ui(cable.guest) == FightUi.CHOOSE or self.match_over(cable)

    def moved_on(self, cable: Cable, turn: int) -> bool:
        """Both engines are past `turn`, or the match is over."""
        return (
            self.cx_read(cable.host)["turn"] != turn and self.cx_read(cable.guest)["turn"] != turn
        ) or self.match_over(cable)

    def in_step(self, cable: Cable) -> bool:
        """Both carts at the same round and turn and settled (on a slow wire one may still be showing the move), or
        out of the fight."""
        host, guest = cable.host, cable.guest
        if not self.both_on(cable, Screen.FIGHT):
            return True
        return (
            self.fight_round(host) == self.fight_round(guest)
            and self.cx_read(host)["turn"] == self.cx_read(guest)["turn"]
            and self.ui(host) in (FightUi.CHOOSE, FightUi.WAIT)
            and self.ui(guest) in (FightUi.CHOOSE, FightUi.WAIT)
        )

    def compare(self, cable: Cable, match: Match) -> CxState:
        """Both engines after a move, in the host's order; returns the host's."""
        host, guest = cable.host, cable.guest
        host_state = host_order(self.cx_read(host), swapped=False)
        guest_state = host_order(self.cx_read(guest), swapped=True)
        same = all(host_state[k] == guest_state[k] for k in COMPARED)  # type: ignore[literal-required]
        if self.match_over(cable) or self.fight_round(host) != self.fight_round(guest):
            same = True  # the match ended between the reads
        match.agree &= same
        if not same:
            match.bad.append(
                (
                    match.moves,
                    host_state["turn"],
                    guest_state["turn"],
                    host_state["s"][0]["hp"],
                    guest_state["s"][0]["hp"],
                    host_state["pot"],
                    guest_state["pot"],
                )
            )
        return host_state

    def note_event(self, cable: Cable, name: str, state: CxState, match: Match) -> None:
        """What the move did, as seen on the host; the first of each kind is captured."""
        x = self.reference
        event = EVENTS[state["ev"]] if state["ev"] < len(EVENTS) else "?"
        seen = match.seen
        if event not in seen:
            self.pair(cable, f"{name}-ev-{event.lower()}", f"{event} on both")
        seen.add(event)
        if event == "HIJACK" and state["chain"] >= 2:
            seen.add("CHAIN")
        pot = state["pot"]
        if pot != EMPTY_POT:
            seen.add(f"CAT{x.CAT[pot]}")
        if pot != EMPTY_POT and x.CAT[pot] == x.C_LIFE and state["heat"] >= 1:
            seen.add("GROWING")

    def check_events(self, name: str, match: Match) -> None:
        x = self.reference
        seen = match.seen
        self.check(
            f"{name}: every move the same on both carts ({match.moves} moves)",
            match.agree and match.moves > 0,
            f"{match.bad[:2]}",
        )
        for event in FIGHT_EVENTS:
            self.check(f"{name}: {event} played over the cable", event in seen)
        self.check(f"{name}: a hijack chain (a hijack taken back)", "CHAIN" in seen)
        categories = (x.C_PLACE, x.C_LIFE, x.C_WEATHER)
        self.check(
            f"{name}: PLACE (locked), LIFE (grows), WEATHER (the sky) pots in play; the four are everyone's",
            all(f"CAT{c}" in seen for c in categories) and ("CAT0" in seen or "FORGE" in seen),
            " ".join(sorted(e for e in seen if e.startswith("CAT"))),
        )
        self.check(f"{name}: more than one round (best of 3)", len(match.rounds) >= 2, f"rounds {sorted(match.rounds)}")

    def bout(
        self,
        cable: Cable,
        name: str,
        pull_at: int | None = None,
        *,
        check_events: bool = True,
        poke_at: int | None = None,
    ) -> tuple[int, bool]:
        """A whole match: each turn the side to move plays a move aiming at every kind of event, and both engines are
        compared (in the host's order) after every move. Pairs are captured for the first move of each kind. With
        `pull_at` the cable is pulled that many moves in; with `poke_at` the guest's engine is corrupted once, that
        many moves in, at the host's move in the first round."""
        host, guest = cable.host, cable.guest
        x = self.reference
        ok = until(cable, lambda: self.both_on(cable, Screen.FIGHT), 900)
        cable.step(120)
        self.pair(cable, f"{name}-1-start", "the match opens on both")
        self.check(f"{name}: both in the match", ok)
        # A noisy wire's setup can take a few seconds.
        until(cable, lambda: self.ui(host) in (FightUi.CHOOSE, FightUi.WAIT), 2400)
        bags = self.cx_read(host)["s"]
        self.check(
            f"{name}: the bags (host / guest)",
            bags[0]["bag"] and bags[1]["bag"],
            " ".join(x.NAME[i] for i in bags[0]["bag"]) + " / " + " ".join(x.NAME[i] for i in bags[1]["bag"]),
        )
        match = Match()
        poked = False
        for _ in range(MAX_BOUT_MOVES):
            if not until(cable, lambda: self.someone_to_move(cable), 4000) or self.match_over(cable):
                break
            if pull_at is not None and match.moves == pull_at:
                cable.connected = False
                self.pair(cable, f"{name}-pulled", "the cable is pulled mid-round")
                break
            first_round_host_move = self.ui(host) == FightUi.CHOOSE and self.fight_round(host) == 0
            if poke_at is not None and match.moves >= poke_at and first_round_host_move and not poked:
                poked = True
                hp = self.cx + CX_HP  # the guest's own side
                guest.m[hp] = (guest.m[hp] + POKED_HP) & 0xFF
                self.check(f"{name}: the guest's state corrupted (HP +3) before the host's move", True)
            side = host if self.ui(host) == FightUi.CHOOSE else guest
            state = self.cx_read(side)
            move = self.pick(state, match.seen)
            match.rounds.add(self.fight_round(host))
            self.drive(cable, side, move)
            match.moves += 1
            until(cable, functools.partial(self.moved_on, cable, state["turn"]), 2400)
            cable.step(4)
            until(cable, lambda: self.in_step(cable), 3000)
            host_state = self.compare(cable, match)
            if host_state["turn"] != state["turn"]:
                self.note_event(cable, name, host_state, match)
            for s in (host, guest):
                if self.screen(s) == Screen.FIGHT and self.ui(s) == FightUi.ROUND:
                    cable.press(s, "a", 10)
        self.mismatches = match.bad
        if pull_at is None and check_events:
            self.check_events(name, match)
        return match.moves, match.agree

    def make_for_the_fight(self, cable: Cable, side: Side, mixes: int) -> None:
        """`mixes` makes from the recipes, then the LIFE results (they grow in the pot) where they can be made."""
        have = self.owned(cable, side)
        for _ in range(mixes):
            self.mix(cable, side, have)
        ids = self.items.ids
        for a, b in LIFE_RECIPES:
            x, y = ids[a], ids[b]
            result = next((r for p, q, r in self.recipes if {p, q} == {x, y}), None)
            if x in have and y in have and result is not None and result not in have:
                self.settle(cable, side)
                self.goto(cable, side, x, len(have))
                cable.press(side, "a", 20)
                self.goto(cable, side, y, len(have))
                cable.press(side, "a")
                until(cable, lambda: self.screen(side) in (Screen.MERGE, Screen.REVEAL), 300)
                until(cable, lambda: self.screen(side) in (Screen.REVEAL, Screen.BENCH), 4000)
                cable.step(60)
                self.settle(cable, side)
                have.add(result)

    def into_versus(self, cable: Cable, linked_limit: int = 600) -> None:
        """Both leave free play for the title, then PLAY > LINK into the link room, linked."""
        for side in (cable.host, cable.guest):
            self.leave_run(cable, side)
        self.to_lobby(cable)
        until(cable, lambda: self.linked(cable), linked_limit)

    def phase_fight(self) -> None:
        cable = self.new_cable()
        host, guest = cable.host, cable.guest
        self.boot(cable)

        # Things made first (free play), so each brings its own bag.
        self.start_free_play(cable)
        for side in (host, guest):
            self.make_for_the_fight(cable, side, FIGHT_MIXES_HOST if side is host else FIGHT_MIXES_GUEST)
        host_found, guest_found = found(host, cable), found(guest, cable)
        self.into_versus(cable)
        self.lobby_set(cable, LinkMode.FIGHT, 0)
        cable.step(150)
        entry = self.pair(cable, "fight-0-rules", "FIGHT chosen on the host's rules; the guest shows it")
        self.check(
            "fight: FIGHT reached from the link rules",
            self.var(guest, "_link_mode") == LinkMode.FIGHT
            and "FIGHT" in " ".join(entry["guest"]["rows"] + entry["host"]["rows"]),
        )
        cable.press(host, "a", 30)
        self.bout(cable, "fight")
        self.vs_end(cable, "fight-over")

        # AGAIN on both: the next match.
        match = (self.var(host, "_lp_match"), self.var(guest, "_lp_match"))
        for side in (host, guest):
            cable.press(side, "a", 30)
        ok = until(
            cable,
            lambda: (
                self.var(host, "_lp_match") != match[0]
                and self.var(guest, "_lp_match") != match[1]
                and self.ui(host) in (FightUi.CHOOSE, FightUi.WAIT)
                and self.ui(guest) in (FightUi.CHOOSE, FightUi.WAIT)
            ),
            3000,
        )
        cable.step(30)
        self.pair(cable, "fight-again", "AGAIN on both: the next match opens")
        now = (self.var(host, "_lp_match"), self.var(guest, "_lp_match"))
        self.check(
            "fight: AGAIN on both starts a new match (fresh HP, round 1, same on both)",
            ok
            and host_order(self.cx_read(host), swapped=False)["s"] == host_order(self.cx_read(guest), swapped=True)["s"]
            and self.var(host, "_fight_round") == 0,
            f"match {match}->{now} ui {self.ui(host)}/{self.ui(guest)}",
        )
        moves, agree = self.bout(cable, "fight-r", check_events=False)
        self.check(
            f"fight: the rematch plays through, the same on both ({moves} moves)",
            agree and moves > 0,
            f"{self.mismatches[:2]}",
        )
        self.vs_end(cable, "fight-r-over")

        # LEAVE on both: the session ends on the result screen.
        for side in (host, guest):
            cable.press(side, "b", 30)
        entry = self.end_both(cable, "fight-end", "", "", 1500)
        host_result = entry["host"]["rows"][RESULT_ROW_INDEX]
        guest_result = entry["guest"]["rows"][RESULT_ROW_INDEX]
        mirrored = (
            ("YOU WIN" in host_result and "THEY WIN" in guest_result)
            or ("THEY WIN" in host_result and "YOU WIN" in guest_result)
            or ("A DRAW" in host_result and "A DRAW" in guest_result)
        )
        self.check("fight: the results mirror", mirrored, f"{host_result.strip()} / {guest_result.strip()}")
        for side in (host, guest):
            cable.press(side, "a", 120)

        # A second match, the cable pulled in the middle.
        self.to_lobby(cable)
        self.wait_linked(cable)
        self.lobby_set(cable, LinkMode.FIGHT, 0)
        cable.press(host, "a", 30)
        self.bout(cable, "fight2", pull_at=FIGHT_PULL_AT)
        # A FIGHT the cable broke counts for nobody.
        self.end_both(cable, "fight2-end", "NO CONTEST", "NO CONTEST", 2400)
        for side in (host, guest):
            cable.press(side, "a", 120)
        saves = (save_ram(host), save_ram(guest))
        stop(host, guest)

        cable = self.new_cable(saves)
        host, guest = cable.host, cable.guest
        self.boot(cable)
        self.start_free_play(cable)
        self.pair(cable, "fight-reboot", "both saves after two bouts, rebooted")
        self.check(
            "fight: both saves load",
            found(host, cable) >= host_found and found(guest, cable) >= guest_found,
            f"host {host_found}->{found(host, cable)} guest {guest_found}->{found(guest, cable)}",
        )
        stop(host, guest)

    def phase_fightnoise(self) -> None:
        """The versus on a bad wire: 1% of the bytes crossing arrive with a bit flipped and each waits 0..6 frames (a
        slow network bridge). The guest's engine is also corrupted once; it must be resynced from the host, and the
        match must still play to the end with both engines the same after every move."""
        host = Side(self.rom, "host", self.hooks)
        guest = Side(self.rom, "guest", self.hooks)
        cable = Cable(host, guest, noise=NOISE, delay=NOISE_DELAY, seed=self.noise_seed)
        self.boot(cable)
        self.start_free_play(cable)
        for side in (host, guest):
            have = self.owned(cable, side)
            for _ in range(NOISE_MIXES):
                self.mix(cable, side, have)
        self.into_versus(cable, linked_limit=1200)
        self.lobby_set(cable, LinkMode.FIGHT, 0)
        cable.step(300)
        cable.press(host, "a", 30)
        moves, agree = self.bout(cable, "noisy", check_events=False, poke_at=NOISE_POKE_AT)
        self.check(
            "noisy wire: the corrupted guest was resynced from the host (P_FSYNC)",
            self.var(guest, "_lp_desyncs") > 0,
            f"resyncs guest {self.var(guest, '_lp_desyncs')} host {self.var(host, '_lp_desyncs')}",
        )
        self.check(
            f"noisy wire: the match plays through, the same on both after every move ({moves} moves, "
            f"{cable.flips} bits flipped)",
            moves > 10 and agree,
            f"resyncs host {self.var(host, '_lp_desyncs')} guest {self.var(guest, '_lp_desyncs')}; "
            f"{self.mismatches[:3]}",
        )
        until(cable, lambda: self.ui(host) == FightUi.END and self.ui(guest) == FightUi.END, 12000)
        self.vs_end(cable, "noisy-over")
        for side in (host, guest):
            cable.press(side, "b", 30)
        entry = self.end_both(cable, "noisy-end", "", "", 12000)
        host_result = entry["host"]["rows"][RESULT_ROW_INDEX]
        guest_result = entry["guest"]["rows"][RESULT_ROW_INDEX]
        self.check(
            "noisy wire: the results mirror",
            ("YOU WIN" in host_result) != ("YOU WIN" in guest_result) or "NEITHER" in host_result,
            f"{host_result.strip()} / {guest_result.strip()}",
        )
        stop(host, guest)

    def run(self, phases: list[str]) -> None:
        for phase in phases:
            getattr(self, f"phase_{phase}")()

    def write_report(self) -> None:
        report = {
            "romSha256": hashlib.sha256(self.rom.read_bytes()).hexdigest(),
            "checks": self.checks,
            "log": self.log,
        }
        (self.out / "link.json").write_text(json.dumps(report, indent=1))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0] if __doc__ else None)
    parser.add_argument("rom", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--objects", type=Path, help="object files and listings (default: ROM's directory / obj)")
    parser.add_argument("--phases", default=",".join(PHASES), help="comma-separated, from: " + ",".join(PHASES))
    parser.add_argument("--catalogue", type=Path, default=catalogue.DEFAULT_DIR)
    parser.add_argument("--noise-seed", type=int, default=NOISE_SEED, help="the bad wire's seed (fightnoise)")
    args = parser.parse_args()
    unknown = [p for p in args.phases.split(",") if p not in PHASES]
    if unknown:
        parser.error(f"unknown phase(s): {', '.join(unknown)}")
    return args


def main() -> int:
    args = parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    rom: Path = args.rom
    objects: Path = args.objects or rom.parent / "obj"
    symbols = RomSymbols(rom, rom.with_suffix(".noi"), objects)
    capture = LinkCapture(rom, args.out, symbols, catalogue.load(args.catalogue), args.noise_seed)
    capture.run(args.phases.split(","))
    capture.write_report()
    failed = [c for c in capture.checks if not c["ok"]]
    print(f"{len(capture.checks) - len(failed)}/{len(capture.checks)} checks passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
