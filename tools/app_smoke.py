# SPDX-License-Identifier: GPL-3.0-only
"""CI panel smoke: reuse wstest, never launch Electron or access devices."""
import os
from pathlib import Path
import subprocess
import sys

import wstest


def smoke(modules: list[str], root: Path = wstest.ROOT, run=subprocess.run) -> None:
    for name in modules:
        if not wstest.NAME_RE.fullmatch(name) or not (root / "modules" / name / "module.cpp").is_file():
            raise ValueError(f"invalid scoped module: {name}")
    browser_installed = False
    for name in modules:
        if not (root / "modules" / name / wstest.PANEL_FILE).is_file():
            print(f"app smoke: {name} skipped (no panel.json)", flush=True)
            continue
        print(f"app smoke: {name} (build only; fake-engine screenshot)", flush=True)
        command = [sys.executable, str(root / "tools" / "wstest.py")]
        run(command + ["ui", name, "--no-serve"], cwd=root, check=True)
        run(command + ["app", name], cwd=root, check=True,
            env={**os.environ, "ELECTRON_SKIP_BINARY_DOWNLOAD": "1"})
        if not browser_installed:
            studio = root / "build" / f"app-{wstest.read_pin(root)[1][:7]}" / "apps" / "studio"
            run(["npx", "playwright", "install", "--with-deps", "chromium"], cwd=studio, check=True)
            browser_installed = True
        run(command + ["shot", name, "--out", str(root / "build" / "shots" / f"{name}.png")],
            cwd=root, check=True)
    if not browser_installed:
        print("app smoke: no scoped panel modules; nothing to build", flush=True)


if __name__ == "__main__":
    smoke(sys.argv[1:])
