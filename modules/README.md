<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Modules: index

One row per folder in `modules/`. Specs are in lab
[`proposals/0024`](https://github.com/kalharbi11/wirescape-lab/blob/main/proposals/0024-test-repo-macros-modules.md)
§4 (and §3.1 for Macro). Tasks are filed as issues in this repo by the lab Architect.
- **Kind:** `new`, or `override` of the lab folder of the same name.
- **Status:** `planned`, `draft`, `T1`, `T2`, `promoting: lab #PR`, `promoted: lab #PR`, or
  `blocked: lab #N`.

| Folder | ID | Kind | Spec | Task | Stage | Status |
|---|---|---|---|---|---|---|
| `function_envelope` | `essentials.function_envelope` | override | §4.1 Trigger mode, `hold`, `delay` | MOD-1 | 1 | planned |
| `adsr` | `essentials.adsr` | override | §4.1 `delay` | MOD-2 | 1 | planned |
| `random` | `essentials.random` | new | §4.2 (starts from lab `sdk/examples/random_source`) | MOD-3 (#7) | 1 | draft |
| `clock_divmult` | `essentials.clock_divmult` | new | §4.3 | MOD-4 | 1 | planned |
| `macro` | `essentials.macro` | new | §3.1 | MOD-5 | 1 | planned |
| `rhythm_lanes` | `sequencing.rhythm_lanes` | new | §4.4 (original design) | MOD-6 | 2 | planned |
| chord, cv_to_notes, groove, host_keyboard, midi_in, midi_to_cv, notes_to_cv, quantizer | (existing) | override | §4.6 batch A, flags only | MOD-7 | 2 | planned |
| `lfo` | `essentials.lfo` | override | §4.5 shapes, division, clock and reset | MOD-8 | 2 | planned |
| delay, reverb, quad_vca, chaos_lite, spectral | (existing) | override | §4.6 batch B | MOD-9 | 3 | planned |

The harness (MOD-0) comes first: `tools/**`, not a module. Run `python tools/wstest.py t0 <folder>`
(see [tools/README.md](../tools/README.md)); its self-test fixtures live in `tools/fixtures/`.

## Notes from the lab at `LAB_PIN`
- **Already modulatable** (corrected in lab 0023 §3.1): clock, function_envelope, ladder, mixer, noise,
  scale_offset and slew.
- **Batch A:** its 8 modules have only stepped, non-modulatable parameters. §4.6 lists which become
  `STEPPED|MODULATABLE`.
- **Function Envelope** is gated AR with modulatable `rise`, `fall` and `curve`. ADSR already has
  Gate/Trigger `mode` and `hold`.
- **Random:** `example.random_source` (clocked sample-and-hold, MIT) is in the lab catalogue. Random
  builds on it.
- **The lab port tests** (`engine/tests/port_behavior_tests.cpp`) require:
  - non-stepped parameters of adsr, lfo, vca, vcf and vco to be modulatable;
  - stepped parameters of mixer, noise, output, scale_offset, slew, clock, function_envelope and ladder
    to stay non-modulatable.
- **The transport** runs only when a client starts it (lab TR1h). Until then the Clock module is the
  tempo source, so tempo-aware modules take a `clock` input (0024 §4.0 C2).
- **Bars** are 4/4 until the transport carries a time signature (owner, 2026-11-26).
- **Gate/trigger delay** is lab work (OB-h4 #609), not this repo's.

Signed: claude-planner, 2026-11-26
