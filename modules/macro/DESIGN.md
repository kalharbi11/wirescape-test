<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Macros design (wirescape-test MOD-5)

Original Wirescape code, GPL-3.0-only; no outside source.

- **Outputs.** Each output copies its parameter signal, which the engine has already resolved: base,
  D7 smoothing and modulation cables (§3.2). The module adds no range logic. A mapping's range is the
  target's base and amount, so Macros needs no mode.
- **Constant flags.** A constant parameter signal writes one sample and marks the output constant.
  Otherwise every sample is written and the flag is cleared.
- **Non-finite guard.** A non-finite parameter value (never expected from the engine) outputs 0.
- **Mono.** The knobs have no `PER_VOICE` flag, and each output has one channel whatever the host's
  channel count.
- **`labels` slot.**
  - It is declared with the SDK's `WS_DEFINE_DATA_STRUCT`: `page char[33]`, then `rows[8]` of
    `name char[33]`, 297 bytes in all.
  - The lab packer requires a text's length to be below the field's count. Hence 33 bytes for "at most
    32 bytes".
  - The DSP never reads it, and the tests check that the output is the same with and without the data
    view.
- **No state** (no `save_state` or `load_state`), no heap, no OS calls, no `double`: portable tier.

Realtime: process writes outputs only, with no allocation, locks, I/O or exceptions.

## Limitations
- **The 8 parameters and 8 outputs exceed the lab's `tests/native/abi2/dsp_fixture.h` (6 ports).**
  `tests.cpp` uses its own process rig, as the harness fixtures do. No lab test wraps this module in
  that fixture, so nothing in the lab is blocked.
