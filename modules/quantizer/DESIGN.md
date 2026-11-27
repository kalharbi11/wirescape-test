<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Quantizer design
Original code, GPL-3.0-only. No upstream source, no third-party code, no new dependency.

- Algorithm: textbook nearest-note quantization on a 12-bit pitch-class mask. Scales are interval lists (rotated by the root); Custom uses the note switches directly. Nearest search looks at most 12 semitones each way, so work per sample is bounded.
- Hysteresis: the held note's region (midpoints to its allowed neighbours, widened by 0.1 semitone) is cached when the note is chosen; inside it nothing is recomputed, outside it the nearest note is taken. A mask change invalidates the cache.
- Shift: counts allowed pitch classes below the note, adds the shift, wraps with floor division into octaves, and picks the matching set bit. Shift and octave come after hysteresis, so they never disturb the held note.
- Output `(midi - 60) / 12` is computed in double and rounded to float once, so allowed notes are the correctly rounded value.
- Settings are read at sample 0 of each block (stepped, unsmoothed), channel 0, for all voices. Both constant and varying pitch paths use the same block configuration. Gated voices apply it only on their existing sampling decisions. Held notes are not saved; they restart on reset or load.
- Input is clamped to +-1200 semitones, so extreme or non-finite input stays finite.

## MOD-7 override (#27)
Base: lab `c083b26722b5b4d09e3285ab43f93e93be192f4f` (the source folder was copied from LAB_PIN before modification). All 16 existing controls gain WS2_MODULATABLE in the shared descriptor helper; their order, ranges, defaults, flags other than that bit, state and DSP read sites remain unchanged. Presentation retains the original choices and visibility, with modulation enabled. The per-voice double hold bounds and double sample rate are inherited from the pin; they preserve the existing hysteresis and pulse timing math. No new persistent state or allocation.

Tests assert the exact descriptor IDs, flags, ranges and defaults, nonconstant effective streams for every control on 1 and 16 voices, block-zero sampling with a later trigger, constant and varying pitch paths, reset and allocation watch; the engine must accept a cable to each control. The unmodulated default C-major keyboard demo is the identity witness.

## Limitations
No new lab-facing requirement identified. Existing once-per-block, channel-zero setting cadence is intentional; modulation does not turn these switches into per-sample or per-voice controls. T2 is owner-requested only.
