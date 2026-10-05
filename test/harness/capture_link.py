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
  fight   FIGHT: a whole match (best of 3), both machines' fight state (HP, turn, rng) compared every turn; then a
          second bout with the cable pulled mid-bout (NO CONTEST)

Writes OUTDIR/<step>.png for every pair and OUTDIR/link.json (ROM hash, checks, per-pair screen rows). Exits 1 when any
check fails.

Usage: python test/harness/capture_link.py ROM OUTDIR [--objects OBJDIR] [--phases bridge,coop,race,draw,lost,fight]
       [--catalogue DIR]
"""

from __future__ import annotations

import argparse
import functools
import hashlib
import json
import sys
from collections.abc import Callable, Iterable
from enum import IntEnum
from pathlib import Path
from typing import Any

from linkbridge import ADDRESS_MASK, Cable, LinkHooks, Side, link_hooks
from PIL import Image
from romsym import RomSymbols

import catalogue

PHASES = ("bridge", "coop", "race", "draw", "lost", "fight")


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

# The font's printable tiles: tile 128 + n is ASCII 32 + n.
FONT_FIRST_TILE = 128
FONT_END_TILE = 192
FONT_TILE_TO_ASCII = 96

# The versus engine's state, `fr` (fr_state in crucible_fight_rules.h): two 160-byte sides, then the shared fields.
FR_SIDE_SIZE = 160
FR_SIDE_HP = 75
FR_RNG = 320  # u16
FR_TURN = 324


class FightUi(IntEnum):
    """Values of `fight_ui` that the bout waits on."""

    CHOOSE = 1
    SHOW = 3
    END = 4


MAX_BOUT_TURNS = 80
PAIRED_TURNS = 2  # turns captured as pairs; the rest are only compared

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
    """Background row y as text; tiles outside the font read as '~'."""
    tiles = side.pb.tilemap_background[0:SCREEN_COLUMNS, y]
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


class LinkCapture:
    def __init__(self, rom: Path, out: Path, symbols: RomSymbols, recipes: list[tuple[int, int, int]]) -> None:
        self.rom = rom
        self.out = out
        self.symbols = symbols.symbols
        self.hooks: LinkHooks = link_hooks(symbols)
        self.screen_at = symbols.pointer("crucible", "_crucible_run", "_screen")
        self.core = self.symbols["_core"]
        self.fight = self.symbols["_fr"] & ADDRESS_MASK
        self.recipes = recipes
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

    def fight_turn(self, side: Side) -> int:
        return side.m[self.fight + FR_TURN]

    def fight_state(self, side: Side, first: int) -> tuple[int, int, int, int]:
        """(HP of the host's side, HP of the guest's side, turn, the shared rng) from this machine's engine, whose
        side `first` is the host's."""
        fr = self.fight

        def hp(k: int) -> int:
            return side.m[fr + k * FR_SIDE_SIZE + FR_SIDE_HP]

        return (hp(first), hp(1 - first), self.fight_turn(side), self.word(side, fr + FR_RNG))

    def turn_played(self, cable: Cable, before: int) -> bool:
        """Both engines moved on from turn `before`, or the host's bout is over."""
        host, guest = cable.host, cable.guest
        if self.screen(host) != Screen.FIGHT:
            return True
        return self.fight_turn(host) != before and self.fight_turn(guest) != before

    def both_choosing(self, cable: Cable) -> bool:
        """Both pick a card now, or the bout is over on either."""
        host, guest = cable.host, cable.guest
        if self.screen(host) != Screen.FIGHT or self.screen(guest) != Screen.FIGHT:
            return True
        return self.var(host, "_fight_ui") == FightUi.CHOOSE and self.var(guest, "_fight_ui") == FightUi.CHOOSE

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
        host_rows = " ".join(entry["host"]["rows"])
        guest_rows = " ".join(entry["guest"]["rows"])
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
        self.check(
            "coop: LINKED on both (the cable row, the partner's name)",
            self.linked(cable)
            and "[#]" in host_row
            and "[#]" in guest_row
            and "YOU" in host_row
            and "YOU" in guest_row,
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

    def bout(self, cable: Cable, name: str, pull_at: int | None = None) -> tuple[int, bool]:
        """Play a whole match (best of 3), or until the cable is pulled at turn `pull_at`, comparing both machines'
        engines every turn. Pairs are captured for the first turns only."""
        host, guest = cable.host, cable.guest
        ok = until(cable, lambda: self.both_on(cable, Screen.FIGHT), 900)
        cable.step(120)
        self.pair(cable, f"{name}-1-start", "the bout opens on both")
        self.check(f"{name}: both in the bout", ok)
        turns, agree = 0, True
        mismatches: list[tuple[int, tuple[int, int, int, int], tuple[int, int, int, int]]] = []
        for turn in range(MAX_BOUT_TURNS):
            if not until(cable, lambda: self.both_choosing(cable), 3000):
                break
            if self.screen(host) != Screen.FIGHT or self.screen(guest) != Screen.FIGHT:
                break
            if pull_at is not None and turn == pull_at:
                cable.connected = False
                self.pair(cable, f"{name}-pulled", "the cable is pulled mid-bout")
                break
            before = self.fight_turn(host)
            # Different picks on each side, varying by turn.
            for _ in range(turn % 3):
                cable.press(host, "right", 8)
            for _ in range((turn + 1) % 4):
                cable.press(guest, "right", 8)
            if turn < PAIRED_TURNS:
                self.pair(cable, f"{name}-t{turn}-pick", f"turn {turn}: picking")
            cable.press(host, "a", 10)
            cable.press(guest, "a", 10)
            until(cable, functools.partial(self.turn_played, cable, before), 2400)
            cable.step(30)
            # The guest's engine is swapped: its side 1 is the host's side 0.
            host_state = self.fight_state(host, 0)
            guest_state = self.fight_state(guest, 1)
            same = host_state == guest_state
            agree &= same
            turns += 1
            if not same:
                mismatches.append((turn, host_state, guest_state))
            if turn < PAIRED_TURNS:
                self.pair(
                    cable,
                    f"{name}-t{turn}-clash",
                    f"turn {host_state[2]}: HP host {host_state[0]} guest {host_state[1]}",
                )
            for side in (host, guest):
                if self.screen(side) == Screen.FIGHT and self.var(side, "_fight_ui") in (FightUi.SHOW, FightUi.END):
                    cable.press(side, "a", 10)
            cable.step(30)
        if pull_at is None:
            self.check(
                f"{name}: every turn the same on both ({turns} turns, a best of 3)",
                agree and turns > 0,
                f"{mismatches[:1]}",
            )
        return turns, agree

    def phase_fight(self) -> None:
        cable = self.new_cable()
        host, guest = cable.host, cable.guest
        self.boot(cable)

        # A few things each first (free play), so each brings its own.
        self.start_free_play(cable)
        for side in (host, guest):
            have = self.owned(cable, side)
            for _ in range(4 if side is host else 2):
                self.mix(cable, side, have)
        host_found, guest_found = found(host, cable), found(guest, cable)
        for side in (host, guest):
            self.leave_run(cable, side)

        self.to_lobby(cable)
        self.wait_linked(cable)
        self.lobby_set(cable, LinkMode.FIGHT, 0)
        cable.step(150)
        entry = self.pair(cable, "fight-0-rules", "FIGHT chosen on the host's rules; the guest shows it")
        self.check(
            "fight: FIGHT reached from the link rules",
            self.var(guest, "_link_mode") == LinkMode.FIGHT
            and "FIGHT" in " ".join(entry["guest"]["rows"] + entry["host"]["rows"]),
        )
        cable.press(host, "a", 30)
        turns, agree = self.bout(cable, "fight")
        self.check("fight: a whole bout, the same on both every turn", turns >= 1 and agree, f"{turns} turns")
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

        # A second bout, the cable pulled in the middle.
        self.to_lobby(cable)
        self.wait_linked(cable)
        self.lobby_set(cable, LinkMode.FIGHT, 0)
        cable.press(host, "a", 30)
        self.bout(cable, "fight2", pull_at=1)
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
    capture = LinkCapture(rom, args.out, symbols, catalogue.load(args.catalogue).recipes)
    capture.run(args.phases.split(","))
    capture.write_report()
    failed = [c for c in capture.checks if not c["ok"]]
    print(f"{len(capture.checks) - len(failed)}/{len(capture.checks)} checks passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
