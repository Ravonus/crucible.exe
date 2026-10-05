# CRUCIBLE.EXE music

## The soundtrack: the trailer's chip groove

The cartridge plays the trailer's bed live. Format v4 (`keel_music.h`) is a register-level groove tracker: 16-row
patterns per channel (kicks as pulse 1 with a falling NR10 sweep, stabs on pulse 1, a 25 % 16th arpeggio on pulse 2,
the cabinet's pad wave as a gated bass, noise snares, claps, hats and risers), full envelope levels, 6-8 frames a
16th, sections with seeded alternates, loop points, a 20-frame gap between songs and effect ducking. Songs:
title-punch, bench-groove (the bed as a loop), book-light, talk-strip (pulse 2 free), night-groove, lost-groove,
boss-drop (149 BPM), link-busy, ending-lift. The bench recorded from the ROM matches the trailer's bed bar for bar
(aligned mel-spectrogram correlation 0.85-0.96 over the groove bars, same tempo, chroma cosine 0.999).

The scores are compiled into `cartridge/data/include/crucible_music_data.h` by the music toolchain, which is not part
of this repository; `cartridge/src/crucible_sound.c` plays them.

## The first soundtrack: ambient chip (format v3), kept as a design record

The first soundtrack aimed at a quiet ambient chiptune. This section records the reference it was measured against,
what was wrong with the music before it, and how the synth got close on the GBC's four channels.

An ambient chiptune loop (26.7 s) was used **only** as a style reference. No melody, chord sequence or arrangement
was copied: every song was written for this game.

## 1. The reference, measured

Analysed with ffmpeg + librosa (CQT, chroma, onset, pyin, band energy, harmonic profiles).

| Property            | Measured                                                                                                                                 | What it means for us                                                     |
| ------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------ |
| Tempo / grid        | onsets every 0.209 s (IOI median), strong periodicity at 0.418 s; 26.67 s = 8 bars                                                       | ~72 BPM in 4/4, everything on a 16th-note grid (≈12.5 frames at 59.7 Hz) |
| Key / mode          | Krumhansl: C major 0.90 (A minor 0.80); no accidentals but a maj7 colour                                                                 | plain diatonic major, Ionian/Lydian colour from 7ths and 9ths            |
| Harmonic rhythm     | one chord per bar (3.33 s), 8 bars per loop, a classic diatonic I–vi–IV–V-family loop ending on V                                        | one chord per bar; the loop turns back on a V or sus chord               |
| Bass                | octave 2 (D2–B2, 73–123 Hz), sustained, re-struck every half bar; harmonics −8 dB (2nd), −7 dB (3rd), quickly gone                       | a round triangle/sine, two notes a bar, never staccato                   |
| Middle (arpeggio)   | C3–B4, a note every 16th, chord tones with the major 7th on top, slight swell                                                            | continuous soft broken chords in octave 4                                |
| Lead                | C5–E5, long notes (1–1.6 s), quick attack, flat sustain, soft release, gaps between phrases; harmonics 2nd −30 dB, 3rd −21 dB, 4th+ gone | almost a sine: on the GB a 50 % pulse, quiet, never above C6             |
| Vibrato             | a gentle wobble on held lead notes: pitch s.d. 4–9 cents, p5–p95 10–30 cents (pyin)                                                      | a late, shallow vibrato: ±2 register units ≈ ±14 cents at C5             |
| Percussion          | none: energy above 4 kHz is 0.015 % of the total                                                                                         | no hats; at most a very quiet tick in a few songs                        |
| Echo / delay        | no distinct delay peak in the onset autocorrelation (only the 0.209 s grid)                                                              | an echo voice is a colour for some sections, not a constant              |
| Band energy (power) | < 250 Hz 57 %, 250 Hz–1 kHz 42 %, 1–2 kHz 0.3 %, > 2 kHz 0.1 %; centroid 290 Hz                                                          | warm and low: the bass carries half the energy, the middle the rest      |
| Dynamics            | RMS IQR ≈ 3 dB (−20.2 … −16.9 dBFS), the last 2 s slightly lower                                                                         | flat, calm level; no accents                                             |
| Stereo              | L/R correlation 0.95; middle band +0.6 dB left, lead band −0.7 dB (right)                                                                | nearly mono, a little width                                              |
| Loop                | 8 bars, 2 × 4-bar halves; the second half swaps one chord and varies the lead                                                            | A/A' forms; variation comes from small changes, not new material         |

## 2. The old music, and why it was annoying

Recorded from the previous ROM (2dc6caa8) in PyBoy: `docs/media/music/before-*.mp3`.

- **It was beeps, not music.** Every melody note was a short falling pluck (NRx2 `0x42`/`0x43`: volume 4 gone in
  0.13–0.19 s; the soft variant `0x31` in 0.05 s), then silence. The same shape and register as the UI's own effects
  (`0x61`–`0x83`), so the music sounded like the interface beeping at random.
- **No harmony.** One pulse melody and one wave-channel bass note per 3.6 s phrase (cut to 25 % halfway through). No
  chords, no arpeggio, no pad: thin and bare, the reverse of the reference's continuous middle.
- **Relentless sameness.** All four moods shared one 31-cell pool of 12-step patterns over the same five scale
  degrees, with identical envelopes and duties: welcome, workshop, reflection and conversation were the same blips.
- **Too short and too regular.** A 12-step (3.6 s) phrase, a 32-phrase form (115 s) and phrase-aligned furnace
  crackle (a noise hit every phrase on the bench) made the grid audible.
- **A lurching tempo.** The bed counted calls, and the main loop slows while art decodes: the driver ran 0.6–0.8
  calls per frame on an idle bench and **0.14** while browsing the shelf, so the music slowed to a seventh of its
  speed whenever you moved.
- **Clashes with effects.** Talk blips use pulse 2, the melody's own channel, so a conversation chopped the tune;
  effects on pulse 1 cut the echo dead.
- **Bright where the reference is warm.** Power centroid ~420 Hz vs 290 Hz; half the energy in 250 Hz–1 kHz
  blips and 3 % above 2 kHz (25 % duty), almost nothing sustained in the bass.

## 3. The synth, upgraded (format v3, `keel_music.h`, `crucible_sound.c`)

Within 2 pulses, the wave channel and noise:

- **Instruments** (`INSTRUMENTS` in the scores): duty per instrument (50 % or 25 % only; the compiler refuses
  12.5 %/75 %), an attack envelope (a _swell_ from 0, or a soft falling pluck), a hold point, a sustain envelope
  retriggered at the hold (flat), a release envelope retriggered at note off (a soft tail instead of a cut), a late
  vibrato (depth in register units, delay in frames). The compiler refuses envelopes louder than volume 6.
- **Lead** (pulse 2): swell → flat sustain → soft release, slides (`~G5`), late vibrato.
- **Harmony** (pulse 1), per section: soft 16th-note _arpeggios_ over the bar's chord (12 patterns, 4 chord tones and
  their octaves, an occasional octave lift), a _pad_ (a swell into a fast 3-tone arpeggio, the classic chip chord,
  3–4 frames a tone), or an _echo_: the lead again, a few rows later and quieter, optionally panned to one side.
- **Bass** (wave): custom wave RAM per song (sine, triangle, a round organ-ish sine, and the cabinet's original pad
  wave, restored for the effects' drones and fanfares), half-bar/whole-bar patterns, a release that steps
  100 → 50 → 25 % → off a row at a time instead of cutting.
- **Drums** (noise): three very soft sounds (tick, shaker, thump), off on quiet screens and in most songs.
- **Mix**: per-song NR51 panning (e.g. the link song puts the lead left and its answer right; the night song puts
  the echo right); effects are always centred while they play.
- **Form**: songs are sections (intro, A, B, …) in an order with a loop point; a song heard before in this power-on
  comes back at its loop point, not its intro.
- **Generative, seeded**: every pass through a section picks a lead variant (authored ones plus derived `thin`,
  `low`, `hush`), one of two arpeggio patterns, sometimes an octave lift, and sometimes leaves the drums out, from the
  music's own xorshift stream (never gameplay RNG, never a random pitch).
- **Crossfade**: a song change releases the held notes (their release envelopes are the tail), drops the bass to
  25 % then off, waits 0.6 s and fades the next song in over half a bar.
- **Ducking, not cutting**: while an effect plays, the music's other channels play 2 steps softer; the music never
  writes a channel an effect holds; when the effect ends, the held note comes back at its sustain level. Effects are
  byte-for-byte the old ones.
- **Key**: the per-save key the effects already used (C, D, F, G or A) transposes the music too, so effects always
  sit in the music's key.
- **Timing from VBlanks** (`sys_time`): rows, envelopes and vibrato keep the screen's time even when art decoding
  slows the main loop; a slow frame plays the rows it owes.
- **Never grating**: the compiler refuses a held lead note (or its echo) a semitone from its chord unless it is a
  chord tone, and keeps derived variants inside the bar.

Cost (measured in PyBoy, every screen and song, samples include interrupts that land in the call): mean
**1.9 %** of a frame per call, p95 4.0 %, peak 9.7 % (old driver: 0.8 %, 1.2 %, 7.6 %). ROM: the sound module grew
from 5,263 to 8,818 bytes (score data 1,731 bytes for nine songs, was 695 for four moods); the autobanker placed it in
a bank with room and the main `crucible` bank is unchanged (15,773 bytes). WRAM: +73 bytes.

## 4. Style rules for the ambient songs

60–90 BPM, one chord a bar, leads C4–C6 with long notes and rests, arpeggios in octave 4, a sine/triangle bass,
little or no percussion.
