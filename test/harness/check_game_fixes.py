"""Real-ROM latency and music continuity regression; every run uses isolated blank SRAM."""

import hashlib
import json
import sys
from pathlib import Path

from capture_discoveries import Bench
from PIL import Image
from session import Screen, Session, rom_symbols

import catalogue

rom, out = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)
sym = rom_symbols(rom, None)
ptr = sym.pointer
game = Session(rom, ptr("crucible", "_crucible_run", "_screen"))
items = catalogue.load()
bench = Bench(game, ptr("crucible_overlay", "_crucible_overlay_tick", "_oc"), items)
mstate = ptr("crucible_sound", "_sound_init", "_m_state")
mrow = ptr("crucible_sound", "_sound_init", "_m_row")
result = []
game.start_free_play(strict=True)
recipes = {(min(a, b), max(a, b)): r for a, b, r in items.recipes}
x, y, r = next((a, b, r) for a, b, r in items.recipes if a in bench.owned and b in bench.owned and r not in bench.owned)
miss = next((a, b) for a in range(4) for b in [r] if (min(a, b), max(a, b)) not in recipes)


def goto(target: int) -> bool:
    for _ in range(len(bench.owned) * 2):
        if bench.focus() == target:
            game.step(60)
            return True
        old = bench.focus()
        game.pyboy.button_press("right")

        def moved(previous: int = old) -> bool:
            return bench.focus() != previous

        game.until(moved, 240)
        game.pyboy.button_release("right")
        game.step(20)
    return bench.focus() == target


for kind, a, b in [("new", x, y), ("known", x, y), ("nothing", *miss)]:
    assert goto(a), (kind, a, bench.focus())
    game.pyboy.button_press("a")
    game.step(24)
    game.pyboy.button_release("a")
    game.step(30)
    assert goto(b), (kind, b, bench.focus())
    game.pyboy.button_press("a")
    frames = 0
    entered = False
    phase_frames: dict[str, int] = {}
    music_off = 0
    rows = set()
    samples: list[Image.Image] = []
    for frames in range(3000):
        game.step(1)
        if frames == 3:
            game.pyboy.button_release("a")
        screen = game.screen
        if screen == Screen.MERGE:
            entered = True
        if entered:
            phase_frames[str(screen)] = phase_frames.get(str(screen), 0) + 1
            if screen == Screen.MERGE:
                music_off += game.peek(mstate) == 0
                rows.add(game.peek(mrow))
                if frames % 16 == 0 and len(samples) < 36:
                    samples.append(game.frame())
            if screen in (Screen.REVEAL, Screen.BENCH):
                break
    assert entered and frames < 2999, (kind, game.screen)
    result.append(
        {
            "kind": kind,
            "pair": [a, b],
            "inputToResultFrames": frames + 1,
            "phaseFrames": phase_frames,
            "musicOffMergeFrames": music_off,
            "musicRows": len(rows),
            "turnFrames": game.peek16(sym.symbols["_reveal_frames"] & 0xFFFF),
        }
    )
    if samples:
        from capture_discoveries import save_strip

        save_strip(samples, out / f"{kind}.png")
    if kind == "new":
        bench.owned.add(r)
    game.step(120)
    if game.screen == Screen.REVEAL:
        game.pulse("a")
        game.wait_for(Screen.BENCH, strict=True)
    game.pulse("b")
    game.step(120)
game.stop()
report = {"romSha256": hashlib.sha256(rom.read_bytes()).hexdigest(), "merges": result}
(out / "game-fixes.json").write_text(json.dumps(report, indent=2))
print(json.dumps(report))
