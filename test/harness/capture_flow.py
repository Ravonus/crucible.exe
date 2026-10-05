"""Play the game flow of docs/game-flow.md on the real ROM and check what the screen says at every step.

The ROM runs in headless PyBoy as a Game Boy Color, each game on its own blank save. Every step is captured as a
screenshot in OUTDIR, and OUTDIR/flow.json records the checks and a log of every capture (screen, SCY, the encounter
director's flow state, the header and cue rows).

Phases (all by default; --phases picks some, in the order given):
  menu     the title and pause menus over many boot timings: never TALK, never FIGHT
  free     a new player: the title (it asks nothing), PLAY > FREE PLAY without any date or birthday ask,
           the sign loader, the first bench (cue rows), the filter flow (SELECT, pick with A, the header shows it,
           B clears it; SELECT on the filter screen clears it), the first mix and discovery,
           the book, TITLES, STATS, SETUP; then 100+ discoveries (the living rooms unlock at 64) and the browse probe
           (every focus's animation finishes loading, no stalls); its save is kept for 'return'
  story    a story run: the bench, the pause menu and LEAVE, encounters arriving in play (forced and waiting, talk
           and fight), the approach with UP/DOWN, the pan there and back to the same bench, an ignored hint fading
  return   a returning player: the free save rebooted with a host clock five hours on (nothing asks; a voice waits
           at the bench), the story slot resumed

Story fights need a faction that dislikes you. To reach one in a short script, the run's standing with one faction is
set wary in RAM (the same state play reaches by making what that faction fears). Everything else is real input.

Usage:
  python test/harness/capture_flow.py ROM OUTDIR [--objects OBJDIR] [--phases menu,free,story,return]

ROM needs its linker map (ROM with a .noi suffix) beside it and the build's object listings in OBJDIR (default: the
ROM's directory / obj), which locate the cartridge's function-local statics. Set FLOW_DEBUG=1 to print every story
mix. The exit status is 1 when any check fails.
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import io
import json
import os
import sys
from collections.abc import Callable, Iterable
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Literal

from pyboy import PyBoy
from romsym import RomSymbols

import catalogue

# Screen ids: the value of crucible_run's `screen` static.
BENCH, MERGE, REVEAL, BOOK, RECORDS, MENU, FILTER, TALK, FIGHT, LINKEND = range(10)
SCREEN_NAMES = ("BENCH", "MERGE", "REVEAL", "BOOK", "RECORDS", "MENU", "FILTER", "TALK", "FIGHT", "LINKEND")
ENCOUNTER_SCREENS = (TALK, FIGHT)

# Offsets into the `core` state struct.
CORE_FOCUS = 0x1FC  # u16: the element the shelf is focused on
CORE_SLOT_A = 0x1FE  # u16: the element held for a mix, NO_SLOT when nothing is held
NO_SLOT = 0xFFFF

# Offsets into the story run's saga (struct crucible_story; SDCC lays it out without padding).
SAGA_STANDING = 58  # int8 stand[8]: the run's standing with each faction
SAGA_CHAPTER = 93
WARY = 0xE2  # -30 as int8: wary, as making what a faction fears would leave it

# The encounter director's state: one global byte each, named `flow_<field>`.
FLOW_FIELDS = ("hint", "force", "arg", "pending", "since", "count")
# Encounter kinds in flow_hint (waiting at the bench's edge) and flow_force (coming in, telegraphed).
FLOW_TALK = 1
FLOW_FIGHT = 2

# Game Boy hardware registers and OAM.
LCDC = 0xFF40
LCDC_OBJ_ENABLE = 0x02
SCY = 0xFF42
OAM = 0xFE00
OAM_ENTRY_SIZE = 4
OVERLAY_SPRITES = 24  # OAM 0..23: the material colour overlay
HINT_SPRITE = 38  # the encounter hint: the face peeking in above, or the lurker below
CURSOR_SPRITE = 39  # the menu cursor

# The cartridge keeps its state in WRAM bank 1 (0xD000..0xDFFF). The colour overlay borrows bank 7 mid-frame, and a
# plain read there follows whatever bank is selected at that moment, so reads in that window always name bank 1.
WRAM_BANKED = range(0xD000, 0xE000)
STATE_WRAM_BANK = 1

# Battery RAM: 16 banks of 8 KB at 0xA000..0xBFFF.
SRAM_BANKS = 16
SRAM_START = 0xA000
SRAM_LAST = 0xBFFF
SRAM_BANK_SIZE = 0x2000
SAVE_SIZE = SRAM_BANKS * SRAM_BANK_SIZE

# Tilemap text: the font's tiles 128..191 are ASCII 32..95; anything else reads as '~'.
FONT_FIRST_TILE = 128
FONT_END_TILE = 192
FONT_FIRST_CHAR = 32
SCREEN_COLUMNS = 20
HEADER_ROW = 0
CUE_ROW = 16  # what A, B and SELECT do
SIGN_ROW = 17  # START's hint, or an encounter's sign
MENU_ROWS = range(10, 15)
ASK_ROWS = range(9, 12)  # where the menu card shows the date, time and birthday asks
FILTER_DETAIL_ROW = 14

# The view is half way through a pan between the bench and an encounter (SCY away from 0 either way).
HALF_PAN = range(40, 91)
FOCUS_LOADED = 255  # focus_load once the focused element's animation has finished loading
ROOMS_SHOT_AT = 70  # owned elements by which the living rooms (unlocked at 64) are captured
TIME_AWAY_HOURS = 5
ASK_LIMIT = 16  # polls of the game start's asks before giving up on them
BIRTHDAY = (11, 4)  # month, day: entered with UP from the 1st of January the ask starts on
LOADER_SPRITES = 4  # the sign loader's mark: OAM 0..3, the first one tile 0

# The clock's time records and the host clock block in battery RAM (docs/time-awareness.md).
TIME_BANK = 15
TIME_RECORDS = (0x1F80, 0x1FA0)  # records A and B: the newer valid one wins
TIME_RECORD_SIZE = 32
TIME_MAGIC = b"CT"
TIME_COMMIT = 0xC7  # byte 27 of a committed record
HOST_CLOCK_AT = 0x1FE0
HOST_CLOCK_MAGIC = b"HCLK"
HOST_CLOCK_VERSION = 1
EPOCH = datetime.date(2000, 1, 1)  # the records count days from here
MINUTES_PER_DAY = 1440


@dataclass(frozen=True)
class Addresses:
    """Where the harness reads and writes the cartridge's state in this build."""

    screen: int
    focus_load: int
    saga: int
    core: int
    story_on: int
    flow: dict[str, int]

    @classmethod
    def resolve(cls, rom: Path, objects: Path) -> Addresses:
        sym = RomSymbols(rom, rom.with_suffix(".noi"), objects)
        return cls(
            screen=sym.pointer("crucible", "_crucible_run", "_screen"),
            focus_load=sym.pointer("crucible", "_crucible_run", "_focus_load"),
            saga=sym.pointer("crucible_talk", "_talk_saga", "_saga_"),
            core=sym.symbols["_core"],
            story_on=sym.symbols["_story_on"],
            flow={name: sym.symbols["_flow_" + name] for name in FLOW_FIELDS},
        )


@dataclass
class Check:
    name: str
    ok: bool
    note: str

    def as_json(self) -> dict[str, object]:
        return {"check": self.name, "ok": self.ok, "note": self.note}


@dataclass
class Shot:
    """One capture: the screenshot's file name and what the game showed when it was taken."""

    shot: str
    screen: str | int  # the screen's name, or its raw id if it is not a known screen
    scy: int
    flow: dict[str, int]
    rows: dict[int, str]
    overlay_oam: int  # overlay sprites on screen
    note: str

    def as_json(self) -> dict[str, object]:
        return {
            "shot": self.shot,
            "screen": self.screen,
            "scy": self.scy,
            "flow": self.flow,
            "rows": self.rows,
            "overlayOam": self.overlay_oam,
            "note": self.note,
        }


@dataclass
class Context:
    """What the phases share: the ROM, its addresses and catalogue, and the report being written."""

    rom: Path
    out: Path
    addr: Addresses
    catalogue: catalogue.Catalogue
    seeds: int
    discoveries: int
    browse: int
    checks: list[Check] = field(default_factory=list)
    log: list[Shot] = field(default_factory=list)
    saves: dict[str, bytes] = field(default_factory=dict)  # battery RAM by phase, for 'return'

    def check(self, name: str, ok: bool, note: str = "") -> None:
        self.checks.append(Check(name, ok, note))
        print(("PASS " if ok else "FAIL ") + name + (" - " + note if note else ""), flush=True)


class BankedMemory:
    """PyBoy's memory, with the cartridge's WRAM window always read and written in bank 1."""

    def __init__(self, memory: Any) -> None:  # noqa: ANN401 - pyboy ships no type information
        self.memory = memory

    def _key(self, address: int) -> int | tuple[int, int]:
        return (STATE_WRAM_BANK, address) if address in WRAM_BANKED else address

    def __getitem__(self, address: int) -> int:
        return int(self.memory[self._key(address)])

    def __setitem__(self, address: int, value: int) -> None:
        self.memory[self._key(address)] = value


MixResult = int | Literal["lost", "stuck"] | None


class Game:
    """One emulated cartridge and the moves a player makes on it."""

    def __init__(self, ctx: Context, tag: str, ram: bytes | None = None) -> None:
        self.ctx = ctx
        self.addr = ctx.addr
        self.tag = tag
        self.pb = PyBoy(
            str(ctx.rom),
            window="null",
            cgb=True,
            sound_emulated=False,
            ram_file=io.BytesIO(ram or bytes(SAVE_SIZE)),
            log_level="ERROR",
        )
        self.pb.set_emulation_speed(0)
        self.m = BankedMemory(self.pb.memory)
        self.frames = 0
        self.seen_asks: list[str] = []

    # ---- time and input ----

    def step(self, frames: int = 1) -> None:
        for _ in range(frames):
            self.pb.tick()
        self.frames += frames

    def pulse(self, button: str, after: int = 6) -> None:
        """Press a button for 3 frames, release it, then run `after` frames."""
        self.pb.button_press(button)
        self.step(3)
        self.pb.button_release(button)
        self.step(after)

    def until(self, condition: Callable[[], bool], limit: int = 3000) -> bool:
        """Run a frame at a time until `condition` holds, at most `limit` frames; whether it holds at the end."""
        for _ in range(limit):
            if condition():
                return True
            self.step(1)
        return condition()

    def wait_screen(self, screen: int, limit: int) -> bool:
        return self.until(lambda: self.screen == screen, limit)

    def wait_screen_in(self, screens: Iterable[int], limit: int) -> bool:
        wanted = tuple(screens)
        return self.until(lambda: self.screen in wanted, limit)

    def wait_menu_item(self, item: str, limit: int) -> bool:
        return self.until(lambda: item in self.menu_items(), limit)

    def stop(self) -> None:
        self.pb.stop(save=False)

    # ---- reading the game ----

    @property
    def screen(self) -> int:
        return self.m[self.addr.screen]

    def word(self, address: int) -> int:
        return self.m[address] | self.m[address + 1] << 8

    def focus(self) -> int:
        return self.word(self.addr.core + CORE_FOCUS)

    def slot_a(self) -> int:
        return self.word(self.addr.core + CORE_SLOT_A)

    def flow(self) -> dict[str, int]:
        return {name: self.m[address] for name, address in self.addr.flow.items()}

    def flow_byte(self, name: str) -> int:
        return self.m[self.addr.flow[name]]

    def saga(self, offset: int) -> int:
        return self.m[self.addr.saga + offset]

    def story_on(self) -> int:
        return self.m[self.addr.story_on]

    def scy(self) -> int:
        return self.m[SCY]

    def at_bench(self) -> bool:
        """On the bench with the view at rest (not panned toward an encounter)."""
        return self.screen == BENCH and self.scy() == 0

    def half_panned(self) -> bool:
        scy = self.scy()
        return min(scy, 256 - scy) in HALF_PAN

    def row(self, y: int) -> str:
        """Row y of the background tilemap as text."""
        tiles = self.pb.tilemap_background[0:SCREEN_COLUMNS, y]
        return "".join(
            chr(tile - FONT_FIRST_TILE + FONT_FIRST_CHAR) if FONT_FIRST_TILE <= tile < FONT_END_TILE else "~"
            for tile in tiles
        )

    def menu_items(self) -> str:
        return " ".join(self.row(y) for y in MENU_ROWS)

    def sprite_byte(self, sprite: int, byte: int = 0) -> int:
        return self.m[OAM + sprite * OAM_ENTRY_SIZE + byte]

    def sram(self) -> bytes:
        """The battery RAM as it stands: each bank's window as a slice up to 0xBFFF, then the byte at 0xBFFF."""
        memory = self.pb.memory
        return b"".join(
            bytes(memory[bank, SRAM_START:SRAM_LAST]) + bytes([memory[bank, SRAM_LAST]]) for bank in range(SRAM_BANKS)
        )

    def snap(self, name: str, note: str = "") -> Shot:
        """Run one frame, save the screen as OUTDIR/<tag>-<name>.png and log what it shows."""
        self.step(1)
        path = self.ctx.out / f"{self.tag}-{name}.png"
        self.pb.screen.image.convert("RGB").save(path)
        screen = self.screen
        shot = Shot(
            shot=path.name,
            screen=SCREEN_NAMES[screen] if screen < len(SCREEN_NAMES) else screen,
            scy=self.scy(),
            flow=self.flow(),
            rows={y: self.row(y) for y in (HEADER_ROW, CUE_ROW, SIGN_ROW)},
            overlay_oam=sum(1 for sprite in range(OVERLAY_SPRITES) if self.sprite_byte(sprite)),
            note=note,
        )
        self.ctx.log.append(shot)
        return shot

    # ---- getting around ----

    def boot(self, wait: int = 0, shot: bool = False) -> None:
        """Power on to the title card, which asks nothing (the time is asked at the first game start)."""
        self.wait_screen(MENU, 6000)
        self.step(240 + wait)
        if shot:
            first = self.snap("title-first", "the first power-on: the title card asks nothing")
            self.ctx.check(
                "title: no time ask on the title card",
                not any("WHAT" in row or "BIRTHDAY" in row for row in first.rows.values()),
                str(first.rows),
            )
        self.step(120)

    def asks(self, shots: bool = False, birthday: tuple[int, int] | Literal["skip"] | None = None) -> list[str]:
        """Answer a game start's asks: on a fresh cartridge the date and time, then the birthday, once each, then the
        sign loader opens the game. A keeps what is shown; `birthday` enters a (month, day), or 'skip' leaves it with
        B. Which asks came, in order."""
        seen: list[str] = []
        for _ in range(ASK_LIMIT):
            if self.screen != MENU:
                break
            text = " ".join(self.row(y) for y in ASK_ROWS)
            if "TODAY" in text:
                if shots:
                    self.snap("ask-date", "a game start asks the date and time (once)")
                seen.append("date")
                self.pulse("a", 30)
            elif "BIRTHDAY" in text:
                if isinstance(birthday, tuple):
                    month, day = birthday
                    for _ in range(month - 1):
                        self.pulse("up", 4)
                    self.pulse("right", 4)
                    for _ in range(day - 1):
                        self.pulse("up", 4)
                if shots:
                    self.snap("ask-birthday", "then the birthday (once; B skips)")
                seen.append("birthday")
                self.pulse("b" if birthday == "skip" else "a", 8)
                if shots:
                    self.step(24)
                    self.snap("loader", "the sign loader: your mark, the moon beside it")
                    marks = [self.sprite_byte(sprite) for sprite in range(LOADER_SPRITES + 1)]
                    self.ctx.check(
                        "loader: the sign mark shows while the game opens",
                        self.screen == MENU and all(marks[:LOADER_SPRITES]) and self.sprite_byte(0, 2) == 0,
                        str(marks),
                    )
            else:
                self.step(20)
        return seen

    def free_play(self, shots: bool = False, birthday: tuple[int, int] | Literal["skip"] | None = None) -> bool:
        """Title card > PLAY > FREE PLAY, through the game start's asks (kept in `seen_asks`), to the bench."""
        self.pulse("a")
        self.step(60)
        self.pulse("down")
        self.step(20)
        self.pulse("a")
        self.step(20)
        self.seen_asks = self.asks(shots, birthday)
        return self.wait_screen(BENCH, 3000)

    def story(self, shot: str | None = None) -> bool:
        """Title card > PLAY > STORY > the first slot, then through the run's opening lines to the bench."""
        self.pulse("a")
        self.step(60)
        self.pulse("a")
        self.step(60)
        if shot:
            self.snap(shot, "the story slots")
        self.pulse("a")
        self.step(60)
        for k in range(300):
            if self.screen == BENCH:
                return True
            self.pulse("start" if k % 5 == 4 else "a", 40)
        return False

    def settle(self, limit: int = 400) -> str:
        """Back to the bench through any reveal, encounter or machine line (A): 'bench', or 'menu' if the run ended."""
        for _ in range(limit):
            screen = self.screen
            if screen == BENCH and self.scy() == 0:
                return "bench"
            if screen == MENU:
                return "menu"
            if screen in (REVEAL, TALK, FIGHT, LINKEND):
                self.pulse("a", 20)
            else:
                self.step(10)
        return SCREEN_NAMES[self.screen]

    # ---- the shelf ----

    def right(self) -> int:
        """One step along the shelf. A busy frame (art decoding) can take a while to read the press."""
        was = self.focus()
        self.pulse("right", 2)
        self.until(lambda: self.focus() != was, 120)
        return self.focus()

    def owned(self) -> set[int]:
        """Walk the whole shelf once. A visitor stepping in mid-walk is played through and the walk restarts."""
        seen: list[int] = []
        for _ in range(4):
            self.settle()
            start = self.focus()
            seen = [start]
            for _ in range(4000):
                focus = self.right()
                if self.screen != BENCH or self.scy():
                    break
                if focus == start and len(seen) == 1:
                    continue  # the press was not read yet (a busy frame): press again
                if focus == start:
                    self.step(20)
                    return set(seen)
                seen.append(focus)
        return set(seen)

    def goto(self, target: int, shelf_size: int) -> bool:
        for _ in range(shelf_size + 2):
            if self.focus() == target:
                return True
            self.right()
        return self.focus() == target

    def mix(self, owned: set[int], shown_categories: set[int] | None = None) -> MixResult:
        """Make the first recipe whose result is not owned yet; its result, or None when there is none left.

        With `shown_categories`, results in a category not shown yet come first. 'lost' when an ingredient could not
        be found on the shelf (`owned` is then refreshed), 'stuck' when the mix did not start.
        """
        recipes = self.ctx.catalogue.recipes
        todo = [(x, y, r) for x, y, r in recipes if x in owned and y in owned and r not in owned]
        if not todo:
            return None
        todo.sort(
            key=lambda t: (
                shown_categories is not None and self.ctx.catalogue.category_index(t[2]) in shown_categories,
                t[2],
            )
        )
        x, y, result = todo[0]
        if self.slot_a() != NO_SLOT:
            self.pulse("b", 20)  # put back what is held
        if not self.goto(x, len(owned)):
            now = self.owned()
            owned.clear()
            owned.update(now)
            return "lost"
        self.pulse("a", 20)
        if x == y:
            self.pulse("a")
        else:
            if not self.goto(y, len(owned)):
                self.pulse("b", 30)
                return "lost"
            self.pulse("a")
        if not self.until(lambda: self.screen != BENCH, 240):
            self.pulse("b", 30)
            return "stuck"
        self.wait_screen_in((REVEAL, BENCH, TALK, MENU), 4000)
        return result


# ---- encounters: the hint or telegraph, the pan there, the scene, the pan back, the bench after ----


def mid_pan(g: Game, name: str, note: str, limit: int = 240) -> Shot | None:
    """Run a frame at a time until the view is half way through a pan, and capture it."""
    for _ in range(limit):
        if g.half_panned():
            return g.snap(name, note)
        g.step(1)
    return None


def check_sign(g: Game, tag: str, forced: bool, fight: bool) -> None:
    """Capture the bench as the encounter announces itself and check the sign row and the hint sprite."""
    shot = g.snap(
        f"{tag}-{'telegraph' if forced else 'hint'}",
        ("forced " if forced else "waiting ") + ("fight" if fight else "talk"),
    )
    for _ in range(3):
        partial = shot.rows[SIGN_ROW].strip("~ ")
        if "~" in partial or not partial:
            g.step(8)  # read mid-redraw (or on a blank frame): again
            shot.rows[SIGN_ROW] = g.row(SIGN_ROW)
    sign = shot.rows[SIGN_ROW]
    # While an element is held, a waiting visitor shows by its sprite alone (the sign row keeps the mix cues).
    held = g.slot_a() != NO_SLOT
    says = ("COMES" in sign or "STEPS IN" in sign) if forced else (held or "UP:" in sign or "DOWN:" in sign)
    g.ctx.check(
        f"{tag}: sign row says it",
        says,
        sign.strip() + (" (holding: the sprite alone)" if held and not forced else ""),
    )
    sprite_shows = any(g.sprite_byte(HINT_SPRITE, i) for i in range(2))
    g.ctx.check(f"{tag}: hint sprite shows", sprite_shows or forced, "OAM 38")


def play_out(g: Game, tag: str) -> Shot | None:
    """Answer the encounter until it ends: in a fight A with the focus (now and then RIGHT to another element), A
    through a talk. Captures the pan back to the bench if it is seen half way."""
    back = None
    for k in range(600):
        if g.screen not in ENCOUNTER_SCREENS:
            break
        if g.screen == FIGHT:
            g.pulse("right" if k % 4 == 3 else "a", 0)
        else:
            g.pulse("a", 0)
        for _ in range(60 if g.screen == FIGHT else 25):
            g.step(1)
            if back is None and g.screen == BENCH and g.flow()["pending"] == 0 and g.half_panned():
                back = g.snap(f"{tag}-return", "the view pans back to the bench")
            if g.screen not in ENCOUNTER_SCREENS and g.scy() == 0 and g.screen != BENCH:
                break
    return back


def encounter(g: Game, tag: str, how: Literal["approach", "wait"]) -> bool:
    """Play an encounter announced on the bench (flow force or hint set) and check it returns to the same bench.

    `how`: 'approach' presses toward it (UP for a talk, DOWN for a fight), 'wait' lets it come. Whether it was a fight.
    """
    if g.screen != BENCH:
        # A secret's visitor walked in first: its talk ends, and the encounter may still be waiting.
        g.settle()
        g.step(30)
    flow = g.flow()
    kind = flow["force"] or flow["hint"]
    if not kind:
        return False
    forced = bool(flow["force"])
    fight = kind == FLOW_FIGHT
    before = (g.focus(), g.slot_a())
    check_sign(g, tag, forced, fight)
    if how == "approach":
        g.pulse("down" if fight else "up", 0)
    pan = mid_pan(g, f"{tag}-pan", "the view pans " + ("down" if fight else "up"), 400)
    g.ctx.check(f"{tag}: the view pans", pan is not None)
    g.wait_screen_in(ENCOUNTER_SCREENS, 600)
    g.step(150)
    scene = g.snap(f"{tag}-{'fight' if g.screen == FIGHT else 'talk'}", "the encounter")
    g.ctx.check(
        f"{tag}: arrives as a {'fight' if fight else 'talk'}", g.screen == (FIGHT if fight else TALK), str(scene.screen)
    )
    back = play_out(g, tag)
    if back is None:
        back = mid_pan(g, f"{tag}-return", "the view pans back to the bench", 300)
    g.until(g.at_bench, 600)
    g.step(90)
    after = g.snap(f"{tag}-after", "back on the bench")
    if g.screen != BENCH:
        g.ctx.check(f"{tag}: ended the run", g.screen == MENU, str(after.screen))
        return fight
    g.ctx.check(f"{tag}: pans back", back is not None)
    # A fight can steal the focused element and a talk can take it as a gift; then (and only then) the focus moves on.
    same = (g.focus(), g.slot_a()) == before
    taken = not same and g.slot_a() == before[1] and before[0] not in g.owned()
    g.ctx.check(
        f"{tag}: same bench after",
        same or taken,
        f"focus/slot {before} -> {(g.focus(), g.slot_a())}" + (" (the focus element was taken)" if taken else ""),
    )
    return fight


# ---- phase: menu ----


def phase_menu(ctx: Context) -> None:
    for seed in range(ctx.seeds):
        g = Game(ctx, f"menu{seed}")
        g.boot(wait=(seed * 53) % 400, shot=seed == 0)
        title = g.menu_items()
        if seed == 0:
            g.snap("title", "the title menu")
        g.free_play()
        g.step(200 + seed * 17)
        g.settle()
        g.step(30)
        g.pulse("start")
        g.wait_screen(MENU, 600)
        g.step(60)
        g.wait_menu_item("RESUME", 300)  # a busy frame can hold back the menu's draw
        pause = g.menu_items()
        lcdc = g.m[LCDC]
        ctx.check(
            f"seed {seed} pause: cursor and sprites show",
            bool(lcdc & LCDC_OBJ_ENABLE) and g.sprite_byte(CURSOR_SPRITE) > 0,
            f"LCDC {lcdc:#x}",
        )
        if seed == 0:
            g.snap("pause", "the pause menu")
        for label, items in (("title", title), ("pause", pause)):
            ctx.check(
                f"seed {seed} {label}: no TALK or FIGHT",
                "TALK" not in items and "FIGHT" not in items,
                " ".join(items.split()),
            )
        ctx.check(
            f"seed {seed}: menus list PLAY/RESUME BOOK TITLES STATS SETUP",
            all(word in title for word in ("PLAY", "BOOK", "TITLES", "STATS", "SETUP"))
            and "RESUME" in pause
            and "LEAVE" in pause,
        )
        g.stop()


# ---- phase: free ----


def free_first_bench(g: Game) -> None:
    g.boot(shot=True)
    g.snap("title")
    g.pulse("a")
    g.step(60)
    g.snap("play", "PLAY: story, free play or link")
    g.pulse("down")
    g.step(20)
    g.pulse("a")
    g.step(20)
    asks = g.asks(shots=True, birthday=BIRTHDAY)
    g.ctx.check(
        "free: no date or birthday ask",
        asks == [],
        str(asks),
    )
    g.wait_screen(BENCH, 3000)
    g.step(500)
    shot = g.snap("bench", "the first bench")
    cues, sign = shot.rows[CUE_ROW], shot.rows[SIGN_ROW]
    g.ctx.check("free: cue row", "ADD" in cues and "SE FILTER" in cues, cues)
    g.ctx.check("free: START hint", "ST MENU" in sign, sign.strip())


def free_filter(g: Game) -> None:
    """SELECT opens the filter grid, A picks one, the header shows it, B (nothing held) clears it."""
    ctx = g.ctx
    g.pulse("select")
    g.wait_screen(FILTER, 300)
    g.step(30)
    g.snap("filter", "SELECT: the filter grid, cursor on the filter in use")
    g.pulse("down")
    g.step(20)
    g.snap("filter-cursor", "the cursor row lit; the line below counts it")
    ctx.check("filter: detail line", "ELEMENT" in g.row(FILTER_DETAIL_ROW), g.row(FILTER_DETAIL_ROW).strip())
    g.pulse("right")
    g.step(20)
    g.snap("filter-traits", "RIGHT: the traits column")
    g.pulse("left")
    g.step(20)
    g.pulse("a")
    g.wait_screen(BENCH, 300)
    g.until(lambda: "ELEMENT" in g.row(HEADER_ROW), 300)
    g.step(60)
    shot = g.snap("bench-filtered", "the bench header shows the filter; B ALL clears it")
    ctx.check("filter: bench header shows it", "ELEMENT" in shot.rows[HEADER_ROW], shot.rows[HEADER_ROW])
    ctx.check("filter: B ALL cue", "ALL" in shot.rows[CUE_ROW], shot.rows[CUE_ROW])
    g.pulse("b")
    g.step(60)
    shot = g.snap("bench-unfiltered", "B with nothing held: back to ALL")
    ctx.check("filter: B clears it", "ELEMENT" not in shot.rows[HEADER_ROW], shot.rows[HEADER_ROW])

    # Pick the filter again, then SELECT on the filter screen clears it.
    g.pulse("select")
    g.wait_screen(FILTER, 300)
    g.pulse("down")
    g.pulse("a")
    g.wait_screen(BENCH, 300)
    g.step(30)
    g.pulse("select")
    g.wait_screen(FILTER, 300)
    g.step(20)
    g.snap("filter-again", "the star marks the filter in use")
    g.pulse("select")
    g.wait_screen(BENCH, 300)
    g.step(60)
    shot = g.snap("bench-select-cleared", "SELECT on the filter screen clears it")
    ctx.check("filter: SELECT clears it", "ELEMENT" not in shot.rows[HEADER_ROW], shot.rows[HEADER_ROW])


def free_first_mix(g: Game) -> set[int]:
    """The first mix and its discovery; what is owned after it."""
    owned = g.owned()
    result = g.mix(owned)
    g.step(60)
    shot = g.snap("first-mix", "the first discovery")
    if isinstance(result, int):
        owned.add(result)
    g.ctx.check("free: first discovery reveal", shot.screen == "REVEAL", str(shot.screen))
    g.settle()
    g.step(120)
    g.snap("after-first", "back on the bench")
    return owned


def free_menu_screens(g: Game) -> None:
    """START, then each item of the pause menu (a 2-column grid: RESUME BOOK / TITLES STATS / SETUP LEAVE)."""
    g.pulse("start")
    g.wait_screen(MENU, 300)
    g.step(30)
    g.pulse("right")
    g.step(10)
    g.pulse("a")
    g.wait_screen(BOOK, 300)
    g.step(120)
    shot = g.snap("book", "the book from the menu")
    cues = shot.rows[CUE_ROW]
    g.ctx.check("book: cue row", "USE" in cues and "SE FILTER" in cues, cues)
    g.pulse("b")
    g.wait_screen(MENU, 300)
    g.step(30)

    g.pulse("down")
    g.step(10)
    g.pulse("a")
    g.wait_screen(RECORDS, 300)
    g.step(60)
    g.snap("titles", "TITLES")
    g.pulse("b")
    g.wait_screen(MENU, 300)

    g.pulse("down")
    g.step(10)
    g.pulse("right")
    g.step(10)
    g.pulse("a")
    g.wait_screen(RECORDS, 300)
    g.step(60)
    g.snap("stats", "STATS")
    g.pulse("b")
    g.wait_screen(MENU, 300)

    g.pulse("down")
    g.step(10)
    g.pulse("down")
    g.step(10)
    g.pulse("a")
    g.step(60)
    g.snap("setup", "SETUP")
    g.pulse("b")
    g.step(30)

    # Up to RESUME, back to the bench.
    g.pulse("up")
    g.pulse("up")
    g.step(10)
    g.pulse("a")
    g.wait_screen(BENCH, 300)
    g.step(60)


def free_discoveries(g: Game, owned: set[int]) -> None:
    """Mix on to 100+ discoveries, preferring new categories. Visitors may come: in free play only talks, never
    fights; the first two are played through."""
    talks = 0
    shown: set[int] = set()
    rooms_shot = False
    for _ in range(g.ctx.discoveries):
        result = g.mix(owned, shown)
        if result is None:
            break
        if isinstance(result, int):
            owned.add(result)
            shown.add(g.ctx.catalogue.category_index(result))
        g.settle()
        g.step(30)
        flow = g.flow()
        kind = flow["force"] or flow["hint"]
        if kind and g.screen == BENCH:
            g.ctx.check("free: no fights in free play", kind == FLOW_TALK)
            if talks < 2:
                encounter(g, f"free-talk{talks}", "approach" if flow["hint"] else "wait")
                talks += 1
        if len(owned) >= ROOMS_SHOT_AT and not rooms_shot:
            g.step(300)
            g.snap("rooms", f"{len(owned)} owned: the living rooms are unlocked")
            rooms_shot = True
    g.ctx.check("free: discoveries", len(owned) >= 100, f"{len(owned)} owned")


def free_browse(g: Game) -> None:
    """Step the focus along the shelf (and down a row every 7th step): every focus's animation must finish loading."""
    browse = g.ctx.browse
    stalls = 0
    worst = 0
    for k in range(browse):
        g.pulse("right" if k % 7 else "down", 2)
        g.step(12)
        start = g.frames
        loaded = g.until(lambda: g.m[g.addr.focus_load] == FOCUS_LOADED, 1500)
        worst = max(worst, g.frames - start)
        if not loaded:
            stalls += 1
            g.snap(f"stall{stalls}", f"focus {g.focus()} never loaded")
    shot = g.snap("browse-end", "after the browse probe")
    g.ctx.check("browse: no stalls", stalls == 0, f"{browse} objects, worst {worst} frames to load")
    g.ctx.check("overlay colours still show", shot.overlay_oam > 0, f"{shot.overlay_oam} overlay sprites")


def phase_free(ctx: Context) -> None:
    g = Game(ctx, "free")
    free_first_bench(g)
    free_filter(g)
    owned = free_first_mix(g)
    free_menu_screens(g)
    free_discoveries(g, owned)
    free_browse(g)
    ctx.saves["free"] = g.sram()
    g.stop()


# ---- phase: story ----


def story_start(g: Game) -> None:
    """A new run to its bench, the pause menu, LEAVE to the title card, and the slot resuming the run."""
    ctx = g.ctx
    reached = g.story(shot="slots")
    g.step(400)
    ctx.check("story: reached the bench", reached)
    g.snap("bench", "a new run: the story bench")
    g.pulse("start")
    g.wait_screen(MENU, 300)
    g.step(60)
    items = g.menu_items()
    g.snap("pause", "the pause menu in a run")
    ctx.check("story pause: no TALK or FIGHT", "TALK" not in items and "FIGHT" not in items, " ".join(items.split()))
    # LEAVE is the pause menu's way back to the title card (the run is saved); then the slot resumes it.
    for button in ("down", "down", "right"):
        g.pulse(button)
        g.step(10)
    g.snap("leave", "the pause menu: LEAVE")
    g.pulse("a")
    g.step(120)
    items = g.menu_items()
    g.snap("left", "back on the title card")
    ctx.check(
        "story: LEAVE returns to the title card, the run saved",
        "PLAY" in items and g.story_on() == 0,
        " ".join(items.split()),
    )
    ctx.check("story: the slot resumes it", g.story() and g.story_on() == 1)
    g.step(300)


def story_fade_hint(g: Game) -> None:
    """Leave a waiting hint alone until it fades."""
    g.snap("ignored-hint", "a waiting hint, left alone")
    g.until(lambda: not g.flow_byte("hint"), 1600)
    g.step(10)
    g.snap("faded", "ignored, it fades")
    g.ctx.check("story: an ignored hint fades", not g.flow_byte("hint"))


def story_encounters(g: Game) -> tuple[dict[str, int], str]:
    """Mix until encounters of every kind have come; what came, and why the loop stopped."""
    owned = g.owned()
    got = {"talk": 0, "fight": 0, "approach": 0, "forced": 0, "faded": 0}
    ignore_next = False  # after an approached hint, the next hint is left alone to fade
    why = "mixes ran out"
    mixes = 0
    for n in range(260):
        mixes = n + 1
        result = g.mix(owned)
        if result is None:
            why = "no recipe left"
            break
        if isinstance(result, int):
            owned.add(result)
        settled = g.settle()
        g.step(20)
        if os.getenv("FLOW_DEBUG"):
            standing = [g.saga(SAGA_STANDING + i) for i in range(6)]
            chapter = g.saga(SAGA_CHAPTER)
            print(f"mix {n} {result} {settled} {g.flow()} owned {len(owned)} stand {standing} ch {chapter}", flush=True)
        if settled == "menu":
            print("story: the run was lost (fights answered blindly by the script)", flush=True)
            why = "the run was lost"
            break
        flow = g.flow()
        kind = flow["force"] or flow["hint"]
        if not kind:
            continue
        if flow["hint"] and ignore_next and not got["faded"]:
            story_fade_hint(g)
            got["faded"] = 1
            continue
        chapter = g.saga(SAGA_CHAPTER)
        tag = f"story-{'fight' if kind == FLOW_FIGHT else 'talk'}{got['fight'] + got['talk']}-ch{chapter}"
        fight = encounter(g, tag, "approach" if flow["hint"] else "wait")
        got["fight" if fight else "talk"] += 1
        got["approach" if flow["hint"] else "forced"] += 1
        if flow["hint"]:
            ignore_next = True
        if g.screen != BENCH:
            why = "the run was lost" if g.screen == MENU else "left the bench: " + SCREEN_NAMES[g.screen]
            break
        now = g.owned()
        owned.clear()
        owned.update(now)
        if all(got.values()) and got["talk"] + got["fight"] >= 4 and g.saga(SAGA_CHAPTER) >= 2:
            break
    print("story loop:", mixes, "mixes,", why, json.dumps(got), flush=True)
    return got, why


def phase_story(ctx: Context) -> None:
    g = Game(ctx, "story")
    g.boot()
    story_start(g)
    # One faction turns wary, as making what it fears would: its rivals lurk, and may grow hostile.
    g.m[g.addr.saga + SAGA_STANDING + 1] = WARY
    got, why = story_encounters(g)
    ctx.check("story: a talk arrived in play", got["talk"] > 0, json.dumps(got))
    ctx.check("story: a fight arrived in play", got["fight"] > 0)
    ctx.check("story: an approach (UP/DOWN)", got["approach"] > 0)
    ctx.check("story: a forced encounter", got["forced"] > 0)
    chapter = g.saga(SAGA_CHAPTER)
    if why == "the run was lost" and not chapter:
        print("story: chapter not reached (the run was lost first)", flush=True)
    else:
        ctx.check("story: chapter moved", chapter > 0, f"chapter {chapter}")
    ctx.saves["story"] = g.sram()
    g.stop()


# ---- phase: return ----


def phase_return(ctx: Context) -> None:
    if "free" in ctx.saves:
        # A host (the website's emulator) leaves a host clock block hours after the saved clock: nothing asks, and the
        # time away is real.
        ram = bytearray(ctx.saves["free"])
        date, minute = saved_clock(ram)
        write_host_clock(ram, date, minute + TIME_AWAY_HOURS * 60)
        g = Game(ctx, "return", bytes(ram))
        g.wait_screen(MENU, 6000)
        g.step(240)
        shot = g.snap("title-return", "coming back: the title card asks nothing")
        ctx.check("return: no ask on the title", not any("WHAT" in row for row in shot.rows.values()))
        g.free_play()
        ctx.check("return: no ask at the game start either", not g.seen_asks, str(g.seen_asks))
        g.until(lambda: g.flow_byte("hint") == FLOW_TALK, 900)
        g.step(30)
        shot = g.snap("bench", "back after hours: someone waits above")
        ctx.check(
            "return: a voice waits after time away", g.flow_byte("hint") == FLOW_TALK, shot.rows[SIGN_ROW].strip()
        )
        g.stop()
    if "story" in ctx.saves:
        g = Game(ctx, "resume", ctx.saves["story"])
        g.boot()
        resumed = g.story(shot="slots")
        g.step(300)
        g.snap("bench", "the saved run resumed")
        ctx.check("resume: the story slot loads", resumed and g.story_on() == 1)
        g.stop()


def saved_clock(ram: bytes | bytearray) -> tuple[int, int]:
    """The newest valid time record's date (days since 2000-01-01) and minute of the day."""
    best: bytes | bytearray | None = None
    for offset in TIME_RECORDS:
        at = TIME_BANK * SRAM_BANK_SIZE + offset
        record = ram[at : at + TIME_RECORD_SIZE]
        if record[0:2] != TIME_MAGIC or record[27] != TIME_COMMIT:
            continue
        # The serial wraps: the newer record is the one at most half the range ahead.
        if best is None or (le16(record, 4) - le16(best, 4)) & 0x8000 == 0:
            best = record
    if best is None:
        raise ValueError("the save has no valid time record")
    return le16(best, 10), le16(best, 6)


def write_host_clock(ram: bytearray, date: int, minute: int) -> None:
    """Write the host clock block for `date` (days since 2000-01-01) plus `minute` (may run past the day)."""
    date += minute // MINUTES_PER_DAY
    minute %= MINUTES_PER_DAY
    day = EPOCH + datetime.timedelta(days=date)
    block = bytearray(HOST_CLOCK_MAGIC)
    block += bytes([HOST_CLOCK_VERSION, 0, day.year & 0xFF, day.year >> 8, day.month, day.day])
    block += bytes([minute // 60, minute % 60, 0, 0])  # hour, minute, second, reserved
    block += crc16_ccitt(block).to_bytes(2, "little")
    at = TIME_BANK * SRAM_BANK_SIZE + HOST_CLOCK_AT
    ram[at : at + len(block)] = block


def crc16_ccitt(data: bytes | bytearray) -> int:
    """CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final XOR."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def le16(data: bytes | bytearray, at: int) -> int:
    return data[at] | data[at + 1] << 8


PHASES: dict[str, Callable[[Context], None]] = {
    "menu": phase_menu,
    "free": phase_free,
    "story": phase_story,
    "return": phase_return,
}


def main() -> int:
    parser = argparse.ArgumentParser(description="Play the CRUCIBLE.EXE game flow on a ROM and check every step.")
    parser.add_argument("rom", type=Path, help="the built ROM (crucible.gbc), with its .noi beside it")
    parser.add_argument("out", type=Path, help="where the screenshots and flow.json go")
    parser.add_argument("--objects", type=Path, help="the build's object listings (default: ROM's directory / obj)")
    parser.add_argument("--catalogue", type=Path, default=catalogue.DEFAULT_DIR, help="items.json and recipes.json")
    parser.add_argument("--phases", default="menu,free,story,return")
    parser.add_argument("--seeds", type=int, default=12, help="boot timings in the menu phase")
    parser.add_argument("--discoveries", type=int, default=110, help="mixes in the free phase")
    parser.add_argument("--browse", type=int, default=120, help="focus steps in the browse probe")
    args = parser.parse_args()

    rom: Path = args.rom
    out: Path = args.out
    out.mkdir(parents=True, exist_ok=True)
    objects: Path = args.objects or rom.parent / "obj"
    ctx = Context(
        rom=rom,
        out=out,
        addr=Addresses.resolve(rom, objects),
        catalogue=catalogue.load(args.catalogue),
        seeds=args.seeds,
        discoveries=args.discoveries,
        browse=args.browse,
    )
    for phase in args.phases.split(","):
        PHASES[phase](ctx)

    report = {
        "romSha256": hashlib.sha256(rom.read_bytes()).hexdigest(),
        "checks": [c.as_json() for c in ctx.checks],
        "log": [shot.as_json() for shot in ctx.log],
    }
    (out / "flow.json").write_text(json.dumps(report, indent=1))
    failed = sum(1 for c in ctx.checks if not c.ok)
    print(f"{len(ctx.checks) - failed}/{len(ctx.checks)} checks passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
