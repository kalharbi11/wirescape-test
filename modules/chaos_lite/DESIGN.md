<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Design and provenance

Original Wirescape code, GPL-3.0-only, no upstream source. No Surge or SST DSP is
included. The CHAOS decomposition inventory informs the behaviour vocabulary only.
The source example structure follows the MIT SDK random-source example.

`comb_body.h`, `coupler.h` and `drift.h` are deliberately independent components
for later M7 decomposition. Delays use linear interpolation; the body uses three
pitch-related ratios (1, 1.4983, 2.003) and saturating feedback. The stereo A/B pan
law is linear, with independent left/right resonator and coupler histories. A
one-pole post filter, DC blocker, rational soft clip and −1 dBFS ceiling bound output.

S7.3 uses a centered cubic PolyBLEP correction for the saw reset and both square
edges. The residual is (1-u)^2(1+u/2) on each side of an edge, from the
integral of a parabolic smoothing kernel. The support is 1.65 samples per side,
capped at 0.45 cycles at high normalized pitch to avoid the first kernel null.
The phase increment drives both correction and phase advance. For normalized
increment d capped at 0.18, the saw and square level factors are respectively
1+d(1.9+11d) and 1+d(1.6+11d), chosen to compensate for lost harmonic energy
without discontinuous tuning or unbounded high-pitch gain. Shape morphing
interpolates the corrected endpoints; pure sine retains the original expression.
The coefficients 1.9 (saw), 1.6 (square), and the common quadratic coefficient
11 are empirical level-compensation constants introduced by the original
Wirescape S7.3 work, not constants derived from the PolyBLEP kernel or taken
from an external implementation. See [S7.3 source and review](https://github.com/kalharbi11/wirescape-lab/pull/432#issuecomment-7143875832)
at `d86c195fa314d6d17ef205ee5a7181edabff361a`. The retained
[full T7.0 validation](https://github.com/kalharbi11/wirescape-lab/pull/432#issuecomment-7143248891)
compares the complete rendered output with the pre-S7.3 baseline: across the
24 reported rate/pitch/shape cases, Linux x64 RMS changes ranged from
−0.234425 to +0.189420 dB, and all 12 gated alias-reduction rows met their
targets on both Linux architectures. This validates the chosen constants on
those stimuli; it does not establish a universal gain match, an optimal fit
or an analytic derivation. No separate coefficient-fitting sweep or fitting
procedure is claimed by the retained evidence.

The correction is stateless and allocation-free; state v1/v2 loading is unchanged.
The T7.0 restricted alias bins measure the complete output including the nonlinear
post stage, so a lower result is not a claim of zero aliasing.

All comb and coupler storage is requested with WS2_LARGE in prepare. The memory
query conservatively includes alignment padding; allocation and destruction belong
to the host. At 48 kHz eight prepared voices reserve approximately 0.60 MB plus
host/wrapper storage. Process and reset allocate nothing. Supported frame counts
are 1..prepared maximum, rates 44.1/48/96 kHz, channels 1..16 with only the first
eight active. Parameters are read per sample, respecting host modulation.

State is versioned little-endian words/floats, staged and validated before commit.
Only generator/instability state is serialized. Delay lines and filter memory are
cleared on load; this intentional patch-state policy means no continuation-equivalence
claim for resonator tails. Reset determinism and state-byte restoration are tested.
No constant flag is set on running audio, including silent capped channels.

## MOD-9 read-site audit

Override source byte-copy base: lab `3d3db7a64090682a62f220d73baef49709048c87`.
Current validation pin: `74f4a3a3260e19d5377a1aa906cc55de400edb99` (workshop #75).
The lab comparison shows no changes to module source between these pins. The shared
`stepped` mask in dsp.h is the sole production change; all five slots retain
STEPPED and AUTOMATABLE and gain MODULATABLE. No audio-rate/per-voice capability
flags are added. The existing effective stream is read per sample for each active voice.

| Parameter | Existing read in dsp.h | Decision |
|---|---|---|
| octave_a (3) | process: rounded p(3) in fa | Modulate; bounded exponent/frequency, no allocation or reset. |
| octave_b (4) | process: rounded p(4) in fb | Modulate; same bounded pitch arithmetic, independent oscillator. |
| route (9) | process: rounded/clamped p(9), per side | Modulate; Coupler::process selects arithmetic over a delay allocated in prepare; cursor/history advance in every mode. |
| lfo_target (22) | process: rounded/clamped p(22), selects mod in fa/tune/coefficient | Modulate; selects existing arithmetic, no topology or allocation changes. |
| lfo_shape (26) | process: rounded/clamped p(26) before waveform selection | Modulate; fixed voice RNG and 10 ms transition state already handle shape switches. |

None of these controls is structural. No new state, double state, ports, parameters,
reset path or sample computation. Existing eight-voice cap, 16-channel silence above
cap, v1/v2 patch-state compatibility and intentional history clearing stay unchanged.
Tests assert exact masks, 1/16-channel effective streams versus a scalar per-frame
oracle, changes relative to unmodulated output, state-byte restoration, bounded finite
PCM and zero callback allocations; graph tests check actual engine modulation cables.
All original matrix, alias, state and component tests remain.

## Limitations

- [LR-7](../../LAB_REQUESTS.md): the pinned offline evidence checker requires equal
  output channels. The default identity demo mixes the module's stereo outputs at
  0.5 each and duplicates the mono result to both render buses. Native tests check
  both original stereo channels independently; the demo is not a stereo identity proof.
- No new lab gaps. Eight active voices maximum is existing behavior, not a new limit.
