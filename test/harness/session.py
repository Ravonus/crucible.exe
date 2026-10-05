"""What the capture harnesses share: a headless PyBoy session on a blank save, deterministic input, the overlay
on/off frame pair and the contact sheet that lays those pairs side by side."""

from __future__ import annotations

import argparse
import io
from collections.abc import Callable, Sequence
from enum import IntEnum
from pathlib import Path

from PIL import Image
from pyboy import PyBoy
from romsym import RomSymbols

BLANK_SAVE_BYTES = 131072  # the cartridge's 128 KiB of SRAM, all zero: every run starts from a first power-on

WRAM_BANKED = range(0xD000, 0xE000)  # WRAMX: the cartridge keeps its state in bank 1
SHADOW_OAM = 0xC000  # GBDK's shadow OAM, copied to OAM (0xFE00) by DMA every VBlank
OAM = 0xFE00
OAM_ENTRY_BYTES = 4  # y, x, tile, attributes; y == 0 puts a sprite off screen
OVERLAY_OAM_ENTRIES = 24  # the material overlay owns OAM entries 0..23


class Screen(IntEnum):
    """crucible_run's `screen`."""

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


def argument_parser(doc: str | None) -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=doc, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rom", type=Path, help="the built crucible.gbc (its .noi beside it)")
    parser.add_argument("out", type=Path, help="directory for the screenshots and the JSON report")
    parser.add_argument("--objects", type=Path, help="the build's .o/.lst files (default: ROM dir / obj)")
    return parser


def rom_symbols(rom: Path, objects: Path | None) -> RomSymbols:
    return RomSymbols(rom, rom.with_suffix(".noi"), objects or rom.parent / "obj")


def visible_sprites(oam: bytes, entries: int) -> int:
    """How many of the first `entries` OAM entries are on screen."""
    return sum(1 for i in range(entries) if oam[i * OAM_ENTRY_BYTES])


class Session:
    """The ROM running headless from a blank save, as fast as it goes."""

    def __init__(self, rom: Path, screen_address: int) -> None:
        self.pyboy = PyBoy(
            str(rom),
            window="null",
            cgb=True,
            sound_emulated=False,
            ram_file=io.BytesIO(bytes(BLANK_SAVE_BYTES)),
            log_level="ERROR",
        )
        self.pyboy.set_emulation_speed(0)
        self.memory = self.pyboy.memory
        self._screen_address = screen_address

    @property
    def screen(self) -> int:
        return self.peek(self._screen_address)

    def peek(self, address: int, bank: int | None = None) -> int:
        """Read memory. 0xD000..0xDFFF reads bank 1, where the cartridge keeps its state: the colour overlay borrows
        banks 6..7 mid-frame, so the bank SVBK happens to select is not the game's."""
        if bank is None and address in WRAM_BANKED:
            bank = 1
        value: int = self.memory[address] if bank is None else self.memory[bank, address]
        return value

    def peek16(self, address: int) -> int:
        return self.peek(address) | self.peek(address + 1) << 8

    def poke(self, address: int, value: int) -> None:
        if address in WRAM_BANKED:
            self.memory[1, address] = value
        else:
            self.memory[address] = value

    def peek_wram(self, address: int) -> int:
        return self.peek(address)

    def poke_wram(self, address: int, value: int) -> None:
        self.poke(address, value)

    def step(self, frames: int) -> None:
        for _ in range(frames):
            self.pyboy.tick()

    def pulse(self, button: str) -> None:
        """A tap: held 3 frames, then 3 frames released."""
        self.pyboy.button_press(button)
        self.step(3)
        self.pyboy.button_release(button)
        self.step(3)

    def until(self, condition: Callable[[], bool], limit: int = 3000) -> bool:
        """Step frame by frame until `condition` holds; False if it did not within `limit` frames."""
        for _ in range(limit):
            if condition():
                return True
            self.step(1)
        return False

    def wait_for(self, screen: Screen, limit: int = 3000, *, strict: bool = False) -> bool:
        reached = self.until(lambda: self.screen == screen, limit)
        if strict and not reached:
            raise RuntimeError(f"screen {self.screen} never became {int(screen)}")
        return reached

    def accept_clock(self, limit: int = 3000, *, strict: bool = False) -> None:
        """Power on to the title menu: the first power-on asks the time on the title card; A accepts the clock."""
        self.wait_for(Screen.MENU, limit, strict=strict)
        self.step(300)
        self.pulse("a")
        self.step(300)

    def start_free_play(self, limit: int = 3000, *, strict: bool = False) -> None:
        """Power on, then PLAY -> FREE PLAY, and let the bench settle."""
        self.accept_clock(limit, strict=strict)
        self.pulse("a")
        self.step(180)
        self.pulse("down")
        self.step(180)
        self.pulse("a")
        self.wait_for(Screen.BENCH, limit, strict=strict)
        self.step(600)

    def frame(self) -> Image.Image:
        image: Image.Image = self.pyboy.screen.image.convert("RGB").copy()
        return image

    def overlay_pair(self, out: Path, name: str) -> bytes:
        """Save `name`-overlay.png (the next frame as played) and `name`-base.png (the frame after, with the overlay's
        sprites hidden in the shadow OAM for that one frame). Returns the overlay's shadow OAM entries as they were."""
        self.step(1)
        with_overlay = self.frame()
        size = OVERLAY_OAM_ENTRIES * OAM_ENTRY_BYTES
        oam = bytes(self.memory[SHADOW_OAM : SHADOW_OAM + size])
        for i in range(OVERLAY_OAM_ENTRIES):
            self.poke(SHADOW_OAM + i * OAM_ENTRY_BYTES, 0)
        self.step(1)
        without_overlay = self.frame()
        for i, byte in enumerate(oam):
            self.poke(SHADOW_OAM + i, byte)
        self.step(1)
        with_overlay.save(out / f"{name}-overlay.png")
        without_overlay.save(out / f"{name}-base.png")
        return oam

    def stop(self) -> None:
        self.pyboy.stop(save=False)


def contact_sheet(out: Path, names: Sequence[str], path: Path) -> None:
    """Every overlay pair saved under `out` as base | overlay at 2x, four pairs to a row."""
    scale, columns = 2, 4
    width, height = Image.open(out / f"{names[0]}-base.png").size
    rows = (len(names) + columns - 1) // columns
    pair_width, pair_height = 2 * width * scale + 12, height * scale + 8
    sheet = Image.new("RGB", (columns * pair_width, rows * pair_height), (30, 30, 30))
    for i, name in enumerate(names):
        x, y = (i % columns) * pair_width, (i // columns) * pair_height
        for column, layer in enumerate(("base", "overlay")):
            image = Image.open(out / f"{name}-{layer}.png").resize(
                (width * scale, height * scale), Image.Resampling.NEAREST
            )
            sheet.paste(image, (x + column * (width * scale + 4), y))
    sheet.save(path)
