<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Quad VCA design and provenance

Original DSP written for Wirescape, 2026-11-11. No ported third-party code, dependencies or external DSP assets.

Four independent per-voice gain cells share one Response control. VCA is exactly linear; LPG uses a one-pole attack/release follower and a two-integrator TPT state-variable low-pass with fixed Q≈0.6. The cutoff follows the same envelope as the amplitude. Cutoff and release coefficient lookup tables are prepared off the audio thread at the configured sample rate; audio processing uses only fixed-size arrays and bounded interpolation. The low-pass state is cleared when returning to VCA. The mix sums four post-gate outputs and clamps at ±4, without normalization.

Task/IDs: `docs/tasks/QUAD_VCA_LPG_490.md`, Q1 #601. No ABI, engine, protocol or existing VCA changes.

## S7.5 mode-switch crossfade

Changing any of the four stepped `modeN` controls ramps a per-voice output blend from the
bit-exact linear VCA path to the existing LPG filter output (or back) over 10 ms measured
in samples at the configured rate. The LPG follower/filter run throughout the fade and
are discarded only when the VCA blend reaches zero. This avoids the old abrupt
VCA-to-LPG discontinuity and preserves the exact VCA fast path, no stale LPG history,
normalled inputs, the same shared `response` parameter, and the original steady-LPG DSP.
On the first sample of each voice after prepare/reset, the blend starts at the selected
mode endpoint, so loading an LPG patch does not leak unfiltered VCA audio. The primed
bit is independent of the LPG state and survives a completed fade back to VCA.
Constant-output optimization is disabled while a previous LPG-to-VCA fade completes,
even if all incoming signals and params have become constant. No allocations, locks,
port/parameter/state-format changes, host edits, or new modes are introduced.

Measured **before** baseline: T7.0 capture [run 44029050283](https://github.com/kalharbi11/wirescape-lab/actions/runs/44029050283), `essentials.quad_vca.mode1` at 48 kHz, switch frame 48000,
peak `0.049578541970749734` FS (`-26.094124986365436` dBFS), pinned rendered
source `1b36751e44d99536166962094d52a713dfb989a2`. S7.5 requires a measured
head peak at least 20 dB lower (or the applicable early-met threshold), with the
other T7.0 `parameter_steps` rows unchanged within the contract tolerance.
**After** measurements, three-platform Golden, Ir and D5 evidence must be recorded
from exact CI/artifact source pins; this code change does not assert those gates.

## MOD-9 batch B (#64)

Base: lab `3d3db7a64090682a62f220d73baef49709048c87` (LAB_PIN). Original six module files copied byte for byte before tests. The only production change is the shared `kMode` descriptor mask, adding `WS2_MODULATABLE` to the existing `WS2_STEPPED | WS2_AUTOMATABLE` bits. No IDs, legacy slots, ranges, defaults, ports, DSP, or state-format changes.

| Parameter | Read-site decision |
|---|---|
| `mode1` | `k=0`: frame-zero prime, constant-path selection and dynamic per-sample `param(4)` read. Fixed follower/filter/fade state, no allocation or structural changes. Enable modulation. |
| `mode2` | `k=1`: identical cadence at `param(5)` for channel 2's independent state. Enable modulation. |
| `mode3` | `k=2`: identical cadence at `param(6)` for channel 3's independent state. Enable modulation. |
| `mode4` | `k=3`: identical cadence at `param(7)` for channel 4's independent state. Enable modulation. |

The constant-output check also reads each mode and disables the shortcut during LPG or an unfinished fade. For nonconstant mode signals the dynamic branch reads each sample; the first-frame priming behavior is unchanged. State is four fixed arrays of 16 voices plus coefficient tables prepared off-thread; no new allocations, locks, I/O, exceptions, logging, or double state. `level1`–`level4` and `response` keep their original flags. No mode is excluded. The SDK state callbacks return `WS2_UNSUPPORTED` for this stateless module; patch parameters are host-owned and reset clears history.

Tests assert all nine flag masks, compare a mid-block effective mode stream to one-frame processing for each mode and 1/16 voices, check neighboring channels, return to exact VCA, reset and no allocations, and connect actual engine modulation cables for all four controls. Stateless contract tests check unsupported save/load, no bytes, and matching fresh-instance versus reset output when host-owned values are restored. The offline default VCA note/envelope demo is an unmodulated identity witness.

## Limitations

No new lab-facing gap identified. Demo uses offline Notes events, no audio or MIDI devices. No custom state payload; no new state serialization. Owner-requested T2 only.
