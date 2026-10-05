# CRUCIBLE.EXE time awareness

CRUCIBLE.EXE knows roughly what time it is, how long the player was away, and how long they have been playing. A few
things change because of that, quietly and for a while.

Code: `cartridge/src/crucible_time.c` and `cartridge/include/crucible_time.h`.

## There is no clock chip

The cartridge is MBC5 with RAM and a battery (`tools/build-rom.ts` links with `-Wm-yt0x1B`, `-Wm-ya16`, an 8 MiB ROM). MBC5
has no real-time clock. MBC3+RTC tops out at 2 MiB of ROM, so it is not an option. So:

- **The player says what time it is.** The first time the cartridge runs (or when an old save has no clock yet), the
  title card asks "WHAT TIME IS IT?", like Pokémon Gold/Silver: day of week, hour and minute.
- **At every power-on the player confirms or adjusts it.** The page is prefilled with the last-known time, which is the
  clock as it stood when the cartridge was switched off. The record is written every in-game minute. The player can
  move it forward and add whole weeks. The size of that move is the time away.
- **While the cartridge runs, the clock counts VBlanks.** GBDK's `sys_time` advances once per VBlank. One VBlank is
  70,224 clocks at 4.194304 MHz, which is exactly 4389/262144 s, about 59.7275 Hz. The module keeps the remainder in an
  18-bit fraction, so no time is lost to rounding, however unevenly it is polled (any gap up to 65,535 frames, about
  18 minutes). `feats_toast` polls it every frame on every screen, and so do the menu and the time page. When the LCD
  is off there are no VBlanks, so those moments are not counted. They only happen for a moment at power-on.
- **Everything persists in SRAM** (below).

### Time away is self-reported

The game takes the player's word and must never punish an honest "I don't know":

- **B on the power-on page means NOT SURE.** The estimate is kept, the absence is `CT_AWAY_UNKNOWN`, and nothing
  reacts: no whisper, no story effect, no counters. If the clock was never set, it stays unset, time-of-day effects
  stay off, and the question is asked again next time.
- **Stepping the clock back counts as a correction.** Moving it back by up to six hours (with no weeks added) is also
  `CT_AWAY_UNKNOWN`. It is never read as "six days and some hours away". Moving it back further reads as days ahead,
  because the day of the week wraps. The weeks field is the explicit way to say "longer than a week".
- **SETUP > TIME is always a correction**, forward or back, and never an absence.
- **Confirming without a change means "just now".** A player who always presses A is treated as coming straight back.
  That is harmless: the reactions to a quick return are as gentle as the others.

## What is kept (SRAM bank 15)

There are two 32-byte records, A at bank 15 offset 0x1F80 and B at 0x1FA0. In the core's linear store addresses
(`bank*0x2000 + offset`) they are **0x1FF80..0x1FF9F and 0x1FFA0..0x1FFBF**. On the cartridge both live at
0xBF80..0xBFBF with RAM bank 15 selected.

Bank 15 holds:

| bank 15 offset                                 | what                                                       | written by                              |
| ---------------------------------------------- | ---------------------------------------------------------- | --------------------------------------- |
| 0x0000..0x1AFF                                 | story slot 0's records, play block, OWNED, RBITS           | core `cru_story.c` (wiped on a new run) |
| 0x1D00..0x1DB7                                 | story slot 0's `crucible_story` record (magic, 180 B, CRC) | core `cru_story.c`                      |
| 0x1F00..0x1F06                                 | easter eggs found ('E' 'G', 4 bytes of bits, DMG flag)     | `crucible_eggs.c`                       |
| **0x1F80..0x1FBF**                             | **time records A and B**                                   | **`crucible_time.c`**                   |
| 0x1DB8..0x1EFF, 0x1F07..0x1F7F, 0x1FC0..0x1FFF | free                                                       |                                         |

Nothing else writes here:

- The Classic save uses bank 0 and, in the 128K layout, banks 4 to 14. The highest it reaches is the WIDE spill table
  at 0x1D800..0x1D87F (bank 14).
- Slot wipes clear only up to 0x1D80.
- RESET GAME rewrites the Classic records only.

Other apparently free areas were checked and not used:

- Bank 2 BD80..BD89 is the palette lease and BD90..BDC7 the dialogue records. BE00..BF07 is borrowed by the v3
  migration at boot. Bank 2 is otherwise fusion and palette scratch.
- Bank 0 0x0400..0x04FF is the second half of the legacy v3 record B. `docs/save-structures.md` lists it as free, but it
  overlaps V3_B (0x0300 + 512 B).

Record layout (little-endian):

| bytes     | field                                                                                   |
| --------- | --------------------------------------------------------------------------------------- |
| 0-1       | magic 'C' 'T'                                                                           |
| 2         | version (1)                                                                             |
| 3         | flags: bit 0 the clock was set by the player                                            |
| 4-5       | serial (wrapping; the newer valid record wins)                                          |
| 6-7       | minute of the week, 0 = SUN 00:00 (< 10080)                                             |
| 8         | seconds (< 60)                                                                          |
| 9         | this power-on's away bucket (`CT_AWAY_*`)                                               |
| 10-11     | weeks elapsed on the player's clock                                                     |
| 12-15     | total play time, seconds                                                                |
| 16-17     | play time since this power-on, minutes (the previous power-on's, until the next minute) |
| 18-19     | the last day played (weeks*7 + weekday)                                                 |
| 20 / 21   | streak of consecutive days played / best streak                                         |
| 22-23     | returns reported (confirmed with a real answer)                                         |
| 24 / 25   | quick returns (under an hour) / long returns (a day or more), saturating                |
| 26, 28-29 | reserved, written as 0                                                                  |
| 27        | commit byte: 0xC7                                                                       |
| 30-31     | CRC-16/CCITT (0x1021, init 0xFFFF) of bytes 0..29                                       |

A write goes to the record that is not the newest valid one, in three steps:

1. Clear the commit byte.
2. Write the 31 other bytes.
3. Set the commit byte last.

This is the dialogue records' protocol. A power cut at any point leaves the previous record valid (checked by cutting
each of the 33 writes and corrupting each byte).

Zeroed SRAM (a save without a clock record), 0xFF (a new battery) or noise has no valid record. The game then
starts a fresh clock (SUN 10:00 AM, unset) and asks. It writes nothing until it has an answer or a minute has passed.
Saves without a clock record load unchanged: the Classic records, the story slots, the dialogue records and the eggs are not read or
written by this module. RESET GAME keeps the clock, like the settings.

## The context API

Dialogue and scenes can ask:

```c
crucible_time_ctx t; crucible_time_context(&t);
```

| field                       | meaning                                                                                                                                                                                                                              |
| --------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `flags`                     | `CT_F_KNOWN` (the clock was set), `CT_F_REPORTED` (this power-on's answer is in), `CT_F_RETURNING` (the first 10 minutes after it: "for a bit"), `CT_F_DEEP` (a special minute at night), `CT_F_PAUSED` (and the menu is showing it) |
| `part`                      | `CT_NIGHT` 22:00-04:59, `CT_DAWN` 05-07, `CT_DAY` 08-17, `CT_DUSK` 18-21                                                                                                                                                             |
| `weekday`, `hour`, `minute` | the clock (0 = SUN)                                                                                                                                                                                                                  |
| `away`                      | `CT_AWAY_NONE` (not asked yet), `_FIRST`, `_UNKNOWN`, `_NOW` (< 5 min), `_MINUTES` (< 1 h), `_HOURS` (< 1 day), `_DAY` (< 1 week), `_WEEK`                                                                                           |
| `marathon`                  | 0; 1 after two hours since power-on; 2 after four ("forever")                                                                                                                                                                        |
| `special`                   | `CT_SP_*`: 1:11, 2:22, 3:33, 4:44, 5:55, 11:11, 12:34, 4:04, 10:10 (all on a 12-hour face, so AM and PM), midnight, noon                                                                                                             |
| `streak`                    | consecutive days played, by the player's clock                                                                                                                                                                                       |
| `session_min`               | minutes played since power-on, saturated at 255                                                                                                                                                                                      |

Time-of-day and special minutes are reported only when the clock is known.

## Effects

None of these explain themselves.

- **Whispers.** A whisper is a card on the bench, drawn by `feats_toast` when no achievement is waiting. It shows an
  hourglass, the time and one line, after two calm seconds on the bench (about four after another whisper). Priority
  order:
  - **A special minute.** It is shown only during that minute. Examples: 3:33 "THE HUM STOPPED.", 4:04 "ROOM NOT
    FOUND.", 11:11 "MAKE A WISH.", midnight "NEW DAY. SAME ROOM".
  - **The return.** The line depends on how long the player was away:
    - just now: "BACK ALREADY?" or "YOU NEVER LEFT.";
    - minutes: "THE ROOM WAITED.";
    - hours: "LIGHTS STAYED ON.", or "YOU KEEP RETURNING" on a streak of three days or more;
    - a day: "THE DUST SETTLED.";
    - a week or more: "WE KEPT YOUR SEAT." or "DID YOU DREAM?".
    - The first setting says "THE CLOCK STARTS."
    - "Not sure" and corrections say nothing.
  - **Marathon.** At two hours: "STILL HERE?" or "HAVE YOU EATEN?". At four, and every hour after: "HAVE YOU BLINKED?",
    "THE CARPET IS DAMP", "WE'RE ALL HERE NOW", "WHAT DAY IS IT?".
  - **Night**, once a power-on: "IT'S LATE.", "THE HUM IS LOUDER.", "EVERYONE IS ASLEEP".
  - **Dawn after playing through the night**: "IT FEELS LIKE DAWN".
- **A special minute with the menu open.**
  - The cues line gives way to the time.
  - At night, the music stops and RESUME (or PLAY) reads WAKE until the minute passes.
  - At 3:33 AM in a story run, once a power-on, the dream notices: the act `CRU_ACT_EGG` and lucidity -6.
- **Story runs.** The run's saga changes through the existing hooks, so the truth matrix keeps evolving from play:
  - **The absence**, once per slot per power-on, and only when a saved run is resumed:
    - The run seed is stirred (`cru_story_event`).
    - Just now or minutes: `CRU_ACT_LOOP`, which leans PLAYED, NEVERLEFT and kin.
    - Hours: `CRU_ACT_IDLE`, which leans ASLEEP, NOCLIP and DREAMER.
    - A day: idle, and lucidity +8.
    - A week: idle twice, and lucidity +16 (the head clears).
  - **Night** when a run opens or first falls: `CRU_ACT_DEEP`.
  - **Marathon** at two hours, four, and each hour after: `CRU_ACT_LOOP` and lucidity -4. The longer the player stays,
    the deeper the dream.
  - **Each special minute** stirs the run seed.

## Options and names

SETUP has no NAME row. Names belong to the three story slots:

- A name is given when a run wakes (the naming scene).
- **SELECT on the slot list renames a saved slot** with the same keyboard. A slot cannot be left blank.
- Resuming a slot makes its name the run's leaderboard name.

SETUP's **TIME** row shows the time, or NOT SET, and opens the clock page as a correction.

SETUP's six rows fit inside the panel (rows 10 to 15), below the title card's sky band, whose cloud bands scroll
anything drawn on it off the left edge: MUSIC, SOUND FX, TURN, TIME, HOW TO PLAY, RESET GAME.
B goes back, as the cue line says. Leaving SETUP puts the cursor back on SETUP, not on STATS.

Free play keeps whatever name its save already has, or YOU for a new save.

## Limits

- The clock is only as right as the player says. Frame counting stops while the LCD is off. A host build that runs
  faster or slower than 59.73 Hz runs the clock faster or slower too.
- Without a weeks answer, an absence of a week or more reads as its remainder within the week.
- Absences are global per power-on. Each story slot hears about the one reported at this power-on, not about the time
  since that slot was last played.
