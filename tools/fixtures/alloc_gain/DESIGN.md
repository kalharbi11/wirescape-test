<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Design

Based on the lab's `sdk/template` (MIT) at the commit in `LAB_PIN`, relicensed GPL-3.0-only here as a
harness fixture. Output = input × gain, zero latency, no state beyond the host arena scratch rows.

## Limitations
None.

## Deliberate defect
`process()` calls `new[]`/`delete[]` every block, so the CPU probe's allocation count is non-zero.
