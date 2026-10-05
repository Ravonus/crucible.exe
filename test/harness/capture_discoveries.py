"""Discover objects by playing the real ROM, and capture each new one with and without its material overlay.

Free play from a blank save, headless. Each mix is planned from the catalogue: recipes whose ingredients are owned,
results from categories not shown yet first. For every new object:

- the reveal is checked: from its first REVEAL frames the turntable is already turning (the overlay's view index
  advances) with its overlay sprites present, and the merge's turn phase (crucible_reveal.c) is timed;
- the reveal and then the object as the bench focus are saved as overlay pairs (`-overlay` as played, `-base` with the
  overlay's OAM entries 0..23 hidden for one frame);
- the first --strips reveals are also saved as frame strips (the turn out of the glitch, the flash, the reveal).

Writes discoveries.json and sheet.png, prints a summary of the reveals and the shots, and exits 1 if a reveal failed
its check.

    python test/harness/capture_discoveries.py ROM OUTDIR [--objects OBJDIR] [--mixes 24] [--strips 3]
"""

from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path
from typing import TypedDict

from PIL import Image
from session import OAM, OAM_ENTRY_BYTES, Screen, Session, argument_parser, contact_sheet, rom_symbols, visible_sprites

import catalogue

STARTING_ITEMS = frozenset({0, 1, 2, 3})  # earth, water, fire, air

# crucible_overlay.c's `oc` cells: {u16 id; u8 frame, x, y, buf, shown, ready, pending, dirty; u16 pal_id; u8 pal_v}
CELL_BYTES = 13
CELL_ID = 0
CELL_FRAME = 2  # the shown view in the low nibble (bit 7 is the turn flag)
REVEAL_CELL = 1  # cell B: the reveal's one object
FOCUS_CELL = 3  # the bench focus
REVEAL_OAM = range(13, 18)  # cell B's sprites on the reveal (the burst has 0..11)
FOCUS_OAM_ENTRIES = 8  # a cell shows at most 8 sprites

STRIP_COLUMNS = 12
REVEAL_SAMPLE_FRAMES = 32
SETTLED_SAMPLE_FRAMES = 16  # the view must advance within the first half of the sample
MAX_DRAW_LAG = 120


class Shot(TypedDict):
    id: int
    concept: str
    category: str
    overlaySprites: int


class Reveal(TypedDict):
    id: int
    turnFrames: int
    drawLag: int
    views: list[int]
    overlaySprites: list[int]
    advancingFromStart: bool
    overlayPresent: bool


class Bench:
    """The bench in free play, with the shelf the cartridge shows: owned objects sorted by (category, id)."""

    def __init__(self, game: Session, cells: int, items: catalogue.Catalogue) -> None:
        self.game = game
        self.cells = cells
        self.catalogue = items
        self.owned = set(STARTING_ITEMS)

    def cell_id(self, cell: int) -> int:
        return self.game.peek16(self.cells + cell * CELL_BYTES + CELL_ID)

    def cell_view(self, cell: int) -> int:
        return self.game.peek(self.cells + cell * CELL_BYTES + CELL_FRAME) & 15

    def focus(self) -> int:
        return self.cell_id(FOCUS_CELL)

    def shelf(self) -> list[int]:
        return sorted(self.owned, key=lambda i: (self.catalogue.category_index(i), i))

    def goto(self, target: int) -> bool:
        """Walk right along the shelf to `target`."""
        shelf = self.shelf()
        distance = (shelf.index(target) - shelf.index(self.focus())) % len(shelf)
        for _ in range(distance):
            self.game.pulse("right")
            self.game.step(6)
        self.game.step(30)
        return self.focus() == target

    def next_recipe(self, shown_categories: set[int]) -> tuple[int, int, int] | None:
        todo = [
            (x, y, r)
            for x, y, r in self.catalogue.recipes
            if x in self.owned and y in self.owned and r not in self.owned
        ]
        if not todo:
            return None
        todo.sort(key=lambda t: (self.catalogue.category_index(t[2]) in shown_categories, t[2]))
        return todo[0]


def save_strip(frames: list[Image.Image], path: Path) -> None:
    width, height = frames[0].size
    columns = min(len(frames), STRIP_COLUMNS)
    rows = (len(frames) + STRIP_COLUMNS - 1) // STRIP_COLUMNS
    sheet = Image.new("RGB", (width * columns, height * rows), (0, 0, 0))
    for i, image in enumerate(frames):
        sheet.paste(image, ((i % STRIP_COLUMNS) * width, (i // STRIP_COLUMNS) * height))
    sheet.save(path)


class Discoveries:
    def __init__(self, game: Session, bench: Bench, out: Path, strip_count: int, reveal_at: tuple[int, int]) -> None:
        self.game = game
        self.bench = bench
        self.out = out
        self.strip_count = strip_count
        self.reveal_frames_at, self.reveal_after_at = reveal_at
        self.shots: list[Shot] = []
        self.reveals: list[Reveal] = []
        self.shown_categories: set[int] = set()
        self.strips = 0

    def abandon(self) -> None:
        self.game.pulse("b")
        self.game.step(60)

    def mix(self, x: int, y: int) -> bool:
        """Put x in slot A and mix it with y; True once the bench has left for the merge."""
        game = self.game
        if not self.bench.goto(x):
            return False
        game.pulse("a")
        game.step(30)
        if x != y and not self.bench.goto(y):
            self.abandon()
            return False
        game.pulse("a")
        if not game.until(lambda: game.screen != Screen.BENCH, 240):
            self.abandon()
            return False
        return True

    def watch_merge(self, keep: bool) -> list[Image.Image]:
        """Step through the merge a frame at a time, keeping the turn phase (every 8th frame of it) and the flash."""
        game = self.game
        strip: list[Image.Image] = []
        seen: set[tuple[int, int]] = set()
        for _ in range(6000):
            if game.screen in (Screen.REVEAL, Screen.BENCH):
                break
            game.step(1)
            turn_low = game.peek_wram(self.reveal_frames_at)  # the low byte only
            after = game.peek_wram(self.reveal_after_at)
            phase = (turn_low // 8, after)
            if keep and turn_low and phase not in seen and (turn_low % 8 == 1 or after in (2, 6)):
                seen.add(phase)
                strip.append(game.frame())
        return strip

    def check_reveal(self, result: int, strip: list[Image.Image], keep: bool) -> None:
        game = self.game
        turn = game.peek_wram(self.reveal_frames_at) | game.peek_wram(self.reveal_frames_at + 1) << 8
        # The switch to REVEAL also commits the save and draws the scene (under the white flash): count frames until
        # its cell shows the new object, then sample from there.
        lag = 0
        while self.bench.cell_id(REVEAL_CELL) != result and lag < MAX_DRAW_LAG:
            lag += 1
            game.step(1)
        views: list[int] = []
        present: list[int] = []
        for f in range(REVEAL_SAMPLE_FRAMES):
            views.append(self.bench.cell_view(REVEAL_CELL))
            present.append(sum(1 for i in REVEAL_OAM if game.peek(OAM + i * OAM_ENTRY_BYTES)))
            if keep and f % 8 == 0:
                strip.append(game.frame())
            game.step(1)
        self.reveals.append(
            {
                "id": result,
                "turnFrames": turn,
                "drawLag": lag,
                "views": views,
                "overlaySprites": present,
                "advancingFromStart": len(set(views[:SETTLED_SAMPLE_FRAMES])) >= 2,
                "overlayPresent": max(present[:4]) > 0,
            }
        )
        if keep and strip:
            save_strip(strip, self.out / f"reveal-strip-{result:05d}.png")
            self.strips += 1

    def shoot(self, result: int) -> None:
        """The reveal's pair, then back to the bench and the new object's pair as the focus."""
        game = self.game
        game.step(120)
        if game.screen == Screen.REVEAL:
            game.overlay_pair(self.out, f"reveal-{result:05d}")
        for _ in range(30):
            if game.screen == Screen.BENCH:
                break
            game.pulse("a")
            game.step(60)
        self.bench.owned.add(result)
        game.step(60)
        if self.bench.goto(result):
            game.step(480)
            oam = game.overlay_pair(self.out, f"object-{result:05d}")
            items = self.bench.catalogue
            item = items.items[result]
            self.shots.append(
                {
                    "id": result,
                    "concept": item.concept,
                    "category": item.category,
                    "overlaySprites": visible_sprites(oam, FOCUS_OAM_ENTRIES),
                }
            )
            self.shown_categories.add(items.category_index(result))

    def run(self, mixes: int) -> None:
        for _ in range(mixes):
            recipe = self.bench.next_recipe(self.shown_categories)
            if recipe is None:
                break
            x, y, result = recipe
            if not self.mix(x, y):
                continue
            keep = self.strips < self.strip_count
            strip = self.watch_merge(keep)
            if self.game.screen == Screen.REVEAL:
                self.check_reveal(result, strip, keep)
            self.shoot(result)

    def failed_reveals(self) -> list[int]:
        # An object without a material layer has no overlay to show on its reveal.
        has_overlay = {shot["id"]: shot["overlaySprites"] > 0 for shot in self.shots}
        return [
            reveal["id"]
            for reveal in self.reveals
            if not (
                reveal["advancingFromStart"] and (reveal["overlayPresent"] or not has_overlay.get(reveal["id"], True))
            )
        ]


def main() -> int:
    parser = argument_parser(__doc__)
    parser.add_argument("--mixes", type=int, default=24, help="mixes to try")
    parser.add_argument("--strips", type=int, default=3, help="reveals to save as frame strips")
    args = parser.parse_args()
    out: Path = args.out
    out.mkdir(parents=True, exist_ok=True)
    symbols = rom_symbols(args.rom, args.objects)
    screen_at = symbols.pointer("crucible", "_crucible_run", "_screen")
    cells = symbols.pointer("crucible_overlay", "_crucible_overlay_tick", "_oc")
    # crucible_reveal.c: reveal_frames (u16) counts the turn's frames; reveal_after is 0 while turning, then
    # 1 + frames since it finished.
    reveal_at = (symbols.symbols["_reveal_frames"], symbols.symbols["_reveal_after"])

    game = Session(args.rom, screen_at)
    game.start_free_play()
    run = Discoveries(game, Bench(game, cells, catalogue.load()), out, args.strips, reveal_at)
    run.run(args.mixes)
    game.stop()

    rom_sha = hashlib.sha256(args.rom.read_bytes()).hexdigest()
    report = {"romSha256": rom_sha, "shots": run.shots, "reveals": run.reveals}
    (out / "discoveries.json").write_text(json.dumps(report, indent=1))
    bad = run.failed_reveals()
    summary = {
        "reveals": len(run.reveals),
        "turningAndOverlaidFromTheFirstFrames": len(run.reveals) - len(bad),
        "bad": bad,
        "turnFrames": [reveal["turnFrames"] for reveal in run.reveals],
        "drawLag": [reveal["drawLag"] for reveal in run.reveals],
    }
    print(json.dumps(summary))
    if run.shots:
        contact_sheet(out, [f"object-{shot['id']:05d}" for shot in run.shots], out / "sheet.png")
    print(json.dumps({"discovered": len(run.shots), "shots": run.shots}))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
