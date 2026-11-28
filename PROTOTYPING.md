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
**a. Sound, offline.** `python tools/wstest.py render <name>` is the fast change-and-listen loop.
It registers the module, builds only the renderer (and the MB1 descriptor prerequisite where required),
and renders every demo to `build/wstest-out/render/<name>/<demo-stem>/audio.wav`, printing peak
and finite status. Use `--demo <file.json>` to select one, and `--rate 44100 --block 48` to override
the demo settings. Open the WAV in your player; the harness never opens devices.
This preview skips test-tier assertions. Run `python tools/wstest.py t0 <name>` for the full checks
and evidence renders under `build/wstest-out/release/<name>/`.

**b. The real app.** `python tools/wstest.py app <name>... --launch`
Builds the engine (`wirescape-engine`), Studio and the Electron desktop app from the scratch copy, with your
module registered and its `panel.json` applied, then starts the app. After a DSP edit, close the app and run
the command again; only changed files rebuild. Without `--launch` it prints the exact launch command.
The app can open your audio device: only you pass `--launch`; agents and CI never do.

**c. UI only, no engine build.** `python tools/wstest.py ui <name>...` then open the printed address
(`--port N`, default 5173). In a plain browser Studio uses its fake engine, so you see the module's panel with
no sound. Edit `modules/<name>/panel.json` or `presentation.json`, save, and the command prints
`updated — reload the browser`. Ctrl+C stops it. The module needs a `presentation.json` to be in the catalogue.

On MB1-containing pins, scratch preparation (including UI refresh) builds the scratch's own
`wire-describe` before regenerating the catalogue (A106). It does not build or launch the engine.
Older pins retain the no-C++-build UI path.

`npm ci` runs on the first use and again only when the lab's `package-lock.json` changes.

## The workshop Studio overlay
`tools/studio/` holds edited Studio files (grid `cells`/`rows`/`grow`, `tone: "band"`, the `slider` widget,
`ports.input.params` with `ui.paramsShown`; see its README). `ui`, `app` and `shot` copy them over the app
copy `build/app-<sha7>/` after it is prepared and before Studio is served or built. The T0/T1/T2 tree
`build/scratch-<sha7>/` never gets them. They are workshop-only until the owner asks for the lab task.

- **Edit it:** change the files under `tools/studio/apps/studio/src/`, then run `python tools/wstest.py
  studio-diff` to regenerate `lab.patch` for review. While `ui` runs, saving an overlay file copies it into the
  app copy (`updated — reload the browser`).
- **Check it:** `ui <name> --check` type-checks Studio; `shot <name> [--dark] [--out f.png]` renders the
  module's card (devices off; set `WIRESCAPE_CHROMIUM` if Playwright's browser is missing). Screenshots are
  not committed.
- **Pin bump:** `tools/studio/BASE.json` holds the SHA-256 of each pinned file the copies replace. If a bump
  changes one, `ui`, `app` and `shot` refuse and name the file. Re-apply the overlay's edits on the new file,
  then run `studio-diff --update-base` (rewrites `BASE.json` from the new pin and regenerates `lab.patch`).
  A pin bump that touches none of them needs nothing. During the MB2 transition, reviewed exact-digest
  alternatives in `tools/studio/variants/` preserve both supported bases; edit the selected copy and run
  `studio-diff` for that pin. `--update-base` refuses while alternatives exist (see the overlay README).

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
- Anything that needs a Studio or engine change beyond the workshop overlay is recorded in `LAB_REQUESTS.md` (AGENTS.md rule 2); file in the lab only on owner request.

## Where files go
`build/app-<sha7>/` is the app copy (pin + your folders + registration + `presentation.json` + panel files).
It is separate from the T0 tree `build/scratch-<sha7>/`, which never receives panel files. The engine builds
in `build/wstest-<sha7>/app/`. Your `modules/` folders are the only files you edit.
