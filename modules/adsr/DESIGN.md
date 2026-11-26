<!-- SPDX-License-Identifier: GPL-3.0-only -->
DSP is in dsp.h, wrapper registration in module.cpp. No Qt/OS/allocating containers or allocation in processing. Fixed16-channel state is prepared once. SDK owns validation; engine owns base smoothing/normalized modulation/taper. Double phase/IIR state is retained where documented for precision/reference matching. No fast tan approximation is used. Voice groups are four-wide; VCA uses SDK SIMD, while transcendental/IIR scalar math is retained pending measured equivalent SIMD implementations.

## Behavior

Gate cable precedence is per voice: when the gate signal is connected, all note events are ignored, even when the cable is low. The Schmitt gate (>0.5 on, <0.25 off) triggers/releases envelopes. Without a connected gate, NOTE_ON establishes source/note ownership; only matching NOTE_OFF/CHOKE releases that voice. Stage durations latch on entry. This added cable/event behavior intentionally extends the ABI1 event-only module.

## Provenance

Source: `native/modules/adsr/module.cpp` and the ABI2 port at main `f23ea96`. License: GPL-3.0-only. Original Wirescape code, GPL-3.0-only, no upstream source. There is no ABI1 DESIGN.md; no external source or pinned upstream revision is invented here.

## MOD-2 delay (wirescape-test, override of lab `modules/adsr` at `LAB_PIN` c083b26)

Base commit: lab `c083b26722b5b4d09e3285ab43f93e93be192f4f`, a byte copy. Changes:
- appended `delay` (index 6);
- `Voice::pending`, the samples left before a delayed Attack;
- the sticky `delay_seen`, mirroring `trigger_mode_seen`;
- the helpers `edge_on` and `edge_off`;
- state version 2.

**Bit-identity.** The default-Gate fast path is taken only when `delay` is a constant 0 and no delay
has been used since reset. The per-voice original loop is taken only when that voice has no wait. The
other paths are the existing general loop, with the start and stop moved into the two helpers; their
arithmetic is unchanged.

**Timing.**
- A cable edge at sample *i* with a wait of *n* samples starts Attack at sample *i + n*: the countdown
  runs before edge detection.
- An event is applied before its sample, so it counts *n + 1* and starts at the same sample.
- `delay` latches at the edge. Attack latches when it begins, as every ADSR stage does.

**State v2.** `delay_seen` follows `trigger_mode_seen`, and each voice appends `pending` as a u64. The
loader accepts v1 (no wait) and v2; `pending` is bounded by 30 s at 192 kHz, plus one.

Realtime: no allocation, locks, I/O or exceptions in process, event or reset; fixed 16 voices. No new
`double` state (the existing `state_rate` stays).

## Limitations
- **Lab test fixture holds 6 parameters.** `tests/native/abi2/dsp_fixture.h` has `ports=6`, and ADSR
  now has 7 parameters.
  - **Workaround:** `wide_fixture.h` in this folder is a copy of the lab file at `c083b26` with
    `ports=8` and a narrower include list. `tests.cpp` includes it.
  - **Still blocked for T2 and promotion:** the lab's shared CTests wrap ADSR in the lab fixture and
    abort at its port check (`abi2_module_tests`, `port_behavior_tests`). The smallest lab change is
    `ports=8` in `dsp_fixture.h`.
  - **Lab issue:** to file, labelled `limitation found`; see `modules/README.md`.
