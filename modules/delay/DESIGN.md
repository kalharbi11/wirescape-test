<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Design notes: fx.delay

Provenance: original code. No third-party source, no pinned revisions.

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
- Only scalar transient state is added; no additional delay lines or realtime allocation. Reset clears transitions; the saved-state format remains version 1. The folder tests exercise isolated switches, reversals, sample-rate and block-size independence, allocation watch and reset during a transition. A27 fixed-window paired renders and M7 cost/Golden/D5 gates are separate acceptance evidence.

- Steady path: cache rounded mode/division and tone targets only for ABI constant rows, otherwise retain sample-addressed control. Route settled endpoints through the original expressions. No change to the transition duration, input or feedback gain, clock follower or read interpolation.
- Settled samples run a verbatim copy of the pre-S7.6 process body (`process_settled`, `loop_settled`, `update_tone_settled`), inlined into `process()` as before. GCC contracts `a*b + c` into FMA where the target has it (ARM64, x86-64-v3), and its choice depends on the whole function, so identical source is not bit-identical by itself. Everything else stays out of line (`settled_block`, `process_transition`, `settled_tail`, `settled_after`). A constant-row block leaves the transition loop at the first sample with no active or pending transition, and its remaining frames run through the same settled body over a `Tail` view, so the result does not depend on the block size. Checked with the real renderer, base against head, at `-march=x86-64-v3` on GCC 13 and 14: 27/27 Golden patches identical (A72).
