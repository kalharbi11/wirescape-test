<!-- SPDX-License-Identifier: GPL-3.0-only -->
# GUTTR Studio panel: a proposal

**Status:** proposal for the Architect's approval. It is not GUTTR's live panel:
`modules/guttr/panel.json` stays as it is until Studio supports what this needs.
Nothing here edits the lab. The folder holds copies of the five pinned Studio files that
change, and a script that previews them in the harness's git-ignored app copy.

## Why
The live panel puts 37 controls in eight sections stacked top to bottom, nearly all as knobs of one
size: 738 × 1271 px at zoom 1. Studio has three reasons for this:
- sections can only stack, never sit side by side;
- a row centres its controls, so a large knob's satellites sit beside it instead of under it;
- the only slider is a vertical fader.

## The design
GUTTR has two kinds of control: what shapes **the voice**, and what happens **after it**.
The layout says so, and gives the controls a three-level order of importance.

```
VOICE ─────────────────────────────────────────────────────────────────────────
│ 01 SOURCE            │ 02 TRACT             │ 03 MOTION                      │
│     ( PRESSURE )     │      ( VOWEL )       │   jitter   ━━━━━━━━━  0.3%     │
│ style   ━━━━━━  45%  │ length  ━━━━━  17.5  │   shimmer  ━━━━━━━━━  2.5%     │
│ breath  ━━━━━━  12%  │ pharynx ━━━━━   0%   │   tremor · wander · vibrato    │
│ whisper ━━━━━━   0%  │ mouth · ring         │                                │
├──────────────────────┴──────────────────────┼────────────────────────────────┤
│ 04 ROUGHNESS                                │ 05 ENSEMBLE                    │
│ growl ━━━━━━   tail fry ━━━━━━              │   throats  ━━━━━━━━━  3        │
│ rasp  ━━━━━━   breaks   ━━━━━━              │   loose · spread · size spr    │
│ creak ━━━━━━   flutter  ━━━━━━              │                                │
╔═ AFTER THE VOICE (inset band) ═══════════════════════════════════════════════╗
║ 06 ENVELOPE          │ 07 SPACE             │ 08 OUTPUT                      ║
║  (ATTACK) (RELEASE)  │   (ROOM)   (WET)     │   (DRIVE) (LEVEL)              ║
║        (glide)       │  (decay)(dist)(azim) │   (body)(air) [POST GRIT]      ║
╚══════════════════════════════════════════════════════════════════════════════╝
```

| Level | Controls | Widget | Why |
|---|---|---|---|
| 1 | Pressure, Vowel | large knob (`hero`), modulation input on the rail | the two things you play: breath energy and the vowel, GUTTR's "cutoff" |
| 2 | the other 22 voice parameters | horizontal slider (new `slider`) | each sets an amount in a related bank; a slider shows its value and position at a glance, and 22 sliders take less room than 22 knobs |
| 2 | Attack, Release; Room, Wet; Drive, Level | medium knob | the main control of each section after the voice |
| 3 | Glide; Decay, Distance, Azimuth; Body, Air | small knob, centred under its section's pair | refinements |
| 3 | Post Grit | toggle | on or off |

- **Signal-flow order:** glottal source, then tract, then motion (left to right). Below them come
  roughness (two columns: main textures Growl/Rasp/Creak, then detail Tail Fry/Breaks/Flutter) and the
  ensemble. Envelope, space and output follow, in the band.
- **Throats** is a slider that snaps to whole throats (1 to 8). It stays `STEPPED|AUTOMATABLE`: no
  modulation input is added.
- Cells with equal shares line up across rows, so the voice grid reads as one table.
- **Size:** 920 × 769 px at zoom 1. That is about 40% shorter and 25% wider than the live panel, a
  landscape card like a plugin window.
- Every one of the 37 parameters is placed exactly once, so no MORE section appears. The parameter
  IDs, flags and DSP are unchanged.

## What Studio would gain (`lab.patch`)
All additions are optional, so existing panel overrides render exactly as before.

| Addition | Where | Meaning |
|---|---|---|
| `cells: PanelSection[]` | `PanelSection` | child sections side by side, split by hairlines; a cell can hold cells of its own |
| `rows: string[][]` | `PanelSection` | explicit rows, each centred under the one before it (a hero, then its satellites) |
| `grow: number` | `PanelSection` | the cell's share of its parent's width (default 1) |
| `tone: "band"` | `PanelSection` | draws the section on the inset surface (`--surface-sunken`) to set a group apart |
| `params` optional | `PanelSection` | a section may place its controls through `rows` or `cells` only |
| role and widget `slider` | `Role`, `WidgetKind` | the new horizontal slider (`widgets/Slider.ts`) |
| `columns` on sliders | `PanelSection` | a row made only of sliders stacks them, in `columns` columns (default 1) |
| a grid's `label` | `PanelBuilder` | names the whole group (VOICE), set in ink as a group heading |

**The slider** has the vertical fader's behaviour. Pressing the track jumps to that point and drags;
dragging the thumb moves relative to it, with Shift for fine control. The wheel and arrow keys nudge,
Home and End go to the ends, double-click resets, and clicking the value lets you type one. Stepped
parameters snap to whole steps. A bipolar range fills from zero, a rust tick shows the modulated value,
and the thumb has ARIA `role="slider"`.

**Width:** `resolveLayout` sizes a grid row from its most demanding cell, given each cell's share,
so content is never squeezed.

## Files
| File | What |
|---|---|
| `panel.json` | the proposed GUTTR layout, in the extended schema |
| `lab/apps/studio/src/…` | full copies of the five Studio files as proposed (four pinned files edited, `widgets/Slider.ts` new), at their lab paths |
| `lab.patch` | the same changes as a unified diff against `LAB_PIN`, for review; `git apply --check` is clean on the pin |
| `BASE.json` | SHA-256 of each pinned file the proposal started from; the preview stops if a pin bump changed one |
| `preview.py` | applies the proposal to `build/app-<sha7>/` and serves it, screenshots it, type-checks it, or rewrites `lab.patch` |

## Try it
```sh
python3 tools/wstest.py fetch                              # once
python3 modules/guttr/studio/preview.py                    # http://localhost:5199/, add GUTTR from the library
python3 modules/guttr/studio/preview.py --shot guttr.png   # one render of the card (add --dark for dark mode)
python3 modules/guttr/studio/preview.py --check            # Studio TypeScript check
python3 modules/guttr/studio/preview.py --diff             # after editing lab/…, refresh lab.patch
```
Studio runs with its fake engine, so there is no sound, no audio or MIDI devices, no LAN and no
Electron. If Playwright's pinned browser is missing, point `WIRESCAPE_CHROMIUM` at a Chromium binary
(the same variable as the lab's `playwright.config.ts`). Screenshots are not committed.

## Evidence (lab pin c083b26, 2026-11-27)
- `preview.py --check` passes `tsc --noEmit`, and the lab's `npm run build` (tsc and vite) succeeds.
- `preview.py --shot`, light and dark: the card is 920 × 769 CSS px with zero console errors.
- Interaction, by Playwright on the fake engine:
  - clicking the Throats track at 0, 33, 61 and 97 % gives 1, 3, 5 and 8;
  - dragging the Growl thumb by 60 px gives 0.6;
  - typing 12 into Tremor's value gives 12;
  - VCF, Macro Oscillator, ADSR and Groove cards load beside GUTTR with no errors.
- Lab Studio Playwright specs on the patched Studio: `conditional-controls`, `kit-controls`,
  `macro-panel`, `module-presets`, `quad-vca`, `mixer-output`, `curated`, `review-35` → 43 passed.

## To adopt it
1. The Architect approves or amends the schema and the layout.
2. The Studio change goes to the lab as its own lab task, by the lab's process. This repo never edits
   the lab (AGENTS.md rule 2). A `limitation found` issue is the link.
3. Once a pin includes it, `panel.json` moves to `modules/guttr/panel.json` and this folder is deleted.
   Until then this folder is proposal material, not part of the module: it is not byte-copied at
   promotion.

## Open questions for the Architect
- Should the grid live in `PanelSection` (`cells`, `rows`) as here, or as a separate top-level key?
- Naming: `slider` beside the existing `fader`, or `fader` with an orientation?
- Is `tone: "band"` the right level of styling for an override, given that per-module colour stays
  out of scope?
- Touch density: sliders keep a 16 px track hit height. Should touch get more, as the rails do?

Signed: claude-builder, 2026-11-27
