<!-- SPDX-License-Identifier: GPL-3.0-only -->
# GUTTR (`guttr`)

A vocal-tract "throat" synthesizer, the owner's GUTTUR synthesis (lab
`docs/context/future_modules/guttur/package`) restructured for the Wirescape SDK. Each note sings with a
group of 1–8 throats (one leader and followers) taken from a budget of 16. A throat is an LF glottal pulse
with closure-time instability (jitter, shimmer, growl, creak, fry, register breaks), breath, rasp and
flutter noise, ten parallel resonators from a 34-section tube model, a pitch-following high-pass, drive,
and a place in space (distance, azimuth, inter-ear delay, head shadow). The throats are mixed into one
stereo pair with early reflections and an eight-line room, body and air shelves, a low-mid guard, DC
blocking, an optional post-grit and a 2.5 ms look-ahead limiter (ceiling about −1 dBFS).

- **Module ID** `guttr`, name `GUTTR`, version 1, category Instrument. Kind `new`. GPL-3.0-only.
- **Latency** `round(0.0025 × sample rate)` frames: 110 at 44.1 kHz, 120 at 48 kHz, 240 at 96 kHz.
- **State:** none beyond the parameters, which the patch saves (no `save_state`).
- Design, memory, per-voice reading and deviations from the old package: [DESIGN.md](DESIGN.md).

## Ports
| ID | Direction | Kind | Hint | Notes |
|---|---|---|---|---|
| `notes` | input | event | `ev_notes`, poly | note-on (key, velocity 0–1), note-off, choke, at their sample offsets |
| `left` | output | signal | `audio`, stereo pair `stereo` | the mixed left side, always one channel |
| `right` | output | signal | `audio`, stereo pair `stereo` | the mixed right side, always one channel |

The 16 throats are internal: they are not 16 cable channels. Up to 16 notes are tracked. A note-on with
a `note_id` of 0 or more is matched by (source, `note_id`); without one, by the newest (source, channel,
key). When all 16 note records are held, the oldest note is stolen; when the 16 throats are busy, the
oldest notes' throats are stolen (released notes first), each with a one-tick fade. A choke with a
`note_id` stops that note, one with a channel and no `note_id` stops that anonymous key, and one with
neither stops every note of the source (source `UINT32_MAX`: every source).

## Parameters
**Per-voice** parameters (`PER_VOICE`) are read for each note on the note's event channel, so poly
modulation on voice channel *c* changes only the note that arrived on channel *c* (`host.keyboard`
sends voice *c* on channel *c*). A one-channel signal applies to every note. **Global** parameters are
read once, on channel 0. All are read at the fixed 128-frame control tick; a note-on also reads them at
its own frame.

| # | ID | Name | Range | Default | Unit | Scope | Flags |
|---:|---|---|---|---:|---|---|---|
| 0 | `pressure` | Pressure | 0 – 1 | 0.5 | | per-voice | M A |
| 1 | `breath` | Breath | 0 – 1 | 0.12 | | per-voice | M A |
| 2 | `whisper` | Whisper | 0 – 1 | 0 | | per-voice | M A |
| 3 | `jitter` | Jitter | 0 – 5 | 0.3 | % | per-voice | M A |
| 4 | `shimmer` | Shimmer | 0 – 20 | 2.5 | % | per-voice | M A |
| 5 | `tremor` | Tremor | 0 – 60 | 6 | cent | per-voice | M A |
| 6 | `wander` | Wander | 0 – 40 | 4 | cent | per-voice | M A |
| 7 | `vibrato` | Vibrato | 0 – 80 | 0 | cent | per-voice | M A |
| 8 | `growl` | Growl | 0 – 1 | 0 | | per-voice | M A |
| 9 | `creak` | Creak | 0 – 1 | 0 | | per-voice | M A |
| 10 | `tail_fry` | Tail fry | 0 – 1 | 0 | | per-voice | M A |
| 11 | `flutter` | Flutter | 0 – 1 | 0 | | per-voice | M A |
| 12 | `rasp` | Rasp | 0 – 1 | 0 | | per-voice | M A |
| 13 | `breaks` | Breaks | 0 – 1 | 0 | | per-voice | M A |
| 14 | `size` | Size (tract length) | 13 – 20.5 | 17.5 | cm | per-voice | M A |
| 15 | `pharynx` | Pharynx | 0 – 1 | 0 | | per-voice | M A |
| 16 | `mouth` | Mouth | 0 – 1 | 0.5 | | per-voice | M A |
| 17 | `ring` | Ring | 0 – 1 | 0.3 | | per-voice | M A |
| 18 | `vowel` | Vowel | 0 – 1 | 0.5 | | per-voice | M A |
| 19 | `style` | Style | 0 – 1 | 0.45 | | per-voice | M A |
| 20 | `attack` | Attack | 3 – 1200 | 60 | ms | global | M A |
| 21 | `release` | Release | 20 – 2500 | 220 | ms | global | M A |
| 22 | `glide` | Glide | 0 – 600 | 60 | ms | global | M A |
| 23 | `throats` | Throats per note | 1 – 8, step 1 | 3 | | global | S A |
| 24 | `looseness` | Looseness | 0 – 1 | 0.35 | | per-voice | M A |
| 25 | `spread` | Spread | 0 – 1 | 0.5 | | per-voice | M A |
| 26 | `size_spread` | Size spread | 0 – 1 | 0.4 | | per-voice | M A |
| 27 | `distance` | Distance | 0 – 1 | 0.15 | | global | M A |
| 28 | `azimuth` | Azimuth | −180 – 180 | 0 | ° | global | M A |
| 29 | `room` | Room size | 0 – 1 | 0.5 | | global | M A |
| 30 | `decay` | Room decay (RT60) | 0.3 – 7 | 1.8 | s | global | M A |
| 31 | `wet` | Room wet | 0 – 1 | 0.28 | | global | M A |
| 32 | `drive` | Drive | 0 – 1 | 0 | | global | M A |
| 33 | `body` | Body shelf (240 Hz) | −12 – 6 | 0 | dB | global | M A |
| 34 | `air` | Air shelf (2.4 kHz) | −6 – 9 | 0 | dB | global | M A |
| 35 | `level` | Level | −24 – 6 | 0 | dB | global | M A |
| 36 | `grit_after` | Grit after (off/on) | 0 – 1, step 1 | 0 | | global | S M A |

Flags: M `MODULATABLE`, A `AUTOMATABLE`, S `STEPPED`; per-voice ones also carry `PER_VOICE`. All tapers
are linear. `throats` sets how many throats a note allocates, so it is stepped and not modulatable; it is
read when a note starts. `grit_after` moves the texture (growl, creak, rasp, flutter, jitter, shimmer)
from each throat onto the mixed bus as a tanh drive.

## Presets
The old package's 11 texture presets, as parameter values (Studio presets are fixed in lab core code, so
they are not loadable yet: DESIGN.md "Limitations"). The package's per-preset trim is shown in `level`
(every preset had `level` 0). The trim acted on each throat before drive, so the match is exact only
with `drive` 0 and `grit_after` off. `chant` is a vocal timbre, not the old chant sequencer.

Names: `clean` Clean voice, `weathered` Weathered baritone, `raspy` Raspy belt, `chant` Chant voice,
`growl` Chest growl, `fry` Vocal fry, `ayn` ع Pharynx, `ring` Ring, `whisper` Whisper, `small` Small
throat, `kargyraa` Deep kargyraa.

| Parameter | `clean` | `weathered` | `raspy` | `chant` | `growl` | `fry` | `ayn` | `ring` | `whisper` | `small` | `kargyraa` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `pressure` | 0.5 | 0.42 | 0.74 | 0.6 | 0.7 | 0.78 | 0.62 | 0.62 | 0.4 | 0.5 | 0.72 |
| `breath` | 0.1 | 0.38 | 0.1 | 0.26 | 0.2 | 0.05 | 0.24 | 0.08 | 0.7 | 0.2 | 0.18 |
| `whisper` | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0.92 | 0 | 0 |
| `jitter` | 0.35 | 2.3 | 1.1 | 1.7 | 2.2 | 1 | 1 | 0.4 | 0.5 | 0.5 | 2 |
| `shimmer` | 2.5 | 6.5 | 9.2 | 8 | 6 | 5 | 5 | 2.8 | 3 | 3.2 | 6 |
| `tremor` | 5 | 26 | 10 | 8 | 6 | 0 | 6 | 6 | 0 | 7 | 4 |
| `wander` | 4 | 12 | 6 | 8 | 6 | 3 | 5 | 3 | 2 | 5 | 4 |
| `vibrato` | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 22 | 0 | 0 | 0 |
| `growl` | 0 | 0.04 | 0.1 | 0.07 | 0.85 | 0 | 0.06 | 0 | 0 | 0 | 1 |
| `creak` | 0 | 0.03 | 0 | 0 | 0 | 0.9 | 0 | 0 | 0 | 0 | 0.08 |
| `tail_fry` | 0 | 0.75 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| `flutter` | 0 | 0.15 | 0.22 | 0.2 | 0.35 | 0 | 0.25 | 0 | 0 | 0 | 0.3 |
| `rasp` | 0 | 0.12 | 0.34 | 0.28 | 0.5 | 0.05 | 0.2 | 0 | 0.05 | 0.03 | 0.35 |
| `breaks` | 0 | 0.15 | 0.1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| `size` | 17.5 | 18.9 | 17.2 | 18.2 | 18.6 | 17.8 | 17.6 | 17.3 | 17.5 | 13.6 | 19.8 |
| `pharynx` | 0 | 0.05 | 0.08 | 0.12 | 0.2 | 0 | 0.92 | 0 | 0 | 0 | 0.15 |
| `mouth` | 0.55 | 0.42 | 0.78 | 0.5 | 0.5 | 0.45 | 0.52 | 0.62 | 0.6 | 0.6 | 0.4 |
| `ring` | 0.35 | 0.2 | 0.62 | 0.25 | 0.3 | 0.2 | 0.3 | 0.95 | 0.2 | 0.4 | 0.55 |
| `vowel` | 0.5 | 0.4 | 0.56 | 0.38 | 0.35 | 0.45 | 0.5 | 0.62 | 0.5 | 0.6 | 0.28 |
| `style` | 0.45 | 0.35 | 0.7 | 0.6 | 0.75 | 0.5 | 0.55 | 0.6 | 0.3 | 0.4 | 0.7 |
| `attack` | 60 | 90 | 60 | 60 | 60 | 60 | 60 | 60 | 60 | 60 | 60 |
| `release` | 220 | 520 | 220 | 220 | 220 | 220 | 220 | 220 | 220 | 220 | 220 |
| `drive` | 0 | 0 | 0.05 | 0 | 0.1 | 0 | 0 | 0 | 0 | 0 | 0 |
| `level` | -1 | -2.1 | 1 | 0.4 | 1 | -2.4 | -0.4 | 2.6 | -0.5 | 0.5 | 1.5 |

Every preset leaves the other parameters at their defaults (`glide` 60, `throats` 3, `looseness` 0.35,
`spread` 0.5, `size_spread` 0.4, `distance` 0.15, `azimuth` 0, `room` 0.5, `decay` 1.8, `wet` 0.28,
`body` 0, `air` 0, `grit_after` 0).

## Files
`dsp.h` (module), `module.cpp` (registration), `guttr/*.h` (header-only core: `params.h`, `common.h`,
`tables.h`, `room.h`, `singer.h`, `engine.h`), `tests.cpp`, `demos/` (a chord with release and tail; a
low textured note; four notes through `host.keyboard` with pitch modulating each note's vowel),
`presentation.json` (catalogue entry), `panel.json` (Studio panel layout, eight numbered sections),
`module.json` (until lab MB2).

```sh
python tools/wstest.py t0 guttr              # check, build, tests, demos, CPU probe
python tools/wstest.py t0 --sanitize guttr   # the same under ASan and UBSan
```
