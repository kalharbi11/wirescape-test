<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Random (`essentials.random`)

A new random value at every division boundary of the beat. Mono in v1. New module from wirescape-test
MOD-3 (lab `proposals/0024` §4.2), starting from the lab's `sdk/examples/random_source`.

## Ports
| Dir | ID | Hint | Meaning |
|---|---|---|---|
| in 0 | `clock` | `GATE` | beat source: one beat per `clock_ppq` rising edges (Schmitt .5/.25). Unpatched: the transport |
| in 1 | `reset` | `TRIGGER` | rising edge: the beat is 0 now (between clock edges too), and the sequence restarts from its seed |
| out 0 | `stepped` | `CV_BI` | the held value × `depth` |
| out 1 | `smooth` | `CV_BI` | the same values, glided with an S-curve over `slew` × the step length, × `depth` |
| out 2 | `step` | `TRIGGER` | a 1 ms pulse on each new value |

## Parameters
| # | ID | Range | Default | Flags |
|---|---|---|---|---|
| 0 | `division` | 0–17 (C3 table: 4 bars … 1/32T) | 7 (1/4) | `STEPPED\|MODULATABLE\|AUTOMATABLE` |
| 1 | `depth` | 0–1 | 1 | `MODULATABLE\|AUTOMATABLE` |
| 2 | `slew` | 0–1 | 1 | `MODULATABLE\|AUTOMATABLE` |
| 3 | `seed` | 0–65535 | 0 | `STEPPED\|AUTOMATABLE`: 0 uses the engine's per-instance seed, 1–65535 fixes the sequence |
| 4 | `clock_ppq` | 1–24 | 1 | `STEPPED\|AUTOMATABLE` |

## Behaviour
- **The beat** follows lab 0024 §4.0 C2 (`beat_clock.h`).
  - **With a `clock` cable:** edges count beats. Between edges the beat is interpolated from the last
    period, never reaching the next edge's beat early. It holds after two periods with no edge.
  - **Without one:** the transport drives it, locked while playing and free-running at its BPM while
    stopped (120 BPM with no transport).
- **Trigger-to-step** is the default. With `clock_ppq` 1 and division 1/4, every rising edge on `clock`
  is one step, so triggers into `clock` give sample-and-hold, even irregular ones. Faster divisions add
  steps between edges.
- **Changing `division`** alone draws no value; a new value comes at the next boundary of the new
  division.
- **Values** are uniform in [−1, 1). The value at power-on is drawn without a glide or a trigger.
- **Seeds:** a fixed `seed` gives the same sequence on every run. Module reset and the `reset` input
  restart it. A change of `seed` restarts the sequence at once.

Demo: `demos/clocked-melody.json`. Tests: `python tools/wstest.py t0 random` (`abi2_random_tests`).
