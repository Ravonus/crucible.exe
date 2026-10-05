"""A link cable between two headless PyBoy instances of the cartridge.

PyBoy emulates no link partner: its serial port reads 0xFF and an external-clock transfer never completes. This module
plays the wire between two emulators so the cartridge's own link code can be exercised.

The cartridge moves its bytes by interrupts (crucible_link_io.c): every VBlank the HOST clocks its next byte out on its
internal clock; the serial interrupt (`link_sio_isr`) reads the byte that came back with `ldh a,(SB)` and, on the
GUEST, arms the next one on the external clock. The bridge runs the two machines in lockstep, one frame each (host,
then guest), and plays the wire between them:

- In the host's frame its own internal-clock transfer completes in PyBoy and its interrupt reads SB. A hook just after
  that read sets register A to the byte the guest has armed (the guest's `lk_cur`) and hands the host's byte (the
  host's `lk_cur`, not yet replaced) to the guest. A guest that is not armed, a pulled cable or a byte still "on the
  wire" (see `Cable.delay`) reads 0xFF on the host, which clocks the same byte again next frame.
- Before the guest's frame, a byte handed over raises the guest's serial interrupt (IF bit 3); the same hook in the
  guest's interrupt sets A to that byte, and the guest arms its next one.

Pulling the cable (`Cable.connected = False`) stops the exchange. `Cable` can also flip bits in crossing bytes
(`noise`), hold bytes on the wire for a while (`delay`) and feed the guest bytes of its own (`inject`).

Used by capture_link.py, which first proves raw bytes cross both ways before playing any link mode.
"""

from __future__ import annotations

import io
import random
import re
from pathlib import Path
from typing import NamedTuple

from pyboy import PyBoy
from romsym import RomSymbols

IDLE = 0xFF  # what an unconnected (or unarmed) serial line reads
SC = 0xFF02
SC_TRANSFER_START = 0x80  # SC bit 7: set while a transfer is armed, cleared by hardware when it completes
IF = 0xFF0F
IF_SERIAL = 0x08
SAVE_RAM_SIZE = 0x20000  # 16 banks of 8 KiB cartridge RAM
ADDRESS_MASK = 0xFFFF  # symbol values carry the bank in their upper bits
BITS_PER_BYTE = 8

WRAM_BANKED_START = 0xD000
WRAM_BANKED_END = 0xE000
WRAM_VARIABLES_BANK = 1

LDH_A_N_LENGTH = 2  # `ldh a,(n)` is F0 nn

# `ldh a,(_SB_REG)` in the listing: F0 with a relocated operand byte.
_LDH_A_SB = re.compile(r"\s+([\da-fA-F]{8})\s+F0r00.*ldh\s+a, \(_SB_REG")


class LinkHooks(NamedTuple):
    """Where to intercept the link: the serial interrupt's bank, the address just after its `ldh a,(SB)`, and the
    address of `lk_cur` (the byte each side has armed or is clocking out)."""

    bank: int
    after_sb_read: int
    current_byte: int


def link_hooks(symbols: RomSymbols) -> LinkHooks:
    module = symbols.module("crucible_link_io", "_link_sio_isr")
    start = module.labels["_link_sio_isr"]
    sb = None
    for line in module.lines:
        m = _LDH_A_SB.match(line)
        # The module's other functions write SB but never read it; the offset test keeps the match inside the ISR.
        if m and int(m[1], 16) >= start:
            sb = module.base + int(m[1], 16) + LDH_A_N_LENGTH
            break
    assert sb, "link_sio_isr reads SB"
    return LinkHooks(module.bank, sb, symbols.symbols["_lk_cur"] & ADDRESS_MASK)


class Memory:
    """PyBoy memory where D000-DFFF always means WRAM bank 1.

    On CGB, D000-DFFF is the switchable WRAM bank (SVBK). The cartridge's variables are linked into bank 1, so reading
    bank 1 explicitly does not depend on which bank happens to be mapped when the harness peeks.
    """

    def __init__(self, pb: PyBoy) -> None:
        self.raw = pb.memory

    @staticmethod
    def _key(address: int) -> int | tuple[int, int]:
        if WRAM_BANKED_START <= address < WRAM_BANKED_END:
            return (WRAM_VARIABLES_BANK, address)
        return address

    def __getitem__(self, address: int) -> int:
        value: int = self.raw[self._key(address)]
        return value

    def __setitem__(self, address: int, value: int) -> None:
        self.raw[self._key(address)] = value


class Side:
    """One cartridge on the cable: a headless CGB PyBoy with the serial interrupt's SB read hooked."""

    def __init__(self, rom: Path, name: str, hooks: LinkHooks, ram: bytes | None = None) -> None:
        self.name = name
        self.pb = PyBoy(
            str(rom),
            window="null",
            cgb=True,
            sound_emulated=False,
            ram_file=io.BytesIO(ram or bytes(SAVE_RAM_SIZE)),
            log_level="ERROR",
        )
        self.pb.set_emulation_speed(0)
        self.m = Memory(self.pb)
        self.cable: Cable | None = None
        self.pending: int | None = None  # guest only: the byte handed over, delivered by its next serial interrupt
        self.current_byte = hooks.current_byte
        self.sent: list[int] = []
        self.got: list[int] = []
        self.held: set[str] = set()  # buttons kept pressed every frame
        # PyBoy calls hook callbacks with the registered context only, so the unbound method gets this Side as `self`.
        self.pb.hook_register(hooks.bank, hooks.after_sb_read, Side._on_sb_read, self)

    def _on_sb_read(self) -> None:
        # The hook fires after `ldh a,(SB)` executed against PyBoy's dead port: replace A with what the wire delivered.
        cable = self.cable
        if cable is None:
            self.pb.register_file.A = IDLE
            return
        if self is cable.host:
            value = cable.exchange()
        else:
            value = self.pending if self.pending is not None else IDLE
            self.pending = None
        self.pb.register_file.A = value
        if value != IDLE:
            self.got.append(value)

    def frame(self) -> None:
        for button in self.held:
            self.pb.button_press(button)
        self.pb.tick()


class Cable:
    """Two Sides in lockstep, a frame each (host first), with the wire between them.

    noise: the chance a byte crossing (either way) arrives with one bit flipped. delay: the most frames a byte may wait
    on the wire; each byte draws 0..delay, and meanwhile the host reads 0xFF ("not listening") and clocks it again, as a
    slow network bridge would. inject: bytes the guest hears instead of the host's (an older cartridge's packet) while
    the host reads 0xFF.
    """

    def __init__(self, host: Side, guest: Side, noise: float = 0.0, delay: int = 0, seed: int = 1) -> None:
        self.host = host
        self.guest = guest
        host.cable = self
        guest.cable = self
        self.connected = True
        self.frames = 0
        self.bytes = 0
        self.noise = noise
        self.delay = delay
        self.rng = random.Random(seed)
        self.wait = 0  # frames the byte now on the wire still has to wait
        self.flips = 0
        self.held_frames = 0
        self.inject: list[int] = []

    def _noisy(self, byte: int) -> int:
        if self.noise and self.rng.random() < self.noise:
            self.flips += 1
            return byte ^ (1 << self.rng.randrange(BITS_PER_BYTE))
        return byte

    def exchange(self) -> int:
        """The host's interrupt reads SB: return what it hears, and hand the host's byte to the guest."""
        host, guest = self.host, self.guest
        if not self.connected:
            return IDLE
        if self.wait:
            self.wait -= 1
            self.held_frames += 1
            return IDLE
        # A guest that is not armed, or has not yet taken the last byte, is not listening.
        if guest.pending is not None or not (guest.pb.memory[SC] & SC_TRANSFER_START):
            return IDLE
        sent = host.m[host.current_byte]
        back = guest.m[guest.current_byte]
        if self.inject:
            guest.pending = self.inject.pop(0)
            return IDLE
        # The draw order (noise on the host's byte, the delay, noise on the guest's byte) fixes what a seed replays.
        guest.pending = self._noisy(sent)
        self.bytes += 1
        host.sent.append(sent)
        guest.sent.append(back)
        if self.delay:
            self.wait = self.rng.randrange(self.delay + 1)
        return self._noisy(back)

    def step(self, n: int = 1) -> None:
        for _ in range(n):
            self.host.frame()
            if self.guest.pending is not None:
                # PyBoy never completes an external-clock transfer: raise the guest's serial interrupt by hand.
                self.guest.pb.memory[IF] = self.guest.pb.memory[IF] | IF_SERIAL
            self.guest.frame()
            self.frames += 1

    def press(self, side: Side, button: str, after: int = 6) -> None:
        """Hold `button` on one side for 3 frames, then run `after` more frames."""
        side.pb.button_press(button)
        self.step(3)
        side.pb.button_release(button)
        self.step(after)
