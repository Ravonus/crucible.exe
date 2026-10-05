# CRUCIBLE.EXE fight system

Status: design spec, 2026-10-04. Sources it builds on: `crucible_fight.c/.h`, `crucible_link.c/.h`,
`crucible_storyrun.*`, `crucible_avatar*.c`, `crucible_feats.c`, `crucible_sound.c` (`cartridge/src`),
`crucible_room.h`, `keel_dialogue_choice.h` (`cartridge/include`), `crucible_truths.h` (`cartridge/data/include`),
`cru_saga.c`, `cru_save.c`, `cru_story.c` (`core/src`).

Simulators (deterministic Python, real catalogue slice from `catalogue/`): `docs/fight-system/sim/`. Run
`python3 docs/fight-system/sim/fightsim.py`, `bosssim.py`, `levelsim.py`.
Every number in this doc's result tables comes from those scripts at the rules written here.

---

## 0. The decisions in one page

1. **One triangle, read from the traits the catalogue already has.** The 12 trait bits fold into three stances:
   **HUM** (hot, airy, shiny, glows), **WALL** (wet, stone, made, big) and **DREAM** (cold, alive, green, magic).
   WALL beats HUM, DREAM beats WALL, HUM beats DREAM. I picked this split by searching all 3¹¹ partitions: it agrees
   with 17 of the 20 edges in the existing `BEATS[]` table in `cru_saga.c`, with only 3 against. The catalogue splits
   about evenly: 1,170 HUM, 1,369 WALL, 1,262 DREAM, and 1,867 duals.
2. **The stance decides who wins. The traits decide by how much.** The winner deals power (1–3, from recipe depth)
   plus 1 if any of its traits beats one of the loser's traits under the existing `BEATS[]` table. Ties go to that
   trait edge. Old knowledge stays useful, and nothing is lookup-table magic.
3. **Hands are open, choices are simultaneous.** In duels and versus you see which stances your opponent holds, but
   not which one they will play. Each turn is a small 3×3 guessing game shaped by history, resources and patterns.
   It is never solved, and reading the opponent wins: the `reader` policy beats `random` 86%.
4. **Bosses telegraph.** A boss shows its two ingredients and a "?". You answer from your hand. Difficulty comes
   from veiled results (you have to know or infer the recipe), tricks with readable tells, phases, and a slow drain.
   HP and timers barely change. The first-try win rate falls from 88% at T1 to 32% at T6.
5. **The loadout is a 6-card kit on a 12-point budget.** Cost = power, +1 for a dual. Duplicate copies cost 1 less
   and grow stronger as they cycle. Stacking is viable (51% against the field), but going mono-stance loses (34%).
6. **Passives are public patterns.** You bring two (one before level 6). Each unlocks only after you play its
   pattern, and pursuing the pattern makes you predictable. The pips are shown to your opponent, and a clean counter
   (SEVER) wipes your progress. Across the 12 passives, the side that brought one wins 45–56%.
7. **Comebacks come from rules, not dice.** Losing a clash earns focus, a hand at ≤4 HP grows to four cards, and
   UNDERTOW, SCAR and SALVE help further. A player who was ever 4+ HP behind still wins 22% of the time.
8. **Everything is generated.** Boss move sets come from faction × tier × seed, passives from 2-byte templates,
   arenas from the room family, time and lucidity, and the avatar from a 6-byte genome. Total new data tables are
   about 0.2 KB. The new code is about 19 KB in 2 newly reserved banks. Only 2 banks are reserved, which costs about
   24 of the slice's 5,668 objects (0.4%), and the main bank gets 0–12 bytes.
9. **Link play is lockstep, using one 7-byte packet per turn.** Saves stay separate. In co-op, each player plays
   their own game, and inventory is shared through a bounded "keep" cap of 12 per session, written as each element
   arrives. Cutscenes and boss encounters are shared, and control passes between players by seed. A dropped cable
   loses nothing.
10. **Avatars are made, not drawn.** A player builds their face from the procedural face generator's parameters.
    Levels grant parts and three capped attributes. Versus normalises attributes by default (52%, which is noise
    around 50%). The host can switch to TRUE STRENGTH, and the suggested handicap brings a level-30 vs level-1 gap
    from 76% down to 57%.

---

## 1. Core loop

### 1.1 The triangle

| stance    | traits (bit)                       | reads as                                         | beats | because (existing `BEATS[]` edges it keeps)                                                        |
| --------- | ---------------------------------- | ------------------------------------------------ | ----- | -------------------------------------------------------------------------------------------------- |
| **HUM**   | HOT 0, AIRY 3, SHINY 5, GLOWS 6    | fluorescent buzz, heat, light, air               | DREAM | heat melts cold, burns green, ends the alive; glow beats cold and magic                            |
| **WALL**  | WET 2, STONE 4, MADE 9, BIG 10     | wet concrete, the building, the backrooms itself | HUM   | water douses heat; stone and bulk stop air and shadow glow; stone and water scratch and rust shine |
| **DREAM** | COLD 1, ALIVE 7, GREEN 8, MAGIC 11 | sleep, growth, the thing behind the wall         | WALL  | roots split stone, cold freezes water, magic undoes the made and the big                           |

In the fiction: walls hold the hum in, the dream noclips through walls, and the hum wakes you from the dream. Players
are never told this sentence. A one-time ticker line hints at it in the first duel ("the walls keep the noise in").

Three of the existing edges point the other way (COLD beats WET, and GLOWS/MAGIC are mixed). These are deliberate
exceptions at the trait level. They show up as the +1 edge bonus inside a stance matchup, so the finer detail still
matters (see 1.3).

**An element's stance** = the family with the most of its trait bits. If two families tie, it is a **dual** and can
be played as either. If all three tie, or the element has no traits, its category decides:
`CAT_FAM = {element:HUM, matter:WALL, weather:DREAM, energy:HUM, life:DREAM, craft:WALL, place:WALL}`.

Catalogue slice (5,668 objects): HUM 1,170 · WALL 1,369 · DREAM 1,262 · HUM/WALL 1,073 · HUM/DREAM 382 ·
WALL/DREAM 412.

| element      | traits                    | stance         | power |
| ------------ | ------------------------- | -------------- | ----- |
| EARTH, WATER | STONE / WET               | WALL           | 1     |
| FIRE, AIR    | HOT GLOWS / AIRY          | HUM            | 1     |
| LIFE         | GLOWS ALIVE MAGIC         | DREAM          | 1     |
| PLANT, TREE  | ALIVE GREEN               | DREAM          | 1     |
| SNOW         | COLD                      | DREAM          | 1     |
| ICE          | COLD SHINY                | HUM/DREAM dual | 1     |
| METAL, GLASS | SHINY MADE                | HUM/WALL dual  | 1     |
| STORM        | WET AIRY GLOWS            | HUM            | 1     |
| DRAGON (d5)  | HOT GLOWS ALIVE BIG MAGIC | HUM/DREAM dual | 2     |

The four starters are two WALL and two HUM. **No starter is DREAM**, so the triangle closes with the first LIFE,
PLANT or SNOW you make. That is the rule for the first fight: no duel can trigger until the player owns a DREAM
element. The first fight therefore arrives just when the player can win every matchup (see 5.7).

### 1.2 Power and edges

- **Power** `P = 1 + (depth ≥ 5) + (depth ≥ 7)` (from `cri_depth`). Of the slice, 755 objects are P1, 3,476 are P2
  and 1,437 are P3. A dual plays at P−1 (minimum 1): flexibility costs weight.
- **Edge** = 1 if any trait of yours beats any trait of theirs under `BEATS[]`, otherwise 0. This is
  `cru_counter(c, theirs, yours) > 0`, the existing core function, reused unchanged.

### 1.3 Clash resolution (duel, versus, gauntlet)

Both sides lock an action. Then:

| your action vs theirs       | result                                                                                              |
| --------------------------- | --------------------------------------------------------------------------------------------------- |
| card beats card (by stance) | the winner deals `P + edge + bonuses`; the loser deals 0 and gains +1 focus                         |
| same stance                 | each deals its own edge (0/1); if the edges are equal, the higher-P card deals 1                    |
| card vs GUARD               | the guard takes `⌊P/2⌋`; the guard side gains +1 focus and discards its weakest hand card (a cycle) |
| GUARD vs GUARD              | nothing; both gain +1 focus                                                                         |
| FUSE (2 focus)              | two hand cards with a real recipe become the product: its stance, its P+1 (max 4)                   |

Bonuses: **stack** +1 per copy of the same element already in your discard (max +2); passives (§4); arena (§3).
**SEVER**: winning with 2 or more trait edges resets the loser's unfinished passive pattern to 0.
A bout ends at 0 HP. At the 30-turn limit, the higher HP wins (draws are 0.7% of bouts).

### 1.4 The numbers

HP 12 (+GRIT) · kit of 6 · hand 3 (4 with PRISM or at HP ≤ 4) · focus 0..3 (start 0 + FOCUS) · budget 12 (+REACH) ·
4 pips of 150 frames each (2.5 s per pip, 10 s per turn) · mean bout 10–13 turns, about 1½–2 minutes.

### 1.5 One turn on screen (20×18 tiles)

```
row 0   DAEMON ######..   ●●○ ○○○           name, HP bar (8 cells), its two passive patterns (pips)
row 1   >> the ticker: what it says, torn letters when the scene glitches
rows 2-7          [ opponent face 48x48: sprites, existing avatar ]
row 8   their hand:  ~2  #1  ~3             stance glyph + power per card (open hand)
row 10        [their card]  vs  [your card] the clash line: the two overlay cells
rows 12-14  [card][card][card]              your hand: focused card is an overlay cell; others BG icons
row 15      ~3    #1/~1   *2                your stances + power; duals show both glyphs, the live one bright
row 16  ◆◆◇  ●●○ ○○○    !!!!                focus, your passive patterns, pips
row 17  STEAM  A PLAY  B GUARD              status / answer line
```

Glyphs: HUM `~` (a buzzing line), WALL `#`, DREAM `*`. They are drawn from the existing font (0 new tiles). The face
on your side is your avatar, drawn into 36 BG tiles (§8.5).

Button by button:

| input                   | does                                                                                                                                          |
| ----------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| LEFT / RIGHT            | move along your hand                                                                                                                          |
| UP                      | on a dual card, flip which stance it plays                                                                                                    |
| DOWN                    | FUSE mode: marks the focused card. LEFT/RIGHT pick its partner; the product's name appears, or "NOTHING". A locks the fuse; DOWN or B cancels |
| A                       | lock the focused card (or the fuse). Your pips freeze, and the opponent's portrait shows a small lock mark when they lock                     |
| B (tap)                 | GUARD (locks)                                                                                                                                 |
| SELECT                  | info strip: the opponent's passive names and patterns, the arena rule glyph (in co-op: NUDGE, §9.4)                                           |
| START                   | pause (solo); in link it offers FORFEIT                                                                                                       |
| B (hold 1 s, duel only) | FLEE: forfeit; −8 lucid; no element loss                                                                                                      |

When both sides are locked, or the last pip runs out (an unlocked side GUARDs), the cards slide to the clash line
and flip. The room flashes: white for your win, `ROOM_C_HOSTILE` for a loss, and the blue flicker for a tie. Sounds
(existing): win `SFX_NEW`, loss `SFX_DENY`, tie `SFX_SWAP`, guard `SFX_CLOSE`. The result holds for 100 frames
(A skips), then you draw back to a full hand.

**Why the decision is never obvious.** Example: they hold `~2 ~3 #1`, and last turn they played HUM. You hold
`#2 *2 ~1`. WALL beats both of their HUMs, so `#2` looks free. But they can see you hold a WALL. Their `#1` ties it,
and if they think you expect HUM, they will play `#1` and take the tie by edge. Your `*2` beats that `#1`, but loses
to either HUM for 3–4 damage. Then add the other pressures: their ECHO pattern needs one more HUM (so HUM is more
likely), your stack bonus makes `#2` hit for 3 if a copy is in your discard, you have 2 focus for a fuse, and you
are at 5 HP (one more loss gives you a 4-card hand). The simulator's numbers back this up. A policy that best-responds
to a uniform guess (`greedy`) is beaten by one that models history (`reader`, 53%). Repeating your own stance
(`stacker`) is crushed (15% against the reader). Pure randomness wins only 14%.

---

## 2. Loadout: the kit

**Pick 6 cards from your shelf within a budget of 12** (+REACH, max +3).

| card                                 | cost                     |
| ------------------------------------ | ------------------------ |
| P1 / P2 / P3 single-stance           | 1 / 2 / 3                |
| dual                                 | +1 (and it plays at P−1) |
| 2nd and 3rd copy of the same element | −1 each (min 1)          |

Synergy rules:

- **Stack**: copies cost less, and each copy in your discard adds +1 to the next one (max +2). A triple P3 costs
  3+2+2 = 7 and late in a bout hits for 5–6. But three copies of one stance tell the opponent what is coming.
- **Fusion pairs**: two kit cards with a real recipe between them are a hidden third option (FUSE). The kit screen
  marks pairs with a small link glyph. That is where recipe knowledge pays off in fights.
- **Related elements** (same category) get no discount. That is deliberate: discounting categories made mono
  builds dominant in early sim runs.

Why cheap versus expensive is interesting, as the archetype table shows (reader vs reader, 300 bouts per cell):

| archetype | what it is                             | vs field |
| --------- | -------------------------------------- | -------- |
| balanced  | 2 of each stance, best P that fits     | 58%      |
| swarm     | duals and P1s: flexible, light         | 56%      |
| fuser     | recipe pairs                           | 54%      |
| random    | anything that fits                     | 53%      |
| stack     | one strong element ×3 + 3 cheap covers | 51%      |
| heavy     | three P3 + three P1                    | 48%      |
| mono      | one stance only                        | **34%**  |

Full matrix in §11.2. All six honest archetypes sit within 48–58%. Mono is the trap the triangle exists to punish.
Heavy kits hit hard but run out of answers. Swarm kits always have an answer but hit for 1–2. Stack kits earn power
as the bout goes on, at the cost of being readable.

**UX.** The kit is persistent: one per save, edited from the bench (SELECT on a shelf item, then "TO KIT"). An
encounter skips the kit screen unless you press SELECT during the 1-second intro tear. The screen shows 6 slots, the
budget as `9/12` pips and your two passive slots. LEFT/RIGHT picks a slot, UP/DOWN cycles shelf items that fit the
budget, A sets, B clears, START is done. If you never touch it, an auto-kit fills it (the `balanced` builder:
deterministic, best coverage that fits).

---

## 3. Modifiers, handicaps and arenas

### 3.1 Arenas come from the living room

The arena is derived, never stored. It is computed at `fight_open` from the room's family
(`room_lean_family[]`), the time part (`crucible_time`), lucidity and the palette state:

| arena (family % 8) | rule (symmetric: both sides get it)         | tell                                             |
| ------------------ | ------------------------------------------- | ------------------------------------------------ |
| 0 HUMMING LIGHTS   | HUM wins +1                                 | the ceiling strip flickers in time with the pips |
| 1 DAMP CARPET      | WALL wins +1                                | floor tiles darken a shade                       |
| 2 STILL AIR        | DREAM wins +1                               | drifters stop moving                             |
| 3 FLICKER          | every 3rd turn, both open hands show as `?` | the whole room blinks off for one frame per turn |
| 4 NARROW           | guards take full damage                     | the plaster closes in by one tile column         |
| 5 ECHO HALL        | stack bonus +1 more                         | every sound plays twice                          |
| 6 EXIT SIGN        | ties go to the side with lower HP           | a green glyph lights in a corner                 |
| 7 EMPTY            | no rule                                     | none                                             |

Overlays on the family rule:

- **Night** (`CT_NIGHT`): focus max +1 for both sides.
- **3:33 or lucid < 64**: the fight becomes a GLITCH fight (§5.6).
- **Fight palette**: `room_tick(ROOM_S_FIGHT)` already turns the room hostile. Win and loss flashes use the
  existing find/miss states.

Arena rules change what is optimal, not who is favoured, so they cannot break the balance between two players.
Implementation step 12 adds a simulator pass for each arena to confirm that.

### 3.2 In-fight modifiers (from the rules above)

- **Bonuses**: stack (+1/+2), trait edge (+1), last-pip CLOSE CALL against bosses (+1, but risks the miss), fusion
  (P+1), passives, arena.
- **Minuses**: a dual plays at P−1; STEAL removes a card for the rest of the fight; ERODE shrinks your hand; the
  boss's drain costs 1 HP every 2–3 rounds; OVERCLOCK costs you 1 HP on each lost clash.

### 3.3 Handicaps

- **Story scale** (existing `s->scale`, Gentle/Normal/Harsh): player HP +2/0/−2 and boss pips +1/0/−1. Gentle also
  turns boss-hit element losses off (as the existing gentle scale does for misses).
- **Versus**: per-player handicap bytes (§9.2).
- **Self-imposed**: none, because the rules screen covers that.

---

## 4. Passives

You equip **2** (1 until level 6) from the passives you know. Each passive is a 2-byte template:
`{pattern kind:4, need:4, effect kind:4, magnitude:4}`. A passive is **dormant** until its pattern completes within
the fight. Progress pips sit under your portrait **for both players to see**.

| #   | passive   | unlock pattern (public)                    | effect once unlocked                         | the risk of chasing it             | counterplay                            |
| --- | --------- | ------------------------------------------ | -------------------------------------------- | ---------------------------------- | -------------------------------------- |
| 1   | ECHO      | same stance 2 turns running                | repeat-stance wins +2, repeat-stance ties +1 | you are announcing your stance     | play its beater on the repeat turn     |
| 2   | PRISM     | all three stances within 3 turns           | hand +1                                      | you must play your weakest stance  | punish the stance it still needs       |
| 3   | SCAR      | lose 5 HP                                  | while at ≤ half HP, wins +1                  | you are already losing             | finish fast; don't trade small hits    |
| 4   | VIGIL     | GUARD 2 turns running                      | guards take 0                                | two passive turns                  | play big when they guard; FUSE into it |
| 5   | CATALYST  | reach full focus (3)                       | fuse costs 1, fused +1                       | you banked focus instead of fusing | deny losses (focus comes from losing)  |
| 6   | STALEMATE | 3 ties                                     | your ties +1                                 | ties are slow                      | avoid mirror stances                   |
| 7   | SALVE     | 3 P1 cards in a row                        | heal 1 per win                               | small cards lose trades            | hit them while they play small         |
| 8   | UNDERTOW  | lose 2 clashes in a row                    | the first win after a loss deals ×2          | two losses                         | after their second loss, guard or tie  |
| 9   | LUCID     | 3 wins                                     | no single hit on you exceeds 2               | none (it is a lead-keeper)         | spread damage; use stack bonuses early |
| 10  | NOCLIP    | win 2 clashes with DREAM                   | DREAM ignores guards, DREAM wins +1          | DREAM-heavy play                   | HUM beats DREAM: hold one              |
| 11  | OVERCLOCK | play 2 P3 cards                            | +1 on all hits; lose 1 HP per lost clash     | P3s are your expensive answers     | read it and make it lose               |
| 12  | MEMORY    | replay 3 elements (cards that cycled back) | stack bonus doubles                          | slow, and needs copies             | burn them before the cycle             |

Measured (reader vs reader, random kits, 2,000 bouts): the side that brought each passive wins between **45%
(CATALYST)** and **56% (STALEMATE)**. No passive is a must-pick, and none is dead. Unlock rates run from 21% (VIGIL)
to 97% (ECHO). The cheap patterns have modest effects, and the dear ones are strong but rare. The full table is in
§11.3.

Learning passives in the story: ECHO, PRISM and VIGIL are known from the start. Each faction at FRIEND tier teaches
one: PROGRAM MEMORY, DAEMON OVERCLOCK, GHOST NOCLIP, AI STALEMATE, OPERATOR SALVE, RELIC CATALYST. SCAR, UNDERTOW and
LUCID are learned by living their condition in any fight: the first time you lose 5 HP, lose twice in a row or win
three in a row, the ticker says one line and the passive is now known. There are no menus that explain this.

---

## 5. Fight types

| type                | rules                                                                                                                                                                                                                                       | where it appears                                                                                                                                 |
| ------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| **5.1 DUEL**        | §1 rules, 8 HP each, AI opponent with a faction personality                                                                                                                                                                                 | walking encounters with WARY/HOSTILE factions; UP/DOWN approaches to a figure in a room. Chance per room step: `1/16 + 1/32 per hostile faction` |
| **5.2 BOSS**        | telegraph model (§6), your kit and hand, phases                                                                                                                                                                                             | a HOSTILE faction's champion (`fight_rival`), at most one per chapter, from the walking flow when `STORY_BOSS` fires                             |
| **5.3 NEMESIS**     | a BOSS that flees at its first phase in its first 2 meetings and returns stronger with what it stole; it opens on RECALL (§6.4)                                                                                                             | chapters 4, 5, 6–7 (3 meetings); then it stays down                                                                                              |
| **5.4 GAUNTLET**    | 3 duels in a row behind "doors". HP carries over, and the deck does not reshuffle between duels. Between duels pick 1 of 2 door modifiers (an arena for the next duel, or +2 HP)                                                            | chapter 4 and the chapter-8 finale                                                                                                               |
| **5.5 LINK VERSUS** | §1 rules in lockstep; match rules from the host (§9.2)                                                                                                                                                                                      | the link room, FIGHT mode                                                                                                                        |
| **5.6 GLITCH**      | a rule fight. One of: **INVERT** (the triangle reverses from turn 4, with a tell: the stance glyphs flip upside down one turn earlier), **FOG** (hands hidden), **DRIFT** (each turn one of your cards becomes its `cru_dream_slot` cousin) | replaces a duel when lucid < 64 or at 3:33 (`CT_SP_333`); the choice is `lucid % 3`                                                              |

5.7 **First contact.** The first DUEL is held back until you own a DREAM element. It uses a fixed tutorial seed:
the opponent's hand is HUM, HUM, WALL, and it always plays HUM on turn 1. Its ticker hints at each relation exactly
once.

Duel AI personalities (each is a few weights, 6 B per faction): PROGRAM repeats loops (exploitable), DAEMON
favours big cards, GHOST plays the stance you just beat, AI counts your last 4 plays, OPERATOR guards when hurt, and
RELIC fuses whenever it can.

---

## 6. Bosses

### 6.1 How a boss round plays

It keeps the current flow. The boss combines two of its hand with the real recipes (`cru_boss_plan`), shows
**a + b = ?** with countdown pips, and you answer from your hand before the pips run out:

- **Plain strike**: the "?" shows the result's stance glyph.
- **Veiled strike**: only the ingredients show. You must know or infer the result. The rule the game never states
  (but every boss teaches): _the ? leans to the stance both halves share; otherwise to the deeper half_. The rule is
  right for 74% of the 13,532 recipes; if you have made the result before, you know it for certain. This makes
  fights reward the combining game itself.
- Clash as in §1.3 against the attack (power `pow`), with these boss specifics:
  - **CLOSE CALL**: answering on the last pip deals +1 and pierces ARMOR, but a late press is a miss and the attack
    lands for `pow + 1`.
  - **The drain**: the room hums, costing 1 HP every N rounds. Stalling loses.
  - **Rehearsal**: the first time any trick appears in a fight, it is shown with an exaggerated tell and lands at half
    power. Each boss teaches before it punishes.
  - **Cooldown**: each trick waits 3 rounds before it can repeat, so patterns stay learnable.
  - **Phases** at ½ and ¼ HP (existing). Phase 1 doubles the weight of its newest trick. Phase 2 adds +1 power and
    one fewer pip.

### 6.2 Move format (generated, never listed per boss)

```c
/* a move kind: what it does, its tell, its counter (16 kinds, 2 B each = 32 B) */
typedef struct { uint8_t kind_flags; uint8_t tell; } fmove;   /* flags: ATTACKS2, HIDES, NEEDS_LAST, BUFF... */
/* a faction's signature order: the tier-t boss knows the first min(t,5) (6 factions x 5 nibbles = 18 B) */
static const uint8_t SIG[6][3];
```

A boss = `(faction, tier, seed)`:

- **moves** = `SIG[f][0..t-1]`;
- **home stance** = `CAT_FAM[LIKE_CAT[f]]`;
- **loop order and favourite trick** = shuffled by the seed;
- **its hand** = `favourite()` elements (existing);
- **attack stance** = the stance of the real recipe product.

Storing 6 factions × 6 tiers × 8 moves explicitly would take about 288 B plus per-boss text. The generated form
costs 18 B plus 32 B shared.

### 6.3 Tiers and the curve (simulated, tuned)

| tier | where                              | HP  | power | pips | veiled | drain          | tricks             | first try | after one loss |
| ---- | ---------------------------------- | --- | ----- | ---- | ------ | -------------- | ------------------ | --------- | -------------- |
| T1   | ch 1                               | 22  | 2     | 4    | 20%    | 1 per 3 rounds | 1                  | **88%**   | 99%            |
| T2   | ch 2–3                             | 30  | 3     | 4    | 35%    | 1/3            | 2                  | **76%**   | 94%            |
| T3   | ch 4–5                             | 30  | 3     | 3    | 55%    | 1/3            | 3                  | **67%**   | 89%            |
| T4   | ch 6–7                             | 30  | 3     | 3    | 70%    | 1/3            | 4                  | **57%**   | 85%            |
| T5   | ch 8 finale                        | 28  | 4     | 2    | 85%    | 1/2            | 5                  | **44%**   | 69%            |
| T6   | every cycle after the first ending | 30  | 4     | 2    | 100%   | 1/2            | 5 + phase emphasis | **32%**   | 53%            |

Faction offsets: RELIC −6 HP from T4 on (ARMOR roughly doubles its effective HP), and AI −2 from T4 on.

Difficulty rises through **information** (veils, tricks, fewer pips) and **power**, not HP. HP is flat from T2 on.
"After one loss" is the same fight once the trick that beat you is known: each loss teaches the counter, and the
retry gains 18–25 points at every tier.

Encoded as `TIER[6] = {hp, pow|pips<<4, veil%/5|drain<<5, …}`: 4 B × 6 = 24 B.

### 6.4 The six bosses: personality, moves, tells and the counter each teaches

Tiers add moves in this order; T1 has move 1, T5 has all five.

| faction                        | personality (home)             | 1      | 2      | 3      | 4      | 5        |
| ------------------------------ | ------------------------------ | ------ | ------ | ------ | ------ | -------- |
| **PROGRAM** (craft, WALL home) | rigid, loops, compiles you     | LOOP   | DOUBLE | FEINT  | SHIELD | COMPILE  |
| **DAEMON** (energy, HUM)       | hot, impatient, all-in         | CHARGE | DOUBLE | STEAL  | FEINT  | OVERHEAT |
| **GHOST** (weather, DREAM)     | takes, lies, hides             | STEAL  | FEINT  | HAUNT  | CHARGE | POSSESS  |
| **AI** (matter, WALL)          | watches and adapts             | MIRROR | ADAPT  | SHIELD | FEINT  | PREDICT  |
| **OPERATOR** (life, DREAM)     | grows, unplugs                 | GROW   | STEAL  | DOUBLE | ADAPT  | JACK     |
| **RELIC** (place, WALL)        | slow, armoured, wears you down | ARMOR  | SHIELD | CHARGE | HAUNT  | ERODE    |

| move             | what it does                                                                             | tell (readable on a 160×144 screen)                                               | the counter it teaches                                         |
| ---------------- | ---------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------- | -------------------------------------------------------------- |
| LOOP             | its stances run a fixed 3-cycle                                                          | at fight start, the background grid lines blink 3 times in the loop's colours     | count the loop                                                 |
| DOUBLE           | two attacks in one round                                                                 | two `?` cells; pips are blue                                                      | keep coverage: hold two answers                                |
| FEINT            | the shown stance is a lie; the real one beats your natural answer                        | the `?` shimmers **twice** (the existing `fight_shimmer`, two pulses)             | mirror the shown stance (it beats the real one)                |
| SHIELD           | immune to one stance for 2 rounds                                                        | a crossed stance glyph by its HP bar                                              | switch stance                                                  |
| COMPILE          | its loop becomes the beaters of your last 3 plays                                        | your last 3 stance glyphs scroll through the ticker                               | break your own patterns before late PROGRAM fights             |
| CHARGE           | skips its attack; the next one is ×2                                                     | the boss sprite glows (palette pulse, `FIGHT_FX_CHARGE`) and the pips fill slowly | use the free round (hit hard or FUSE), then answer the big one |
| STEAL            | if it wins, it takes the card you played for the fight (it comes back when it falls)     | the ticker names the card it wants                                                | don't risk your best card: play a cheap answer or guard        |
| OVERHEAT         | DOUBLE at +1; it takes 2 itself if both are answered                                     | the screen tints toward red                                                       | answer both and it burns                                       |
| HAUNT            | the attack shows ingredients only, and the `?` is blank                                  | the ingredient cells flicker their true palettes once                             | recipe knowledge, the lean rule                                |
| POSSESS          | one of your hand cards becomes its dream cousin for 2 rounds                             | that card's cell glitches                                                         | plan around a card you cannot trust                            |
| MIRROR           | attacks with your last stance                                                            | its face copies your face's hue for a beat                                        | play what beats your own last play                             |
| ADAPT            | attacks the beater of your most-played stance in your last 4                             | its eye tracks the stance glyph you used most                                     | bait: repeat a stance, then switch                             |
| PREDICT          | ADAPT on your stance-after-stance habit                                                  | the eye moves before you do                                                       | look random; use duals                                         |
| GROW             | heals 1 if not hit this round                                                            | a green tint on its sprite                                                        | pressure; don't guard                                          |
| JACK             | discards one card from your hand after the round                                         | a cable glyph by your hand                                                        | play your best card first                                      |
| ARMOR            | takes at most 3 per hit unless CLOSE CALL or fused                                       | metallic palette on hits                                                          | fuse, or answer on the last pip                                |
| ERODE            | your hand is 2 for the rest of the fight                                                 | the bottom tile row crumbles                                                      | coverage in the kit; PRISM                                     |
| RECALL (nemesis) | shields the stance you beat it with most last time for 5 rounds, and opens on its beater | its first line names your old winning element                                     | bring and open with a different stance                         |

Each boss's last line uses `crucible_lines` with the intent `SAY_HURT`, coloured by the player's alignment cell
(§7.2).

### 6.5 Nemesis memory

The nemesis `crucible_boss` stays in `crucible_story` (unchanged). The player record (§10.4) adds 2 bytes: the stance
you dealt the most damage with, plus a 4-bit count of how it was beaten (by fusion, close calls, or by guard
attrition), which picks its RECALL variant. The rematch comes at a later chapter, so it is at least one tier higher.

Simulated at T3: a first meeting is won 63% of the time; the rematch with memory 69%; the same rematch without
memory 74%. The memory erases most of what you learned until you vary your opening.

---

## 7. Rewards, costs and the hidden matrix

### 7.1 Rewards and costs

| outcome              | XP        | elements                                                                      | standing / lucid                                           |
| -------------------- | --------- | ----------------------------------------------------------------------------- | ---------------------------------------------------------- |
| duel won             | 15        | 25% chance to salvage one of their hand cards (if new)                        | beaten faction −3, its rival +3                            |
| duel lost            | 5         | Gentle: none. Normal/Harsh: one element by the existing `cru_story_loss` odds | lucid −8                                                   |
| fled                 | 0         | none                                                                          | lucid −8, `CRU_ACT_REFUSE`                                 |
| boss down            | 30 × tier | stolen cards return; plus the end choice (7.2)                                | existing: faction −10, rival +10, lucid +24, `CRU_ACT_WIN` |
| boss wins            | 10        | existing `CRU_BOSS_HIT` losses per run scale                                  | lucid −12 per hit (existing)                               |
| nemesis down (final) | 60 × tier | everything it stole                                                           | as boss, plus a clue toward the leading truth              |

### 7.2 The end choice, and how fights lean the matrix

When a boss breaks, the screen holds its face mid-glitch. You choose with no labels, only three glyphs that are
sometimes misread in the dream:

- **A TAKE**: keep one element of its hand. Moves HEART −3 (toward evil). Its faction −6 more.
- **B LET GO**: it fades. Moves HEART +3, its faction +4 (less hated), lucid +8.
- **SELECT UNMAKE**: split its strongest attack (`cru_story_split`); gain both halves. Moves ORDER −3 (toward
  chaotic), lucid −8.

Play style leans ORDER (`kdc` byte 14, ±60) quietly, counted per fight:

- repeating patterns (ECHO and MEMORY unlocks, beating a LOOP by counting): +1 per fight, toward lawful;
- winning through FEINT reads, POSSESS turns or DRIFT: −1, toward chaotic.

Clamped at ±2 per fight. These go through a new 3-line `kdc_nudge(state, axis, d)` in `keel_dialogue_choice.h`
(a KEEL change; it uses the existing clamp).

The truth matrix gets the existing acts `CRU_ACT_WIN/HIT/LOSS` and two new ones, **SPARE** and **TAKE**. That is a
core change: `T_ACTS` has 4 free bits (12–15), so it costs no size, but the core peer lands it.

Never explained, only hinted:

- the boss's last line is picked by the player's alignment cell;
- after the fight the room's ambient cycle steadies (lawful) or flickers more (chaotic) for one chapter;
- the avatar's mark (pattern) can shift once per chapter toward the cell (§8.4);
- in dialogue, factions mention "the one you let go" or "what you took" by item name.

---

## 8. The player avatar

### 8.1 The genome: 6 bytes over the existing generator

`avatar_make(seed, faction)` rolls `style_, head_, eyes_, mouth_, gear_, pat_, neck_, hue_, dither_, fx_, rx_, ry_`.
The creator sets those values directly instead of rolling them:

```
byte 0  style:4 (0..8)        | hue:4 (0..15; style 1 uses SKIN[hue&7])
byte 1  head:3                | eyes:4 (0..9)        | wild:1 (lets the dream re-roll one feature per chapter)
byte 2  mouth:3               | gear:3               | neck:2
byte 3  pattern:3             | dither:3             | size:2 (rx/ry presets 11/12, 13/14, 15/16)
byte 4  fx:3                  | mark:3 (secret glyph layer) | glitch-in style:2
byte 5  secret bits (8): halo-from-sparing, horns-from-taking, DMG-green, 3:33 sheet, ... (§8.4)
```

`avatar_make_genome(const uint8_t g[6])` sets the globals and then applies the per-style geometry from the existing
`switch (style_)`, but skips its feature re-rolls. The editor only offers combinations that switch allows: for
example, style 1 (human) fixes the eyes to 8, so the EYES row is hidden. About 300 B in the avatar bank. The same
6 bytes regenerate the face on the partner's cartridge and on the web (`AVATAR_HOST` already builds this C for
tools).

### 8.2 The creator (new game, per slot)

The creator opens after the slot is named, as the first glitch-in of the run. Layout: the face (48×48, sprites) on
the left; 9 rows on the right — STYLE, HEAD, EYES, MOUTH, CROWN, MARK, HUE, GRAIN, AURA — each showing `3/8`
(unlocked/total).

| input        | does                                                                                                                      |
| ------------ | ------------------------------------------------------------------------------------------------------------------------- |
| UP / DOWN    | row                                                                                                                       |
| LEFT / RIGHT | cycle that row's unlocked options; the face regenerates (tiles stream a third per frame, as now)                          |
| SELECT       | "dream it": a seeded random roll within the unlocked options (the old generator)                                          |
| B            | undo the last change (one step); on an unchanged face, back to naming                                                     |
| A or START   | done. The face glitches out and in (`avatar_glitch`); you never see the full menu again until the bench's SELECT → MIRROR |

At the start, 30 of 81 options are open:

- style: human, robot, pixel
- 4 heads, 4 eyes, 4 mouths
- crown: none, antenna
- pattern: none, freckles
- 8 hues, 2 grains, aura: none

### 8.3 Progression

XP sources (deterministic; no time-based XP):

| source                | XP                                                        |
| --------------------- | --------------------------------------------------------- |
| a new discovery       | 5                                                         |
| a known recipe remade | 1 (max 20 per chapter)                                    |
| a clue found          | 10                                                        |
| a chapter advanced    | 50                                                        |
| an ending             | 200                                                       |
| duel won / lost       | 15 / 5                                                    |
| boss                  | 30 × tier                                                 |
| nemesis               | 60 × tier                                                 |
| link session          | 20, plus 2 per partner find you were notified of (max 40) |
| versus won / lost     | 15 / 10                                                   |

Level curve: XP to the next level = `40 + 20·L`. Level 10 takes about 1,300 XP, level 20 about 4,600, level 30 about
9,900. One campaign of 8 chapters yields about 4,000 XP (level 18–19); a second cycle reaches 30. Computed with one
multiply-add per level-up; no table.

Per level:

- **one cosmetic option** (levels 2–30: 29 options), unlocked in a seeded order (a permutation from the slot seed),
  so two saves open different things first;
- **an attribute point** at levels 3, 6, …, 27 (9 points):
  - GRIT: +1 HP each, cap 4;
  - FOCUS: +1 starting focus each, cap 2;
  - REACH: +1 kit budget each, cap 3.

  The caps add up to exactly 9, so a full build is reached at level 27. Points are spent on the level-up card
  (LEFT/RIGHT, A).

- **The second passive slot** at level 6.
- Hand size, power and the triangle are never touched by attributes. That protects the core game.

### 8.4 Strange and secret unlocks (never explained)

| unlock              | condition                                                                                  | how it shows                                        |
| ------------------- | ------------------------------------------------------------------------------------------ | --------------------------------------------------- |
| style: sheet ghost  | pause at 3:33 at night (`CT_SP_333`, the existing once-per-power-on event) with a run open | your face flickers in the pause card                |
| style: radiant      | spend a whole chapter in cell LG (lawful good)                                             | the creator's STYLE count goes up by one, silently  |
| style: flame        | a whole chapter in CE (chaotic evil)                                                       | same                                                |
| style: cloud        | a whole chapter in TN (true neutral)                                                       | same                                                |
| style: AI eye       | win against AI three times without being ADAPTed                                           | same                                                |
| crown: halo / horns | LET GO 3 bosses / TAKE 3 bosses                                                            | your face in dialogue wears it before you choose it |
| pattern: glitch     | reach lucid 0 and survive (the run ends; the unlock is kept by the slot's next run)        | your mark tears in cutscenes                        |
| hue: DMG green      | the existing DMG-green egg                                                                 | the menu hue row gets a 17th swatch                 |
| mark layer          | shifts once per chapter toward your alignment cell (secret byte 4)                         | a 3×3 glyph on the forehead changes                 |

The remaining options are faction gifts: each faction at ALLY tier opens its TASTE heads and eyes.

### 8.5 Where the face shows

- **Dialogue box**: your face (left) when choosing a reply; the speaker's (existing) when they talk.
- **Fights**: the opponent or boss face as sprites (as now). **Yours as 36 BG tiles** in VRAM bank 1, using the
  palette slot of the fight lease. That keeps OAM 0–23 (overlay) and 34–36 (rooms) untouched. Your face is static
  except for glitch-in at the start and a one-row tear when you lose a clash.
- **Link room**: both faces. The partner's genome arrives as 2 packets (§9.5) and is regenerated locally.
- **Notices**: a 1-tile head glyph (from the font) in the partner's hue.
- **Web link room**: render from the same 6 bytes with the host C build (wasm), or with a TS port checked against it
  by golden images.

---

## 9. Link play

### 9.1 Versus fight (lockstep)

Each player sees the fight from their own side (you left, them right), on their own scene.

**Setup**:

1. Both send `P_CAT` (catalogue size and recipe count). A mismatch disables FIGHT mode with the ticker line
   "the other room is different".
2. Rules (§9.2).
3. Both send `P_AVATAR` ×2 and their kit as `P_DECK` ×3 (2 ids each; `id | slot<<13`).
4. Session seed = `xorshift(seedHost ^ rotl(seedGuest, 5) ^ cellHost<<8 ^ cellGuest)`. It is symmetric because the
   host/guest order is fixed.

**Per turn**: each side sends **one `P_FTURN`** when it locks:

```
P_FTURN (type 11): a.lo action = kind:2 (PLAY/FUSE/GUARD) | slotA:2 | slotB:2 | stance:2
                   a.hi turn number (mod 256)
                   b.lo state hash: 8-bit sum of both HP, focus, deck tops, rng.lo — before this turn
                   b.hi lock pip (0..4) | have-your-turn ack bit (bit 7)
```

- Both carts resolve once both `P_FTURN` packets for turn _n_ are in hand.
- The lock is sent in the clear, but the UI never shows the partner's pick before your own lock.
- **Reliability**: the transport drops bad packets silently. Each side therefore re-sends its latest `P_FTURN` every
  30 frames until it receives the partner's turn _n_ with the ack bit set.
- A turn costs 7–9 bytes each way, about 0.15 s on the 1-byte-per-frame link.
- **Hash mismatch**: the host sends `P_FSYNC` (4 packets: a 16-byte state snapshot: HP, focus, discard counts, hand
  slots, pattern progress, rng), and the guest adopts it. The ticker shows a tear, nothing more.
- **Cable lost for 3 s**: the bout ends. Both saves record "the signal tore" as no-contest: XP 5, no win or loss.

**Determinism rules**:

- All fight logic draws from `frng` (16-bit xorshift, as `kdc_roll`), seeded per fight. Never `DIV_REG`.
  Today's `crucible_fight.c` uses `DIV_REG` for taunt picks; those move to a separate cosmetic rng that never feeds
  state.
- Fusion lookups use local recipe tables, which are identical when `P_CAT` matches.

### 9.2 Match rules

The host picks on the link-room rules screen: LEFT/RIGHT preset; DOWN to customise rows; A to toggle; START sends.
The guest sees every byte rendered as rows and presses A to accept or B to ask for a change. "Ask" sends `P_READY=2`;
the host's screen shows a `?` by the guest's face. The last rules are saved (8 B).

The rules travel in the **existing `P_RULES` packet** (its `a.hi` and `b.hi` are 0 today, so older guests still read
mode, time and goal) plus a new `P_RULES2`:

| byte | where         | bits                                                                                                                                                                                                                                                             |
| ---- | ------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| R0   | P_RULES a.lo  | mode:4 (0 CO-OP, 1 RACE, 2 FIGHT) — existing, low nibble · time:4 — existing                                                                                                                                                                                     |
| R1   | P_RULES a.hi  | preset:2 (FAIR, TRUE STRENGTH, CHAOS, CUSTOM) · attributes:2 (NORMALISED, CAPPED, FULL) · best-of:2 (1/3/5) · timer:2 (pips 6/4/3/2)                                                                                                                             |
| R2   | P_RULES b.lo  | goal — existing (race); FIGHT: base HP = 8 + 2·(R2 & 7)                                                                                                                                                                                                          |
| R3   | P_RULES b.hi  | toggles: 0 no-stack · 1 passives off · 2 glitch arena · 3 random kits (seeded from each player's own shelf) · 4 sudden death (from turn 15 every hit +1) · 5 fog (hands hidden) · 6 mirror kits (both play the host's kit) · 7 co-op control: random / alternate |
| R4   | P_RULES2 a.lo | host handicap: HP +0..7 :3 · budget +0..3 :2 · focus +1 :1 · −1 passive slot :1 · +1 pip :1                                                                                                                                                                      |
| R5   | P_RULES2 a.hi | guest handicap (same layout)                                                                                                                                                                                                                                     |
| R6   | P_RULES2 b.lo | banned categories mask :7 · handicap-was-suggested :1                                                                                                                                                                                                            |
| R7   | P_RULES2 b.hi | arena: 0 the host's room · 1..8 a fixed arena · 9..15 seeded random                                                                                                                                                                                              |

Attribute modes:

- **NORMALISED**: both play GRIT 2, FOCUS 1, REACH 1 and 2 slots.
- **CAPPED**: real attributes clipped to those values.
- **FULL**: real attributes.

Presets:

- **FAIR** (default): NORMALISED, best of 3, 4 pips, no handicap.
- **TRUE STRENGTH**: FULL, with the suggested handicap shown but off.
- **CHAOS**: random kits, glitch arena, sudden death, attributes CAPPED.
- **CUSTOM**.

**Suggested handicap**: attribute power = `GRIT + 2·FOCUS + REACH + 2·(slots−1)`. The weaker side gets HP
`min(8, ⌊2·gap/3⌋)`. Its cost is one line of arithmetic.

Simulated, level 30 (GRIT 4, FOCUS 2, REACH 3, 2 slots) vs level 1, win rate of the high-level side:

| rules                               | reader vs reader | high-level greedy vs low-level reader |
| ----------------------------------- | ---------------- | ------------------------------------- |
| FULL                                | 78%              | 78%                                   |
| FULL + suggested handicap (+8 HP)   | 55%              | 52%                                   |
| CAPPED                              | 64%              | 59%                                   |
| CAPPED + suggested handicap (+4 HP) | 48%              | 43%                                   |
| NORMALISED (default)                | 48%              | 43%                                   |

(Re-run at plan step 25 on the 5,629-object slice, 600 bouts a row; the first edition read 76 / 57 / 68 / 54 / 52%.)
Under the default, levels do not matter; under CAPPED, a better reader beats a higher level. The suggestion brings a
78% gap down to 55% while leaving levels meaningful. On the cartridge the edge is at most 7 HP (R4/R5 hold 3 bits):
the level-30-vs-level-1 suggestion of 8 is given as 7.

Co-op and race use the same screen: race uses R0 time/goal and R3 bit 3; co-op uses R3 bit 7 (control style) and
R1 timer for cutscene choices.

### 9.3 Saves stay separate

- Joining any link mode asks **which save to bring: FREE, 1, 2 or 3**. That save _is_ your game for the session.
- Only your own save is ever written, and only by your own cartridge. Nothing the partner sends can write their
  save.
- Avatar, level, attributes, unlocks, story and kit all stay in their own save.
- If the partner leaves or the cable drops, you keep playing solo on the same save. There is no session wrapper to
  close and nothing is pending. You can rejoin later, with the same or another partner.

### 9.4 Co-op: two games, shared inventory, shared scenes

Each player walks, combines and fights in **their own game on their own screen**. Shared between them:

**Inventory (union for the session, bounded keep).**

- Every success sends `P_MADE`.
- The receiver may use the partner's element for the session as **borrowed**: a RAM list of up to 32 ids, shown on
  the shelf with a dotted frame. Borrowed elements are never written to SRAM.
- The first **12 partner elements per session** (and at most 24 per chapter on a story slot) that are new to your
  save are **kept**: granted at once through the existing `cru_grant` path, which is exactly how `LINK_EV_GIFT`
  works today. Before each grant, the cartridge first increments `kept` in the link record, then grants. A crash
  between the two leaves the counter one high, so the cap can never be exceeded.
- Anything you **make with a borrowed element** is a normal discovery of your own, written normally.
- When the session ends or the cable drops, borrowed elements vanish. Nothing has to be resolved, because nothing
  pending was ever stored.

Why the union over the intersection or the active player's shelf: the intersection makes co-op smaller than solo,
and the active shelf does not apply when both play their own games. The union with a keep cap gives the feeling of
pooling without letting one session copy a whole save.

**Cutscenes and boss encounters (shared, in sync).**

1. **Trigger**: whichever player's game reaches a story beat or a boss encounter becomes the scene **owner**. It
   sends `P_CUE(scene id, seed)` and shows a waiting tear for up to 4 s.
2. **Pull-in**: the partner's cartridge finishes its current atomic action at the next safe point, at most 2 s
   (a mix finishes its reveal; menus close; mid-walk stops at the tile; a duel in progress finishes its current turn
   and then pauses). Then it plays the pull glitch (screen tear plus the owner's face flashing) and answers
   `P_READY(1)`.
3. **Busy or declined**: if the partner is in its own scene, it answers `P_READY(0)`. The owner then plays solo, and
   the partner later gets the notice "SOMETHING HAPPENED ELSEWHERE".
4. **Beats**: the owner sends `P_BEAT(0)`, and both run the scene from the cue seed. At each choice beat (a dialogue
   choice, a boss answer, the end-of-boss choice), the **active player** = `frng(session seed, beat)`, with an
   anti-streak rule (never 3 in a row, so R3 bit 7 = random) or alternation.
5. **Handoff UX**: the active player's face slides in at the top left with a one-row glitch and a chime
   (`SFX_SWAP` pitched up). Their cursor moves on both screens (`P_CURSOR` sent on change, at most 10 per second).
   The watcher's screen shows "WATCHING" as a dim frame around the menu.
6. **Watcher influence**, simulated (§11.6):
   - **NUDGE** (SELECT): unlimited. A ghost cursor the active player sees, worth +1–2 points.
   - **VETO** (hold B for 1 s): once per scene or per boss phase. Takes the current beat, worth +2–3 points.
   - With VETO, a veteran + newcomer pair wins 69% against T3 bosses: the veteran alone wins 73%, the newcomer
     alone 66%.
7. **Co-op boss**: one shared HP bar = the average of both players' HP + 2. Each beat is answered from the active
   player's own hand. Damage and XP land in each save by its own rules.
8. **Partner disconnect mid-scene** (no packet for 3 s): the local player takes over every remaining beat, and the
   scene resolves locally with no restart. If the scene is the partner's story beat, it ends at the next safe line,
   and you keep the linked memory.

**Outcomes**:

- The **owner's** save gets the full story effect.
- The **watcher's** save gets a bounded trace: the linked memory (below) and an alignment tint of a quarter of the
  choice's axis change (at most ±2 per scene).

**Seeds grow together (bounded, lasting).** These are written at session start and at each shared scene, never only
at the end, so a disconnect cannot lose them:

- **Seed drift**: `cru_story_event(s, CRU_EV_LINK, partnerSeed16, partnerCell)`. The run seed already re-hashes on
  every event, so the partner enters your dream's randomness exactly as much as one event does.
- **Alignment tint**: each axis moves toward the partner's by `clamp((theirs − yours) / 8, ±2)` per shared scene,
  at most ±6 per session.
- **Linked memory**: the partner's genome, name and session count, plus `cru_story_remember(partner's last make)`,
  so it drifts into your dream's `{ITEM}` slots.
- **Shared discoveries**: at most 12 per session (above).

### 9.5 Partner notifications

- **A partner's make** → `P_MADE(id, flags)`. The receiver shows a toast through the existing feats toast card
  (`feats_toast`): the partner's head glyph in their hue, the object's name, and **NEW** if it is new _to the
  receiver's own save_ (the receiver decides that locally; it is not in the packet).
- **A partner's miss** → `P_MISS`. The receiver plays `SFX_PMISS`, a new short descending ch4 noise blip, and blinks
  the partner's header glyph for 8 frames. Nothing else.

Throttling:

- The partner toast queue is 4 entries (3 B each, link bank WRAM). It is shown only when the core toast queue is
  empty.
- Partner toasts are at least 3 s apart.
- When the queue is full, it merges into one card: "+N MORE".
- Miss sounds play at most once per 2 s, and misses are never queued (a dropped miss is fine).

| mode               | toasts                                                            | miss cue |
| ------------------ | ----------------------------------------------------------------- | -------- |
| co-op, own games   | yes: this is how you know what they are doing                     | yes      |
| co-op shared scene | suppressed (both see it); queued toasts wait for the scene to end | dropped  |
| race               | yes, with their score ("THEY MADE X 12/25")                       | yes      |
| versus fight       | no (the fight is the shared thing)                                | no       |

SFX: the table in `crucible_sound.c` has empty rows (`SFX_PICK` 2, 8, 9, `SFX_LINK` 15).

- `SFX_PMISS` reuses row 15 (`SFX_LINK`, currently silent).
- `SFX_PMADE` (two rising ch1 notes) takes row 9.
- About 24 B of envelope data in the sound bank. No new music.

### 9.6 Link packet layout (all 7 bytes: `A5 type a.lo a.hi b.lo b.hi sum`)

As built the numbers moved: 8 (TURN, the old fight's input, retired), 9 and 10 (the partner's name) were taken.

| type   | name                                     | a                                                                                          | b                                                      | when                                                                |
| ------ | ---------------------------------------- | ------------------------------------------------------------------------------------------ | ------------------------------------------------------ | ------------------------------------------------------------------- |
| 1–7    | HELLO, RULES, GO, FIND, GIFT, SCORE, END | unchanged; RULES now uses a.hi and b.hi (R1, R3); HELLO b bit 7: the guest's session began |                                                        | GO is said each second until then; END while the cable lives        |
| 9, 10  | NAME, NAME2                              | four letters each                                                                          |                                                        | in the room, every 4 s                                              |
| 11     | P_FTURN                                  | action, turn                                                                               | hash, pip \| ack << 7                                  | each versus turn (re-sent every 30 frames until acked)              |
| 12     | P_FSYNC                                  | chunk 0..25 \| byte << 8 (0xff: the guest asks)                                            | two bytes                                              | on a hash mismatch: the host's state, a chunk at a time             |
| 13     | P_MADE                                   | item id                                                                                    | flags (b0 new to the sender, b7 an ack) \| serial << 8 | each make in co-op (one in flight, re-sent each second until acked) |
| 14     | P_MISS                                   | serial                                                                                     | 0                                                      | each miss (at most one a second)                                    |
| 15     | P_CUE                                    | scene (1 a boss)                                                                           | seed                                                   | a shared scene starts (again with its setup until P_READY)          |
| 16     | P_READY                                  | scene; 0xff: the guest asks for other rules; 0x80: the scene ended or was given up         | the watcher's HP \| 0x100 (joined)                     | reply                                                               |
| 17     | P_BEAT                                   | round \| pick 0 << 8 (0xC0 + k: setup word k)                                              | pick 1 \| close calls << 8 (0xffff: an ack)            | each round of a shared boss                                         |
| 18     | P_CURSOR                                 | round \| cursor << 8 (0xff: a veto)                                                        | 1 on a veto                                            | the watcher points or vetoes                                        |
| 19     | P_CAT                                    | items                                                                                      | recipes                                                | versus setup                                                        |
| 20     | P_DECK                                   | id \| slot << 13                                                                           | id \| slot << 13                                       | versus setup (3 packets)                                            |
| 21     | P_LEAVE                                  | 0                                                                                          | 0                                                      | graceful leave                                                      |
| 22     | P_RULES2                                 | R4, R5                                                                                     | R6, R7                                                 | with RULES                                                          |
| 23, 24 | P_AVATAR, P_AVATAR2                      | genome 0–3; genome 4–5                                                                     | ; level \| attributes << 8                             | in the room (the edge) and in the versus setup                      |
| 25     | P_PAS                                    | passives \| (cell \| got-all << 7) << 8                                                    | the setup's seed                                       | versus setup                                                        |
| 26     | P_SAVE                                   | (reserved)                                                                                 |                                                        |                                                                     |
| 27     | P_SEED                                   | the run's seed                                                                             | order \| heart << 8                                    | co-op: every 2 s for the session's first 20 s (seeds grow together) |

The bytes move by interrupts (`crucible_link_io.c`): the host clocks one each VBlank, the serial interrupt takes the
reply and arms the guest's next, so a byte crosses each way every frame whatever the game's loop is doing. Network
tolerance (100–200 ms a byte over WebRTC): nothing assumes timing; everything that matters is re-sent until the other
side says it arrived; the link's timers count real frames; silence is 3 s in the room, 8 s in a session, 3 s in a
shared scene (the local side plays on). `docs/game-flow.md` (Link) has the rest.

At the existing rate of 1 byte per frame, a busy co-op second carries HELLO plus a couple of MADE, MISS or CURSOR
packets: well under the queue of 64.

---

## 10. Data layout, bytes, banks and performance

### 10.1 Fight state (WRAM, about 150 B, live only during a fight)

```c
typedef struct {                 /* one side: 38 B */
  uint16_t kit[6];               /* element ids */
  uint8_t order[6], top, ndisc;  /* the shuffled draw order (indices into kit), draw pointer, discard count */
  uint8_t disc_mask;             /* which kit slots are in the discard (stack bonus: count copies) */
  uint8_t hand[4], nhand;        /* kit slot per hand card */
  int8_t hp; uint8_t focus, last, lostrow;
  uint8_t pas[2], prog[2], on;   /* equipped passive templates, pattern progress, unlocked bits */
  uint8_t window, hist;          /* PRISM window (3x2 bits), last 4 stances (4x2 bits) for ADAPT and the AI */
  uint8_t flags;                 /* guard, fused, close call, ... */
} fside;
typedef struct {                 /* 2 x 38 + 30 = ~106 B */
  fside s[2];
  uint16_t rng;                  /* frng xorshift */
  uint8_t turn, kind, arena, rules[8], pip, state;
  /* boss only (the boss itself is the existing 24 B crucible_boss) */
  uint8_t tier, moves, cool[3], seen, shield, shield_t, charged, li, erode, memory;
} fstate;
```

`fstate` lives in WRAM as one static. Statics end at about 0xDAC5 and the stack bottom is at 0xDF00 (measured depth
about 270 B), which leaves about 0.8 KB, so this fits. The fight's existing `ticker_[96]` stays.

### 10.2 Tables (generated where possible)

| table                                      | bytes      | generative rule                                   | stored alternative                      |
| ------------------------------------------ | ---------- | ------------------------------------------------- | --------------------------------------- |
| `FAM_MASK[3]` (u16 trait masks per stance) | 6          | stance = argmax popcount(t & mask)                | 5,668 × 2 bits = 1,417 B                |
| `CAT_FAM` (7 × 2 bits)                     | 2          | tie-break                                         | —                                       |
| nibble popcount                            | 16         | (shared with `bits()` callers)                    | —                                       |
| `PASSIVE[12]` templates                    | 24         | pattern/effect interpreters                       | 12 hand-written routines (~1.5 KB code) |
| `FMOVE[16]` kinds + tells                  | 32         | —                                                 | —                                       |
| `SIG[6]` faction signature order           | 18         | boss = (faction, tier, seed)                      | 6 × 6 tiers × 8 moves = 288 B           |
| `TIER[6]`                                  | 24         | —                                                 | —                                       |
| duel AI weights (6 factions)               | 36         | —                                                 | —                                       |
| `ARENA[8]` effects                         | 8          | arena = room family % 8, plus time/lucid overlays | per-room arena table: 12 × 8 B = 96 B   |
| avatar option counts                       | 9          | unlock order = seeded permutation of 81 options   | 81 B order list                         |
| XP curve                                   | 0          | `40 + 20·L`                                       | 30 × 2 = 60 B                           |
| SFX envelopes (2)                          | 24         | —                                                 | —                                       |
| **total data**                             | **~200 B** |                                                   |                                         |

Text: about 60 new lines (tells, the end choice, notices, glitch rules) in the existing line bank via
`crucible_lines` intents, about 1.5 KB baked by the text toolchain. Words come from the existing dictionary (`crucible_lines_dict.c`).

### 10.3 Code and banks

| module (new banked unit)                     | content                                                         | est. code  |
| -------------------------------------------- | --------------------------------------------------------------- | ---------- |
| `crucible_fight.c` (rewritten, same API)     | turn engine: kit, draw, clash, passives, arena, duel AI, frng   | 4.5 KB     |
| `crucible_fight_boss.c`                      | moves, tells, tiers, phases, rehearsal/cooldown, nemesis memory | 2.5 KB     |
| `crucible_fight_ui.c`                        | fight screen, kit screen, end choice, level-up card             | 5.0 KB     |
| `crucible_link.c` (+ `crucible_link_play.c`) | lockstep, rules, co-op borrowed/keep, cutscene sync, notices    | 3.5 KB     |
| `crucible_avatar_edit.c`                     | genome, creator, unlocks, XP and levels                         | 3.0 KB     |
| player and link records                      | CRC, init, migrate                                              | 0.8 KB     |
| **total**                                    |                                                                 | **~19 KB** |

**Reserve 2 banks** (`CGB_CODE_RESERVE_BANKS` +2 in the bake): 32 KB for about 19 KB of code and
1.5 KB of text, with slack for tuning and for the old `crucible_fight.c` bank (about 2.5 KB freed). The cost is
**about 24 objects** (at about a dozen per 16 KB bank) of the 5,668 slice, or 0.4%. Do not take a third bank unless
a measured link map needs it.

**As built** (plan steps 1-25): the fight system and link play took **three** reserved banks, not two
(`CGB_CODE_RESERVE_BANKS` 7): the code banks filled until a 1.6 KB module had no bank left. The slice is **5,629**
objects (5,668 on ROM 2dc6caa8: 39 fewer, 0.7%). Modules (bytes as `bank-budgets.json` reports them): `crucible_fight`
13,626, `crucible_fight_rules` 12,099, `crucible_fight_bossui` 8,660, `crucible_fight_boss` 4,195, `crucible_fight_kit`
4,284, `crucible_player` 4,436, `crucible_avatar_edit` 1,981, `crucible_link` 3,853, `crucible_link_play` 5,005,
`crucible_link_rules` 2,335, `crucible_link_coop` 2,237, `crucible_link_scene` 1,725, the test mailbox
`crucible_fight_dbg` + `_dbg2` 2,291; the link's interrupt handlers (`crucible_link_io.c`, 100 B) sit in bank 0, which
has about 13 KB free. The main bank is **15,724 bytes** (15,773 on the base ROM: 49 fewer; the one addition is a
3-byte `wait_vbl_done` in `bust_place`). 28 code banks keep 6,538 bytes free, the largest gap 1,393. The core's
shelf-answer boss (`cru_counter`, `cru_boss_plan`, `cru_boss_answer`) is compiled out of the cartridge
(`-DCRU_HAND_FIGHT`); the classic and NES ports keep it. WRAM: the avatar's base layer moved to SRAM bank 2 scratch
(576 bytes), so the statics end at 0xDB49 and the deepest measured stack (a story's start) leaves about 630 bytes.

**Main bank (frozen, ~215 B free): 0 new bytes in the target.** `crucible.c` already calls `fight_open`,
`fight_tick`, `fight_fx` and friends. The kit screen, creator and level-up card run as sub-states inside
`fight_tick`'s and `talk`'s banked modules. If a new top-level screen is unavoidable, one generic `sub_tick()`
dispatch case costs about 12 B. Never more.

### 10.4 Save layout

**Story slot** (SRAM banks 15/1/3): the story record is at 0x1D00–0x1DB7 and the clock at 0x1F00–0x1F06
(`crucible_time.c`). New: **the player record at 0x1DC0–0x1E3F (128 B)**, with its own CRC, outside both. Note that
`wipe()` only clears up to 0x1D80, so `story_begin` writes this record explicitly.

| off | bytes | field                                                                                                                                                                |
| --- | ----- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 0   | 2     | magic 'P' 'L'                                                                                                                                                        |
| 2   | 1     | version                                                                                                                                                              |
| 3   | 6     | avatar genome                                                                                                                                                        |
| 9   | 1     | level                                                                                                                                                                |
| 10  | 2     | XP                                                                                                                                                                   |
| 12  | 1     | attributes: GRIT:3 \| FOCUS:2 \| REACH:2 \| slot2:1                                                                                                                  |
| 13  | 11    | cosmetic unlock bitset (81 options)                                                                                                                                  |
| 24  | 2     | secret counters (spared:3, taken:3, AI wins:2...)                                                                                                                    |
| 26  | 2     | passives known (12 bits)                                                                                                                                             |
| 28  | 12    | kit (6 ids)                                                                                                                                                          |
| 40  | 1     | equipped passives (2 × 4 bits)                                                                                                                                       |
| 41  | 2     | nemesis memory (stance, how beaten, met)                                                                                                                             |
| 43  | 8     | last match rules R0–R7                                                                                                                                               |
| 51  | 32    | link record: partner genome 6, partner name 8, sessions 1, session id 2, kept this session 1, kept this chapter 1, tint this session 2, last partner make 2, spare 9 |
| 83  | 43    | spare                                                                                                                                                                |
| 126 | 2     | CRC-16 (the existing table)                                                                                                                                          |

**Classic (free play) record**: bytes 264–375 are zero today. Use **264–345 (82 B)** for the same layout minus kit
history (the "link card"). Its avatar is copied from the last-played slot until edited. 346–375 stay free. It is
covered by the record's existing CRC; the lost-piece block at 246–263 is untouched.

### 10.5 Performance (SM83, GBC)

- A turn's resolution makes about 6 popcount lookups, 2 `cru_counter` calls (12-bit loops), and some compares and
  adds: about 3,000 cycles, under 5% of one frame.
- No divides or 32-bit math in hot paths:
  - The HP bar's `hp·8/max` becomes a per-fight step value: `max>>3`, then repeated subtraction, once per hit.
  - `below(n)` already uses an 8×8 multiply-shift.
- Fusion lookups (`cru_recipe`) for the hand's pairs (at most 6) are spread one per frame after each draw and cached.
- The boss's ADAPT and PREDICT count 4 history entries.
- Avatar generation is the existing cost; the face streams a third per frame.

---

## 11. Balance: proof by simulation

All tables come from `docs/fight-system/sim/` on the cartridge's own slice as built (plan step 25: 5,629 objects, 13,439 recipes,
exported from the bake into `catalogue/`; the first edition ran on the 5,668-object slice and its 13,532 recipes). Policies:

- `random`: uniform over legal actions.
- `greedy`: best response to a uniform guess over the opponent's hand.
- `stacker`: always repeats its stance, biggest card first.
- `rusher`: chases its passive patterns.
- `reader`: models the opponent's stance-after-stance habits, pattern needs and hand; best response 80% of the time,
  second best 20%.

### 11.1 Policy vs policy (balanced kits, 300 bouts per cell, row win rate)

| row \ col | random | greedy | stacker | rusher | reader | field   |
| --------- | ------ | ------ | ------- | ------ | ------ | ------- |
| random    | 50%    | 10%    | 38%     | 16%    | 11%    | **19%** |
| greedy    | 93%    | 55%    | 72%     | 55%    | 50%    | **58%** |
| stacker   | 62%    | 32%    | 48%     | 36%    | 19%    | **34%** |
| rusher    | 87%    | 46%    | 62%     | 46%    | 35%    | **47%** |
| reader    | 87%    | 55%    | 84%     | 70%    | 46%    | **64%** |

"Field" = mean against the four non-random policies; random is the calibration floor. Mean bout: 10.3 turns.
No fixed strategy exceeds about 60% against the field; the reader (the skill policy) reaches 64% and beats random 87%.

### 11.2 Kit archetype vs archetype (both play `reader`, 300 bouts per cell)

| row \ col | balanced | stack | heavy | swarm | fuser | mono | random | field   |
| --------- | -------- | ----- | ----- | ----- | ----- | ---- | ------ | ------- |
| balanced  | 49%      | 57%   | 61%   | 46%   | 58%   | 73%  | 58%    | **58%** |
| stack     | 40%      | 50%   | 62%   | 46%   | 50%   | 68%  | 51%    | **52%** |
| heavy     | 39%      | 43%   | 52%   | 36%   | 44%   | 63%  | 44%    | **46%** |
| swarm     | 52%      | 56%   | 59%   | 50%   | 56%   | 70%  | 54%    | **57%** |
| fuser     | 45%      | 53%   | 55%   | 44%   | 49%   | 68%  | 49%    | **52%** |
| mono      | 28%      | 35%   | 45%   | 26%   | 33%   | 46%  | 32%    | **35%** |
| random    | 46%      | 52%   | 57%   | 48%   | 51%   | 66%  | 52%    | **53%** |

Mean bout: 13.4 turns. **First-player advantage: none.** In the mirror `reader`, the host wins 48.4% and draws are
0.3% (1,800 bouts). Choices are simultaneous, so there is no first mover.

### 11.3 Passives (reader vs reader, random kits, 2,000 bouts)

| passive   | win | unlocked |     | passive   | win | unlocked |
| --------- | --- | -------- | --- | --------- | --- | -------- |
| ECHO      | 52% | 97%      |     | SALVE     | 51% | 30%      |
| PRISM     | 53% | 55%      |     | UNDERTOW  | 49% | 64%      |
| SCAR      | 51% | 90%      |     | LUCID     | 47% | 61%      |
| VIGIL     | 50% | 19%      |     | NOCLIP    | 48% | 28%      |
| CATALYST  | 49% | 84%      |     | OVERCLOCK | 46% | 31%      |
| STALEMATE | 59% | 64%      |     | MEMORY    | 47% | 92%      |

### 11.4 Comebacks (reader mirror, balanced kits)

| ever behind by | bouts | still won |
| -------------- | ----- | --------- |
| 4+ HP          | 2,008 | 22%       |
| 6+ HP          | 1,323 | 14%       |
| 8+ HP          | 727   | 6%        |

Possible, earned, and not a coin flip. The levers are all deterministic: focus from losses, the 4-card hand at
≤4 HP, UNDERTOW, SCAR and the EXIT SIGN arena.

### 11.5 Why the claims hold

- **The cycle closes**: every stance beats exactly one and loses to exactly one. Power and edges change the margin,
  not the winner, so stacking power cannot turn a loss into a win. Mono kits fall to 34%.
- **Cost cannot beat the cycle**: the heavy kit (all budget in three P3s) sits at 48%, because the triangle punishes
  a narrow hand. A dual plays at P−1, so flexibility is paid for (swarm at 56% is the ceiling).
- **Passives have counterplay**: the patterns are public, chasing them makes you readable, and SEVER resets them.
  The rusher scores 50% against the field: chasing passives blindly is no better than ignoring them.
- **Skill beats luck**: reader 86% vs random; random 19% against the field. Draws are 0.7%.

### 11.6 Bosses (competent player model, 250 fights per faction per tier)

The competent player reads plain strikes perfectly. It reads a trick it has already been taught 92% of the time, a
new trick 30% (55% after the rehearsal), and a veiled result through recipe knowledge (50%) or the lean rule (74%).
It slips on 4–12% of inputs and misses 5–22% of last-pip attempts as the pips shrink.

| tier | PROGRAM | DAEMON | GHOST | AI  | OPERATOR | RELIC | first try | after one loss | rounds |
| ---- | ------- | ------ | ----- | --- | -------- | ----- | --------- | -------------- | ------ |
| T1   | 95%     | 92%    | 88%   | 90% | 76%      | 86%   | **88%**   | 99%            | 14.1   |
| T2   | 88%     | 81%    | 51%   | 84% | 77%      | 68%   | **75%**   | 93%            | 13.0   |
| T3   | 65%     | 76%    | 59%   | 62% | 67%      | 66%   | **66%**   | 89%            | 13.8   |
| T4   | 60%     | 48%    | 74%   | 48% | 59%      | 56%   | **57%**   | 85%            | 13.0   |
| T5   | 44%     | 45%    | 41%   | 47% | 42%      | 35%   | **42%**   | 69%            | 10.9   |
| T6   | 32%     | 32%    | 41%   | 36% | 24%      | 21%   | **31%**   | 52%            | 11.0   |

Reading the spikes: GHOST at T2 (51%) is the FEINT lesson. PROGRAM at T3 is FEINT on top of a loop. AI and DAEMON at
T4 are FEINT plus SHIELD or STEAL. These are the intended "you lost to a trick, now you know it" fights; their retry
rates are 85–94%.

Approximations in the model: the simulator plays POSSESS, JACK, PREDICT and COMPILE as "read the tell or not". Its
player never fuses against bosses, so real RELIC fights should run a few points easier.

Nemesis (T3 GHOST): first meeting 61%; rematch with memory 69%; same rematch without memory 72%.

Co-op boss (T3, all factions; control random per beat):

| pairing            | strong solo | weak solo | co-op, no influence | + NUDGE | + VETO (1 per phase) |
| ------------------ | ----------- | --------- | ------------------- | ------- | -------------------- |
| veteran + newcomer | 71%         | 65%       | 68%                 | 69%     | 70%                  |
| veteran + casual   | 71%         | 67%       | 69%                 | 69%     | 70%                  |

VETO is the default: it is one decisive moment per phase, it is fun, and it is not backseat driving.

---

## 12. Migration from `crucible_fight.c`

What it is today:

- the boss combines its hand (`cru_boss_plan`), with cue pips;
- you answer with **any owned element** from the whole shelf (LEFT/RIGHT/UP/DOWN shelf navigation);
- `cru_counter` scores the answer: 0 hit, 1 parry, 2+ counter;
- phases at ½ and ¼; the nemesis flees;
- it uses `DIV_REG` for cosmetic picks.

Keep:

- the public API: `fight_open`, `fight_tick`, `fight_attack_a/b`, `fight_result`, `fight_answer`, `fight_fx`,
  `fight_state`, `fight_lost`, `fight_rival`, `fight_shimmer`, `FIGHT_*`. The main bank stays untouched;
- ticker, `say`, HP bar, pips, FX flags, `cru_boss_begin` and `cru_boss_plan` (the boss hand and the recipe attack);
- the `crucible_boss` struct, the nemesis in `crucible_story`, and the existing story side effects (loss, steal,
  lucid, standing, acts).

Change:

1. The answer comes from the **hand** (the kit), not the shelf. `cru_shelf_step` and `cru_shelf_group` leave the
   fight.
2. Resolution moves to the clash rules. `cru_boss_answer`'s score becomes our outcome. Our module calls the public
   core functions for side effects (`cru_story_loss/gain/lucid/act/event`). Standing changes need one small public
   core function, `cru_story_shift(s, f, d)` (`shift()` is static today). That, plus the two new acts, is the only
   core ask, and the core peer lands it atomically.
3. `level` maps to a tier: `tier = 1 + min(5, pressure/2)`. Phases keep the same thresholds.
4. `DIV_REG` moves to a cosmetic rng. Logic uses `frng`.
5. `fight_open(f, level, nemesis)` gains a kind (`fight_open_kind(kind, f, tier, flags)` banked). The old function
   is a wrapper for BOSS.

Old saves have no player record (magic missing), so defaults apply: the genome is rolled from the slot seed (the
old face), level 1, and an auto-kit.

---

## 13. Implementation plan (ordered, to the end)

Each step builds and is verified before the next. "PyBoy" means headless PyBoy driven from Python. Read WRAM by
symbol from the build's `.noi` map and the listings in `build/obj` (`test/harness/romsym.py`), inject joypad input,
and press A once on the title clock first.

1. **Reserve banks.**
   - Change: `CGB_CODE_RESERVE_BANKS` +2; rebake.
   - Test: PyBoy boots to the bench with no KERNEL PANIC; header count reads the new slice (about 5,644); link map
     shows 2 empty banks.
2. **Golden vectors.**
   - Change: export from the simulators 2,000 clash cases, 300 full seeded duels (inputs plus final state) and the
     boss tier runs to `docs/fight-system/sim/golden/`. Build `crucible_fight.c` as host C (`FIGHT_HOST`, like
     `AVATAR_HOST`).
   - Test: the host build matches every vector byte for byte.
3. **Stance, power and edge** (`fight_stance`, `fight_power`).
   - Test: a debug hook fills a WRAM buffer with stances for ids 0..255; matches `items.json`.
4. **frng, kit, shuffle, draw, discard, stack counts.**
   - Test: for a given seed, 20 draws match golden; then sample the SM83's cycle count around `fight_draw`.
5. **Clash, guard, focus, fuse, SEVER.**
   - Test: scripted inputs replay 50 golden duels against a scripted opponent; final HP and turn count match.
6. **Passives** (12 templates, public progress).
   - Test: per-passive vectors (unlock turn, effect applied).
7. **Duel screen and controls** (§1.5), with status and pips; the fight's own palette lease.
   - Test: BG map snapshot after each input of a script; OAM 0–23 unchanged; the resolve frame stays under the LY
     budget (read LY after resolve; never past line 153 of the same frame).
8. **Duel AI and encounters** from walking and UP/DOWN approaches. First contact waits for a DREAM element.
   - Test: a fresh run with no DREAM never triggers a duel in 2,000 scripted steps; after LIFE, the tutorial seed's
     turn-1 HUM appears.
9. **Kit screen and auto-kit; budget; save kit.**
   - Test: over-budget is refused; stack discount costs; the kit persists across a soft reset.
10. **Boss engine on the new rules** (telegraph/veil, tricks, tiers, rehearsal, cooldown, phases, drain). Keep the
    API.
    - Test: for each faction × T1..T6, a "perfect" bot (reads the real stance from WRAM) wins; a "naive" bot
      (answers the shown stance) loses to FEINT rounds; win rates of scripted bots against bosses match `bosssim`
      within ±5 points.
11. **Nemesis memory and RECALL; flee rule.**
    - Test: beat it with WALL; in the rematch, SHIELD covers WALL for 5 rounds.
12. **Arenas, GLITCH fights, GAUNTLET.** Add the arena pass to the simulator first (each arena: archetype spread
    still ≤ 60%).
    - Test: forced room family, time and lucid (debug pokes) give the expected arena id and rule.
13. **End choice, alignment and truth.**
    - Change: `kdc_nudge` in KEEL; core asks: `cru_story_shift`, the SPARE and TAKE acts.
    - Test: `kdc` bytes 14/15 and `lean[][]` deltas after each choice.
14. **Player record** (slot 0x1DC0, Classic 264–345), CRC, init in `story_begin`, defaults for old saves.
    - Test: write, soft reset, read back; corrupt the CRC and get defaults; slot wipe and new game; Classic CRC still
      valid.
15. **Avatar genome and creator** (§8.1–8.2).
    - Test: for 50 genomes, the cartridge's tiles hash equals the host-built `AVATAR_HOST` tiles; a scripted creator
      session produces the expected 6 bytes.
16. **XP, levels, attributes, passive slot, unlock order, secrets.**
    - Test: inject events and check level/points; set the RTC to 3:33 paused at night and the sheet-ghost bit sets;
      a chapter in LG sets radiant.
17. **Show the face**: dialogue box, fight BG tiles (VRAM bank 1), link room, notice glyph.
    - Test: VRAM tile range check; sprite count per line ≤ 10 in the fight screen.
18. **Link transport hardening**: ack and re-send for lockstep types, `P_CAT`, `P_RULES` extra bytes, `P_RULES2`.
    - Test: a two-PyBoy harness steps both emulators frame by frame and shuttles SB/SC bytes through a Python bridge,
      with 1% random byte corruption; all packets arrive (re-sent) and old-format RULES still parse.
19. **Versus fight**: kit/avatar exchange, `P_FTURN` lockstep, hash and `P_FSYNC`, rules screen, presets,
    handicap suggestion, last rules saved.
    - Test: 200 seeded bouts with random inputs on both cartridges end in identical state on both; forced
      desyncs recover; a 3-s cable cut gives no-contest.
20. **Co-op inventory**: save selection on join, borrowed list, keep cap with counter-then-grant, `P_MADE`/`P_MISS`,
    toast queue, `SFX_PMISS`/`SFX_PMADE`, race notices.
    - Test: A makes 30 new things and B keeps exactly 12. Cut the link between B's counter write and grant (break on
      `cru_grant` with a hook) and B's save is valid with kept ≤ 12. Toast cadence ≥ 3 s.
21. **Shared scenes**: `P_CUE`/`P_READY`/`P_BEAT`/`P_CURSOR`, pull-in at safe points, active player by seed,
    NUDGE/VETO, co-op boss, disconnect takeover.
    - Test: A triggers a boss while B is mid-mix; B enters within 120 frames of its reveal; the control sequence
      matches the seed; cut at beat 3 and the local side finishes; both saves valid.
22. **Seeds grow together**: `CRU_EV_LINK` events, tint bounds, linked memory.
    - Test: 50 simulated sessions keep the axis drift ≤ 6 per session; the event count and memory ring are as
      specified.
23. **Web link room avatar** (wasm of `AVATAR_HOST`, or a TS port).
    - Test: golden image equality for 50 genomes.
24. **Text**: new line intents and lines through the text bake; line bank space check.
    - Test: render every new line id in PyBoy; no overflow.
25. **Final balance pass**: export the cartridge's real tables into the simulators and rerun §11; real-hardware or
    real-WebKit timing pass (touch-check tool); remove the dead shelf-answer path; update this doc's tables.

---

## 13a. As built: status and deviations (plan steps 1–25)

Every step was built and verified in order with headless PyBoy step tests, a host build of the fight engine replayed
against the simulator's golden vectors, a wasm build of the avatar checked against the host build, and the link
bridge (`test/harness/linkbridge.py`, two cartridges, noise and delays). Only the bridge is part of this repository.

Deviations from the text above, and why:

- **Packet numbers** moved (9.6): 8–10 were taken.
- **Three reserved banks**, not two (10.3): the code banks filled; 39 objects fewer than the base ROM instead of 24.
- **The link moves bytes by interrupts** (bank 0, 100 B) instead of one per main-loop turn: a fight's loop can take 26
  frames, and the old rate made a versus setup take seconds and starve GO.
- **Boss**: its stance target follows the simulator's lean (with the product matched), MEMORY keeps 2 products, a
  boss's loss costs once per fight (not per hit), JACK drops a card and STEAL takes the deck's copies, as `bosssim`
  plays them. The "perfect bot wins every tier" test became "beats the competent bot at every tier and beats every
  faction": a hand can lack the answer, so reading alone cannot win them all.
- **SCAR and SALVE** use the side's own maximum HP in the simulator, as on the cartridge.
- **Shared bosses**: one engine on the owner's hand (not each beat from the active player's own hand), so the two
  cartridges cannot differ; the end-of-boss choice stays the owner's. The owner waits up to 10 s (not 4) for a partner
  mid-mix to finish its reveal.
- **Co-op borrowing**: the borrowed shelf (9.4) is not built. Partner makes beyond the keep cap are toasted, not
  usable; the first 12 new ones are kept (granted) as specified.
- **Versus handicaps**: the HP edge (at most 7, the field's width), the focus bit, a passive slot less and a pip more
  work; the budget handicap and banned categories (R6) are carried but not applied.
- **No-contest**: a cable cut ends a FIGHT after the 8 s session silence (not 3 s); a 3 s cut is survived and the bout
  goes on, identical on both.
- **Face**: the idle bob is off during fights (a bob split across a DMA put 12 sprites on a line); faces draw 2–3x
  faster (spans by bytes, no division per shaded pixel, the aura a row at a time), identical output. Two sdcc
  miscompiles in the avatar were worked around, so the cartridge's faces now equal the host build's.
- **XP sources**: a known recipe remade gives XP (1, 20 a chapter) but crosses to a co-op partner only as a toast;
  link sessions give 20 + 2 per partner make told (40 at most).

Not done:

- The link room's figures and the partner's face in the room (9, 8.5): the room has its cable row and the partner's
  name; both faces show in the versus fight, and the web link room has the wasm face generator.
- A notice glyph in the partner's hue (8.5): partner toasts use the link glyph in the toast's own colours.
- Real-hardware and real-WebKit timing passes (step 25): timing was measured in PyBoy only (a turn's resolve under a
  frame; a face 10–33 frames; the link a byte a frame each way).

## 14. Open questions for the owner (with my defaults)

**Owner decisions 2026-10-04:** 1 open hands = YES, 2 duel loss destroys elements = NO, 3 kit = PERSISTENT,
4 co-op boss = SHARED BAR. 5 (only FAIR counts for feats) = the default, not yet confirmed.

1. **Open hands in duels and versus?** Default: **yes**, you see the stances they hold (that is where reading
   comes from). FOG is a rule toggle and a GLITCH fight.
2. **Can a duel loss destroy elements?** Default: **no** (it costs lucid and XP). Bosses keep the current steal and
   loss by run scale. Gentle never destroys.
3. **Kit before every fight, or a persistent kit?** Default: **persistent**. Edit it from the bench; SELECT during
   the intro tear opens it.
4. **Co-op boss: shared HP bar or one bar each?** Default: **shared bar** (average + 2). Every beat matters to both.
5. **Should TRUE STRENGTH versus results count anywhere (feats, leaderboard)?** Default: **only FAIR counts** for
   feats. TRUE STRENGTH and CHAOS are for fun.
