<!-- SPDX-License-Identifier: GPL-3.0-only -->
# MOD-8 design

Pinned source: lab c083b26722b5b4d09e3285ab43f93e93be192f4f, GPL-3.0-only, original Wirescape LFO. Existing parameter and output indices and flags retained. Original sine calculation and free-running advance remain unchanged at defaults. Phase is sampled before advancing. No persisted DSP state existed in the pinned module; reset clears runtime oscillator/clock/random phase.

C2/C3 uses the exact MOD-3 module-local beat_clock.h. Sync with an external clock follows its interpolated edges, holds after missing edges; sync without a clock follows transport (free-running at its BPM while stopped per C2, unlike the old stopped hold). Without either transport or clock manual rate remains the legacy fallback. Reset edge resets phase and beat origin; coincident clock/reset takes reset as origin. Clock and reset are mono-first for all voices. Shape and division are read per sample; division only affects tempo sync, width sets square duty. Triangle starts at zero, peaks at quarter cycle, dips at three quarters; ramps start at -1/+1. Smooth random uses cubic smoothstep between adjacent counter-keyed bipolar values; seed 0 uses host instance seed. Reset reproduces the sequence. Transport locates address a cycle directly; random sync cycle index wraps modulo 2^32. These waveform conventions, width semantics and seed mapping are implementation choices not fixed by §4.5.

Fixed arrays of 16 voices. Double phase and beat-clock accumulators preserve low-rate precision. No heap, locks, I/O, logging or exceptions in processing/reset. No lab modifications.

## Limitations
- SDK-1 beat-clock promotion pending; exact local copy.
- Workshop issue #39: pin registers LFO directly in engine/CMakeLists.txt and does not add the module-local CMake target. Hosted T0/T1 must prove abi2_lfo_tests actually executes; no green legacy-only gate is sufficient. No shared tooling edits here.
- Tests and identity evidence pending hosted CI; independent review required. No T2 without owner request.
