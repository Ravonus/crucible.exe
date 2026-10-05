"""Run the core's scenario ROM headless in PyBoy (a real SM83 core) and compare its results with the host build of the
same scenarios: check counts, failures, and the per-scenario CRC-32 hashes of the core state and the store image must
be byte-identical.

Usage: python scen_check.py <scen.gbc> <scen.map> <host results hex>
"""

from __future__ import annotations

import io
import re
import sys
import time
from pathlib import Path

from pyboy import PyBoy

# The result block scen_res (scen.h): "CRU1" when done, check and failure counts, the first failure, per-scenario
# failure counts, state and image hashes, power-cut counts and the v3 migration's cuts.
RESULT_BYTES = 216
DONE = b"CRU1"
CHECKS, FAILS, FIRST_LINE, FIRST_SCENARIO = 4, 6, 8, 10
SCENARIO_FAILS, STATE_HASHES, IMAGE_HASHES = 12, 32, 104
POWER_CUTS, MIGRATION_CUTS, MIGRATION_WRITES = 176, 200, 202
CURRENT_SCENARIO = 208
SCENARIOS = 18
CUT_KINDS = ("NEW", "ROUTE", "KNOWN", "dud", "repeat", "rotation")

FRAMES_PER_SECOND = 59.7275
FRAMES_PER_STEP = 600
FRAME_LIMIT = 60 * 60 * 120  # two emulated hours
SRAM_BYTES = 16 * 8192


def u16(b: bytes, at: int) -> int:
    return b[at] | (b[at + 1] << 8)


def u32(b: bytes, at: int) -> int:
    return u16(b, at) | (u16(b, at + 2) << 16)


def run_rom(rom: str, address: int) -> tuple[bytes, int, float]:
    pb = PyBoy(
        rom, window="null", cgb=True, sound_emulated=False, ram_file=io.BytesIO(bytes(SRAM_BYTES)), log_level="ERROR"
    )
    pb.set_emulation_speed(0)
    started, frames, last = time.time(), 0, -1
    while frames < FRAME_LIMIT:
        pb.tick(FRAMES_PER_STEP, False)
        frames += FRAMES_PER_STEP
        if bytes(pb.memory[address : address + 4]) == DONE:
            break
        scenario = pb.memory[address + CURRENT_SCENARIO]
        if scenario != last:
            emulated, wall = frames / FRAMES_PER_SECOND, time.time() - started
            print(f"  scenario {scenario:2} at {emulated:7.1f} s emulated, {wall:6.1f} s wall", flush=True)
            last = scenario
    result = bytes(pb.memory[address : address + RESULT_BYTES])
    pb.stop(False)
    return result, frames, time.time() - started


def report(host: bytes, sm83: bytes) -> None:
    for name, b in (("host", host), ("sm83", sm83)):
        fails = u16(b, FAILS)
        first = f" (first: scenario {b[FIRST_SCENARIO]}, line {u16(b, FIRST_LINE)})" if fails else ""
        print(f"  {name}: {u16(b, CHECKS)} checks, {fails} failed{first}")
    print("  scenario  fails host/sm83   state hash host / sm83      image hash host / sm83")
    for s in range(SCENARIOS):
        hs, rs = u32(host, STATE_HASHES + 4 * s), u32(sm83, STATE_HASHES + 4 * s)
        hi, ri = u32(host, IMAGE_HASHES + 4 * s), u32(sm83, IMAGE_HASHES + 4 * s)
        hf, rf = host[SCENARIO_FAILS + s], sm83[SCENARIO_FAILS + s]
        flag = "" if (hs, hi, hf) == (rs, ri, rf) else "   <-- DIFFERS"
        print(f"  {s:8}  {hf:5}/{rf:<5}   {hs:08x} / {rs:08x}   {hi:08x} / {ri:08x}{flag}")
    cuts = ", ".join(
        f"{kind} {u16(sm83, POWER_CUTS + 4 * i)}/{u16(sm83, POWER_CUTS + 2 + 4 * i)}"
        for i, kind in enumerate(CUT_KINDS)
    )
    migration = f"v3 migration {u16(sm83, MIGRATION_CUTS)} cuts over {u16(sm83, MIGRATION_WRITES)} writes"
    print(f"  power cuts (boots to before / after): {cuts}; {migration}")


def main() -> int:
    rom, map_file, host_file = sys.argv[1:4]
    found = re.search(r"([0-9A-F]{8})\s+_scen_res\b", Path(map_file).read_text())
    if not found:
        sys.exit("scen_res not found in the map")
    address = int(found.group(1), 16)
    host = bytes.fromhex("".join(Path(host_file).read_text().split()))[:RESULT_BYTES]

    sm83, frames, wall = run_rom(rom, address)
    done = sm83[:4] == DONE
    status = "finished" if done else "DID NOT FINISH"
    print(
        f"SM83 (PyBoy): {status} after {frames} frames = {frames / FRAMES_PER_SECOND:.1f} s emulated, {wall:.1f} s wall"
    )
    report(host, sm83)

    same = done and host[4:] == sm83[4:] and not u16(sm83, FAILS)
    print(
        "RESULT: "
        + ("identical: every check, count and hash matches the host build" if same else "MISMATCH or failures")
    )
    return 0 if same else 1


if __name__ == "__main__":
    sys.exit(main())
