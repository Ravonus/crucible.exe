"""Capture a lost piece (core/src/cru_lost.c) in the real ROM, from a blank save, headless.

Free play: make STEAM (FIRE + WATER) through real input, lose it, then:

1. the first attempt: the merge plays and corrupts, the machine glitches in with one line and leaves by itself;
2. later attempts: the bench's result cell tears while FIRE and WATER sit in the slots; mixing anyway fails quietly;
3. N other mixes that make something, then FIRE + WATER makes STEAM again.

Free play has no way to lose a piece by input (the losses are a story run's: gifts, thefts, fights, misses, splits), so
the loss itself goes through the core's own API: a few bytes of SM83 code poked into free HRAM call
cru_drop(&core, STEAM) once, from the start of a frame's crucible_lost_tick (the ROM carries no debug code).
Everything after is input. Writes lost-piece.json and prints a summary; fails on the first broken expectation.

    python test/harness/capture_lost_piece.py ROM OUTDIR [--objects OBJDIR]
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any, TypedDict

from session import Screen, Session, argument_parser, rom_symbols

# struct crucible_core field offsets on SM83 (SDCC packs it), from core/include/crucible_core.h
O_POINTS = 0x56
O_MADE = 0x5A
O_SEED = 0x5E
O_FOCUS = 0x1FC
O_SLOT_A = 0x1FE
O_MIX = 0x203
O_MIX_OUTCOME = O_MIX + 8  # 0: a new find
O_MIX_LOST = O_MIX + 15  # 1: this mix was the lost piece's first glitch, 2: a later, quiet failure
O_LOST = 0x213  # the lost table: version, serial, then 4 entries of {u16 id, u8 left, u8 flags (bit 0: warned)}
LOST_BYTES = 18
LOST_ENTRIES = 4
NO_ITEM = 0xFFFF

# Free HRAM above GBDK's 19 bytes at 0xFF80 (the stack, from 0xDF00 down, never reaches it).
STUB = 0xFFC0
RESULT = 0xFFE8
RESULT_PENDING = 0xEE

EARTH, WATER, FIRE, AIR, STEAM = 0, 1, 2, 3, 4
OTHER_MIXES = [
    (EARTH, WATER),
    (EARTH, FIRE),
    (AIR, EARTH),
    (AIR, WATER),
    (AIR, FIRE),
    (WATER, WATER),
    (EARTH, EARTH),
    (FIRE, FIRE),
    (AIR, AIR),
]

# The bench's result cell CR sits at map (15,4)..(18,7); its middle 2x2 tiles are the plain "?" when nothing is cued.
UI_QUESTION = 117
LCDC = 0xFF40
LCDC_BG_MAP_HIGH = 0x08
CR_MIDDLE = [(5, 16), (5, 17), (6, 16), (6, 17)]  # (row, column)

# The save: two 512-byte records in SRAM bank 0; the newer one carries the lost table at bytes 246..263.
RECORDS = (0xA500, 0xA700)
RECORD_BYTES = 512
RECORD_VALID = 0xC1
RECORD_LOST = slice(246, 246 + LOST_BYTES)


class LostEntry(TypedDict):
    id: int
    left: int
    warned: int


class LostTable(TypedDict):
    version: int
    serial: int
    entries: list[LostEntry]


def span(seed: int, item: int, serial: int) -> int:
    """cru_lost.c's span(): how many other mixes a loss lasts."""
    x = (seed ^ 0x9E37 ^ ((item << 5) & 0xFFFF) ^ (item >> 3) ^ ((serial << 9) & 0xFFFF) ^ serial) & 0xFFFF
    for _ in range(2):
        x ^= (x << 7) & 0xFFFF
        x ^= x >> 9
        x ^= (x << 8) & 0xFFFF
    return 2 + ((x ^ (x >> 8)) & 0xFF) % 9


def drop_stub(core: int, item: int, drop: int, drop_bank: int, banked_call: int) -> list[int]:
    """SM83 code for `RESULT = cru_drop(&core, item)`, saving every register and returning to the hooked code."""
    return [
        *(0xF5, 0xC5, 0xD5, 0xE5),  # push af, bc, de, hl
        *(0x11, item & 0xFF, item >> 8),  # ld de, item
        0xD5,  # push de
        *(0x11, core & 0xFF, core >> 8),  # ld de, &core
        0xD5,  # push de
        *(0x1E, drop_bank),  # ld e, bank of cru_drop
        *(0x21, drop & 0xFF, drop >> 8),  # ld hl, cru_drop
        *(0xCD, banked_call & 0xFF, banked_call >> 8),  # call ___sdcc_bcall_ehl
        *(0xE8, 0x04),  # add sp, 4 (the two arguments)
        *(0xEA, RESULT & 0xFF, RESULT >> 8),  # ld (RESULT), a
        *(0xE1, 0xD1, 0xC1, 0xF1),  # pop hl, de, bc, af
        0xC9,  # ret
    ]


class LostPieceRun:
    def __init__(self, game: Session, core: int, out: Path) -> None:
        self.game = game
        self.core = core
        self.out = out
        self.log: list[dict[str, Any]] = []
        self.shots: list[str] = []

    def core8(self, offset: int) -> int:
        return self.game.peek(self.core + offset)

    def core16(self, offset: int) -> int:
        return self.game.peek16(self.core + offset)

    def lost(self) -> LostTable:
        t = [self.core8(O_LOST + i) for i in range(LOST_BYTES)]
        entries: list[LostEntry] = [
            {"id": t[2 + 4 * k] | t[3 + 4 * k] << 8, "left": t[4 + 4 * k], "warned": t[5 + 4 * k] & 1}
            for k in range(LOST_ENTRIES)
            if t[4 + 4 * k]
        ]
        return {"version": t[0], "serial": t[1], "entries": entries}

    def left(self, item: int) -> int:
        return next((e["left"] for e in self.lost()["entries"] if e["id"] == item), 0)

    def snap(self, name: str, note: str = "") -> None:
        self.game.frame().save(self.out / f"{name}.png")
        self.shots.append(name)
        self.log.append({"shot": name, "screen": self.game.screen, "note": note, "lost": self.lost()})

    def goto(self, target: int, limit: int = 40) -> bool:
        for _ in range(limit):
            # A random Story visitor's talk can open mid-walk in free play: leave it first.
            if self.game.screen != Screen.BENCH:
                self.to_bench()
            if self.core16(O_FOCUS) == target:
                return True
            self.game.pulse("right")
            self.game.step(8)
        return self.core16(O_FOCUS) == target

    def to_bench(self, limit: int = 60) -> bool:
        for _ in range(limit):
            if self.game.screen == Screen.BENCH:
                return True
            self.game.pulse("a" if self.game.screen in (Screen.REVEAL, Screen.TALK) else "b")
            self.game.step(30)
        return self.game.screen == Screen.BENCH

    def pick_pair(self, x: int, y: int) -> None:
        """Slot A = x, focus = y: the bench ready for A (the mix). Clears a stale slot A first."""
        for _ in range(3):
            if self.core16(O_SLOT_A) != NO_ITEM:
                self.game.pulse("b")
                self.game.step(20)
            if not self.goto(x):
                raise SystemExit(f"cannot focus {x}")
            self.game.pulse("a")
            self.game.step(20)
            if self.core16(O_SLOT_A) != x:
                continue
            if not self.goto(y):
                raise SystemExit(f"cannot focus {y}")
            self.game.step(10)
            if self.core16(O_SLOT_A) == x and self.core16(O_FOCUS) == y:
                return
        raise SystemExit(f"cannot set up {x} + {y}")

    def mix(self, x: int, y: int) -> int:
        """Mix x + y through input and come back to the bench; returns the mix's outcome."""
        game = self.game
        self.pick_pair(x, y)
        game.pulse("a")
        game.until(lambda: game.screen != Screen.BENCH, 240)
        game.until(lambda: game.screen in (Screen.BENCH, Screen.REVEAL, Screen.TALK), 4000)
        game.step(30)
        outcome = self.core8(O_MIX_OUTCOME)
        self.to_bench()
        game.step(20)
        return outcome

    def result_cell_torn(self) -> bool:
        map_base = 0x9C00 if self.game.peek(LCDC) & LCDC_BG_MAP_HIGH else 0x9800
        # VRAM bank 0 (the map), whatever VBK has selected.
        tiles = [self.game.peek(map_base + row * 32 + column, 0) for row, column in CR_MIDDLE]
        return tiles != [UI_QUESTION, UI_QUESTION + 1, UI_QUESTION + 2, UI_QUESTION + 3]


def drop_by_hook(game: Session, stub: list[int], tick: int, tick_bank: int) -> int:
    """Run `stub` once from the start of crucible_lost_tick, wait for its RESULT and return the frames waited."""
    for i, byte in enumerate(stub):
        game.poke(STUB + i, byte)
    game.poke(RESULT, RESULT_PENDING)
    armed = True

    def inject(_: object) -> None:
        # Push the hooked PC and jump to the stub: a CALL the game did not make.
        nonlocal armed
        if not armed:
            return
        armed = False
        registers = game.pyboy.register_file
        sp = (registers.SP - 2) & 0xFFFF
        game.poke(sp, registers.PC & 0xFF)
        game.poke(sp + 1, registers.PC >> 8)
        registers.SP = sp
        registers.PC = STUB

    game.pyboy.hook_register(tick_bank, tick & 0xFFFF, inject, None)
    game.until(lambda: not armed, 600)
    frames = 0
    # A drop re-derives the found counts over the whole catalogue: it can take frames.
    while game.peek(RESULT) == RESULT_PENDING and frames < 20000:
        game.step(1)
        frames += 1
    game.step(30)
    # The stub stays in HRAM (nothing else uses 0xFFC0..): never clear code that may still be returning.
    game.pyboy.hook_deregister(tick_bank, tick & 0xFFFF)
    return frames


def make_and_lose_steam(run: LostPieceRun, symbols: dict[str, int]) -> int:
    """Make STEAM by input, then drop it through the core's API; returns N, the mixes the loss lasts."""
    game = run.game
    outcome = run.mix(FIRE, WATER)
    run.log.append({"make": "FIRE+WATER", "outcome": outcome})
    assert outcome == 0, "STEAM was not a new find"

    stub = drop_stub(
        run.core, STEAM, symbols["_cru_drop"] & 0xFFFF, symbols["b_cru_drop"], symbols["___sdcc_bcall_ehl"]
    )
    frames = drop_by_hook(game, stub, symbols["_crucible_lost_tick"], symbols["b_crucible_lost_tick"])
    table = run.lost()
    n = run.left(STEAM)
    expected = span(run.core16(O_SEED), STEAM, 1)
    run.log.append(
        {
            "drop": "cru_drop(&core, STEAM)",
            "returned": game.peek(RESULT),
            "frames": frames,
            "lost": table,
            "expectN": expected,
        }
    )
    assert game.peek(RESULT) == 1 and n == expected and 2 <= n <= 10, ("drop failed", game.peek(RESULT), table)
    game.pulse("right")
    game.step(20)
    game.pulse("left")
    game.step(60)
    run.snap(
        "lost-0-bench-after-loss",
        "STEAM dropped (the bench redrawn by a move): FIRE and WATER still on the shelf",
    )
    return n


def first_attempt(run: LostPieceRun, n: int) -> None:
    """The merge corrupts, then the machine speaks one line and leaves by itself; nothing is scored."""
    game = run.game
    run.pick_pair(FIRE, WATER)
    game.step(30)
    run.snap(
        "lost-1a-first-attempt-slots",
        "FIRE in slot A, WATER focused: no cue yet (the box previews what the pair made before)",
    )
    points = run.core16(O_POINTS)
    made = run.core16(O_MADE)
    game.pulse("a")
    game.until(lambda: game.screen == Screen.MERGE, 240)
    assert run.core8(O_MIX_LOST) == 1, "the first attempt was not a lost glitch"
    for k in range(6):
        game.step(7)
        run.snap(f"lost-1b-first-merge-{k}", "the fusion tears toward the lost shape")
    game.until(lambda: game.screen == Screen.TALK, 600)
    talk_frames = 0
    last = None
    while game.screen == Screen.TALK and talk_frames < 1200:
        game.step(1)
        talk_frames += 1
        if game.screen == Screen.TALK:
            last = game.frame()
        if talk_frames == 110:
            run.snap("lost-1c-first-avatar-arrives", "the machine glitches in")
    if last is not None:
        name = "lost-1d-first-avatar-line"
        last.save(run.out / f"{name}.png")
        run.shots.append(name)
        run.log.append({"shot": name, "note": "its one line, the last frame before it leaves by itself"})
    run.log.append({"talkFramesUntilItLeftByItself": talk_frames, "screen": game.screen})
    assert game.screen == Screen.BENCH, "the line did not leave by itself"
    assert (
        run.core16(O_POINTS) == points
        and run.core16(O_MADE) == made
        and run.left(STEAM) == n
        and run.lost()["entries"][0]["warned"] == 1
    )


def later_attempts(run: LostPieceRun, n: int) -> None:
    """The result cell is cued while the pair sits on the bench; mixing anyway fails quietly."""
    game = run.game
    game.step(20)
    torn = 0
    for _ in range(40):
        game.step(1)
        if run.result_cell_torn():
            torn += 1
            if torn <= 3:
                run.snap(f"lost-2a-cued-box-{torn}", "FIRE + WATER on the bench: the result cell tears")
    game.pulse("b")
    game.step(30)
    game.pulse("right")
    game.step(30)
    clean = []
    for _ in range(12):
        game.step(1)
        clean.append(run.result_cell_torn())
    run.log.append({"tornFramesOf40": torn, "tornAfterSlotCleared": sum(clean)})
    assert torn > 0 and not any(clean)
    outcome = run.mix(FIRE, WATER)
    run.log.append(
        {
            "quietAttempt": outcome,
            "mixLost": run.core8(O_MIX_LOST),
            "screenAfter": game.screen,
            "left": run.left(STEAM),
        }
    )
    assert run.core8(O_MIX_LOST) == 2 and run.left(STEAM) == n and game.screen == Screen.BENCH, run.log[-1]


def check_saved(run: LostPieceRun) -> None:
    """The newer save record carries the lost table as it is in RAM."""
    records = [[run.game.peek(at + i, 0) for i in range(RECORD_BYTES)] for at in RECORDS]
    newest = max(records, key=lambda r: (r[0] == RECORD_VALID, r[2] | r[3] << 8))
    ram_lost = [run.core8(O_LOST + i) for i in range(LOST_BYTES)]
    run.log.append({"recordLost": newest[RECORD_LOST], "ramLost": ram_lost})
    assert newest[RECORD_LOST] == ram_lost


def cool_down(run: LostPieceRun, n: int) -> None:
    """N other mixes that make something, then STEAM forms again."""
    game = run.game
    k = 0
    while run.left(STEAM):
        x, y = OTHER_MIXES[k % len(OTHER_MIXES)]
        outcome = run.mix(x, y)
        run.log.append({"combo": [x, y], "outcome": outcome, "left": run.left(STEAM)})
        k += 1
        assert k < 30
    run.log.append({"combosToClear": k, "N": n})
    run.pick_pair(FIRE, WATER)
    game.step(20)
    run.snap("lost-3a-cleared-slots", "cooled down: no cue, the box is clean again")
    game.pulse("a")
    game.until(lambda: game.screen == Screen.REVEAL, 4000)
    game.step(60)
    run.snap("lost-3b-steam-again", "STEAM forms normally")
    assert run.core8(O_MIX_OUTCOME) == 0
    run.to_bench()


def main() -> None:
    args = argument_parser(__doc__).parse_args()
    out: Path = args.out
    out.mkdir(parents=True, exist_ok=True)
    symbols = rom_symbols(args.rom, args.objects)
    screen_at = symbols.pointer("crucible", "_crucible_run", "_screen")

    game = Session(args.rom, screen_at)
    run = LostPieceRun(game, symbols.symbols["_core"], out)
    game.start_free_play()
    n = make_and_lose_steam(run, symbols.symbols)
    first_attempt(run, n)
    later_attempts(run, n)
    check_saved(run)
    cool_down(run, n)
    game.stop()

    rom_sha = hashlib.sha256(args.rom.read_bytes()).hexdigest()
    report = {"romSha256": rom_sha, "N": n, "shots": run.shots, "log": run.log}
    (out / "lost-piece.json").write_text(json.dumps(report, indent=1))
    print(json.dumps({"romSha256": rom_sha, "N": n, "shots": run.shots}))


if __name__ == "__main__":
    main()
