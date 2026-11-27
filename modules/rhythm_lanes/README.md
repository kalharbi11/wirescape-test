<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Rhythm Lanes

`sequencing.rhythm_lanes`, module version 1. Four independent tempo-synchronized mono CV lanes.

| Port | Direction | Hint | Meaning |
|---|---|---|---|
| clock | input | GATE | External clock; otherwise transport or free-running tempo |
| reset | input | TRIGGER | Reset beat origin and all cycle counters |
| lane1–lane4 | output | CV_UNI | Independent 0–1 rhythmic control signals |

Each lane has eight parameters (replace `n` with 1–4), listed in descriptor order:

| ID | Range | Default | Meaning |
|---|---|---|---|
| division_n | 0–17 | 7, 10, 13, 2 respectively | C3 division index |
| shape_n | 0–7 | 0 | Gate, Trigger, Ramp up, Ramp down, Triangle, Sine, Random stepped, Random smooth |
| width_n | 0–1 | 0.5 | Gate duty, trigger length, triangle peak or random glide |
| phase_n | 0–1 | 0 | Delay within a cycle |
| probability_n | 0–1 | 1 | Chance an eligible cycle plays |
| steps_n | 1–16 | 1 | Euclidean cycle count |
| fill_n | 0–16 | 1 | Hits, clamped to steps |
| rotate_n | 0–15 | 0 | Rotate hits later modulo steps |

All lane parameters are modulatable and automatable; division, shape, steps, fill and rotate are stepped.
They latch on cycle starts. Globals (after the four lane groups): `clock_ppq` 1–24 (default 1),
`seed` 0–65535 (default 0), both stepped and automatable, not modulatable.

Division choices in order: 4 bars, 2 bars, 1 bar, 1/2., 1/2, 1/4., 1/2T, 1/4, 1/8., 1/4T,
1/8, 1/16., 1/8T, 1/16, 1/32., 1/16T, 1/32, 1/32T. Bars are 4/4 at the pin.

Inactive cycles are silent; phase delays without wrap. Trigger length is width/16 of a cycle.
See DESIGN.md for decisions, clock behavior and state. Demo: `demos/euclidean-vca.json`
uses lane 1 to rhythmically control a VCA. Run `python tools/wstest.py t0 rhythm_lanes` offline.
