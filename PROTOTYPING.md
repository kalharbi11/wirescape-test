<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Prototyping a module

Three local loops, from fastest to most real. None of them touches the lab: every command works in a
scratch copy of the pinned lab under `build/` (git-ignored). Devices stay off unless you pass `--launch`.

## One-time setup
Python 3.10+, CMake, Ninja, a C++17 compiler (MSVC on Windows), git, and Node 20+ (loops b and c).
```sh
python tools/wstest.py fetch      # the pinned lab into .lab/ (add --git-deps behind a proxy)
```

## The loops
**a. Sound, offline.** `python tools/wstest.py t0 <name>` builds, runs the tests and renders every
`modules/<name>/demos/*.json` to WAVs in `build/wstest-out/release/<name>/`. Open them in any player.

**b. The real app.** `python tools/wstest.py app <name>... --launch`
Builds the engine (`wirescape-engine`), Studio and the Electron desktop app from the scratch copy, with your
module registered and its `panel.json` applied, then starts the app. After a DSP edit, close the app and run
the command again; only changed files rebuild. Without `--launch` it prints the exact launch command.
The app can open your audio device: only you pass `--launch`; agents and CI never do.

**c. UI only, no C++ build.** `python tools/wstest.py ui <name>...` then open the printed address
(`--port N`, default 5173). In a plain browser Studio uses its fake engine, so you see the module's panel with
no sound. Edit `modules/<name>/panel.json` or `presentation.json`, save, and the command prints
`updated — reload the browser`. Ctrl+C stops it. The module needs a `presentation.json` to be in the catalogue.

`npm ci` runs on the first use and again only when the lab's `package-lock.json` changes.

## `panel.json`
Optional, one per module, next to `presentation.json`. It is Studio's `PanelOverride` plus the SPDX key.
Unknown keys are refused. Every key is optional; a module with no panel file still gets a full panel.

| Key | Meaning |
|---|---|
| `width` | card width |
| `code` | header code, e.g. `VCF` (derived from the id when absent) |
| `layout` | `"tall"` or `"wide"` |
| `sections` | list of `{label, hero, columns, params}`: `params` are parameter ids placed in that section; `columns` = controls per row; `hero` is a legacy flag |
| `short` | `{param: "shorter label"}` for cramped controls |
| `roles` | `{param: role}` widget role (preferred over `widgets`) |
| `widgets` | `{param: widget kind}` forced widget (older overrides) |
| `panes` | display panes to show (pane kinds from `layouts.ts`) |
| `paneOptions` | free-form options per pane, e.g. which parameters a filter display reads |
| `hidden` | parameters left off the card (still saved and modulatable) |
| `modPorts` | modulatable parameters that always get a node on the left rail (default: hero + primary, at most 2) |
| `ports` | per side `input`/`output`: `{shown, priority, assign: "cc"}` |

```json
{
  "SPDX-License-Identifier": "GPL-3.0-only",
  "width": 6,
  "layout": "wide",
  "sections": [
    {"label": "Macros", "columns": 4, "params": ["macro1", "macro2", "macro3", "macro4"]},
    {"label": "Range", "params": ["depth"]}
  ],
  "hidden": ["debug_mode"]
}
```
The id is the one in `dsp.h`; the file may omit `id`, and if it has one it must match.

## Not possible yet
- Per-module colours or backgrounds, and custom HTML panels.
- Presets: they are hard-coded in Studio (`src/app/modulePresets.ts`).
- Live module telemetry in the panel.
- Parameters you do not place in a section still appear, under "MORE". `hidden` removes one from the card
  but keeps it saved and modulatable.
- Anything that needs a Studio or engine change is a lab issue labelled `limitation found` (AGENTS.md rule 2).

## Where files go
`build/app-<sha7>/` is the app copy (pin + your folders + registration + `presentation.json` + panel files).
It is separate from the T0 tree `build/scratch-<sha7>/`, which never receives panel files. The engine builds
in `build/wstest-<sha7>/app/`. Your `modules/` folders are the only files you edit.
