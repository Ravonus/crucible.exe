"""Capture the material overlay in the real ROM across bench and book views.

Free play from a blank save, headless, deterministic input. Each view is saved as a pair: `-overlay` is one frame as
played (base + overlay sprites), `-base` the next frame with the overlay's OAM entries 0..23 hidden. The views are the
bench, --browse steps along the shelf, slot A and the focus beside it, then the book. Writes capture.json and
sheet.png and prints the report.

    python test/harness/capture_overlay.py ROM OUTDIR [--objects OBJDIR] [--browse 10]
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

from session import OVERLAY_OAM_ENTRIES, Screen, Session, argument_parser, contact_sheet, rom_symbols, visible_sprites

WAIT_LIMIT = 2400  # frames to reach a screen before the run fails


def main() -> None:
    parser = argument_parser(__doc__)
    parser.add_argument("--browse", type=int, default=10, help="shelf steps to capture")
    args = parser.parse_args()
    out: Path = args.out
    out.mkdir(parents=True, exist_ok=True)
    rom_sha = hashlib.sha256(args.rom.read_bytes()).hexdigest()
    screen_at = rom_symbols(args.rom, args.objects).pointer("crucible", "_crucible_run", "_screen")

    game = Session(args.rom, screen_at)
    shots: list[dict[str, str | int]] = []

    def snap(name: str) -> None:
        oam = game.overlay_pair(out, name)
        shots.append({"name": name, "overlaySprites": visible_sprites(oam, OVERLAY_OAM_ENTRIES)})

    game.start_free_play(WAIT_LIMIT, strict=True, until_bench=True)
    snap("bench")
    for i in range(args.browse):
        game.pulse("right")
        game.step(240)
        snap(f"browse-{i:02d}")
    game.pulse("a")  # the focus into slot A
    game.step(240)
    snap("slot-a")
    game.pulse("right")
    game.step(240)
    snap("slot-a-focus")
    # The pause menu is a grid (RESUME BOOK / TITLES STATS / SETUP LEAVE): BOOK is one step right of where it opens.
    game.pulse("start")
    game.wait_for(Screen.MENU, WAIT_LIMIT, strict=True)
    game.step(60)
    game.pulse("right")
    game.pulse("a")
    game.wait_for(Screen.BOOK, WAIT_LIMIT, strict=True)
    game.step(300)
    snap("book")
    game.stop()

    contact_sheet(out, [str(shot["name"]) for shot in shots], out / "sheet.png")
    report = {"romSha256": rom_sha, "shots": shots}
    (out / "capture.json").write_text(json.dumps(report, indent=1))
    print(json.dumps(report))


if __name__ == "__main__":
    main()
