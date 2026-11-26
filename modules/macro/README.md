<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Macros (`essentials.macro`)

Eight macro knobs. Each knob's effective value (base plus modulation) appears on its output. New
module from wirescape-test MOD-5 (lab `proposals/0024` §3.1). A patch can hold any number of Macro
modules: page *n* is the *n*-th Macro module in the patch's `modules` array.

## Ports
| Dir | ID | Hint | Meaning |
|---|---|---|---|
| out 0–7 | `out1` … `out8` | `CV_UNI` | knob *k*'s effective value per sample, mono. Constant when the parameter signal is constant |

No inputs. A CC or an LFO drives a knob through a modulation cable into its parameter.

## Parameters
| # | ID | Range | Default | Flags |
|---|---|---|---|---|
| 0–7 | `knob1` … `knob8` | 0–1, linear | 0 | `MODULATABLE\|AUTOMATABLE\|AUDIO_RATE_OK`, mono |

## Data slot `labels`
`page` (text, ≤ 32 bytes) and `rows[8].name` (text, ≤ 32 bytes). Empty means "Macros *n*" and
"Knob *k*". The DSP never reads it. Clients edit names live with `setData`, and the names live in the
patch. `rows` always has 8 entries (a fixed-length record):
```json
"data": {"labels": {"page": "Drums", "rows": [{"name": "Cutoff"}, {"name": "Drive"}, {"name": ""}, {"name": ""},
                             {"name": ""}, {"name": ""}, {"name": ""}, {"name": ""}]}}
```

Demo: `demos/lfo-into-knob.json` (an LFO modulation cable into `knob1` at base 0.3, amount 0.4). Tests:
`python tools/wstest.py t0 macro` (`abi2_macro_tests`).
