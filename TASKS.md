<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Task pool (any agent)

Short lettered bundles (owner, 2026-11-27).
- **No agent is assigned a bundle until a session claims it.** Each bundle is one session, at most three
  tasks and **one open PR at a time**.
- Only the bundles below are open. The owner can stop after any bundle and re-plan.
- Issues hold the acceptance criteria and live status.
- One task issue, one branch and one PR. A bundle is a sequence of tasks, never a combined PR.

## Rules

1. **Read first:** AGENTS.md, README.md, tools/README.md, PROTOTYPING.md, this file and the task issue.
   Check open PRs and the issue's last comments.
2. **Review first when practical.** Review another session's eligible PR before you start (AGENTS.md
   checklist, exact head, `Reviewed <sha7>: ok`). Squash-merge needs that review and T0 and T1 green on
   the head; docs-only PRs need only the review.
3. **Claim.** Comment `Claimed: <session>; <date>; branch: <branch>` on the bundle's next open task issue.
   - Before taking over an old claim, check its branch and PR and record the handoff. Never overwrite
     active work.
   - No permanent assignees or agent names in repo docs.
4. **Work** one task at a time, tests first. Put the task ID in the PR title and `Closes #N` in the body,
   and add the AGENTS.md handoff with the tested head and evidence. The next task starts from current main.
5. **Serial paths:** `tools/**`, `.github/workflows/**` and `LAB_PIN` take one PR at a time across all
   bundles. Module bundles edit only their own folders and their own `modules/README.md` rows.
6. **Lab gaps:** a lab gap gets a `LAB_REQUESTS.md` row (reuse one if it exists). Nothing is filed in the
   lab unless the owner asks. Never weaken a gate.
7. **Stopping:** comment `handoff-ready` or `blocked: <dependency>` on the issue and remove your claim.
   Close the issue after merge.

## Bundles

| Bundle | Tasks in order | Scope | Start condition |
|---|---|---|---|
| **G: pin integration** | PIN-next #35 → finish chord #26 (PR #40) → finish midi_to_cv #31 (PR #47) | `LAB_PIN` and pin evidence; then the two existing module PRs | **Lab PR [#724](https://github.com/kalharbi11/wirescape-lab/pull/724) merged** (fixes LR-11 and LR-12). Bump to a lab main commit that contains it. |
| **H: batch A finish** | MOD-7-quantizer #27 → MOD-7-midi_in audit #32 → MOD-7-notes_to_cv audit #33 | those three folders and their rows | Ready now. No lab dependency. |
| **A: workshop CI** | CI-app-smoke #23 → CI-windows-app #24 | `tools/**`, workflows, tools docs | Ready now. Never in parallel with G's pin PR: whichever opens first goes first. |

**G details.**
- The bump picks up lab MB1 (first MB1 pin, with the #18 compatibility), the #683 fixes for LR-9 and
  LR-10, and the #724 fixes.
- Run T0 and T1 on every module, including GUTTR.
- Check that the five Studio overlay BASE files still match at the new pin. If one changed, rebase the
  overlay in A's serial queue; never refresh hashes blindly.
- Then rebase #40 and #47 on the new main and re-run their exact-head T0/T1. If their original sessions
  are idle, record the handoff on the PR.
- #47's active MIDI render coverage waits for lab #722 (LR-13). The idle demo is acceptable until then.
  Keep this listed in its DESIGN.md "Limitations".

Overrides stay bit-identical without modulation (`wstest identical`). Each module PR includes
presentation.json, a demo, tests, README and DESIGN. `panel.json` is optional.
`midi_in/channel_filter` and `notes_to_cv/voices` stay setParam-only (lab 0024 §4.6). Their audits
document and verify why.

**Later, not open:** MOD-9 batch B (delay, reverb, quad_vca, chaos_lite, spectral). Promotions to the
lab wait for lab MB2 (lab bundle M).

## Status snapshot (2026-11-27)

| Task | Issue | PR | State |
|---|---|---|---|
| GUTTR | — | #38 | Merged `3cc1b4b`; owner listening is next |
| MOD-T2c, TOOL-render, TOOL-shot | #18, #21, #22 | #41, #52, overlay #54 | Merged |
| MOD-6, MOD-7 groove / cv_to_notes / host_keyboard, MOD-8 | #25, #28, #29, #30, #34 | #42, #43, #55, #44, #50 | Merged |
| MOD-7-chord | #26 | #40 | blocked: LR-11 → lab #724 (bundle G) |
| MOD-7-midi_to_cv | #31 | #47 (draft) | blocked: LR-12 → lab #724 (bundle G) |
| PIN-next | #35 | — | Waits on lab #724 (bundle G) |
| MOD-7-quantizer, midi_in, notes_to_cv | #27, #32, #33 | — | Ready (bundle H) |
| CI-app-smoke, CI-windows-app | #23, #24 | — | Ready (bundle A) |

Signed: claude-architect, 2026-11-27
