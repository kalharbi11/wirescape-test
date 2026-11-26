<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Random design (wirescape-test MOD-3)

**Source.** Lab `sdk/examples/random_source` at `c083b26722b5b4d09e3285ab43f93e93be192f4f` (MIT, lab
repository).
- Kept: the `ws::Random` generator (`bipolar()`), Schmitt edge detection and the versioned,
  validate-then-commit state.
- Replaced: the one-pole smoother. §4.2 asks for an S-curve over `slew` × the step length, so `smooth`
  is a smoothstep `u²(3 − 2u)` from the current smoothed value to the new one.
- New: the clocking, which comes from `beat_clock.h`.

## `beat_clock.h` (C2, C3)
- **Licence:** `GPL-3.0-only OR MIT`. GPL satisfies lab `wsmod` C2 for files under `modules/`; MIT is
  what SDK-1 needs (§4.0 C6). Copies must stay byte-identical.
- **Include guard:** a named one (`WSBEAT_BEAT_CLOCK_H`), not `#pragma once`. Then MOD-4's copy and,
  later, the SDK's collapse to one definition when one catalogue includes several modules.
- **Clock mode.**
  - An edge adds 1/`clock_ppq` beat.
  - After two edges, the fraction since the last edge is divided by the last period and capped at
    1 − 2⁻²⁰, so the beat never reaches the next edge's beat early.
  - After two periods with no edge it holds. The next edge then counts, but its long interval is not a
    period: the beat moves only at edges until a new period is measured.
  - The beat never decreases in clock mode.
- **Transport mode.**
  - While playing: `transport.beat` (advanced per sample) minus the origin.
  - While stopped: free-running at `transport.bpm` (120 without a transport) from the last beat.
  - The origin is the transport's beat at the last `reset`, and 0 until then.
- **Reset.** The beat is 0 at the reset sample, and a clock edge in the same sample is that origin.
  Switching between cable and transport keeps the beat continuous.
  - **Between edges (MOD-3a ruling):** a reset in clock mode with no edge in the same sample sets
    `base = −fraction × per_edge`, where `fraction` is the interpolated (or held) fraction of the
    current edge interval. The beat is exactly 0 at the reset, and the next edge lands at
    `(1 − fraction) × per_edge`, so a reset just before an edge does not give a double step.
  - **Unchanged:** a reset in the same sample as an edge, a reset before the second edge (fraction 0)
    and transport mode.
- **Boundaries.** `floor(beat / length + 1e-9)`, so exact multiples of 1/3 and 1/12 land on their
  boundary despite rounding. The tests cover 600 multiples of each.
- **A division change alone draws nothing (MOD-3a).** When only the division length changed, the
  module recomputes the period index of the beat for the new length and keeps its value. A boundary
  crossed by the beat in the same sample still draws one. `last_length` is not saved: after a load it
  is unknown and the index is recomputed, which leaves it unchanged for the same length.
- **Step length** for the glide is `length × samples_per_beat`: the measured period × `clock_ppq` in
  clock mode, or 60·rate/BPM. It is capped at 30 s.

## Decisions not fixed by §4.2 (documented here, open to the Architect)
- **The `reset` input also re-seeds the sequence.** That makes "reset reproduces it" (C4) hold for the
  input as well as for module reset.
- **The power-on value** is drawn at the first sample without a glide or a `step` pulse. A value drawn
  at a reset input does glide and pulse.
- **`step` is 1 ms** (`ceil(rate/1000)` samples), as lab Gate Delay's trigger.

## Realtime and portability
No allocation, locks, I/O, exceptions or logging in process or reset; no heap; fixed state (one voice).
**`double` state:** the beat clock's `base`, `beat`, `origin`, `held_fraction` and `samples_per_beat`, and the module's `last_length`.
Beats must stay exact over long runs: a float beat drifts after about 2²⁴ samples. Integer counters
are `uint64_t`.

## State v1
All fields are little-endian u32 words, in this order:
- the beat clock's five f64 fields, then its three u64 counters and its flags;
- the Random state (u64 state, u64 sequence; the sequence is odd);
- the division index (i64);
- the held, from and to values (f32);
- the glide position, the glide length, the trigger count, the seed used, `started` and `reset_high`.

The loader validates everything before committing. Future versions return `WS2_UNSUPPORTED`.

## Limitations
- **`beat_clock.h` belongs in the lab SDK** (SDK-1). MOD-4 files the `limitation found` issue when it
  copies the header (§4.0 C6). Until then each module carries its own copy.
