<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Rhythm Lanes design — MOD-6

Source: lab proposal 0024 §4.0 (C2–C6) and §4.4. New module. Original DSP.
Four mono lanes, 34 parameters, two inputs and four CV_UNI outputs. Only the first input/parameter
channel is used. The eight lane parameters are sampled on each cycle boundary, not on each block.
The two global controls are stepped, setParam/automation only.

## Timing and decisions not specified by §4.4

- Reuse the unchanged module-local MOD-3 `beat_clock.h` (dual GPL-3.0-only OR MIT), including
  clock interpolation and hold, transport/free run and reset. C3 uses 4-beat bars.
- The first sample initializes a cycle; a reset starts cycle zero immediately. A backward transport
  jump before a lane's start restarts that lane. Forward jumps skip elapsed cycles at the old division
  without an unbounded catch-up loop. A division change takes effect after the old cycle ends.
- Bjorklund quotient/remainder expansion uses fixed 16-element arrays and bounded recursion.
  Patterns start at their first hit; positive rotation moves hits later. Index is the elapsed cycle
  count modulo steps. Fill above steps clamps, fill zero never plays.
- Probability and random waveform targets are independent counter-keyed streams (lane, cycle, seed,
  stream). Seed zero uses the engine instance seed; explicit 1–65535 seeds repeat between instances.
  Reset restarts the same sequence. A seed edit affects the next latched cycle.
- Shape defaults to Gate; rotate defaults to zero. Width zero makes gates/triggers silent; trigger
  length is width/16 of a cycle rather than gate duty. Triangle peak uses width, with endpoint
  widths degenerating to a ramp; ramp and sine ignore width. Sine is a unipolar raised cosine.
  Random smooth uses smoothstep from the preceding active cycle's target (zero after a silent cycle)
  to the current target over the width fraction of the cycle. Width zero is immediate.
- Phase delays the waveform by that fraction of a cycle, truncating the tail at the boundary (no
  wrapping across inactive cycles). Phase one is silent. This preserves the rule that inactive cycles
  output zero. Hold freezes phase, including an already-high gate, until clock movement resumes.

## Realtime and state

No allocations, locks, I/O, exceptions, JSON or logging in process/reset. All state and Bjorklund
work storage are fixed size. The only double state is the shared beat clock's five fields and
sample rate plus four lane start beats; double beat coordinates prevent long-run float drift.

State version 1 encodes the beat-clock fields (five f64, three u64 and flags), reset latch, instance
seed, and each lane's start beat, cycle count, division, shape, width, phase, random endpoints,
active and begun flags as little-endian words. Load stages and validates before committing.
The random stream is counter-keyed, so no mutable generator state is needed.

## Tests and limitations

Acceptance tests precede DSP in branch history. Tests exercise the Bjorklund table and all
steps/fill/rotate counts, 34 parameter IDs and flags, deterministic probability and reset,
latching, phase and all shapes, PPQ clock, the C3 division table, state continuation and rejected
truncation, 16-channel input and realtime allocation watch.

The pinned lab's generic test fixture has room for six parameters only; a module-local adaptation
(`test_fixture.h`, from pin c083b26722b5b4d09e3285ab43f93e93be192f4f) raises its capacity to 34.
No lab edit or engine behavior change is needed. The shared beat-clock SDK migration is the
existing SDK-1 dependency tracked by MOD-4; retain the local copy until the eligible pin includes it.
No new lab changes or T2 run from this task.

Verification pending T0/T1 CI: this session has no direct local GitHub checkout/network access.
The independent reviewer must check the exact PR head; no merge until both tiers are green.
