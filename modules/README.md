<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Modules: index

One row per folder in `modules/`. Specs are in lab
[`proposals/0024`](https://github.com/kalharbi11/wirescape-lab/blob/main/proposals/0024-test-repo-macros-modules.md)
§4 (and §3.1 for Macro). Tasks are filed as issues in this repo by the lab Architect.
- **Kind:** `new`, or `override` of the lab folder of the same name.
- **Status:** `planned`, `draft`, `T1`, `T2`, `promoting: lab #PR`, `promoted: lab #PR`, or
  `blocked: LR-n` (a [`LAB_REQUESTS.md`](../LAB_REQUESTS.md) row); legacy `blocked: lab #N` statuses remain during migration.

| Folder | ID | Kind | Spec | Task | Stage | Status |
|---|---|---|---|---|---|---|
| `function_envelope` | `essentials.function_envelope` | override | §4.1 Trigger mode, `hold`, `delay` | MOD-1 (#5) | 1 | T1; T2 waits for the next pin (LR-10, fixed in lab #683) |
| `adsr` | `essentials.adsr` | override | §4.1 `delay` | MOD-2 (#6) | 1 | T1; T2 waits for the next pin (LR-9, fixed in lab #683) |
| `random` | `essentials.random` | new | §4.2 (starts from lab `sdk/examples/random_source`) | MOD-3 (#7), MOD-3a (#13) | 1 | T1 |
| `clock_divmult` | `essentials.clock_divmult` | new | §4.3 | MOD-4 (#14) | 1 | T1; SDK-1 (`beat_clock.h`), [LR-8](../LAB_REQUESTS.md) |
| `macro` | `essentials.macro` | new | §3.1 | MOD-5 (#8) | 1 | T1 |
| `guttr` | `guttr` | new | owner package (lab `docs/context/future_modules/guttur`) | GUTTR (#38) | — | T1 |
| `rhythm_lanes` | `sequencing.rhythm_lanes` | new | §4.4 (original design) | MOD-6 (#25) | 2 | T1 |
| `midi_to_cv` | `midi.to_cv` | override | §4.6 batch A, performance modulation | MOD-7 (#31) | 2 | T1; active UMP and idle demos (#76) |
| `chord` | `essentials.chord` | override | §4.6 enable/shift modulation | MOD-7 (#26) | 2 | T1 |
| `notes_to_cv` | `essentials.notes_to_cv` | override | §4.6 batch A, setParam-only voices audit | MOD-7 (#33) | 2 | T1 |
| `midi_in` | `midi.in` | override | §4.6 batch A, setParam-only filter audit | MOD-7 (#32) | 2 | T1 |
| `quantizer` | `essentials.quantizer` | override | §4.6 batch A, all 16 flags | MOD-7 (#27) | 2 | T1 |
| `cv_to_notes` | `essentials.cv_to_notes` | override | §4.6 batch A, legato modulation | MOD-7 (#29) | 2 | T1 |
| `groove` | `sequencing.groove` | override | §4.6 batch A, ten flags | MOD-7-groove (#28) | 2 | T1 |
| `host_keyboard` | `host.keyboard` | override | §4.6 octave modulation | MOD-7 (#30) | 2 | T1 |
| `lfo` | `essentials.lfo` | override | §4.5 shapes, division, clock and reset | MOD-8 (#34) | 2 | T1 |
| `delay` | `fx.delay` | override | §4.6 division/mode modulation | MOD-9 (#62) | 3 | T1 |
| `quad_vca` | `essentials.quad_vca` | override | §4.6 batch B, four mode flags | MOD-9 (#64) | 3 | T1 |
| `reverb` | `fx.reverb` | override | §4.6 freeze modulation | MOD-9 (#63) | 3 | T1 |
| `chaos_lite` | `essentials.chaos_lite` | override | §4.6 batch B, five stepped flags | MOD-9 (#65) | 3 | T1 |
| spectral | (existing) | override | §4.6 batch B | MOD-9 | 3 | planned |

The harness (MOD-0, merged) is `tools/**`, not a module. Run `python tools/wstest.py t0 <folder>`
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
