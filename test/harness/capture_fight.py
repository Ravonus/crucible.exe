"""Capture a story-mode fight in the real ROM, with the scene palette lease checked, then the bench after it.

From a blank save, headless: start a story run, get through the intro, let a champion come to the bench, fight it with
a fixed input pattern and capture the bench after. Fights are not a menu item: the encounter director
(crucible_flow.c) brings them in play. To have one now, this sets the director's telegraph in RAM (flow_force = a
fight, flow_arg = the daemons' champion as the nemesis); the game then announces it on the sign row and pans to it two
seconds later, as in play. Writes fight.json and prints it.

    python test/harness/capture_fight.py ROM OUTDIR [--objects OBJDIR]
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any

from session import OAM_ENTRY_BYTES, OVERLAY_OAM_ENTRIES, SHADOW_OAM, Screen, Session, argument_parser, rom_symbols

FLOW_FIGHT = 2  # crucible_flow.h
NEMESIS = 0x80  # flow_arg: faction | 0x80 for the nemesis
DAEMONS = 1
# The scene-palette lease (crucible_palette_lease.c) lives at BD80..BD88 in SRAM bank 2; BD88 is the slot it holds,
# 0xFF when free.
LEASE_SLOT_BANK = 2
LEASE_SLOT = 0xBD88


def main() -> None:
    args = argument_parser(__doc__).parse_args()
    out: Path = args.out
    out.mkdir(parents=True, exist_ok=True)
    symbols = rom_symbols(args.rom, args.objects)
    screen_at = symbols.pointer("crucible", "_crucible_run", "_screen")

    game = Session(args.rom, screen_at)
    log: list[dict[str, Any]] = []

    def snap(name: str) -> None:
        game.step(1)
        game.frame().save(out / f"{name}.png")
        overlay_oam = sum(1 for i in range(OVERLAY_OAM_ENTRIES) if game.peek(SHADOW_OAM + i * OAM_ENTRY_BYTES))
        log.append(
            {
                "shot": name,
                "screen": game.screen,
                "leaseSlot": game.peek(LEASE_SLOT, LEASE_SLOT_BANK),
                "overlayOam": overlay_oam,
            }
        )

    game.power_on()
    game.pulse("a")  # PLAY
    game.step(240)
    snap("play-menu")
    game.pulse("a")  # STORY
    game.step(240)
    snap("story-pick")
    # Slots, naming, the intro: A (and START every fifth press) until the bench.
    for k in range(240):
        if game.screen == Screen.BENCH:
            break
        game.pulse("start" if k % 5 == 4 else "a")
        game.step(45)
    if game.screen != Screen.BENCH:
        snap("stuck")
        raise SystemExit(f"never reached the story bench (screen {game.screen})")
    game.step(600)
    snap("story-bench")

    game.poke_wram(symbols.symbols["_flow_arg"], DAEMONS | NEMESIS)
    game.poke_wram(symbols.symbols["_flow_force"], FLOW_FIGHT)
    game.step(30)
    snap("fight-telegraph")
    if not game.until(lambda: game.screen == Screen.FIGHT, 600):
        snap("no-fight")
        raise SystemExit("the champion never came")
    game.step(180)
    snap("fight-start")
    for i in range(6):
        game.pulse("right" if i % 2 else "a")
        game.step(150)
        snap(f"fight-{i}")
    for _ in range(200):
        if game.screen != Screen.FIGHT:
            break
        game.pulse("a")
        game.step(90)
    game.step(300)
    snap("after-fight")
    game.stop()

    report = {"romSha256": hashlib.sha256(args.rom.read_bytes()).hexdigest(), "log": log}
    (out / "fight.json").write_text(json.dumps(report, indent=1))
    print(json.dumps(report))


if __name__ == "__main__":
    main()
