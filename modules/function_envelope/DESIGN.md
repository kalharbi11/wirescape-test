<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Function Envelope design
Source: `native/modules/function_envelope/module.cpp` at main `f23ea96`. Double held/start/end levels, edge-latched curve and exact expm1 shaping are retained. Gate changes start a rounded sample-count segment from the current level, including falling from a held nonzero level. No note-event or transport semantics are added. Per-voice state is fixed; process/reset have no allocation, locks or I/O. Continuous controls are per-voice D7 parameters. Plain-unit ABI1 ramps are retained where present; the host also applies D7 base smoothing. Version-1 state uses explicit little-endian fields and atomic validation before loading.

Provenance: the existing ABI1 DESIGN.md records original DSP and no external third-party implementation or external pinned revision. License: GPL-3.0-only, full text in the root LICENSE; no new third-party source or unverified upstream revision is claimed.

## MOD-1: Trigger mode, hold and delay (wirescape-test override of lab `modules/function_envelope`)

Base commit: lab `c083b26722b5b4d09e3285ab43f93e93be192f4f`, a byte copy. The per-sample code keeps the
original statements and order. New work happens only when a voice has a wait, a Trigger cycle or a
hold count.
- **Wait.** A start edge with a wait of *n* samples sets `pending`. The countdown runs before the edge
  check, so the rise starts at sample *edge + n*, which is where an undelayed edge would put it. The
  previous segment carries on meanwhile.
- **Trigger mode.** When the rise reaches 1, `hold_left` counts `l_hold` samples at 1, then the fall
  starts. With `hold` 0 the fall starts on the sample the rise ends.
- **Latching.** `l_rise`, `l_fall`, `l_curve` and `l_hold` latch at the start edge.
- **New `double` state:** `l_rise`, `l_fall` and `l_curve`. They are copies of the existing `double`
  fields `rise`, `fall` and `curve`, so a delayed or triggered segment uses the same values an undelayed
  edge would.
- **State v2** appends, per voice, three f64 and four u32 fields, with `trig` last, so a corrupt final
  byte is rejected. Version 1 loads with no wait and no Trigger cycle.

Realtime: no allocation, locks, I/O or exceptions in process or reset; fixed 16 voices.

## Limitations
- **Lab `engine/tests/port_behavior_tests.cpp` (`cv_new_ports`) cannot pass for this override.**
  It is a lab CTest, so it is red only in T2.
  - It drives every modulatable parameter of eight modules in Gate patches and requires each one to
    change the output (`VERIFY(changed)`). `hold` only acts in Trigger mode, as §4.1 specifies.
  - It also counts exactly 17 such parameters (`VERIFY(covered==17)`), and this override adds two.
  - **Smallest lab change:** count from the descriptors instead of a constant, and set
    `function_envelope`'s `mode` to Trigger before driving `hold`.
  - **No workaround is possible in the folder**, so T2 and promotion are blocked until the lab test
    changes. **Lab issue:** [lab #665](https://github.com/kalharbi11/wirescape-lab/issues/665), labelled `limitation found`.
