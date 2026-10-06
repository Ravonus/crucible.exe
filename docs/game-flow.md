# CRUCIBLE.EXE game flow (Game Boy Color)

One straight path: **the bench is home**. You make things there; the book, titles and settings are one START away; talks
and fights come to you at the bench, never from a menu; chapters move because of what happens there.

Code (in `cartridge/src`): `crucible_flow.c` (the encounter director, cue rows, the pan), `crucible.c` (screens),
`crucible_menu.c`, `crucible_filter.c`, `crucible_storyrun.c`.
Playthrough: `test/harness/capture_flow.py` (see [Verification](#verification)).

## Before: the menu-driven flow

```
power-on ─ title card ─ WHAT TIME IS IT? ─┐
                                          v
   ┌──────────────── MENU (2x4 grid) ─────────────────────────────────┐
   │ PLAY/RESUME  TALK*      BOOK    AWARDS                           │
   │ TITLES       RANKS|FIGHT|SEND   STATS   SETUP                    │
   └─┬───────┬──────┬───────────┬────────┬────────────────────────────┘
     │       │      │           │        └ SETUP: music, sfx, turn, time, how to play, reset
     │       │      │           └ AWARDS / TITLES / RANKS / STATS: four items, one ledger (tabs)
     │       │      └ FIGHT (story only): a rival's champion, or a random faction's
     │       └ TALK (always there): a generated visitor; every 3rd talk advances the chapter
     └ PLAY: STORY (3 slots, SELECT renames) | FREE PLAY | LINK (host/join, lobby)
             │
             v
          BENCH ── A add/mix ─> MERGE ─> REVEAL ─> BENCH
            │  B put back   START menu   SELECT filter ("SE TAGS", only with nothing held)
            ├─ after a mix (story): a hostile faction (<= -50) -> FIGHT at once, no warning (1 in 4)
            ├─ after a make: a visitor -> TALK at once (story: from 3 makes, free: from 6)
            ├─ a miss in a run -> the machine's line; the run lost -> the title card
            └─ secrets: eggs (title SELECT taps, codes, idle title, relic SELECT, names, makes)
                        rooms (door: UP past the top; sky: SELECT during a mix; edge: hold B + push)
```

What was wrong:

| #   | Problem                                                                                                                     | Why it hurt                                                                                    |
| --- | --------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------- |
| B1  | FIGHT and TALK were menu items (TALK always; FIGHT in place of RANKS during a run)                                          | Story beats were a menu choice, not play; talks could be farmed from the menu to push chapters |
| B2  | Story fights and visits interrupted the bench instantly, with no warning and no way back to the same view                   | Jarring; nothing telegraphed                                                                   |
| B3  | Four menu items (AWARDS, TITLES, RANKS, STATS) opened one ledger whose LEFT/RIGHT already turns its tabs                    | Duplicate routes; the grid was crowded                                                         |
| B4  | The filter hint read "SE TAGS", appeared only with nothing held, and the screen called itself FILTER                        | Unclear what SELECT did; two names for one thing                                               |
| B5  | The filter screen was a 12-row scrolling list (TYPES, THEN TRAITS) with LEFT/RIGHT paging by 12; the cursor row was not lit | Half the choices off screen; selection unclear                                                 |
| B6  | In a story run the HUD wrote over the header's filter label                                                                 | The active filter was invisible on the story bench                                             |
| B7  | No way out of a story run back to the title (only a soft reset)                                                             | A dead end: free play and other slots unreachable                                              |
| B8  | B with nothing held did nothing on the bench                                                                                | B was not "back" there                                                                         |
| B9  | START was both "menu" and "apply" on the filter screen; SELECT both "back" and "open"                                       | The grammar was inconsistent                                                                   |

## After: one path

```
power-on ─ WHAT TIME IS IT? ─ TITLE: PLAY · BOOK · TITLES · STATS · SETUP
                                 └ PLAY: STORY (slots) | FREE PLAY | LINK
                                          v
  ┌──────────────────────────── BENCH (home) ─────────────────────────────┐
  │ A add/mix · B back (put back, then clear the filter) · SELECT filter   │
  │ START: RESUME · BOOK · TITLES · STATS · SETUP · LEAVE (or SEND, linked) │
  │                                                                         │
  │   a face peeks in above ──UP──> pan up ──> TALK ──> pan back ──┐       │
  │   eyes at the floor's edge ──DOWN──> pan down ──> FIGHT ─> pan back ─┤  │
  │   "!! IT COMES !!" / "SOMEONE STEPS IN" (2 s) ──> pan ──> FIGHT/TALK ─┤ │
  │   ignored hints fade after ~20 s (progression notices)                        │
  │   chapters advance from talks; each new chapter sends its gatekeeper    │
  └────────────────────────────────────────────────────────────── same bench┘
```

- **Talks and fights are never menu items.** They happen at the bench, either forcing themselves in (telegraphed) or
  waiting at the edge for you to approach.
- **The bench is always home.** Every encounter pans away from the bench and pans back to the same bench: same focus,
  same held item, same filter.
- **The menu is short:** PLAY (title) or RESUME (pause), BOOK, TITLES, STATS, SETUP, and a sixth cell only when it
  means something: SEND while linked, LEAVE on the pause menu (the run is saved; back to the title card).

## Encounters

`crucible_flow.c` decides, after every mix (`flow_after_mix`, fed the story run's reaction) and on the bench every
frame (`flow_tick`).

Other hosts share the same director as portable C, `core/src/cru_encounter.c`, as it stood before the fight system.
The cartridge does not link that file: its `crucible_flow.c` adds rules that read the player record and the fight
tables, so the two differ. Only the cartridge: no fight
until there is a bag to bring (twelve things on the shelf; THE CRUCIBLE, crucible-fight-system.md), the first fight is
always the first duel, the first champion follows ten makes after it, a duelist of a disliked
faction may walk in (1/16, +1/32 per hostile faction), chapter 4 and the finale send a gauntlet, a co-op session keeps
its encounters, and a new chapter gives 50 XP. Forced talks roll `roll() % 5` (the core: `roll() < 52`), so seeded runs
differ even where the rules agree. Bringing the cartridge onto the shared file means moving those rules into the core
behind host hooks first.

### Two ways in

| Way         | What the player sees                                                                                                                                                                                                                                       | What happens                                                                                                                                                                                                              |
| ----------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Forced**  | The sign row (row 17) reads `!! IT COMES !!` (a fight) or `SOMEONE STEPS IN` (a talk); the hint flickers fast at its edge; a tick sounds every half second                                                                                                 | After 2 s (120 frames) the view pans to it by itself. Pressing its direction goes at once                                                                                                                                 |
| **Waiting** | A hint at the bench's edge: a **face peeking in under the header** (a talker; OBJ tile 93) or **horned eyes at the floor's edge** under the shelf (a fighter; OBJ tile 94). With nothing held, the sign row reads `UP: SOMEONE WAITS` / `DOWN: IT WATCHES` | **UP** (talker) or **DOWN** (fighter) approaches: the view pans there. Only the hinted direction changes meaning; the other keeps its shelf job. Ignored, it flickers for its last 3 s and fades after 20 s of bench time |

Direction is meaning: **above is a voice, below is trouble.**

### The pan

The background scrolls (SCY) 112 pixels, eased, over 30 frames, into the hidden map rows 18..31, which are painted as a
fluorescent corridor receding from the bench (walls closing in, tube lights, humming carpet static, a `?` or `!` at the
far end): the noclip into the encounter. Sprites are off during the pan (clouds, overlays and the cursor do not scroll
with the map), the toast window is hidden, and the last frames tear sideways. The scene then opens with its usual glitch
(`fight_go` / `talk_enter`), and SCY returns to 0. Coming back (`to_bench`), the bench is drawn with the view still in
the corridor and pans home. Presses made during a pan are dropped (an A that closed a talk must not ADD on the bench).

### Random story arrivals

In Story, a separate clock brings a visitor or champion after about 18..44 seconds of active bench time, even on a
friendly starter-only save. It pauses in menus, recipes, reveals and encounters, and while linked. A random arrival
uses the existing UP/DOWN cue and waits for an approach; it never forces a scene, and ignoring it has no standing or
lucidity penalty. At most two random arrivals of one kind occur before the other gets a turn. Its next interval begins
after the encounter or the hint fades. Boss faction and timing come from the director's seed, without requiring a
DREAM discovery, a previous duel or hostile standing. Existing progression encounters retain their triggers and stakes.
Free Play keeps its existing visitors and has no bosses.

Conversation headers name both faction and attitude, such as `PROGRAM: FRIEND`. HOW TO PLAY explains that making and
answering affect standing, friendly factions help, and hostile factions may fight. It also explains optional arrivals,
the cost of Story fights, and the avatar's LOOK and reroll controls. These are existing screen and help-page locations.

### Triggers and pacing (seeded)

Rolls use a xorshift seeded from the bench's stirred rng (`core.rng`, moved by every press) and DIV; the story's own
signals (`story_after_mix`) come from the run. Counts are in makes (mixes).

| Trigger                                                                                                               | Mode                     | Comes as                                                                                                                               | Rule                                                                                                                                  |
| --------------------------------------------------------------------------------------------------------------------- | ------------------------ | -------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| A miss destroyed an element / the run is lost / a lost piece's first glitch                                           | story, free (lost piece) | the machine, at once                                                                                                                   | unchanged: `STORY_LOSS` / `STORY_OVER` go straight to the machine's lines                                                             |
| A visitor (`STORY_VISIT`: story from 3 makes since the last, rising with the chapter; free play from 6 makes, 1 in 4) | both                     | a talk: forced 1 in 5, else waiting above                                                                                              | at least 2 makes after any encounter; a visit that cannot come yet (a breath, someone already waiting) is owed, not lost              |
| A **hostile** faction (standing <= -30; -50 until 2026-10-05) sends its champion (`STORY_BOSS`, 1 in 4 makes)         | story                    | a fight                                                                                                                                | 6+ makes since the last fight: waits below; 12+: forced. Not due yet: a voice may come instead (5+ makes since the last talk, 1 in 3) |
| A champion already waiting, and the faction is still hostile 12+ makes on                                             | story                    | the fight forces itself in                                                                                                             | "loses patience"                                                                                                                      |
| A **rival** (standing <= -8, wary or worse; -15 until 2026-10-05: honest play never got there)                        | story                    | a fight, waiting below                                                                                                                 | 8+ makes since the last fight, 1 in 4; certain right after a lost piece would not form ("it has it")                                  |
| **The first champion** (the nemesis' first meeting), once the first duel is fought                                    | story                    | a forced fight                                                                                                                         | 10+ makes since the last fight                                                                                                        |
| The **nemesis** that fled (met 1..2 times)                                                                            | story                    | a forced fight                                                                                                                         | 10+ makes since the last fight, 1 in 2                                                                                                |
| **A new chapter** (the dialogue cadence advanced it during a talk)                                                    | story                    | chapter 1+: a forced fight with its gatekeeper (a rival, else the coldest faction); with no bag yet it is owed, not turned into a talk | two makes after the chapter turns                                                                                                     |
| **Back after hours or more** (the power-on answer: `CT_AWAY_HOURS`, `_DAY`, `_WEEK`, within the 10 returning minutes) | both                     | a talk waiting above                                                                                                                   | once per power-on                                                                                                                     |
| **A special minute** (1:11, 3:33, 4:04, 11:11, midnight...)                                                           | both                     | a talk waiting above                                                                                                                   | once per special minute                                                                                                               |
| Ignored progression hint                                                                                              | story                    | (fades)                                                                                                                                | a fighter: that faction's standing -3 (the grudge grows); a talker: the act `CRU_ACT_IDLE` (the dream leans to standing still)        |

Never: a fight in free play (fights cost elements; the Classic save is never at stake), any encounter in a race or a fight session (a co-op session keeps them: its bosses are shared), a
hint over the room's door anomaly (UP is the door's while it shows), two encounters at once.

Free play's choice: **occasional visitors only** (from the run's visit signal, the clock's return and special minutes),
mostly waiting above, now and then stepping in.

Measured in the playthrough (one faction set wary, -30, at the start of a run; everything else from play; the script
approaches every hint and answers fights blindly): about one encounter per 3 to 4 makes, two thirds of them waiting
hints (5 talks and 3 fights, 5 approached, 3 forced, 1 ignored and faded; chapter 0 to 2 in about 30 makes). A player
who ignores hints meets only the forced ones, about one per 8 to 10 makes. Free play: a visitor every 6 to 10 makes.

### Chapters

Chapters advance on the dialogue cadence (every third conversation, `dialogue_cadence`), and conversations only happen
in play, so the story moves with what you make. Each chapter turn sends its gatekeeper two makes later (above). Rooms
unlock at chapter 3 in a run (64 discoveries in free play).

## Reveal: the card comes up at once (2026-10-05)

The owner's complaint: after the merge the game sat on a frozen frame for seconds before the card ("what you made")
appeared. It was not slow drawing: the main loop was blocked. Measured with a PyBoy timing probe ( 34
fresh discoveries in a row, frames counted from the mix's confirm; a "still" is a frame identical to the one before):

|                                                                         | before (ROM 098e9190) | after (ROM a13fa4a2) |
| ----------------------------------------------------------------------- | --------------------- | -------------------- |
| merge start to the card readable, median / worst                        | 363 / 490 frames      | 86 / 89              |
| after the merge's animation (~68 frames): longest still, median / worst | 32 / 36 frames        | 4 / 9                |
| frozen frames (stills of 4+) after the animation, median / worst        | 247 / 384             | 8 / 13               |
| the object turning (overlay view advancing), median / worst             | 365 / 495             | 125 / 161            |
| a known result back on the bench / longest still                        | 34 / 10               | 25 / 6               |
| a failed mix back on the bench / longest still                          | 63 / 11               | 55 / 6               |

Where the time went, and what replaced it:

1. **The turn phase waited for the whole turntable and overlay** (`crucible_reveal.c` reveal_turn: up to 300 frames) and
   decoded at the urgent rate, 16 rows a tick: one tick blocked the loop 4..6 frames. Now the merge goes straight from
   the alternation to a four-frame flash, and every decode tick in the merge and on the card is the idle rate (one
   frame's worth; the alternation does the art and the overlay on alternate frames).
2. **The commit wrote the 512-byte save record in one go** (`cru_mix_finish`: about 9 frames of SRAM writes through the
   store, plus 2..3 for the play memory and feats). The core now writes a record as a resumable job
   (`cru_mix_finish_later`, `cru_save_step`, `cru_save_flush`, core): the commit runs at the start of the
   alternation (inside its first hold), and the record goes out 24 bytes a frame under the merge, the card or the bench.
   The play memory changes only once the record is whole (the journal order holds; a torn record still falls back to
   the other slot), and a mix, a save, another screen or closing the card finishes it first. The bench redraws once it
   lands (a failed pair's cross).
3. **The card's scene was drawn in one go** (`scene_draw`: 18 rows, about 9 frames of VRAM writes with the screen on).
   Scenes now draw in parts (`scene_begin`, `scene_rows`; living rooms `room_begin/row/end`): the tiles go in under the
   flash, the rows two a frame, and a baked scene over the bench skips the rows and tiles it shares with it (rows 0..8
   and 16, 28 tiles). Then the card's text and its object a part a frame. The fade from white runs on real frames (10).
4. **The object shows at once on its keyframe and turns through the views already decoded** (a turntable cell cycles
   the cached views while the rest decode; the owner chose speed over the old "never a still keyframe" rule). Its
   turntable is queued from the first frame of the merge (and predicted while the second ingredient is chosen).

`test/harness/capture_discoveries.py` now checks that the card's object turns within 300 frames with its overlay: 24 of 24, a median
of 33 frames after its cell shows (13..64, one heavy object 120); the cell shows 21 frames after the switch.

The merge uses elapsed VBlank time, shorter holds and exact lookup tables for the original fusion geometry. While
choosing the second ingredient, the likely recipe result is prepared in the background; result jobs preempt lower
priority art, and the approach and alternation also decode ahead. The reveal no longer waits for either turntable.
In an isolated emulator comparison, FIRE + WATER reached its first discovery reveal in 154 frames instead of 376;
repeating it reached the bench in 47 instead of 133. WATER + STEAM (no recipe) took 79 instead of 127. These are
sample timings, including the input and screen transition, not a bound for every cold object.

Music keeps its melody and bass channels throughout merges, reveals, talk and fight cues. Effects use pulse 1 and
noise over the score. Object voices stir the music's own seed and temporarily change the lead octave and duty,
without changing gameplay randomness or lowering the music volume. Music and SFX retain independent volume controls.

## Link

Two players on a cable. The bytes move by interrupts (`crucible_link_io.c`, bank 0): every VBlank the HOST clocks its
next byte (internal clock), the serial interrupt takes the byte that came back into a ring and, on the GUEST, arms the
next one (external clock). One byte crosses each way every frame, however slow the game's loop is (a fight's loop can
take 26 frames). Packets (`crucible_link.c`) are `A5 type a.lo a.hi b.lo b.hi sum` with escapes; the full table is in
`docs/fight-system.md` 9.6.

From the title card: PLAY > LINK > HOST or JOIN > **which save to bring** (FREE PLAY or a story slot: that save is your
game for the session; only your own cartridge ever writes it), then the **link room**: the host sets MODE (`CO-OP` /
`RACE` / `FIGHT`), TIME and GOAL (race), or for FIGHT the RULES (FAIR / STRENGTH / CHAOS / custom rows) and the EDGE
(the suggested handicap); START. The guest sees the rules as they change and SELECT asks for others (a `?` by its name
on the host's cable row). The cable row shows your handheld, the cable (a spark down a dashed line while waiting, a
solid humming line when linked) and the partner's name. The host's last rules are kept in its record.

| Mode  | What happens                                                                                                                                                                                                                                                             | Ends                                                           |
| ----- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------- |
| CO-OP | each make crosses (P_MADE, acked): the first 12 new to your save are kept (`cru_grant`, the counter saved first), all are toasted (3 s apart, NEW, +N MORE); a miss is a short noise on the other side; a boss met by either is **shared** (below); SEND gives the focus | the cable, or TIME when one is set (none by default)           |
| RACE  | to GOAL new finds; scores travel each second (HUD `05:03 9:59`); their finds are toasted with their score                                                                                                                                                                | the GOAL (YOU WIN / THEY WIN), the TIME (or A DRAW), the cable |
| FIGHT | the versus fight in lockstep, best of 3 (or 1, 5)                                                                                                                                                                                                                        | the match, NO CONTEST when the cable breaks it                 |

**Shared bosses (co-op).** The player who meets a boss is its owner: P_CUE and the boss's setup cross in chunks, and the
partner is pulled in at its next safe point (its bench, idle: the reveal of a mix finishes first); the owner waits up to
10 s, else plays alone and the partner later reads "SOMETHING HAPPENED ELSEWHERE". Both run one engine on the owner's
hand, HP the average of both players' plus 2; who answers each round comes from the seed (never three in a row); the
watcher points with SELECT and once a phase takes the round by holding B. Three seconds without a packet and each side
finishes alone. Each shared scene moves your alignment a little toward the partner's (at most 2, 6 a session) and the
partner drifts into your dream's seed and memory.

**Network tolerance** (a cable bridged over WebRTC moves a byte every 100-200 ms at worst): nothing assumes timing.
Everything that matters is said again until the partner says it arrived: GO (each second until the guest's HELLO says
its session began), the versus setup (a packet at a time on an idle line, until both hold all), each turn (P_FTURN,
every half second until acked, a partner still on the last turn gets that one again), each make (P_MADE, one in flight,
each second until acked), a shared scene's setup and each of its answers, the race's END. Timers run on real frames, not
the loop's capped step. Silence: 3 s in the room, 8 s in a session (the session ends: LINK LOST, or NO CONTEST for a
fight); 3 s in a shared scene hands it to the local player. A desync (the state hashes differ) is repaired by the host's
state, streamed a chunk at a time and adopted only whole and only for the turn it was taken at.

**What a bridge must do** (a network bridge such as the website's netplay, or any emulator link; ROM 533bec8e on,
transport unchanged since the fight system's step 18). Earlier ROMs (2dc6caa8) armed and clocked one byte per main-loop
turn; from 533bec8e the bytes are moved by interrupts, so a bridge built for the earlier ROMs needs the points marked
_new_.

- **Clocking.** The HOST starts a transfer with `SC=0x81` (internal clock) once per VBlank, with `SB` = its current
  byte. The GUEST keeps a transfer armed with `SC=0x80` (external clock): it re-arms _inside its serial interrupt_,
  immediately after each completed transfer, and again at VBlank if it finds itself unarmed. _New:_ an armed guest is
  therefore always pending, so a bridge must complete **at most one guest transfer per emulated frame** (as a real host
  clocks it), never each time the emulator stops on the guest's arm. Completing them as fast as they arm makes the
  guest free-run (370,601 transfers in about 40 s in a test), starves its main loop and it never sends a packet.
- **Rate.** One byte each way per frame (60 B/s). Never deliver more than one byte a frame to either side: the receive
  ring is 32 bytes and is read once per main-loop turn (a fight's turn can take 26 frames); overflow is counted in
  `lk_rxlost` and costs a resend.
- **Bytes.** `00` is idle. A live partner never sends `FF` (`FE`/`FF`/`A5` inside a packet go out as `FE 00` / `FE 01` /
  `FE 02`). Packets are `A5 type a.lo a.hi b.lo b.hi sum`, sum = `5A` + the five body bytes.
- **FF = not delivered.** A host that reads `FF` sends the same byte again next frame; return `FF` to a host only when its
  byte was _not_ forwarded (no guest armed, cable out), or the byte arrives twice. The guest drops `FF`.
- **Completion time.** A host transfer not completed within 4 VBlanks is clocked again with the same byte (a watchdog):
  complete each host transfer within 4 frames (at once is best), or a byte forwarded at the first clock is duplicated.
- **No per-byte round trips.** The byte a side returns is its own next queued byte (or `00`), never a reply to the byte
  it receives; a bridge may answer at once from a local queue and ship whole packets with any latency.
- **Acks are packets, not bytes.** Resent until acked: GO every 60 frames until the guest's HELLO has bit 7 set;
  P_FTURN every 30 frames; P_MADE every 60; P_BEAT every 30; the versus setup a packet every 6 frames on an idle line;
  P_SEED every 120 frames for a session's first 20 s; HELLO / names / rules every 60 frames on an idle line.
- **Timeouts** (real frames): no valid packet for 180 frames (3 s) in the room = UNLINKED; 480 (8 s) in a session = LINK
  LOST, or NO CONTEST for a FIGHT; 180 in a shared scene hands it to the local player; the boss owner waits 600 frames
  (10 s) for the partner's READY. So the one-way latency must stay well under ~2.5 s in the room and ~7 s in a session.
- **The room's pages** (for a scripted pilot): PLAY > LINK > HOST / JOIN > BRING WHICH? (FREE PLAY or a story slot) > the
  room. A FIGHT room shows MODE / RULES / EDGE / SEED and no GOAL; the cable row reads `[#]` + a dashed or solid line +
  the partner's name.

**Verified** with `test/harness/linkbridge.py`, which plays the wire between two PyBoy instances (hooks on the serial
interrupt's read of `SB`, the guest's interrupt raised by the bridge), with noise (bit flips), per-byte delays and
injected packets: `test/harness/capture_link.py` (co-op, race, draw, a pulled cable, a whole versus match), and the fight
system's link checks (1% flipped bits, 0..6 frames a byte).

## Button grammar

| Button        | Everywhere           | Notes                                                                                                            |
| ------------- | -------------------- | ---------------------------------------------------------------------------------------------------------------- |
| **A**         | choose, confirm, add | bench: ADD then MIX; book: USE; filter: SHOW; talk: next / answer; fight: answer                                 |
| **B**         | back, cancel         | bench: put the held thing back, else clear the filter (`B ALL`); book, filter, records, setup: back; talk: leave |
| **START**     | the menu             | from the bench; START also closes the book; on the filter screen it does nothing                                 |
| **SELECT**    | filters              | bench and book: open the filter; filter screen: clear it (ALL)                                                   |
| **UP / DOWN** | shelf groups         | only while a hint shows, its direction approaches it                                                             |

Hidden and never shown: A+B+START+SELECT resets; SELECT taps and codes on the title (eggs); SELECT while a
relic talks; the room secrets (UP past the top at the door, SELECT during a mix at the sky glitch, hold B and push toward
the edge glitch).

Cue rows:

| Screen                   | Row 16                                      | Row 17                                                                                  |
| ------------------------ | ------------------------------------------- | --------------------------------------------------------------------------------------- |
| Bench, nothing held      | `Ⓐ ADD` (`Ⓑ ALL` with a filter) `SE FILTER` | `ST MENU`, or a waiting hint (`UP: SOMEONE WAITS` / `DOWN: IT WATCHES`), or a telegraph |
| Bench, holding           | `Ⓐ MIX Ⓑ BACK SE FILTER`                    | `NO RESULT` / `=RESULT`, or a telegraph                                                 |
| Reveal                   | `Ⓐ OK`                                      |                                                                                         |
| Book                     | `Ⓐ USE Ⓑ BACK SE FILTER`                    |                                                                                         |
| Filter                   | `Ⓐ SHOW Ⓑ BACK SE ALL`                      |                                                                                         |
| Menu, setup, talk, fight | `Ⓐ CHOOSE Ⓑ BACK` etc.                      |                                                                                         |

## Filters

One screen, one grid, all twenty on screen: **TYPE** (ALL and the seven categories) on the left, **TRAIT** (twelve) on
the right. The arrows move the cursor (LEFT/RIGHT switch columns, keeping the row), the cursor's row is lit (the cream
strip), a star marks the filter in use, the header names it, and the line under the grid counts what is under the
cursor (`ELEMENT 04/226`, or `COLD: NONE YET`, dim). A shows only that (refused while none are found), B goes back
unchanged, SELECT clears it. On the bench the header shows the filter (free play: the header's middle; a story run: the
HUD alternates it with the miss odds), and B with nothing held clears it.

## Menu

`PLAY`/`RESUME`, `BOOK`, `TITLES` (the ledger on its titles tab; LEFT/RIGHT turn to awards and ranks), `STATS` (the
ledger's stats), `SETUP`, and the sixth cell: `SEND` while linked, `LEAVE` on the pause menu, empty on the title card
(the cursor skips it). The time's special minute turns RESUME/PLAY into WAKE. HOW TO PLAY's pages say the
grammar and, cryptically, the encounters: "NOT ALONE. A FACE ABOVE? PRESS UP. EYES BELOW? PRESS DOWN."

**The title card asks nothing.** The time ask left the power-on flow: the first time a story run or free play is
started from PLAY on a cartridge whose clock was never set, the menu asks the date and time (WHAT IS TODAY?) and then
the birthday, once each, then the sign loader (about a second) opens the game; later starts show only the loader. A
link session never asks. A host (the website) can leave a HOST CLOCK block in SRAM so the date is never asked. Details:
`time-awareness.md`. On a fresh cartridge the harnesses (`test/harness`) answer the two asks after choosing a game.

## Systems kept

| System           | Where it surfaces                                                                                                                                                            |
| ---------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Time             | the power-on ask; whispers on the bench; a voice waiting when you come back after hours or at a special minute; WAKE at night                                                |
| Rooms            | unchanged (home room until 64 discoveries / chapter 3); the waiting talker never covers the door anomaly                                                                     |
| Lost pieces      | unchanged (the torn result cell, the machine's first-time line); a rival lurks right after a lost piece would not form                                                       |
| Material overlay | unchanged (OAM 0..23); sprites off only during the half-second pan                                                                                                           |
| Dialogue matrix  | unchanged; ignoring a voice leans it (`CRU_ACT_IDLE`)                                                                                                                        |
| Fights           | unchanged mechanics, palette lease and outcomes; started by the director instead of the menu                                                                                 |
| Saves, link      | each save written only by its own cartridge; a player record per story slot (fight spec 10.4); co-op keeps its encounters (bosses shared), race and fight sessions have none |

## Hardware

- **OAM:** 0..23 material overlay; 24..31 clouds; 32..33 motes; 34..36 rooms; 37 loading spark; **38 the encounter
  hint**; 39 cursor. Fusion and busts use 0..35 during merges and talks.
- **OBJ tiles:** 93..94 in VRAM bank 0 (the peeking face and the lurker), between the UI sprites (64..92) and the
  rooms (112..127). OBJ palette 4 (the spark palette: cyan, white, magenta).
- **BG map:** rows 18..31 (otherwise never shown) are painted for the pan.
- **Banks** (bytes of code per module from the linker map; before = the menu-driven flow above, after = everything here: flow,
  reveal, link):

| Module                       | Before | After | Delta | Free  |
| ---------------------------- | ------ | ----- | ----- | ----- |
| `crucible` (frozen at 16361) | 16138  | 15773 | -365  | 588   |
| `crucible_fight`             | 2764   | 5057  | +2293 | 11327 |
| `crucible_filter`            | 1784   | 1526  | -258  | 14858 |
| `crucible_flow` (new)        |        | 3362  | +3362 | 13022 |
| `crucible_link`              | 2394   | 3094  | +700  | 13290 |
| `crucible_menu`              | 8140   | 8746  | +606  | 7638  |
| `crucible_overlay`           | 14703  | 14903 | +200  | 1481  |
| `crucible_reveal` (new)      |        | 960   | +960  | 15424 |
| `crucible_room`              | 7648   | 7665  | +17   | 8719  |
| `crucible_storyrun`          | 1986   | 2038  | +52   | 14346 |

## Saves

**No save change.** The director keeps its state in RAM for the power-on: the hint, the telegraph, the gaps, the
chapter edge, the clock triggers. A story run's lasting effects (standing, the dream's leanings, the chapter) go through
the run's existing versioned, CRC'd record (`cru_story_save`). Link games add no state to a save either: a co-op find, a gift and a bout's prize all go through
`cru_grant`, the same path as any find (the playthrough reboots both players' saves after every link game). Existing saves load unchanged (checked: the playthrough's
free-play and story saves reboot and resume).

## Verification

`python test/harness/capture_flow.py build/crucible.gbc OUTDIR` (PyBoy, CGB, isolated blank saves) plays:

- **menu:** twelve boot timings; the title and pause menus never show TALK or FIGHT.
- **free:** a new player through the time ask, PLAY, the first bench (cue rows), the filter flow (SELECT, cursor, A,
  the header, B ALL, SELECT clears), the first discovery, the book, TITLES, STATS, SETUP, then 110 discoveries (rooms
  unlock) with visitors approached, and the browse probe (120 foci; every focus's turntable must finish loading).
- **story:** a run; the pause menu; encounters through play until talks, fights, approaches, a forced one, an ignored
  one that fades and a chapter turn have all happened; every encounter captured as hint/telegraph, mid-pan, scene,
  mid-pan back, bench after, with the bench state compared.
- **return:** the free save rebooted five hours later (a voice waits); the story slot resumed.

## Designer review

Played as a new player from a blank save and as a returning one (the free save rebooted five hours later, the story slot
resumed) with `test/harness/capture_flow.py`; screenshots are `OUTDIR/<phase>-<step>.png`. Severity: H high, M medium, L low.

| #   | Where       | Oddity                                                                                                                                                                                                                                        | Sev | Shot                           | Status                                                                                                          |
| --- | ----------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --- | ------------------------------ | --------------------------------------------------------------------------------------------------------------- |
| R1  | Pause menu  | START pressed while a toast card shows left sprites off: the menu had no cursor and no bust (the toast's scanline cut turned sprites off and the menu's event list never turns them on). Pre-existing: about 3 in 10 timings before this work | M   | `menu0-pause`                  | FIXED: every scene but the bench turns sprites on (`sky_mode`); the playthrough checks the cursor on 12 timings |
| R2  | Filter      | Opening the filter with a category filter active crashed (green screen): category names are copied 10 bytes wide into an 8-byte buffer; the story HUD had the same buffer                                                                     | H   | `free-filter-again`            | FIXED (found during this work)                                                                                  |
| R3  | Encounters  | A press made during the pan back (the A that closed a talk) landed on the bench and added the focus                                                                                                                                           | M   | `story-talk*-after`            | FIXED: presses during a pan, and any latched at a return to the bench, are dropped                              |
| R4  | New run     | A START latched while "THE GRID IS BUILDING YOU" opened the pause menu on the first bench                                                                                                                                                     | L   | `story-bench`                  | FIXED (same)                                                                                                    |
| R5  | Pacing      | One hostile faction brought a fight every ~4 makes and crowded out every talk                                                                                                                                                                 | H   | `story-story-fight*`           | FIXED: fights 6 makes apart (12 to force), rivals 8, a voice instead when a champion is not due                 |
| R6  | Pacing      | Visits that came during a breath or while someone waited were lost (2 talks in ~100 makes)                                                                                                                                                    | M   |                                | FIXED: owed until they can come                                                                                 |
| R7  | Pacing      | Forced talks (1 in 3) interrupted too often                                                                                                                                                                                                   | M   | `story-story-talk*-telegraph`  | FIXED: 1 in 5                                                                                                   |
| R8  | Hints       | A waiting hint vanished whenever something was held, and its time froze: it blinked in and out through every mix                                                                                                                              | L   |                                | FIXED: it stays at its edge; the sign row names it when nothing is held                                         |
| R9  | Pan         | The corridor's walls were `\|`, which the font maps to another glyph: they did not show                                                                                                                                                       | L   | `story-*-pan`                  | FIXED: `[` `]`                                                                                                  |
| R10 | Story bench | The active filter was invisible (the HUD wrote over the label)                                                                                                                                                                                | M   | `story-bench`                  | FIXED: the HUD alternates it with the miss odds                                                                 |
| R11 | Menu        | No way out of a run to the title card (only a soft reset)                                                                                                                                                                                     | M   | `story-leave`, `story-left`    | FIXED: LEAVE                                                                                                    |
| R12 | Bench       | B with nothing held did nothing                                                                                                                                                                                                               | L   | `free-bench-filtered`          | FIXED: B clears the filter (`Ⓑ ALL`)                                                                            |
| R13 | Filter      | Half the filters off screen, cursor row not lit, START applied                                                                                                                                                                                | M   | `free-filter*`                 | FIXED: the grid                                                                                                 |
| R14 | Encounters  | A fight's hit can steal the focus element and a talk can take it as a gift: back on the bench the focus has moved to the next thing                                                                                                           | L   | `story-*-after`                | WON'T-FIX: the existing mechanics; the HUD's `-1 LOST` says it                                                  |
| R15 | Hints       | The hint is one 8x8 sprite (the only free OAM entry); a toast card hides the talker above for its 2-4 seconds                                                                                                                                 | L   | `story-story-talk*-hint`       | WON'T-FIX: the sign row names it; the toast passes                                                              |
| R16 | Story       | A chapter turn has no marker of its own (the slot list shows the chapter)                                                                                                                                                                     | L   |                                | WON'T-FIX: the tone is cryptic; the gatekeeper who comes two makes later is the marker                          |
| R17 | Story HUD   | `MISS 51%` shows from a run's first bench                                                                                                                                                                                                     | L   | `story-bench`                  | WON'T-FIX: pre-existing and true (the run's odds)                                                               |
| R18 | Fights      | Answered blindly, a fight can take 4 of 9 elements; a weak run dies after 2-3 fights                                                                                                                                                          | M   | `story-story-fight*-after`     | WON'T-FIX here: this work keeps fight mechanics and outcomes; open question                                     |
| R19 | Records     | SELECT closes the ledger (SELECT means filters elsewhere)                                                                                                                                                                                     | L   | `free-titles`                  | WON'T-FIX: harmless (no filter there); B is the documented back                                                 |
| R20 | Tools       | The fight capture opened FIGHT from the menu                                                                                                                                                                                                  | L   |                                | FIXED: it raises the director's telegraph instead                                                               |
| R21 | Link        | Link play was untested headlessly                                                                                                                                                                                                             | M   | `test/harness/capture_link.py` | FIXED: a two-instance bridge; 40 checks across co-op, race, draw, a pulled cable and two bouts                  |
| R23 | Link        | A long merge on one side tore packets (the partner's queue dropped single bytes) and the link game ended LINK LOST                                                                                                                            | H   | `race-5-end` (before)          | FIXED: whole packets only; periodic packets wait for an idle line                                               |
| R24 | Link        | The 3 s silence window was shorter than a busy scene's packet time (a fight opening on the guest)                                                                                                                                             | H   | `fight-t0-pick` (before)       | FIXED: 8 s during a link game                                                                                   |
| R25 | Link        | A 10-minute race's clock showed "0"                                                                                                                                                                                                           | M   | `race-2-go`                    | FIXED: M:SS                                                                                                     |
| R26 | Link        | CO-OP could only end by pulling the cable                                                                                                                                                                                                     | M   | `coop-5-end`                   | FIXED: its TIME ends it (TIME UP)                                                                               |
| R27 | Link        | The room's status line sat on the scrolling cloud band and was cut off ("LINKED: HOS")                                                                                                                                                        | L   | `coop-1-linked`                | FIXED: the cable row (with the partner's name)                                                                  |
| R28 | Reveal      | A new object appeared still and started turning only once its turntable decoded; no overlay on the reveal                                                                                                                                     | M   | `reveal-strip-*`               | FIXED: the turn phase; costs 2-5 s on uncached results (open question)                                          |

Systems a player meets without being told: the clock (the power-on question, whispers, a voice waiting after hours
away), rooms (the bench changes at 64 discoveries, `free-rooms`), lost pieces (the torn result cell and the machine's
line, `test/harness/capture_lost_piece.py`), the overlay colours (on every bench and book object), the dialogue matrix (who comes,
how they talk, what ignoring does).
