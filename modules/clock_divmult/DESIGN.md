<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Clock Div/Mult design (wirescape-test MOD-4)

New module, lab `proposals/0024` §4.3 and the §4.0 conventions (C2 beat clock, C5 realtime, C6 shared
code). There is no outside source.

## `beat_clock.h` (C2, C6)
- **A byte copy of `modules/random/beat_clock.h`** from `main` at `fe4b974` (after MOD-3a).
- **Licence:** `GPL-3.0-only OR MIT`, as in Random's DESIGN.md.
- **The copy check:**
  - `beat_clock_copy_matches_random` in `tests.cpp` reads this folder's copy at test time, with CRs
    removed so a Windows checkout still matches, and checks its FNV-1a 64 hash and length against
    Random's copy.
  - When both folders are staged in one scratch tree, it also compares the two files directly.
  - Changing either copy means changing both, and the constant.
- **Clock mode** uses `clock_ppq` 1, fixed (§4.3: one beat per input edge).

## How outputs are clocked
- **Edges.** The module runs its own Schmitt on `clock`, with the same thresholds and input as the
  beat clock's, so both see the same edges. It counts edges up to 2: the tempo is known from the
  second edge.
- **Position.** `position = beat − grid`.
  - In clock mode, `grid` is the beat at the armed edge, so ÷N boundaries fall exactly on edges 0, N,
    2N, …
  - In transport mode, `grid` is 0, so ÷4 follows the transport's bars (4/4, owner) from its origin.
- **Period index.** ÷N: `division_index(position, N)`. ×N: `floor(position × N + 1e-9)`.
  - A new index is a period start.
  - The beat clock caps interpolation at 1 − 2⁻²⁰ of an edge interval, so ×16 reaches index 15 and
    never 16 before the edge. That is the "never more than N per period" guarantee.
- **Arming (clock mode).** At power-on, on a `reset` and on a switch to the cable, the module is armed.
  No period starts until the next edge. That edge fires every output and sets `grid`.
  - This does not depend on the MOD-3a reset ruling: the grid is taken from the edge, not from the
    reset's beat.
- **Transport mode.**
  - On entry (power-on or unpatching), indices are taken at `position − 1e-6`. A start exactly on a
    boundary (transport at beat 0) then fires, and a start mid-period waits for the next boundary.
  - A `reset` is beat 0 (C2 origin): every output fires.
  - A transport jump (a loop) is a new index, so the outputs fire there.
- **Ratio changes.** The ratio parameter is read every sample into `pending`. At the next index change
  of the latched ratio, the pending ratio is latched. The output fires only if that sample is also a
  boundary of the new ratio's grid; otherwise its first gate is at the new ratio's next boundary. The
  gate in progress always runs out.
- **Gate length.**
  - With the tempo known: `round(width × period_beats × samples_per_beat)`, clamped to
    [1, ⌊period⌋ − 1] samples, where `samples_per_beat` is the beat clock's measured period or
    60·rate/BPM. `width` is read at the gate's start.
  - With the tempo unknown (before the second edge): the gate ends when `position` reaches
    start + `width` × period, which in practice is the clock edge where that happens.
- **A start while the output is still high** (an accelerating clock, or the first period) begins one
  sample late, so there is always a falling edge. Two starts that coincide merge into one gate.

## Decisions not fixed by §4.3 (documented here, open to the Architect)
- **Transport reset fires at the reset**, not at the next beat: the reset is beat 0, which is an
  edge of every output.
- **In free-run with no transport**, the first gate is at the first boundary after power-on (beat
  1/24000 at 48 kHz), not at sample 0.
- **Width is latched per gate**, so modulating it never cuts a gate.

## Realtime and portability
- No allocation, locks, I/O, exceptions or logging in process or reset. No heap; fixed state (one
  voice, four outputs).
- **`double` state:**
  - the beat clock's `base`, `beat`, `origin`, `held_fraction` and `samples_per_beat`;
  - the module's `grid` and previous position;
  - each output's `end_beat`.

  Beats must stay exact over long runs: a float beat drifts after about 2²⁴ samples.

## State v1
All fields are little-endian u32 words, in this order:
- the beat clock (five f64, three u64 counters, flags);
- `grid` and the previous position (f64), the clock and reset latches, `armed`, `mode` (0–2) and the
  edge count (0–2);
- then, for each output: the latched and pending ratio (0–18), the period index (i64), the samples
  left, the delayed start, `end_beat` (f64) and `high`.

The loader validates everything before committing. Future versions return `WS2_UNSUPPORTED`.

## Limitations
- **`beat_clock.h` belongs in the lab SDK** (SDK-1, lab 0024 §5.3).
  - Both `modules/random` and `modules/clock_divmult` carry a byte copy.
  - **Smallest lab change:** add `sdk/include/wirescape/beat_clock.h` (the MIT side of the licence),
    unchanged in behaviour, with SDK tests. Modules then include it from the SDK include path and drop
    their copies at the next pin bump.
  - **Lab issue:** to file, labelled `limitation found` (requested from the lab Architect in this PR);
    see `modules/README.md`.
  - **Workaround:** the copies in the two folders, checked identical by `tests.cpp`.
