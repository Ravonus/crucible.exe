# CRUCIBLE.EXE time awareness

CRUCIBLE.EXE knows roughly what time it is, how long the player was away, and how long they have been playing. A few
things change because of that, quietly and for a while.

Code: `cartridge/src/crucible_time.c` and `cartridge/include/crucible_time.h`.

## There is no clock chip

The cartridge is MBC5 with RAM and a battery (`tools/build-rom.ts` links with `-Wm-yt0x1B`, `-Wm-ya16`, an 8 MiB ROM). MBC5
has no real-time clock. MBC3+RTC tops out at 2 MiB of ROM, so it is not an option. So:

- **The date and time are asked once, at the first game start.** Never on the title card. The first time a story run
  or free play is started from PLAY on a cartridge whose clock was never set, the menu asks **WHAT IS TODAY?**: month,
  day, year, hour and minute (LEFT/RIGHT a field, UP/DOWN its value, A keeps it, B skips). Then it asks the player's
  **birthday** (month and day, no year; B skips), then the sign loader runs and the game opens. Neither is asked again:
  a skipped clock stays unset (time-of-day effects stay off) until SETUP > TIME sets it; a skipped birthday means no
  sign. A link session (PLAY > LINK) never asks. The answers are kept in the battery save (the records below).
- **A host may set the clock instead.** A host (the website's emulator) can write a **HOST CLOCK** block into SRAM
  before power-on (format below). A valid block is used silently: no date or time is ever asked, the clock is set to
  it exactly, and the time since the last session is real. The block is consumed at boot (its first byte is cleared),
  so a host writes a fresh one before every boot.
- **Between power-ons, the clock stops.** The cartridge has no clock chip, so without a host clock it resumes where it
  stopped: time away is `CT_AWAY_UNKNOWN` and nothing reacts to it. Only play time moves the clock.
- **While the cartridge runs, the clock counts VBlanks.** GBDK's `sys_time` advances once per VBlank. One VBlank is
  70,224 clocks at 4.194304 MHz, which is exactly 4389/262144 s, about 59.7275 Hz. The module keeps the remainder in an
  18-bit fraction, so no time is lost to rounding, however unevenly it is polled (any gap up to 65,535 frames, about
  18 minutes). `feats_toast` polls it every frame on every screen, and so do the menu and the time page. When the LCD
  is off there are no VBlanks, so those moments are not counted. They only happen for a moment at power-on. The
  calendar advances with it: days, months of the right length, leap years (2000 leaps; 2100 and 2200 do not).
- **Everything persists in SRAM** (below).

### Old saves

- A version 1 record (the minute of the week the player last confirmed) is read and migrated: its weekday and time are
  kept, in the week of 2026-01-04 on (no date was ever kept), and it is never asked the date again. SETUP > TIME
  corrects it.
- A save with no clock record at all (from before the time module) is asked once, at its next game start.
- Every older save is asked the birthday once, at its next game start (then never again).

## The HOST CLOCK block (for the website)

16 bytes in **SRAM bank 15 at offset 0x1FE0** (cartridge address 0xBFE0 with RAM bank 15 selected; byte
**0x1FFE0** of the 128 KiB `.sav` file, `bank * 0x2000 + offset`). Free before this change; nothing else writes it.

| byte  | value                                                                                                    |
| ----- | -------------------------------------------------------------------------------------------------------- |
| 0-3   | magic `48 43 4C 4B` ("HCLK")                                                                             |
| 4     | version: 1                                                                                               |
| 5     | flags: 0 (reserved)                                                                                      |
| 6-7   | year, little-endian, 2000..2099                                                                          |
| 8     | month, 1..12                                                                                             |
| 9     | day of the month, 1..28/29/30/31 (checked against the month and leap year)                               |
| 10    | hour, 0..23 (the player's local time)                                                                    |
| 11    | minute, 0..59                                                                                            |
| 12    | second, 0..59                                                                                            |
| 13    | 0 (reserved)                                                                                             |
| 14-15 | CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no final XOR) of bytes 0..13, little-endian |

The cartridge reads it once at power-on. If the magic, version, ranges and CRC are right, it sets the clock to it (the
seconds too), marks the clock known (never asked), computes the time away from its own saved clock (a negative step is
a correction: unknown), and writes 0 over byte 0. Anything else is ignored. Example, 2026-10-05 11:10:30: `48 43 4C 4B
01 00 EA 07 0A 05 0B 0A 1E 00` + the CRC (low byte first). `test/harness/capture_flow.py`
(`host_clock`) builds it.

## What is kept (SRAM bank 15)

There are two 32-byte records, A at bank 15 offset 0x1F80 and B at 0x1FA0. In the core's linear store addresses
(`bank*0x2000 + offset`) they are **0x1FF80..0x1FF9F and 0x1FFA0..0x1FFBF**. On the cartridge both live at
0xBF80..0xBFBF with RAM bank 15 selected.

Bank 15 holds:

| bank 15 offset                                                 | what                                                           | written by                              |
| -------------------------------------------------------------- | -------------------------------------------------------------- | --------------------------------------- |
| 0x0000..0x1AFF                                                 | story slot 0's records, play block, OWNED, RBITS               | core `cru_story.c` (wiped on a new run) |
| 0x1D00..0x1DB7                                                 | story slot 0's `crucible_story` record (magic, 180 B, CRC)     | core `cru_story.c`                      |
| 0x1F00..0x1F06                                                 | easter eggs found ('E' 'G', 4 bytes of bits, DMG flag)         | `crucible_eggs.c`                       |
| **0x1F80..0x1FBF**                                             | **time records A and B**                                       | **`crucible_time.c`**                   |
| **0x1FE0..0x1FEF**                                             | **the HOST CLOCK block (written by a host, consumed at boot)** | **`crucible_time.c`**                   |
| 0x1DB8..0x1EFF, 0x1F07..0x1F7F, 0x1FC0..0x1FDF, 0x1FF0..0x1FFF | free                                                           |                                         |

(0x1DC0..0x1E3F is story slot 0's player record since the fight system; a dump after a story run, a fight and free
play showed 0x1FC0..0x1FFF all zero before the host block moved in.)

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

Record layout, version 2 (little-endian). Version 1 records (bytes 6-7 the minute of the week, 10-11 weeks elapsed,
18-19 weeks*7 + weekday) are still read and migrated.

| bytes   | field                                                                                                                                           |
| ------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| 0-1     | magic 'C' 'T'                                                                                                                                   |
| 2       | version (2)                                                                                                                                     |
| 3       | flags: bit 0 the clock is set (by the player or a host); bit 1 the date and time were asked (answered or skipped); bit 2 the birthday was asked |
| 4-5     | serial (wrapping; the newer valid record wins)                                                                                                  |
| 6-7     | minute of the day (< 1440)                                                                                                                      |
| 8       | seconds (< 60)                                                                                                                                  |
| 9       | this power-on's away bucket (`CT_AWAY_*`)                                                                                                       |
| 10-11   | the date: days since 2000-01-01 (a Saturday)                                                                                                    |
| 12-15   | total play time, seconds                                                                                                                        |
| 16-17   | play time since this power-on, minutes (the previous power-on's, until the next minute)                                                         |
| 18-19   | the last day played (days since 2000-01-01)                                                                                                     |
| 20 / 21 | streak of consecutive days played / best streak                                                                                                 |
| 22-23   | returns with a known absence (a host clock)                                                                                                     |
| 24 / 25 | quick returns (under an hour) / long returns (a day or more), saturating                                                                        |
| 26 / 28 | the birthday: month (1..12, 0 none) / day                                                                                                       |
| 29      | reserved, written as 0                                                                                                                          |
| 27      | commit byte: 0xC7                                                                                                                               |
| 30-31   | CRC-16/CCITT (0x1021, init 0xFFFF) of bytes 0..29                                                                                               |

A write goes to the record that is not the newest valid one, in three steps:

1. Clear the commit byte.
2. Write the 31 other bytes.
3. Set the commit byte last.

This is the dialogue records' protocol. A power cut at any point leaves the previous record valid (checked by cutting
each of the 33 writes and corrupting each byte).

Zeroed SRAM (a save without a clock record), 0xFF (a new battery) or noise has no valid record. The game then
starts a fresh clock (2026-01-01 8:00 AM, unset) and asks at the first game start. It writes nothing until it has an
answer, a host clock, or a minute has passed.
Saves without a clock record load unchanged: the Classic records, the story slots, the dialogue records and the eggs are not read or
written by this module. RESET GAME keeps the clock, like the settings.

## The context API

Dialogue and scenes can ask:

```c
crucible_time_ctx t; crucible_time_context(&t);
```

| field                                 | meaning                                                                                                                                                                                                                                                                                                                     |
| ------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `flags`                               | `CT_F_KNOWN` (the clock was set), `CT_F_REPORTED`, `CT_F_RETURNING` (the first 10 minutes after a set or a host clock: "for a bit"), `CT_F_DEEP` (a special minute at night), `CT_F_PAUSED` (and the menu is showing it), `CT_F_HOST` (this power-on's clock came from a host), `CT_F_ANGEL_DATE` (day = month: 3/3, 11/11) |
| `part`                                | `CT_NIGHT` 22:00-04:59, `CT_DAWN` 05-07, `CT_DAY` 08-17, `CT_DUSK` 18-21                                                                                                                                                                                                                                                    |
| `weekday`, `hour`, `minute`           | the clock (0 = SUN)                                                                                                                                                                                                                                                                                                         |
| `away`                                | `_FIRST` (just set), `_UNKNOWN` (no host clock: the clock only knows play time), `_NOW` (< 5 min), `_MINUTES` (< 1 h), `_HOURS` (< 1 day), `_DAY` (< 1 week), `_WEEK` (the last five need a host clock)                                                                                                                     |
| `year`, `month`, `day`, `date`, `mod` | the calendar: years since 2000, 1..12, 1..31, days since 2000-01-01, minute of the day                                                                                                                                                                                                                                      |
| `bmonth`, `bday`                      | the player's birthday (0 0: none)                                                                                                                                                                                                                                                                                           |
| `marathon`                            | 0; 1 after two hours since power-on; 2 after four ("forever")                                                                                                                                                                                                                                                               |
| `special`                             | `CT_SP_*`: 1:11, 2:22, 3:33, 4:44, 5:55, 11:11, 12:34, 4:04, 10:10, 12:12, and the mirrored 12:21 and 10:01 (all on a 12-hour face, so AM and PM), midnight, noon. `CT_ANGEL(sp)`: all but 4:04, midnight and noon                                                                                                          |
| `streak`                              | consecutive days played, by the player's clock                                                                                                                                                                                                                                                                              |
| `session_min`                         | minutes played since power-on, saturated at 255                                                                                                                                                                                                                                                                             |

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

SETUP's **TIME** row shows the time, or NOT SET, and opens the date and time page as a correction (never an
absence).

SETUP's six rows fit inside the panel (rows 10 to 15), below the title card's sky band, whose cloud bands scroll
anything drawn on it off the left edge: MUSIC, SOUND FX, TURN, TIME, HOW TO PLAY, RESET GAME.
B goes back, as the cue line says. Leaving SETUP puts the cursor back on SETUP, not on STATS.

Free play keeps whatever name its save already has, or YOU for a new save.

## Limits

- The clock is only as right as the player (or the host) says. Frame counting stops while the LCD is off. A host build that runs
  faster or slower than 59.73 Hz runs the clock faster or slower too.
- Without a host clock, an absence is unknown: the clock resumes where it stopped.
- Absences are global per power-on. Each story slot hears about the one reported at this power-on, not about the time
  since that slot was last played.

## The birthday, the sign and the loader

- **The sign** (`time_sign`): the tropical sun sign of the birthday, from a 12-entry table of cusp days (ARIES from
  MAR 21 .. PISCES from FEB 19). The host test checks all 366 days of a leap year.
- **Its only effect on play is seed randomness:** a new story run's seed (`talk_intro_hash`) has a few bits of the sign
  folded in. Nothing else about the birthday reaches the rules, fights or balance.
- **The loader** (`crucible_sky.c`): each time a game is started or continued from PLAY (story or free play), about one
  second (64 frames) before it opens: the player's sign mark (16x16, four sprites) dissolves in, its ordered dither
  crawls, and it dissolves out, with the moon's phase (8x8) beside it while the mark shows. No birthday: a neutral mark.
  OAM 0..4, OBJ tiles 0..4 (VRAM bank 0), palette 6; the title bust is hidden meanwhile.
- **The marks** (`cartridge/data/include/crucible_sign_marks.h`, generated by the art toolchain): twelve original 16x16 marks and a neutral one, one bit a pixel (32 bytes each); the cartridge shades them
  with the KEEL ordered dither (lit from the top left, three colours). Moon tiles: 8 x 16 bytes.

## The sky (`crucible_sky.c`, flavour only)

Integer approximations, a day's error budget (the clock is the player's local time; the tables are UTC dates):

| what                    | how                                                                                                                                                                                                                                                                                                                    |
| ----------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| the moon's 8 phases     | a mean synodic month of 42,524 minutes (29.530589 d) from the new moon of 2000-01-06 18:14 UTC; full and new are 18 hours either side (the true moon wanders up to about 14 hours from the mean)                                                                                                                       |
| blue moon               | a full moon whose previous one fell in the same month                                                                                                                                                                                                                                                                  |
| supermoon               | a full moon within 1.5 days of perigee (anomalistic month 39,679 minutes from 2016-11-14 11:23)                                                                                                                                                                                                                        |
| equinoxes and solstices | MAR 20, JUN 21, SEP 22, DEC 21, a day either side                                                                                                                                                                                                                                                                      |
| meteor showers          | the peak nights: JAN 3, APR 22, MAY 6, AUG 12, OCT 21, NOV 17, DEC 14                                                                                                                                                                                                                                                  |
| Friday the 13th         | the weekday and the day                                                                                                                                                                                                                                                                                                |
| eclipses                | the 34 solar and 35 lunar eclipses of 2026-02-17 .. 2040-11-18 as day gaps (68 bytes) and a type bit each (9 bytes); outside those years the date is wrapped by the Saros (6,585 days), so they keep coming, drifting slowly from reality; the Saros' last three years (2040-11 .. 2044-02, and their wraps) have none |
| backward months         | one known start each, repeated by the synodic period: Mercury 2026-02-26, 23 days, every 115.88 d; Venus 2026-10-03, 42 days, every 583.92 d; Mars 2027-01-10, 80 days, every 779.94 d (rough)                                                                                                                         |

`sky_event` picks the day's strongest: an eclipse, the player's birthday, a blue moon, a supermoon, a full moon, a new
moon, an equinox or solstice, a meteor shower, Friday the 13th, then a backward month. The host test checks the moon
against Meeus' true phases for 2026-2027 (every new and full moon seen within a day), the eclipses (2026-08-12,
2027-08-02, 2026-03-03, 2026-08-28 and all 69 in the table, the Saros wrap both ways), and each fixed day.

## Rare lines and angel minutes (crucible_talk.c, crucible_flow.c)

- **Flavour, about one talk in 32** (`roll() & 31` of the talk's own seed: between the 1-in-20 and 1-in-50 asked
  for): the talker's greeting gives way to one line, chosen by the day: the sky event's line, else the player's sign
  (half the time, when there is one), else a general one; never the same line twice running. No choice, no truth
  weight, no new branch. Lines (mystic, sky, sign) are oblique on purpose: the moon, the
  clock and the sign are never named.
- **An angel minute** (`CT_ANGEL`: 1:11, 2:22, 3:33, 4:44, 5:55, 11:11, 12:34, 10:10, 12:12, 12:21, 10:01): a talk that
  opens in it is led: its greeting says the time ({SECTOR} is "11:11"; half the time the time also sits in the frame's
  top-right corner), a lead line comes before the question, a line after the answer, and the answer leans the truth
  matrix once more: **one extra `cru_story_act`**: the chosen answer's own act, or, for an answer with no act, AGREE
  when it was taken warmly and REFUSE otherwise. That is +3 to each truth cell the act leans (what one act adds),
  once per talk. Once per talk, so once per angel minute at
  most from a called-in talk. No new choices or branches.
- **Called in:** something meaningful the minute before an angel minute (a mix, a talk's answer, a fight won:
  `time_mark_act`) makes someone step in when the minute turns, through the director's forced path ("SOMEONE STEPS IN",
  `force_(FLOW_TALK)`), at most once per angel minute, never while linked, never over an encounter already showing; free
  play gets it too (a talk, the visitors' kind).
- **A count on the shelf:** 111, 222 .. 999 things owned calls someone in the same way; its greeting says the count.

Budget (ROM 7f988286 build receipt): `crucible_time` 7,595 bytes (+1,766), `crucible_sky` 3,289 (new: code, the eclipse
and planet tables, 13 marks and 8 moon tiles), the line bank +1,166 packed bytes (36 lines; a third chunk,
`crucible_lines_text_2`, 1,240 bytes), menu +~400, talk +~520; the main module +19 bytes (the loader's hook). All placed
by the autobanker in existing free space; the catalogue stays 5,629 objects with `CGB_CODE_RESERVE_BANKS` 7.
