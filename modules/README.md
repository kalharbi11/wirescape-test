<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Module candidates (owner wants, 2026-11-26)

The specs come from the Fable design run 2. Each folder uses the lab layout: `README.md` (IDs, ports,
parameters, ranges), `dsp.h`, `module.cpp`, `module.json`, `tests.cpp`, `DESIGN.md`, `CMakeLists.txt`.
Start from lab `sdk/template/` or `sdk/examples/` (`random_source`, `step_sequencer`).

| Candidate | Kind | Notes from the lab at `LAB_PIN` |
|---|---|---|
| Envelope Trigger mode + `delay` (DAHDSR) | change to existing | ADSR already has Gate/Trigger `mode` + `hold`; Function Envelope is gate-only and has no modulatable parameters |
| Random (stepped and smooth, musical divisions incl. dotted/triplet, depth, slew, trigger-to-step, seed) | new | no shipped random or S&H module; `sdk/examples/random_source` exists; LFO tempo sync reads the transport beat |
| Clock Div/Mult | new | Clock has BPM, ticks per beat, transport sync (its README says no sync; its code has it) |
| Multi-output rhythmic modulator (division, shape, width, phase, probability, Euclidean, delay per output) | new | original design, Pamela's-New-Workout-like functions |
| Macro (named knobs → outputs, pages of 8) | new | ranges per target are set by Studio through modulation amounts (lab spec ABI2 D7) |
| Modulatable-flag batch | change to existing | function_envelope, ladder, mixer, slew, noise, clock and others have no `WS2_MODULATABLE` parameter |
| Gate/trigger delay | exists on a lab branch | lab #329, `task/329-gate-delay` |

Bars are 4/4 until the transport carries a time signature (owner, 2026-11-26).
