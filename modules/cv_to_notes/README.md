<!-- SPDX-License-Identifier: GPL-3.0-only -->
# CV to Notes (`essentials.cv_to_notes`) — EXPERIMENTAL

Polyphonic CV-to-note-event adapter, inverse to `essentials.notes_to_cv`.
This is an **experimental** module: parameter naming and patch compatibility are not frozen.

## Ports and parameters

| Kind | ID | Hint / behaviour |
| --- | --- | --- |
| Input | `pitch` | Poly PITCH; 1 V/oct, 0 V = C4 (MIDI 60); unconnected 0 V |
| Input | `gate` | Poly GATE; rising/falling threshold 0.5; unconnected low |
| Input | `velocity` | Poly unipolar CV 0–1; **unconnected defaults to 0.8**; latched at attack |
| Output | `notes` | Poly NOTES event stream, one paired NOTE_ON/NOTE_OFF per active lane |
| Parameter | `legato` | Off (0, default) / On (1), stepped, modulatable and automatable; no pitch-change retrigger when Off |

At a gate rise, the channel generates MIDI key `clamp(round(60 + 12*pitch), 0, 127)`
and a velocity-latched NOTE_ON. On a gate fall it emits NOTE_OFF for the **original
latched key**, even if pitch moved. With legato On, changing the quantized key
while high releases the old key **before** attacking the new one at the same
sample. Legato uses the effective channel-zero parameter value at each sample,
including while a gate is already high; enabling it can immediately retrigger
if the quantized pitch differs from the latched key. Signals broadcast normally from a mono cable to multiple poly lanes.
The highest present poly input determines the number of examined lanes (up to
the host's 16-channel limit); silent/unconnected gates never trigger.

Note identity is unique by node seed and lane: events preserve the same
`source_id`, `channel`, `note_id` and key on release. The owner is
(`module instance`, `channel`), not the changing pitch.

Reset and CHOKE/Panic owe NOTE_OFF for all held keys. The reset API has no event
sink, so releases are queued in the fixed per-lane state and flushed at the
start of the next processed block. Failed NOTE_OFF sends are retried without
ever creating a replacement NOTE_ON before an old release succeeds. After
reset/choke, a still-high gate is inhibited until a new low-to-high edge.
Realtime paths perform no allocations, I/O or locking.

A patch can send OSC In poly pitch/gate/velocity to CV to Notes, then connect
`notes` to OSC Out's `notes` event input (or Chord). For separate monophonic
rows, use the independent experimental Poly Merge module once available.

Only a version number is saved; transient held notes are not saved. No audio,
MIDI device or LAN access is used in the device-off module tests.
