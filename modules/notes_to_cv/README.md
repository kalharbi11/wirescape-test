<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Notes to CV (`essentials.notes_to_cv`)

Production event-note-to-poly-CV adapter. Connect `host.keyboard.notes`, `sequencing.groove.t1_notes`, `example.step_sequencer.notes` or `essentials.chord.notes` to `notes` (an **event** port). The module allocates a fresh poly voice for each note using the shared `wirescape::voice::NoteToCv` allocator; the incoming note channel is an identity field, **not** the destination poly lane. The stepped `voices` control selects 1–16 voices (default 4); the effective count is capped by the host's available poly signal channels when fewer are provided. When all are busy, allocation rotates and steals an existing voice. Original MIDI and note identities remain distinct at input fan-in.

Poly **signal** outputs, in order: `pitch` (1 V/oct, zero = MIDI C4), `gate` (0/1), `velocity` (0..1), and `trig` (1 ms gate pulse on an accepted Note On assigned to that lane). Use the same poly signal cables to drive an oscillator/envelope/VCA voice chain. No MIDI-device source or raw MIDI parsing is performed by this module; `midi.to_cv` remains the existing raw MIDI converter. Chord requires the separate `essentials.chord` event transformer. Valid explicit note IDs include 0; anonymous notes use the existing source/channel/key matching semantics in `NoteToCv`. Note steals, source choke, and expression are owned by the shared allocator, with no new graph engine or ABI behavior.

Reset/load clears active notes; no gate or allocated voice is persisted. Agent tests use device-free native events and fake inputs; actual MIDI controller and audible listening tests remain owner-owned.

## MOD-7 audit

| Parameter ID | Range | Default | Flags |
|---|---|---|---|
| `voices` | 1–16 (capped by host) | 4 | STEPPED, AUTOMATABLE; no modulation cable |

The voice count is a setParam/automation control that changes the allocator and output channel count at the start of the block. It is intentionally not modulatable. See DESIGN.md. Shrinking releases removed lanes, preserves retained note ownership and drops removed trigger pulses; growing introduces idle lanes, not resurrected notes.
