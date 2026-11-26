<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Function Envelope (ABI 2)
Double held/start/end levels, edge-latched curve and exact expm1 shaping are retained. Gate changes start a rounded sample-count segment from the current level, including falling from a held nonzero level. No note-event or transport semantics are added.
Run `abi2_function_envelope_tests` for exact references, the rate/block/channel matrix, state and allocation checks.

## Trigger mode, hold and delay (wirescape-test MOD-1, lab 0024 §4.1)

| # | ID | Range | Default | Flags |
|---|---|---|---|---|
| 3 | `mode` | 0 Gate, 1 Trigger | 0 | `STEPPED\|AUTOMATABLE` (setParam-only; not modulatable, 0023 R3.3) |
| 4 | `hold` | 0–30 s | 0 | `port_flags` (modulatable, per voice, audio-rate OK) |
| 5 | `delay` | 0–30 s | 0 | `port_flags` |

Appended after `curve`; IDs 0–2 and the ports (`gate` in, `envelope` out) are unchanged.
- **Gate mode** is unchanged.
  - With `delay` > 0, a rising edge waits `delay` before the rise.
  - A falling edge during the wait cancels the rise; the fall starts from the current level.
  - `hold` has no effect in Gate mode.
- **Trigger mode:** a rising edge runs delay → rise (from the current level) → hold at 1 → fall to 0.
  The falling edge is ignored. A new rising edge restarts at the delay, from the current level.
- **Latching:** `rise`, `fall`, `curve`, `delay` and `hold` latch at the start edge, and `mode` is read
  there too. In Gate mode the fall still uses `fall` and `curve` at the falling edge, as before.
- **Bit-identity:** at `mode` 0 and `delay` 0 the output is bit-identical (`wstest identical`, the frozen
  copy in `tests.cpp` and the lab's frozen ABI1 references).
- **State version 2** adds the stage counters and latched values; version 1 still loads.

Demo: `demos/trigger-hold-delay.json`. Tests: `python tools/wstest.py t0 function_envelope`.
