<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Task pool (any agent)

The owner distributes lettered bundles; no agent is permanently assigned here. This follows
wirescape-headless's task-pool format. Issues hold acceptance criteria and live status.
One task issue, one branch and one PR. A bundle is a sequence of tasks, never a combined implementation PR.

## GUTTR hold rules (owner, 2026-11-27)

- GUTTR is the active first case study. Its existing build owns `modules/guttr/**`; no bundle edits it.
- This queue is wirescape-test only. Lab is held; only lab #708 is excepted by the owner, outside
  this queue. Do not open lab PRs, promote modules, or merge lab changes from these tasks.
  Lab-facing gaps go to `LAB_REQUESTS.md`; link the rows from DESIGN.md.
- `tools/**` and `.github/workflows/**` have one PR at a time, including fixes. PR #19 is merged.
  Every new tooling PR starts from main after #19 and after its preceding tooling task merges.
  Do not stack tooling branches or start later tooling implementations early.
- `LAB_PIN` stays frozen until #18 merges AND the GUTTR PR is open. This overrides the weekly
  bump rule while GUTTR is in progress. A separate bump PR runs T0/T1 on every module including GUTTR.
- T2 runs only when the owner asks. No audio/MIDI devices, LAN or Electron launches in agent runs.
- Module bundles edit only their own folders and their own rows of `modules/README.md`.
  Keep both rows when resolving index conflicts. Top-level planning docs belong to the planning PR,
  not every module PR; tooling documentation belongs to the serial tooling queue.
- Use separate checkouts and build directories per session. Harness runs recreate scratch/app trees;
  never run independent sessions' builds in one checkout.

## Run protocol

1. Read AGENTS.md, README.md, tools/README.md, PROTOTYPING.md, this file and the task issue/spec.
   Check the current open PRs and issue comments before starting.
2. Review another session's eligible PR first when practical. Review the exact head against AGENTS.md;
   finish with `Reviewed <sha7>: ok` or required changes. Squash-merge only with the required review
   and T0/T1 green on that head; docs-only PRs need the independent review.
3. After the owner selects a bundle, record a temporary claim in its next task issue:
   `Claimed: <run-token>; <date>; branch: <branch>`. No permanent assignees or agent names in repo docs.
   A claim is coordination, not permission to ignore a dependency. Before reclaiming an old claim,
   check its PR/branch and record the handoff; do not overwrite active work.
4. Work on one eligible task at a time. Tests first; task ID in PR title, `Closes #N`,
   and the AGENTS.md handoff with tested head and evidence. Each subsequent task gets a new branch
   from eligible main. Module tasks in separate folders can proceed concurrently.
5. Issue comments record `ready`, `in progress`, `review` or `blocked: <dependency>` and the PR link.
   Close after merge. Hand back the claim when stopping. No task is assigned by this document.
6. If a module needs a lab change, add or reuse one row in `LAB_REQUESTS.md` and stop that dependency
   (nothing is filed in the lab unless the owner asks); only a folder-local documented workaround is
   allowed. Never weaken gates.

## Bundles

| Bundle | Tasks in order | Scope | Start condition / benefit |
|---|---|---|---|
| **A — Workshop tools** | #18 → #21 → #22 → #23 → #24 | tools, workflows, tools docs / PROTOTYPING | #19 merged; #18 is ready. Faster renders, panel screenshots and app-build coverage. Strictly serial PRs. |
| **B — Rhythm Lanes** | MOD-6 #25 | modules/rhythm_lanes + index row | Ready; MOD-3 merged. Four rhythmic modulation lanes with probability and Euclidean patterns. |
| **C — Pitch modulation** | MOD-7-chord #26 → MOD-7-quantizer #27 | modules/chord, modules/quantizer + rows | Ready. Cable control of chord choices and quantization. One PR per folder. |
| **D — Rhythm/note controls** | MOD-7-groove #28 → MOD-7-cv_to_notes #29 | modules/groove, modules/cv_to_notes + rows | Ready. Modulated run/reset/mute and note legato decisions. |
| **E — Keyboard/MIDI policy** | MOD-7-host_keyboard #30 → MOD-7-midi_to_cv #31 → MOD-7-midi_in #32 → MOD-7-notes_to_cv #33 | Those four module folders + rows | Ready. Safe performance controls plus explicit exclusions for allocator/filter settings. #32/#33 are audits, not invented flag changes. |
| **F — Optional LFO** | MOD-8 #34 | modules/lfo + row | Held: owner must confirm it is wanted beside Rhythm Lanes. |
| **G — Pin integration** | PIN-next #35 | LAB_PIN and pin evidence/docs | Held: #18 merged and GUTTR PR open. Full-module compatibility evidence, including GUTTR. Coordinate with A; no concurrent tooling/pin changes. |

**Parallel capacity:** A, B, C, D and E have disjoint implementation scopes and can support five sessions,
in addition to the reserved GUTTR build. For three sessions start A/B/C; for four add D; for five add E.
If the available count includes the GUTTR session, reduce that selection by one.
Review each other's PRs in a different session. Module bundles do not wait for later tooling;
they use the tools already merged on main. Any harness gap joins A's serial queue.

Batch A follows lab 0024 §4.6 exactly. In particular, midi_in/channel_filter and
notes_to_cv/voices stay setParam-only; their tasks document and verify why.
Overrides must remain bit-identical without modulation. Each module PR includes presentation.json,
demo, tests, README and DESIGN; panel.json is optional now #19 is merged.

## Current task status

Snapshot: 2026-11-27. Follow issues/PRs for later progress; do not edit this snapshot in every module PR.

| Task | Issue | PR | State | Waiting on |
|---|---|---|---|---|
| REVIEW-19 | #20 | #19 | Done, squash-merged at 0d3ca4b | None; reviewed 98d43b2, T0/T1 green, 31 unit tests pass |
| MOD-T2c | #18 | — | Ready | None; test an MB1-containing lab commit without bumping LAB_PIN |
| TOOL-render | #21 | — | Queued | #18 merged |
| TOOL-shot | #22 | — | Queued | #21 merged |
| CI-app-smoke | #23 | — | Queued | #22 merged |
| CI-windows-app | #24 | — | Queued | #23 merged |
| MOD-6 | #25 | — | Ready | None |
| MOD-7-chord | #26 | — | Ready | None |
| MOD-7-quantizer | #27 | — | Ready in C | C's preceding task completed |
| MOD-7-groove | #28 | — | Ready | None |
| MOD-7-cv_to_notes | #29 | — | Ready in D | D's preceding task completed |
| MOD-7-host_keyboard | #30 | — | Ready | None |
| MOD-7-midi_to_cv | #31 | — | Ready in E | E's preceding task completed |
| MOD-7-midi_in | #32 | — | Audit in E | E's preceding task completed |
| MOD-7-notes_to_cv | #33 | — | Audit in E | E's preceding task completed |
| MOD-8 | #34 | — | Held | Explicit owner decision |
| PIN-next | #35 | — | Held | #18 merged + open GUTTR PR; coordinate tooling lock |

Within C/D/E, ordering sizes a run and avoids accidental duplicate pickup; it is not a code dependency.
The owner may split a bundle by individual issue if more parallelism is useful, keeping one folder
per task and the same review rules. Batch B (MOD-9) stays later, after MOD-7; it is not released here.

## Pin integration details

#18 implements A106 exactly: build scratch pin's own wire-describe before generator --describe;
only the two specified inventory paths join the strict allowlist; no UI/S0 or equality gate is waived.
Test against an MB1-containing commit using isolated scratch configuration, leaving LAB_PIN unchanged.

#35 is a separate PR after both prerequisites. If GUTTR is not on main, validate the open PR's exact
module snapshot with the proposed pin in an isolated integration checkout, without changing its source;
record both commits. T0/T1 must cover every module including GUTTR before merging the bump.
Coordinate failures with the active GUTTR build; do not repair its folder from this bundle.
