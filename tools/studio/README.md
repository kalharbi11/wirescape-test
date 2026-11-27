<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Workshop Studio overlay

A set of edited Studio files that `wstest ui`, `wstest app` and `wstest shot` copy into the git-ignored app
tree `build/app-<sha7>/`, so a module's `panel.json` can use a richer layout in the real app now. Ruling:
Architect comment on PR #48, §4a.

**Workshop-only.** It is not part of the lab and not part of any module. The lab is never edited from here
(AGENTS.md rule 2). The Studio change goes to the lab as its own lab task only when the owner asks for it;
until then a `panel.json` that uses these fields renders as designed only through this overlay.

## What it adds
All additions are optional, so panels that do not use them render exactly as before.

| Addition | Where | Meaning |
|---|---|---|
| `cells: PanelSection[]` | `PanelSection` | child sections side by side, split by hairlines; a cell can hold cells of its own |
| `rows: string[][]` | `PanelSection` | explicit rows, each centred under the one before it (a hero, then its satellites) |
| `grow: number` | `PanelSection` | the cell's share of its parent's width (default 1) |
| `tone: "band"` | `PanelSection` | draws the section on the inset surface (`--surface-sunken`); a semantic token, not a colour |
| `params` optional | `PanelSection` | a section may place its controls through `rows` or `cells` only |
| `slider` | `Role`, `WidgetKind` | a horizontal slider (`widgets/Slider.ts`); `columns` stacks a row of sliders |
| `ports.input.params: string[]` | `RailOptions` | more modulatable parameters that can get a rail node, revealed one at a time by a **− n/N +** control |
| `ui.paramsShown` | patch state | how many of them are shown. Display state only: it never changes sound |

A modulated parameter keeps its node whatever the count, and only modulatable IDs are accepted.

## Files
| File | What |
|---|---|
| `apps/studio/src/…` | full copies of the edited Studio files (and the new `widgets/Slider.ts`), at their lab-relative paths |
| `BASE.json` | SHA-256 of each pinned file the copies started from (`null` for a new file) |
| `lab.patch` | the same changes as a unified diff against `LAB_PIN`, generated for review (`git apply --check` is clean on the pin) |

## Use
```sh
python tools/wstest.py ui <name>             # the overlay is applied after the app tree is prepared
python tools/wstest.py app <name>            # same tree, then the engine and desktop build
python tools/wstest.py shot <name> [--dark]  # one render of the card
python tools/wstest.py ui <name> --check     # Studio type-check (tsc --noEmit)
python tools/wstest.py studio-diff           # after editing the copies: regenerate lab.patch
```
It is applied to the app tree only, never to the T0/T1/T2 scratch tree. See PROTOTYPING.md for editing it
and for what a pin bump does.
