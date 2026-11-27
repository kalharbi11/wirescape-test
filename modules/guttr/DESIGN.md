<!-- SPDX-License-Identifier: GPL-3.0-only -->
# GUTTR design (`guttr`, kind `new`)

## Origin and licence
GUTTR is the owner's own GUTTUR synthesis, GPL-3.0-only (owner's licence choice of 2026-11-15, recorded in
the package's `NOTICE.txt`). Source: lab `docs/context/future_modules/guttur/package/` (C++ translation of
the owner's `guttur-worklet.js`; no third-party code). This folder restructures `src/engine.cpp`,
`src/tables.cpp`, `src/tube.cpp`, `src/room.cpp`, `src/internal.h` and `src/reference_constants.h` into
header-only files under `guttr/`, so that `module.cpp` alone (wsmod C4) and the lab's module catalogue
both compile it. The synthesis equations are unchanged. Left in the lab folder, not ported: the JS
reference (`reference/`), the comparison and render tools (`tools/`), the offline and benchmark apps
(`apps/`), the standalone allocator path, telemetry, `loadPreset` and the old ABI 2 adapter.

**Port check.** A scratch harness (not committed) built the package's standalone engine and this
module side by side and rendered three 3-second timelines through each: one note; three notes with
growl, creak, flutter, rasp, vibrato and `wet` 0.4, with events between control ticks; and one note with
8 throats, `looseness` 0.9 and `grit_after` on. With the package's parameters rounded to float, as the
host delivers them, the outputs were **bit-identical** on every sample (x86-64, GCC 13.3, -O2,
2026-11-27). With the package's double defaults the difference was at most 2e-6.

## Signal path
```
notes ──▶ note table (16) ──▶ throat allocator (16 throats, 1–8 per note, leader + followers)
           per throat: LF pulse mipmaps (17 Rd × 8 levels) ─ closure decisions (jitter, shimmer,
           growl, creak, fry, breaks) ─ breath + rasp + flutter noise ─ 10 parallel SVF resonators
           (tube grid, ramped over the tick) ─ pitch-following HPF ─ antiderivative tanh drive ─
           gain, distance low-pass, pan, ITD, head shadow ──▶ dry L/R + room send
bus: early reflections + 8-line FDN ─ [post-grit] ─ body/air shelves ─ low-mid guard ─ DC block ─
     level ─ 2.5 ms look-ahead limiter (ceiling 0.891) ──▶ left, right (one channel each)
```
Files: `guttr/params.h` (the 37-row table), `guttr/common.h` (constants, hash and noise generators,
biquad), `guttr/tables.h` (pulse mipmaps with the radix-2 FFT, tube grid, bandwidth model),
`guttr/room.h`, `guttr/singer.h` (one throat), `guttr/engine.h` (memory layout, notes, control tick,
per-sample paths, idle bus), `dsp.h` (descriptor, parameter reading, events, process).

## Per-voice parameters
- 23 parameters carry `PER_VOICE`: the phonation, instability, texture and tract controls, `style`,
  `looseness`, `spread` and `size_spread`. They shape one note's throats. 14 are global: `attack`,
  `release`, `glide`, `throats`, `distance`, `azimuth`, `room`, `decay`, `wet`, `drive`, `body`, `air`,
  `level` and `grit_after`.
- Each note remembers its event channel (clamped to 0–15). Its throats read per-voice parameters from
  that channel and global ones from channel 0. A signal with fewer channels than the note's channel is
  read on channel 0: `Signal::ch` already does that for one channel; for 2–15 channels the SDK would
  return 0, so `dsp.h` clamps itself.
- The core keeps a snapshot per voice channel, `chP[16]` (37 doubles each), with the global values copied
  in, plus `G`, every parameter at channel 0. At each control tick it refreshes `G` and the snapshots of
  the channels that own active or fading throats or live notes. Throat code reads its owner's snapshot
  where the package read one global array.
- A note-on reads its own snapshot at its event frame. This gives the group size (`throats`) and the
  placement (`size`, `size_spread`, `spread`, `looseness`, `azimuth`, `distance`). A channel no throat
  uses starts its snapshot from these values.
- A tick that sees a channel's values change re-places that channel's throats, as the package did for
  any change. A change of a global value re-places every throat.
- The post-grit drive amount (`grit_after` on) acts on the mixed bus. So it follows the channel-0 values
  of `growl`, `rasp`, `shimmer`, `jitter` and `creak`.
- Reasons for the global set: `distance` and `azimuth` place the whole group in space. `attack`, `release`
  and `glide` stay with the package's ensemble behaviour. `throats` sets allocation. The rest act on the
  shared bus.
- **In the engine.** `demos/keyboard-poly-vowel.json` plays four notes through `host.keyboard` (4 voices),
  with its poly `pitch` on a modulation cable to `vowel`. A scratch instrumentation of that render (not
  committed, 2026-11-27) saw note-ons on channels 0–3, a 4-channel `vowel` signal, and channel snapshots
  of 0.050, 0.254, 0.458 and 0.663: each note had its own vowel.

## Control clock
- One internal 128-frame tick, counted from `prepare`/`reset`, whatever the host block size.
- The host block is split at event offsets (`Block::split`) and at tick boundaries. At a tick boundary the
  parameters are sampled at that frame and `control()` runs. After the tick's 128th frame, `finish_tick()`
  closes it: RNG hand-over, voice lifecycle, steal hand-over, ramp snapping, low-mid guard, idle check.
- Note events act at their sample offsets. An event on a tick boundary acts before that tick's control.
- Inside a tick the throats are independent: they read only tick-constant values. Each active throat
  therefore renders its whole run into `dryL/dryR/send`, and then the bus runs per sample. Every sum is
  in the same order as the package's sample-by-sample loop, so the output is bit-identical. Throat state
  also stays hot in cache.
- `tests.cpp` checks that host blocks of 1, 17, 128 and 512 frames are bit-identical. The timeline has
  notes between ticks, a choke, per-voice ramps on two channels and global ramps.

## Notes and throats
- **16 note records.** `note_id` ≥ 0 matches by (source, `note_id`). Otherwise the newest record with
  no `note_id` and the same (source, channel, key) matches, as in the engine's held-note ledger. A
  note-on with an identity already held releases the old note first.
- **A note-on with every record taken** reuses the oldest released record, else the oldest held one.
  That note's throats fade and go to the new note first.
- **Throats per note:** `throats` (1–8), read at note-on. Free throats are used first, then ownerless
  fading ones, then stolen ones: released notes' throats first, oldest first.
- **Throat lifecycle.** A stolen or choked throat is marked (`killFade` 1). At the next tick it fades to
  zero over 128 frames (`killFade` 2). At that tick's end it is reset and, if its new note still exists,
  assigned to it with the package's placement, lag and scoop. The package's leader/follower lag, slip,
  scoop and settle are unchanged.
- **Choke.** A choke with a `note_id` matches that note. One with a channel and no `note_id` matches the
  anonymous (channel, key). One with neither matches every note of its source; source `UINT32_MAX`
  means every source. Matching notes are removed at once and their throats fade within two ticks (at
  most 256 frames), with no release.

## Memory
- The module struct holds three pointers and a size (24 bytes). Everything else sits in **one WS2_LARGE
  allocation** made in `prepare`. `memory_requirements` reports exactly its size, `Layout::of(rate)`,
  and `tests.cpp` checks that the two are equal.

  | Part | Size |
  |---|---|
  | Core: pulse tables 3.35 MB, tube grid 76 KB, 16 throats 42 KB, limiter 32 KB, snapshots 4.7 KB | 3,515,392 B |
  | Room lines, 8 × 8192 floats (16384 at 96 kHz) | 256 KB (512 KB) |
  | Early-reflection buffer, 4096 floats (8192 at 96 kHz) | 16 KB (32 KB) |
  | FFT scratch, used only while `prepare` builds the tables | 768 KB |
  | **Total** at 44.1 and 48 kHz (96 kHz) | **4,580,352 B** (4,858,880 B) |
- Room line length is the next power of two at or above `2861 × rate / 48000 × 1.6 + 16`. Line 8 at
  `room` 1 is the longest, and the margin covers modulation and interpolation, so a delay never clamps at
  any rate up to the SDK's 384 kHz. The package's fixed 8192-sample lines clamped at 8000 samples above
  about 70 kHz.
- Tables are built in `prepare` (about 0.1 s on x86-64). A second `prepare` reuses the allocation when it
  is large enough.
- `process`, the event path and `reset` allocate nothing. They take no locks, do no I/O, raise no
  exceptions and log nothing. `tests.cpp` checks every process call with the allocation watch, and the CPU
  probe counted 0 allocations and 0 faults. `reset` clears the room lines (about 280 KB at 48 kHz) with a
  plain fill.
- The bus goes idle when no throat is active and the bus and room peaks stay below 1e-7 (−140 dBFS) for
  `(room line + look-ahead) / 128 + 4` ticks (68 at 48 kHz). Then the room, shelves, guard, DC blocker
  and limiter are cleared, and later segments write exact zeros, flagged constant, until a throat starts.
  This removes denormal tails and makes silence free.

## `double` state
All synthesis state is `double`, as in the package, and the port check above depends on it. The
Cortex-M7's FPv5-D16 has double-precision hardware. This covers:
- per throat: phase, increment, envelopes, pitch, the SVF integrators, coefficients and their ramps,
  noise and flutter filters, gains, ITD, drive memory and the pending start and stop times;
- the bus: biquads, DC blocker, low-mid envelopes, room damping filters, limiter queue and buffers,
  master and wet gains;
- the core: control time, shared jitter and the parameter snapshots.

Pulse tables, flow and harmonic tables, the tube grid, room lines and ITD lines are `float`. C6 reports
11 double-precision libm symbols (`cos`, `exp`, `hypot`, `log`, `log10`, `log1p`, `log2`, `pow`, `sin`,
`sqrt`, `tan`), with no heap or static-guard symbols.

## Flags (modulatable policy)
- Every continuous parameter is `MODULATABLE|AUTOMATABLE`, plus `PER_VOICE` per the list above.
- `throats` is `STEPPED|AUTOMATABLE`, not modulatable, because it sets how many throats a note allocates.
- `grit_after` is a two-state switch (the package tested `> 0.5`). Its old adapter already made it
  stepped with step 1, and as a stepped parameter that sets neither allocation nor structure it is
  `STEPPED|MODULATABLE|AUTOMATABLE` by policy. Moving it under modulation switches the grit stage.

## Latency and state
- `latency_frames()` is `round(0.0025 × rate)`, the limiter look-ahead.
- No `save_state`/`load_state`: the patch saves the parameters, and nothing else needs to survive a save.
  The package's state held parameters, seed and preset trim. The seed comes from the host config, and
  the trim is now `level` (README "Presets").

## Deviations from the package
1. **New ABI adapter.**
   - The ID is `guttr` (was `essentials.guttur`).
   - Parameter names are capitalised, with units, decimals and `legacy_index` = order (all were 0).
   - `grit_after` is now also modulatable.
   - Rates from 1 Hz to 384 kHz are accepted (the package refused anything outside 44.1–96 kHz). Rates
     other than 44.1, 48 and 96 kHz are untested.
2. **Per-voice parameters** (above). With one-channel parameter signals the sound is unchanged.
3. **A note-on reads parameters at its own frame.** The package's adapter used the values of the latest
   tick boundary.
4. **A 17th note steals the oldest record.** The package refused a note-on when all 16 records were held.
   Keys outside 0–127 are clamped (the package refused them).
5. **Choke stops the note.** It fades over one tick; the package's adapter treated choke as a note-off.
6. **Steal fades always complete.**
   - The package cut a throat stolen between ticks at that tick's end, with no fade. Now the fade runs
     over the whole next tick.
   - A fading throat holds its coefficient ramps. The package kept extrapolating the previous tick's ramps.
   - The silence check no longer resets a fading throat before hand-over, which could leave the new note
     without that throat.
7. **No orphan throats.** When the package stole a note record, the hand-over could assign that record's
   throats to note id 0 while any record was free. Such a throat sang with no owner and no release. Now
   these throats fade and the new note adopts them.
8. **Room lines are sized from the rate** (above). The early-reflection buffer is 4096 at 48 kHz (was
   8192). The output is the same, because every tap is shorter than the buffer.
9. **Idle bus** at −140 dBFS (above).
10. **Removed:** telemetry (cycle history, f0, Rd, jitter and shimmer estimates, peak and RMS, low-mid
    and limiter readouts), the preset trim, `loadPreset` and the atomic `setParam` path.
11. **Same output, less work:**
    - throat-major rendering inside a tick;
    - the drive's previous antiderivative value is cached instead of recomputed.

## CPU (diagnostic, not facts)
x86-64 VM ("Intel(R) Xeon(R) Processor", 4 vCPU), GCC 13.3, Release `-O3`, 2026-11-27, this commit's
tree. Hosted and VM timings are noisy, and the maximum varies from run to run.
- **Lab CPU probe** (T0: 2 instances, 48 kHz, 128 frames, no notes, so it measures the idle bus):
  - 0 allocations, 0 faults;
  - worst block 15.3–28.4 µs over three runs (0.6–1.1% of the 2,667 µs period), p99 about 1 µs.
- **`tests.cpp` bench** (one instance, 48 kHz, 128 frames, all 16 throats voicing, two runs). Each row
  gives median, p99 and max per block:

  | Case | Median | p99 | Max |
  |---|---|---|---|
  | 8 notes × 3 throats (budget-limited to 16) | 96.7–96.8 µs (3.6%) | 124–131 µs | 234–295 µs |
  | 16 notes × 1 throat | 98.7–115.4 µs (3.7–4.3%) | 178–193 µs | 247–262 µs |
  | 2 notes × 8 throats, rough texture (growl, rasp, creak, flutter, drive, vibrato) | 153.7–166.8 µs (5.8–6.3%) | 230–519 µs | 324–1,090 µs |

  The package's report measured 16 throats at a 196 µs median and a 2,261 µs worst block on its host
  (different machine).
- No Raspberry Pi or Cortex-M7 measurement has been made. T2 (callgrind and Cortex-M7 advisory) is
  owner-requested.

## Limitations
Lab-facing gaps are rows in [`LAB_REQUESTS.md`](../../LAB_REQUESTS.md) (rule 2 of AGENTS.md); none is filed in
the lab unless the owner asks. Each bullet names its row. The module has no code workaround for any of them.
- **`panel.json` needs the workshop Studio overlay** (`tools/studio`, applied by `wstest ui` and `wstest app`;
  overlay PR #54). The panel uses grid cells, horizontal sliders, a band and the parameter
  rail +/-, which the pinned Studio lacks. Without the overlay it does not render as designed. IDs, flags and DSP
  do not depend on it. LAB_REQUESTS.md LR-1.
- **No module-to-UI telemetry in the lab ABI.** The package's live readouts (active throats, f0, Rd,
  jitter and shimmer %, low-mid cut, limiter reduction) have no channel to Studio, so they were dropped. LR-2.
- **Studio presets are hard-coded in lab core code.** A module cannot ship presets, so the 11 texture
  presets live in README.md as values. LR-3.
- **No per-module panel colour.** `PanelOverride` has no colour or accent key, so `panel.json` cannot give
  GUTTR its own colour. LR-4.
- **The lab CPU probe sends no notes,** so for an instrument it times only the idle path. The active cost
  above comes from `tests.cpp`. LR-5.
- **FFT scratch stays allocated.** The SDK has no prepare-time scratch that can be returned, so 768 KB of
  each instance's allocation is used only during `prepare`. LR-6.
- **Render cases send velocity 1 on channel 0 only**, and `evidence.py` requires equal left and right.
  The demos therefore sum `left` and `right` through `essentials.mixer` at 0.5 each. Distinct voice
  channels need `host.keyboard` in the patch (the third demo). The evidence checks cannot tell whether each
  note got its own value, so `tests.cpp` checks that. LR-7.
