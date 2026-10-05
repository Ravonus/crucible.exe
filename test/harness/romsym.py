"""Addresses inside a built ROM, read from the linker's .noi and the per-object listings in build/obj.

Global symbols come straight from the .noi. A function-local static (`static uint8_t screen` inside crucible_run) has
no global symbol, so it is found where the code uses it: the listing of the object that defines an anchor function
shows the instruction that loads the static's address, and the linked ROM holds that address at the matching offset.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

BANK_SIZE = 0x4000

_NOI_SYMBOL = re.compile(r"DEF (\S+) 0x([\da-fA-F]+)")
_AREA = re.compile(r"\.area (\S+)")
_LABEL = re.compile(r"\s+([\da-fA-F]{8})\s+\d+ (_\w+):")
# Instructions whose two operand bytes are an absolute address (relocated, so `rXX rXX` in the listing):
# ld a,(nn) / ld (nn),a / ld hl,#nn / ld de,#nn / ld bc,#nn.
_ADDRESS_LOAD = re.compile(r"\s+([\da-fA-F]{8})\s+(?:FA|EA|21|11|01)(?:r[\da-fA-F]{2}){2}")


@dataclass(frozen=True)
class Module:
    """The listing lines of the code area that holds an anchor, and where that area was linked."""

    lines: list[str]
    labels: dict[str, int]
    bank: int
    base: int


class RomSymbols:
    def __init__(self, rom: Path, noi: Path, objects: Path) -> None:
        self.rom = rom.read_bytes()
        self.objects = objects
        self.symbols: dict[str, int] = {
            m[1]: int(m[2], 16) for m in _NOI_SYMBOL.finditer(noi.read_text(encoding="latin-1"))
        }
        self._modules: dict[str, Module] = {}

    def module(self, name: str, anchor: str) -> Module:
        """The code area of object `name` that defines the global function `anchor`."""
        if name not in self._modules:
            self._modules[name] = self._load_module(name, anchor)
        return self._modules[name]

    def pointer(self, name: str, anchor: str, symbol: str) -> int:
        """The address of `symbol` (a static, possibly `symbol + n`) as the first instruction that loads it holds it."""
        mod = self.module(name, anchor)
        operand = re.compile(r"#" + re.escape(symbol) + r"\b(?:\s*([+-])\s*(0x[\da-fA-F]+|\d+))?")
        for line in mod.lines:
            ins = _ADDRESS_LOAD.match(line)
            use = operand.search(line)
            if not (ins and use):
                continue
            at = self._rom_offset(mod.bank, mod.base + int(ins[1], 16)) + 1
            value = int.from_bytes(self.rom[at : at + 2], "little")
            offset = int(use[2], 0) if use[2] else 0
            return value + offset if use[1] == "-" else value - offset
        raise ValueError(f"no instruction in {name} loads {symbol}")

    def _load_module(self, name: str, anchor: str) -> Module:
        obj = (self.objects / f"{name}.o").read_text(encoding="latin-1")
        listing = (self.objects / f"{name}.lst").read_text(encoding="latin-1")
        target = _area_defining(obj, anchor)
        lines: list[str] = []
        area = None
        for line in listing.splitlines():
            if m := _AREA.search(line):
                area = m[1]
            if area == target:
                lines.append(line)
        labels = {m[2]: int(m[1], 16) for line in lines if (m := _LABEL.match(line))}
        linked = self.symbols[anchor]
        return Module(lines=lines, labels=labels, bank=linked >> 16, base=(linked & 0xFFFF) - labels[anchor])

    @staticmethod
    def _rom_offset(bank: int, address: int) -> int:
        return bank * BANK_SIZE + address - (BANK_SIZE if bank else 0)


def _area_defining(obj: str, symbol: str) -> str:
    area = None
    for line in obj.splitlines():
        if line.startswith("A "):
            area = line.split()[1]
        if line.startswith(f"S {symbol} Def") and area is not None:
            return area
    raise ValueError(f"{symbol} is not defined in this object")
