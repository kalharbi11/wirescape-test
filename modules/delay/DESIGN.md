<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Design notes: fx.delay

Provenance: original lab code, no third-party source. Workshop override copied byte-for-byte from `kalharbi11/wirescape-lab` at `3d3db7a64090682a62f220d73baef49709048c87` (LAB_PIN); only the shared stepped descriptor flags change in production.

Textbook pieces:
- Fractional delay reads use 4-point, 3rd-order Hermite (Catmull-Rom) interpolation (standard in the DSP literature,
  for example the musicdsp.org "Hermite interpolator" note and Julius O. Smith's *Physical Audio Signal Processing*,
  "Delay-line interpolation").
- Feedback tone: one-pole low-pass, or high-pass as `x - lowpass(x)`, cross-faded in over |tone| < 0.25 so the middle is exactly flat.
- DC blocker: first-order `y[n] = x[n] - x[n-1] + R y[n-1]`, R set for 10 Hz.
- Soft limiter: identity up to 1.0, then `1 + 0.5 tanh((|x|-1)/0.5)` (continuous slope at the knee, ceiling 1.5).

Decisions:
- The limiter sits on the value written to the line (input plus feedback), so line contents are at most 1.5 and the equal-power
  output is at most sqrt(1 + 1.6^2) < 2; the wet read is also clamped to 1.6 as a guard against interpolation overshoot.
- Glide slews at most 0.5 samples per sample (read speed 0.5x..1.5x) on top of a 50 ms one-pole, so big jumps stay click-free.
- Ping-pong sums to mono as an average, so an unpatched `right` (which follows `left`) is not doubled.

## S7.6 live switch transitions

- Stereo/ping-pong: a 10 ms linear blend of the existing input and feedback routing equations before the existing limiter. Start directly in the selected mode after reset; a reversal retargets from the current blend.
- Tone: keep the one-pole state and ramp its low-pass/high-pass contribution and cutoff across bypass or a polarity change. Fade toward flat with the previous cutoff; unchanged polarity outside a transition retains the original coefficient update. Retarget an active transition from its current coefficients. The original settled equations remain in use outside the transition.
- Clocked division: a 10 ms blend of the two existing Hermite reads, with an exact destination tap at completion. New requests during the transition coalesce to the latest division and start after the current fade. Clock acquisition, tempo changes and free time retain the 50 ms glide and 0.5-sample slew. Unclocked division edits have no effect.
- Only scalar transient state is added; no additional delay lines or realtime allocation. Reset clears transitions; the original S7.6 state format was version 1; the pinned A70 implementation now saves version 2 (same-rate runtime snapshot, 1 MiB cap, legacy v1 resets). MOD-9 leaves this format unchanged. The folder tests exercise isolated switches, reversals, sample-rate and block-size independence, allocation watch and reset during a transition. A27 fixed-window paired renders and M7 cost/Golden/D5 gates are separate acceptance evidence.

- Steady path: cache rounded mode/division and tone targets only for ABI constant rows, otherwise retain sample-addressed control. Route settled endpoints through the original expressions. No change to the transition duration, input or feedback gain, clock follower or read interpolation.
- Settled samples run a verbatim copy of the pre-S7.6 process body (`process_settled`, `loop_settled`, `update_tone_settled`), inlined into `process()` as before. GCC contracts `a*b + c` into FMA where the target has it (ARM64, x86-64-v3), and its choice depends on the whole function, so identical source is not bit-identical by itself. Everything else stays out of line (`settled_block`, `process_transition`, `settled_tail`, `settled_after`). A constant-row block leaves the transition loop at the first sample with no active or pending transition, and its remaining frames run through the same settled body over a `Tail` view, so the result does not depend on the block size. Checked with the real renderer, base against head, at `-march=x86-64-v3` on GCC 13 and 14: 27/27 Golden patches identical (A72).

## MOD-9 — stepped modulation read-site audit

| Parameter | Effective reads and original cadence | Decision |
|---|---|---|
| `division` (slot 1) | `settled_block` checks ABI constancy and compares rounded channel 0 against `division_seen` only for connected clock; `process_settled` reads each sample only after `clock_tick` yields an applied period; `process_transition` caches a constant row or reads each sample, clamps 0–7, and coalesces changes during the existing bounded 10 ms tap fade; `settled_after` records the constant division or clears it without clock/period. | Add `WS2_MODULATABLE`, retain `WS2_STEPPED\|WS2_AUTOMATABLE`. Only tap targets and scalar transition state change, not memory or structure. Free-running division remains inert. |
| `mode` (slot 4) | `settled_block` checks constant channel 0 and settled blend; `process_settled` reads each sample for stereo/ping-pong routing; `process_transition` caches constant rows or reads each sample, rounds, and retargets the existing 10 ms blend. | Same flags; switches routing equations on preallocated lines, never allocation or topology. |

The existing `Tail` view preserves channel rows and offsets only nonconstant rows; transition-to-settled dispatch is restricted to constant mode/tone and constant division when clocked. No read-site, rounding, transition, sample or block cadence, port/parameter order, range, default, event or state changes. Both controls are global stereo controls (effective channel 0); the audio inputs sum 1–16 channels into one stereo pair. No other flags touched.

Realtime: two large rings allocated only through `Setup::alloc(..., WS2_LARGE)` in prepare. Process and reset use fixed arrays/scalars and existing bounded line clearing, no allocation, locks, I/O, logging or exceptions. The existing double state (`rate`, `cur`, `alpha`, `division_to`, `applied`, `pending`) represents fractional sample timing, clock intervals and the glide coefficient; no new double state. State load is non-RT, stages its existing heap buffer before committing validated data.

Tests: batch B exact flags and effective nonconstant rows switching at frame 80 within a 128-frame callback, clocked and free-running, 1/16 summed inputs; verify measured period, exact tap fade target/duration, mode blend and ring routing at the switch, finite stereo samples and zero process/reset allocations. Retain pinned state continuation, transition interruption, settled-reference, rate/block matrix and poly-sum tests. Default-stereo demo uses pinned defaults and mirrored offline output.

## Limitations

[LR-7](../../LAB_REQUESTS.md) — pinned offline evidence expects mirrored buses; demo observes one Delay output on both buses. Distinct left/right routing and clocked division are covered by native tests. No harness or lab change. T2 requires owner request; promotion remains lab work after MB2.
