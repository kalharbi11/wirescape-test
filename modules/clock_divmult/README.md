<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Clock Div/Mult (`essentials.clock_divmult`)

Four gate outputs that divide or multiply the beat: one beat per rising edge on `clock`, or the
transport when `clock` is not patched. Mono in v1. New module from wirescape-test MOD-4 (lab
`proposals/0024` §4.3).

## Ports
| Dir | ID | Hint | Meaning |
|---|---|---|---|
| in 0 | `clock` | `GATE` | one beat per rising edge (Schmitt .5/.25). Unpatched: the transport drives it |
| in 1 | `reset` | `TRIGGER` | rising edge: every output fires on the next clock edge (transport: at the reset) |
| out 0–3 | `out1`…`out4` | `GATE` | gates at `ratio1`…`ratio4` of the beat, `width` of each output period long |

## Parameters
| # | ID | Range | Default | Flags |
|---|---|---|---|---|
| 0 | `ratio1` | 0–18 | 10 (×2) | `STEPPED\|MODULATABLE\|AUTOMATABLE` |
| 1 | `ratio2` | 0–18 | 8 (÷2) | `STEPPED\|MODULATABLE\|AUTOMATABLE` |
| 2 | `ratio3` | 0–18 | 6 (÷4) | `STEPPED\|MODULATABLE\|AUTOMATABLE` |
| 3 | `ratio4` | 0–18 | 2 (÷8) | `STEPPED\|MODULATABLE\|AUTOMATABLE` |
| 4 | `width` | 0.01–0.99 | 0.5 | `MODULATABLE\|AUTOMATABLE`: the gate length as a fraction of the output period |

### Ratio table
| Index | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Ratio | ÷16 | ÷12 | ÷8 | ÷7 | ÷6 | ÷5 | ÷4 | ÷3 | ÷2 | ×1 | ×2 | ×3 | ×4 | ×5 | ×6 | ×7 | ×8 | ×12 | ×16 |
| Period (beats) | 16 | 12 | 8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 1/2 | 1/3 | 1/4 | 1/5 | 1/6 | 1/7 | 1/8 | 1/12 | 1/16 |

## Behaviour
- **÷N** gates on every Nth clock edge, counted from power-on or the last reset (edges 0, N, 2N, …).
- **×N** gives N gates per input period, the first on the edge. The others follow the beat interpolated
  from the last measured period, so there are never more than N in one input period, even when the clock
  speeds up or jitters. Before the second edge the period is unknown, and ×N gives one gate per edge.
- **`reset`** arms the module: no gates until the next clock edge, then every output fires on that edge.
- **Without a `clock` cable** the transport is the clock: gates are locked to `transport.beat` while
  playing (÷4 lands on bars from beat 0), and free-run at its BPM while stopped (120 BPM with no
  transport). A reset here is beat 0, and every output fires at once.
- **A ratio change** applies at that output's next period start, on the new ratio's grid. The gate in
  progress is never cut.
- **Width:** each gate is `width` × the output period long. It is at least 1 sample, and at most the
  period minus 1, so consecutive gates always have a falling edge. Before the tempo is known (first
  clock period), a gate ends at the clock edge where its width runs out.

Demo: `demos/divided-gates.json`. Tests: `python tools/wstest.py t0 clock_divmult` (`abi2_clock_divmult_tests`).
