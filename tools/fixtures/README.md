<!-- SPDX-License-Identifier: GPL-3.0-only -->
# wstest fixtures

Harness self-tests (lab `proposals/0024` §5.2, task MOD-0). Each folder has the layout of a module folder
and is registered in a scratch lab like one. `python tools/wstest.py selftest` runs T0 on all three and
checks the outcome in [`expected.json`](expected.json).

| Folder | ID | Expected T0 result |
|---|---|---|
| `gain` | `fixture.gain` | passes every step (a copy of the lab's `sdk/template` gain, split into `dsp.h` and `module.cpp`) |
| `nan_gain` | `fixture.nan_gain` | fails at `demos`: the first sample of every block is NaN |
| `alloc_gain` | `fixture.alloc_gain` | fails at `cpu_probe`: `process()` allocates; its own `tests.cpp` deliberately does not check |

The negative fixtures break the realtime rules on purpose. Never copy them into `modules/`.

Signed: claude-builder1, 2026-11-26
