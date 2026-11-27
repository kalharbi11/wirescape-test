#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Preview the GUTTR Studio proposal: the pinned lab's Studio plus the files under lab/.

Runs from the repository root, standard library only. It never edits the lab: it lets
`wstest ui guttr --no-serve` prepare the git-ignored app copy build/app-<sha7>/, then copies
the proposal's Studio files and studio/panel.json into that copy.

  python3 modules/guttr/studio/preview.py                 # serve on http://localhost:5199/
  python3 modules/guttr/studio/preview.py --shot out.png  # render the card once and exit
  python3 modules/guttr/studio/preview.py --check         # TypeScript check of the copy
  python3 modules/guttr/studio/preview.py --diff          # rewrite lab.patch against the pin

Every proposal file replaces a pinned file of the same path (or adds one). BASE.json holds
the SHA-256 of each pinned file the proposal started from, so a pin bump that touches one of
them stops here instead of silently previewing a stale merge.
"""
from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
OVERLAY = HERE / "lab"
STUDIO = "apps/studio"


def pin() -> str:
    for line in (ROOT / "LAB_PIN").read_text().splitlines():
        if line.startswith("commit="):
            return line.split("=", 1)[1].strip()
    sys.exit("preview: LAB_PIN has no commit= line")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def overlay_files() -> list[str]:
    return sorted(p.relative_to(OVERLAY).as_posix() for p in OVERLAY.rglob("*") if p.is_file())


def check_base(lab: Path) -> None:
    base = json.loads((HERE / "BASE.json").read_text())
    if base["lab_commit"] != pin():
        print(f"preview: note: proposal written against lab {base['lab_commit'][:7]}, pin is {pin()[:7]}")
    stale = [rel for rel, digest in base["files"].items()
             if digest and (not (lab / rel).is_file() or sha256(lab / rel) != digest)]
    if stale:
        sys.exit("preview: the pinned lab changed files this proposal replaces; rebase them first:\n  "
                 + "\n  ".join(stale))


def write_patch(lab: Path) -> None:
    out = []
    for rel in overlay_files():
        old = (lab / rel).read_text().splitlines(keepends=True) if (lab / rel).is_file() else []
        new = (OVERLAY / rel).read_text().splitlines(keepends=True)
        out += difflib.unified_diff(old, new, f"a/{rel}" if old else "/dev/null", f"b/{rel}")
    header = "SPDX-License-Identifier: GPL-3.0-only\nGUTTR Studio proposal against the lab at LAB_PIN; see README.md.\n\n"
    (HERE / "lab.patch").write_text(header + "".join(out))
    print(f"preview: wrote {(HERE / 'lab.patch').relative_to(ROOT)} ({len(out)} lines)")


def prepare() -> Path:
    r = subprocess.run([sys.executable, "tools/wstest.py", "ui", "guttr", "--no-serve"], cwd=ROOT)
    if r.returncode:
        sys.exit(r.returncode)
    app = ROOT / "build" / f"app-{pin()[:7]}"
    for rel in overlay_files():
        dest = app / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(OVERLAY / rel, dest)
    panel = json.loads((HERE / "panel.json").read_text())
    panel.pop("SPDX-License-Identifier", None)
    (app / STUDIO / "public/panels/guttr.json").write_text(json.dumps(panel, indent=2) + "\n")
    print(f"preview: proposal applied to {app.relative_to(ROOT)}/ ({len(overlay_files())} Studio files + panel)")
    return app / STUDIO


SHOT_JS = r"""
const { chromium } = require('@playwright/test');
(async () => {
  const [url, out, theme] = process.argv.slice(2);
  const exe = process.env.WIRESCAPE_CHROMIUM;
  const browser = await chromium.launch(exe ? { executablePath: exe } : {});
  const page = await browser.newPage({ viewport: { width: 1800, height: 1600 }, deviceScaleFactor: 2});
  const errors = [];
  page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
  await page.goto(url);
  await page.waitForSelector('body[data-ready="true"]', { timeout: 60000 });
  if (theme === 'dark') await page.evaluate(() => { document.documentElement.dataset.theme = 'dark'; });
  await page.evaluate(() => window.__wirescapeTest.addModule('guttr'));
  await page.evaluate(() => window.__wirescapeTest.zoomTo(1));
  await page.waitForTimeout(800);
  const mods = (await page.evaluate(() => window.__wirescapeTest.state())).modules;
  const id = Array.isArray(mods) ? mods[mods.length - 1].id : Object.keys(mods).pop();
  const card = page.getByTestId('module-' + id);
  const box = await card.boundingBox();
  await card.screenshot({ path: out });
  console.log(`card ${Math.round(box.width)} x ${Math.round(box.height)} CSS px, console errors: ${errors.length}`);
  for (const e of errors) console.log('  ' + e);
  await browser.close();
  process.exit(errors.length ? 1 : 0);
})();
"""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=5199)
    ap.add_argument("--shot", metavar="PNG", help="render the GUTTR card to this PNG and exit")
    ap.add_argument("--dark", action="store_true", help="with --shot: dark colour scheme")
    ap.add_argument("--check", action="store_true", help="run the Studio TypeScript check and exit")
    ap.add_argument("--diff", action="store_true", help="rewrite lab.patch against the pinned lab and exit")
    args = ap.parse_args()

    lab = ROOT / ".lab" / pin()
    if not lab.is_dir():
        sys.exit("preview: run `python3 tools/wstest.py fetch` first")
    check_base(lab)
    if args.diff:
        write_patch(lab)
        return 0
    studio = prepare()
    npx = shutil.which("npx") or "npx"
    if args.check:
        return subprocess.run([npx, "tsc", "--noEmit", "-p", "."], cwd=studio).returncode
    server = [npx, "vite", "--port", str(args.port), "--strictPort"]
    if not args.shot:
        print(f"preview: http://localhost:{args.port}/  (add GUTTR from the library; Ctrl+C stops)")
        return subprocess.run(server, cwd=studio).returncode
    proc = subprocess.Popen(server, cwd=studio, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    try:
        time.sleep(4)
        script = studio / "node_modules" / ".guttr-preview-shot.cjs"
        script.write_text(SHOT_JS)
        shot = str(Path(args.shot).resolve())
        r = subprocess.run(["node", str(script), f"http://localhost:{args.port}/", shot,
                            "dark" if args.dark else "light"], cwd=studio, env=os.environ)
        return r.returncode
    finally:
        proc.terminate()


if __name__ == "__main__":
    sys.exit(main())
