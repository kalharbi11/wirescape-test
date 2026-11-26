<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Design

Based on the lab's `sdk/template` (MIT) at the commit in `LAB_PIN`, relicensed GPL-3.0-only here as a
harness fixture. Output = input × gain, zero latency, no state beyond the host arena scratch rows.

## Limitations
None.

## Deliberate defect
Sample 0 of every block is a quiet NaN, so the demo render must fail its finiteness check.
