<!-- SPDX-License-Identifier: GPL-3.0-only -->
# MIDI In (`midi.in`)

An event source. The `midi` output preserves bounded UMP words, source identity and
sample offsets. `channel_filter` is stepped 0 (Omni) or 1–16. System messages pass
regardless of channel filter. MIDI 1 and MIDI 2 zero-velocity Note Ons stay distinct.

The host configuration chooses `endpoint` (saved name, or `all_enabled`) plus optional
`system_name` fallback. It is resolved only against explicitly enabled endpoints;
missing or ambiguous names stay offline. Multiple modules never reopen a device.
On disconnect a source choke releases its voices. Changing the channel filter
chokes previously forwarded sources before accepting the new filter, so old-channel
notes cannot become stranded. Failed release emissions are retried, with a bounded
panic fallback. No hardware service or device is created by this module.

## MOD-7 audit

| Parameter ID | Range | Default | Flags |
|---|---|---|---|
| `channel_filter` | 0 (Omni), 1–16 | 0 | STEPPED, AUTOMATABLE; no modulation cable |

No input ports; output `midi` is an event stream (UMP), not a CV signal. The filter is a setParam/automation control, not a modulation target: changing it releases the previously forwarded sources before accepting new-channel events. See DESIGN.md. Active source and filter tests run without MIDI devices; the offline idle demo is an identity witness, not an active UMP render.
