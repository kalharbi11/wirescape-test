#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""wstest: build and test wirescape-test module folders against the pinned lab commit.

Design: lab proposals/0024 §2.2-§2.4 (Planner run 2a). Python 3.10+, standard library only.
Devices stay off: nothing here opens audio, MIDI or a LAN socket.

  fetch                      the lab at LAB_PIN into .lab/<sha>/ (never another commit)
  build <module>...          scratch lab copy + registration + build of the T0 targets
  t0 <module>...             check, build, tests, demo renders, CPU probe, identical (overrides)
  identical <module>...      overrides: byte-identical renders against a pristine build of the pin
  t2 <module>                full lab build and CTest, demo matrix, callgrind, Cortex-M7 (advisory)
  selftest                   T0 on tools/fixtures; outcomes must match tools/fixtures/expected.json
  scope                      CI: which modules a change touches (and whether everything runs)
  app <module>...            local convenience (not a test tier): scratch lab copy with panel.json
                             files, engine + Studio + desktop build; --launch starts the real app
  ui <module>...             local convenience: Studio dev server in a browser (fake engine), with
                             panel.json / presentation.json save-and-reload

A <module> is a folder name under modules/ (else under tools/fixtures/), or a path to a module folder.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import os
import platform
import re
import shlex
import shutil
import signal
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NAME_RE = re.compile(r"^[a-z][a-z0-9_]*$")
REGISTER_RE = re.compile(r"^[ \t]*WS_REGISTER_MODULE[ \t]*\(([^)]*)\)", re.M)
CATALOG_INC = "engine/control/module_catalog.inc"
CATALOG_H = "engine/control/module_catalog.h"
ENGINE_CMAKE = "engine/CMakeLists.txt"
# 0024 §9.1: a module's presentation.json goes into the scratch sidecar; the pin's generator then runs.
SIDECAR = "apps/studio/public/catalogue-presentation.json"
UI_CATALOGUE = "apps/studio/public/wirescape_modules.json"
UI_GENERATOR = "tools/gen_ui_catalogue.py"
S0_FIXTURE = "host/sidecar/testdata/s0_catalogue.ndjson"
PANELS_DIR = "apps/studio/public/panels"
PANEL_FILE = "panel.json"
# Keys of Studio's PanelOverride (apps/studio/src/panels/layouts.ts) plus the licence key.
PANEL_KEYS = ("width", "code", "layout", "sections", "short", "roles", "widgets", "panes",
              "paneOptions", "hidden", "modPorts", "ports")
APP_KEEP = ("apps/studio/node_modules", "apps/studio/dist", "apps/desktop/node_modules",
            "apps/desktop/build")  # survive an app-tree refresh so npm and Vite stay incremental

INFO_RE = re.compile(r'ws::Info\s+info\s*\{\s*"([^"]+)"')
T0_TARGETS = ("wirescape_render", "abi2_module_cpu_probe")
SANITIZE_FLAGS = "-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"
PROBE = {"instances": 2, "rate": 48000, "block": 128}
T2_RATES = (44100, 48000, 96000)
T2_BLOCKS = (16, 17, 48, 128, 512)  # 0024 §9.2: 48 is a non-power-of-two the renderer accepts
FEEDBACK_BLOCK_ISSUE = "lab #663"  # a patch with no feedback_block cannot prepare at max_block < 32
M7_FLAGS = ["-std=c++17", "-mcpu=cortex-m7", "-mthumb", "-mfpu=fpv5-d16", "-mfloat-abi=hard",
            "-fno-exceptions", "-fno-rtti", "-Os", "-ffunction-sections", "-fdata-sections",
            "-DWS_SIMD_SCALAR"]
EXE = ".exe" if os.name == "nt" else ""


class Refused(Exception):
    """The harness refuses to continue; the message names why."""


class StepFailed(Exception):
    def __init__(self, step: str, detail: str):
        super().__init__(f"{step}: {detail}")
        self.step, self.detail = step, detail


# --------------------------------------------------------------------------- utilities

def log(message: str) -> None:
    print(f"[wstest] {message}", flush=True)


def run(cmd: list[str], cwd: Path | None = None, timeout: float = 3600, env: dict | None = None,
        capture: bool = True) -> subprocess.CompletedProcess:
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
    if env:
        environment.update(env)
    return subprocess.run([str(c) for c in cmd], cwd=str(cwd) if cwd else None, env=environment,
                          capture_output=capture, text=True, timeout=timeout, check=False)


def tail(text: str, lines: int = 40) -> str:
    return "\n".join((text or "").strip().splitlines()[-lines:])


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_pin(root: Path = ROOT) -> tuple[str, str]:
    values = {}
    for line in (root / "LAB_PIN").read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#") and "=" in line:
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip()
    repo, commit = values.get("repo", ""), values.get("commit", "")
    if not re.fullmatch(r"[\w.-]+/[\w.-]+", repo) or not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise Refused("LAB_PIN needs repo=<owner>/<name> and a full 40-hex commit=")
    return repo, commit


# --------------------------------------------------------------------------- modules

@dataclass
class Module:
    name: str
    folder: Path
    kind: str = "new"            # "new" or "override"
    cls: str = ""                # class from WS_REGISTER_MODULE
    module_id: str | None = None
    steps: list = field(default_factory=list)
    failed: str | None = None

    def record(self, step: str, status: str, seconds: float, detail: str = "", **extra) -> None:
        self.steps.append({"step": step, "status": status, "seconds": round(seconds, 2),
                           "detail": detail, **extra})
        if status == "fail" and self.failed is None:
            self.failed = step


def resolve_module(arg: str, root: Path = ROOT) -> Module:
    candidate = root / "modules" / arg
    fixture = root / "tools" / "fixtures" / arg
    if NAME_RE.match(arg) and not candidate.is_dir() and fixture.is_dir():
        candidate = fixture  # a harness fixture by bare name (e.g. a T2 dispatch with "gain")
    folder = candidate if NAME_RE.match(arg) and candidate.is_dir() else Path(arg)
    if not folder.is_absolute():
        folder = (Path.cwd() / folder) if (Path.cwd() / folder).is_dir() else (root / folder)
    folder = folder.resolve()
    if not folder.is_dir():
        raise Refused(f"module folder not found: {arg}")
    if not NAME_RE.match(folder.name):
        raise Refused(f"module folder name must match {NAME_RE.pattern}: {folder.name}")
    return Module(folder.name, folder)


def registration_class(folder: Path) -> str:
    """The class named by the folder's single WS_REGISTER_MODULE line; zero or two refuse."""
    found = []
    for path in sorted(folder.rglob("*")):
        if path.is_file() and path.suffix in (".cpp", ".h", ".hpp", ".cc"):
            text = re.sub(r"/\*.*?\*/|//[^\n]*", "", path.read_text(encoding="utf-8", errors="replace"),
                          flags=re.S)
            found += [(path.name, m.group(1).strip()) for m in REGISTER_RE.finditer(text)]
    if len(found) != 1:
        where = ", ".join(f"{f}:{c}" for f, c in found) or "none"
        raise Refused(f"{folder.name}: exactly one WS_REGISTER_MODULE line is required, found "
                      f"{len(found)} ({where})")
    cls = found[0][1]
    if not re.fullmatch(r"[A-Za-z_]\w*(::[A-Za-z_]\w*)*", cls):
        raise Refused(f"{folder.name}: WS_REGISTER_MODULE names an invalid class: {cls!r}")
    return cls


def folder_files(folder: Path) -> dict[str, Path]:
    files = {}
    for path in sorted(folder.rglob("*")):
        if path.is_symlink():
            raise Refused(f"{folder.name}: symbolic links are not allowed in a module folder: {path.name}")
        if path.is_file() and "__pycache__" not in path.parts:
            rel = path.relative_to(folder).as_posix()
            if rel != PANEL_FILE:  # Studio layout, never a module file (it goes to panels/ in the app tree)
                files[rel] = path
    return files


# --------------------------------------------------------------------------- the pinned lab

class Lab:
    def __init__(self, root: Path = ROOT):
        self.root = root
        self.repo, self.sha = read_pin(root)
        self.sha7 = self.sha[:7]
        self.dir = root / ".lab" / self.sha

    def git(self, *args: str, cwd: Path | None = None, check: bool = True) -> str:
        result = run(["git", *args], cwd=cwd or self.dir, timeout=600)
        if check and result.returncode:
            raise Refused(f"git {' '.join(args)} failed: {tail(result.stderr, 5)}")
        return result.stdout

    def fetch(self, git_deps: bool = False) -> None:
        if not (self.dir / ".git").is_dir():
            if self.dir.exists():
                shutil.rmtree(self.dir)
            self.dir.mkdir(parents=True)
            url = os.environ.get("WSTEST_LAB_URL", f"https://github.com/{self.repo}.git")
            log(f"fetching {self.repo}@{self.sha7} into .lab/{self.sha}/")
            self.git("init", "-q")
            self.git("-c", "core.autocrlf=false", "fetch", "-q", "--depth", "1", url, self.sha)
            self.git("-c", "core.autocrlf=false", "checkout", "-q", "--detach", "FETCH_HEAD")
        self.verify()
        if git_deps and not (self.dir / "build/deps-cache/fetchcontent.cmake").is_file():
            self.git_deps()

    def verify(self) -> None:
        if not (self.dir / ".git").is_dir():
            raise Refused(f"the lab is not fetched; run: python tools/wstest.py fetch")
        head = self.git("rev-parse", "HEAD").strip()
        if head != self.sha:
            raise Refused(f".lab/{self.sha} is at {head}, not the pinned commit; delete it and fetch")
        dirty = self.git("status", "--porcelain", "--untracked-files=no").strip()
        if dirty:
            raise Refused(f".lab/{self.sha} has modified tracked files; delete it and fetch:\n{dirty}")

    def git_deps(self) -> None:
        """The lab's own fallback for proxies that block codeload (tools/cloud/git_deps.py)."""
        script = self.dir / "tools/cloud/git_deps.py"
        if not script.is_file():
            raise Refused("the pinned lab has no tools/cloud/git_deps.py")
        result = run([sys.executable, str(script), "--force"], cwd=self.dir, timeout=1800)
        if result.returncode:
            raise Refused("git_deps.py failed:\n" + tail(result.stdout + result.stderr))
        log("dependency cache ready (lab tools/cloud/git_deps.py)")

    def tracked(self) -> list[str]:
        return [p for p in self.git("ls-files", "-z").split("\0") if p]

    @property
    def pre_mb2(self) -> bool:
        return (self.dir / CATALOG_INC).is_file()

    def cache_init(self, build_dir: Path) -> Path | None:
        """Per-build copy of the lab's git_deps cache script, pointed at this build directory."""
        source = self.dir / "build/deps-cache/fetchcontent.cmake"
        if not source.is_file():
            return None
        text = source.read_text(encoding="utf-8")
        for form in {str(self.dir / "build/native"), (self.dir / "build/native").as_posix()}:
            text = text.replace(form, build_dir.as_posix())
        target = build_dir.parent / f"{build_dir.name}.deps-cache.cmake"
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8")
        return target


# --------------------------------------------------------------------------- scratch tree (§2.3)

def registration_edits(lab_dir: Path, modules: list[Module]) -> dict[str, str]:
    """The only edits allowed in a scratch tree, keyed by lab path: three per new module, pre-MB2."""
    new = [m for m in modules if m.kind == "new"]
    if not new or not (lab_dir / CATALOG_INC).is_file():
        return {}
    cmake = (lab_dir / ENGINE_CMAKE).read_text(encoding="utf-8")
    header = (lab_dir / CATALOG_H).read_text(encoding="utf-8")
    inc = (lab_dir / CATALOG_INC).read_text(encoding="utf-8")
    newline = "\r\n" if "\r\n" in header else "\n"
    lines = header.split(newline)
    last = max((i for i, line in enumerate(lines) if line.startswith('#include "../../modules/')),
               default=None)
    if last is None:
        raise Refused(f"{CATALOG_H} has no module #include line to follow")
    lines[last + 1:last + 1] = [f'#include "../../modules/{m.name}/dsp.h"' for m in new]
    def append(text: str, extra: list[str]) -> str:
        nl = "\r\n" if "\r\n" in text else "\n"
        return (text if text.endswith(nl) else text + nl) + nl.join(extra) + nl
    return {
        ENGINE_CMAKE: append(cmake, [f"add_subdirectory(../modules/{m.name} {m.name})" for m in new]),
        CATALOG_H: newline.join(lines),
        CATALOG_INC: append(inc, [f"WS_APP_MODULE({m.cls})" for m in new]),
    }


def module_info_id(folder: Path) -> str | None:
    found = INFO_RE.search((folder / "dsp.h").read_text(encoding="utf-8")) if (folder / "dsp.h").is_file() else None
    return found.group(1) if found else None


def presentation_edit(lab_dir: Path, modules: list[Module]) -> dict[str, str]:
    """0024 §9.1 rule 2: each module's presentation.json object, inserted into the scratch sidecar
    (appended for a new module, replacing the same id for an override). Empty without any."""
    shipped = [m for m in modules if (m.folder / "presentation.json").is_file()]
    if not shipped:
        return {}
    if not (lab_dir / SIDECAR).is_file():
        raise Refused(f"the pin has no {SIDECAR} for presentation.json (0024 §9.1)")
    sidecar = json.loads((lab_dir / SIDECAR).read_text(encoding="utf-8"))
    entries = sidecar["modules"]
    for m in shipped:
        try:
            entry = json.loads((m.folder / "presentation.json").read_text(encoding="utf-8"))
        except ValueError as error:
            raise Refused(f"{m.name}/presentation.json is not JSON: {error}")
        if not isinstance(entry, dict):
            raise Refused(f"{m.name}/presentation.json must hold one module object")
        entry.pop("SPDX-License-Identifier", None)
        ident = module_info_id(m.folder)
        if entry.get("id") != ident:
            raise Refused(f"{m.name}/presentation.json id {entry.get('id')!r} does not match dsp.h {ident!r}")
        same = [i for i, e in enumerate(entries) if e.get("id") == ident]
        if m.kind == "override" and same:
            entries[same[0]] = entry
        elif m.kind == "new" and not same:
            entries.append(entry)
        else:
            raise Refused(f"{m.name}: {ident} is {'already' if same else 'not'} in the pin's sidecar")
    return {SIDECAR: json.dumps(sidecar, indent=1, ensure_ascii=False) + "\n"}


def panel_entry(m: Module) -> tuple[str, dict] | None:
    """The module's panel.json as (module id, PanelOverride object), or None without one."""
    path = m.folder / PANEL_FILE
    if not path.is_file():
        return None
    try:
        entry = json.loads(path.read_text(encoding="utf-8"))
    except ValueError as error:
        raise Refused(f"{m.name}/{PANEL_FILE} is not JSON: {error}")
    if not isinstance(entry, dict):
        raise Refused(f"{m.name}/{PANEL_FILE} must hold one PanelOverride object")
    entry.pop("SPDX-License-Identifier", None)
    ident = module_info_id(m.folder)
    if not ident:
        raise Refused(f"{m.name}: no ws::Info id found in dsp.h for {PANEL_FILE}")
    if "id" in entry:
        if entry.pop("id") != ident:
            raise Refused(f"{m.name}/{PANEL_FILE} id does not match dsp.h {ident!r}")
    unknown = sorted(set(entry) - set(PANEL_KEYS))
    if unknown:
        raise Refused(f"{m.name}/{PANEL_FILE} has unknown keys {unknown}; allowed: {', '.join(PANEL_KEYS)}")
    return ident, entry


def panel_edits(lab_dir: Path, modules: list[Module]) -> dict[str, str]:
    """App tree only: panels/<id>.json per panel.json, and its id in panels/index.json (once)."""
    found = [e for e in (panel_entry(m) for m in modules) if e]
    if not found:
        return {}
    index_path = lab_dir / PANELS_DIR / "index.json"
    index = json.loads(index_path.read_text(encoding="utf-8")) if index_path.is_file() else []
    edits = {}
    for ident, entry in found:
        edits[f"{PANELS_DIR}/{ident}.json"] = json.dumps(entry, indent=2, ensure_ascii=False) + "\n"
        if ident not in index:
            index.append(ident)
    edits[f"{PANELS_DIR}/index.json"] = json.dumps(index, indent=1, ensure_ascii=False) + "\n"
    return edits


def expected_tree(lab: Lab, tracked: list[str], modules: list[Module],
                  panels: bool = False) -> dict[str, tuple[str, Path | bytes]]:
    """relative path -> ("lab"|"module"|"edit", source) for every file the scratch tree must hold."""
    replaced = {m.name for m in modules}
    tree: dict[str, tuple[str, Path | bytes]] = {}
    for rel in tracked:
        parts = rel.split("/")
        if len(parts) > 2 and parts[0] == "modules" and parts[1] in replaced:
            continue
        tree[rel] = ("lab", lab.dir / rel)
    for m in modules:
        for rel, path in folder_files(m.folder).items():
            tree[f"modules/{m.name}/{rel}"] = ("module", path)
    extra = panel_edits(lab.dir, modules) if panels else {}
    for rel, text in {**registration_edits(lab.dir, modules), **presentation_edit(lab.dir, modules),
                      **extra}.items():
        tree[rel] = ("edit", text.encode("utf-8"))
    return tree


def walk_files(root: Path, keep: tuple[str, ...] = ()):
    """(relative posix path of every file or symlink, skipping __pycache__ and the keep subtrees)."""
    for here, dirs, files in os.walk(root):
        rel_dir = Path(here).relative_to(root).as_posix()
        rel_dir = "" if rel_dir == "." else rel_dir
        dirs[:] = [d for d in dirs if d != "__pycache__"
                   and (f"{rel_dir}/{d}" if rel_dir else d) not in keep]
        for d in list(dirs):  # a symlinked directory counts as one entry
            if (Path(here) / d).is_symlink():
                dirs.remove(d)
                yield f"{rel_dir}/{d}" if rel_dir else d
        for f in files:
            yield f"{rel_dir}/{f}" if rel_dir else f


def scratch_files(scratch: Path, keep: tuple[str, ...] = ()) -> set[str]:
    return set(walk_files(scratch, keep))


def verify_scratch(scratch: Path, tree: dict, keep: tuple[str, ...] = ()) -> None:
    """Refuse any scratch change other than the module folders and the registration edits.
    `keep` (app tree only) names install and build outputs that are not part of the check."""
    actual = scratch_files(scratch, keep)
    extra, missing = sorted(actual - tree.keys()), sorted(tree.keys() - actual)
    changed = []
    for rel in sorted(actual & tree.keys()):
        kind, source = tree[rel]
        want = source if isinstance(source, bytes) else source.read_bytes()
        if (scratch / rel).read_bytes() != want:
            changed.append(rel)
    if extra or missing or changed:
        detail = "; ".join(f"{label}: {', '.join(items[:8])}" for label, items in
                           (("unexpected", extra), ("missing", missing), ("changed", changed)) if items)
        raise Refused("scratch tree differs from the pin plus registration; refusing ("
                      + detail + "). A module that needs a lab change files a `limitation found` issue.")


def run_ui_generator(dest: Path) -> bytes:
    """§9.1 rule 2: the pin's own generator, unmodified, without --check. Its output is the only
    other allowed difference; verify_scratch refuses anything else it touches."""
    result = run([sys.executable, str(dest / UI_GENERATOR)], cwd=dest, timeout=300)
    if result.returncode:
        raise Refused(f"{UI_GENERATOR} refused the presentation.json entries:\n"
                      + tail(result.stdout + result.stderr, 10))
    return (dest / UI_CATALOGUE).read_bytes()


def clean_except(dest: Path, keep: tuple[str, ...]) -> None:
    """Empty dest but leave the `keep` subtrees (relative, posix) in place."""
    for rel in list(walk_files(dest, keep)):
        (dest / rel).unlink()
    for here, dirs, _ in os.walk(dest, topdown=False):
        rel = Path(here).relative_to(dest).as_posix()
        if rel == "." or any(k == rel or k.startswith(rel + "/") or rel.startswith(k + "/") for k in keep):
            continue
        if not os.listdir(here):
            os.rmdir(here)


def prepare_tree(lab: Lab, dest: Path, modules: list[Module], panels: bool = False,
                 keep: tuple[str, ...] = ()) -> dict:
    """Recreate dest from the pin, apply the module folders and registration, then verify.
    `panels` (the app tree) also writes panels/<id>.json; `keep` leaves install outputs in place."""
    tracked = lab.tracked()
    tree = expected_tree(lab, tracked, modules, panels)
    stamp_path = dest.parent / f"{dest.name}.stamp.json"
    try:
        previous = json.loads(stamp_path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        previous = {}
    if dest.exists() and keep:
        clean_except(dest, keep)
    elif dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True, exist_ok=True)
    stamp = {}
    for rel, (kind, source) in tree.items():
        target = dest / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        if kind == "edit":
            target.write_bytes(source)
        elif (lab.dir / rel).is_symlink() and kind == "lab":
            os.symlink(os.readlink(lab.dir / rel), target)
            continue
        else:
            shutil.copy2(source, target)
        if kind != "lab":
            stamp[rel] = sha256(target.read_bytes())
    if SIDECAR in tree and tree[SIDECAR][0] == "edit" and (dest / UI_GENERATOR).is_file():
        generated = run_ui_generator(dest)
        tree[UI_CATALOGUE] = ("edit", generated)
        stamp[UI_CATALOGUE] = sha256(generated)
    # Ninja rebuilds by mtime. Files that differ from the pin (now or in the last run) get a fresh
    # mtime when their content changed since the last run, and keep the recorded one otherwise.
    now = time.time_ns()
    times = previous.get("mtimes", {})
    hashes = previous.get("hashes", {})
    for rel in set(stamp) | set(hashes):
        target = dest / rel
        if not target.is_file() or target.is_symlink():
            continue
        digest = stamp.get(rel) or sha256(target.read_bytes())
        stamp[rel] = digest
        if hashes.get(rel) == digest and rel in times:
            os.utime(target, ns=(times[rel], times[rel]))
        else:
            os.utime(target, ns=(now, now))
            times[rel] = now
    stamp_path.write_text(json.dumps({"hashes": stamp, "mtimes": {k: v for k, v in times.items()
                                                                  if k in stamp}}), encoding="utf-8")
    verify_scratch(dest, tree, keep)
    return tree


# --------------------------------------------------------------------------- CMake

def cmake_build(lab: Lab, source: Path, build_dir: Path, targets: list[str] | None, sanitize: bool,
                config: str) -> tuple[bool, str]:
    configure = ["cmake", "-S", str(source / "native"), "-B", str(build_dir)]
    if shutil.which("ninja") and os.environ.get("WSTEST_GENERATOR", "Ninja") == "Ninja":
        configure += ["-G", "Ninja"]
    elif os.environ.get("WSTEST_GENERATOR"):
        configure += ["-G", os.environ["WSTEST_GENERATOR"]]
        if os.environ["WSTEST_GENERATOR"].startswith("Visual Studio"):
            configure += ["-A", "x64"]
    configure.append(f"-DCMAKE_BUILD_TYPE={'RelWithDebInfo' if sanitize else config}")
    if sanitize:
        if os.name == "nt":
            raise Refused("sanitizer builds are Linux-only")
        configure += [f"-DCMAKE_CXX_FLAGS={SANITIZE_FLAGS}", f"-DCMAKE_C_FLAGS={SANITIZE_FLAGS}",
                      "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"]
    if shutil.which("ccache") and os.environ.get("WSTEST_CCACHE", "1") != "0":
        configure += ["-DCMAKE_C_COMPILER_LAUNCHER=ccache", "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache"]
    init = lab.cache_init(build_dir)
    if init:
        configure[1:1] = ["-C", str(init)]
    result = run(configure, timeout=1800)
    if result.returncode:
        return False, "configure failed:\n" + tail(result.stdout + result.stderr)
    command = ["cmake", "--build", str(build_dir), "--config", config, "--parallel"]
    if os.environ.get("CMAKE_BUILD_PARALLEL_LEVEL"):
        command.append(os.environ["CMAKE_BUILD_PARALLEL_LEVEL"])
    if targets:
        command += ["--target", *targets]
    result = run(command, timeout=5400)
    if result.returncode:
        return False, "build failed:\n" + tail(result.stdout + result.stderr, 60)
    return True, ""


def binary(build_dir: Path, name: str, config: str) -> Path:
    for candidate in (build_dir / (name + EXE), build_dir / config / (name + EXE)):
        if candidate.is_file():
            return candidate
    raise StepFailed("build", f"{name}{EXE} not found under {build_dir}")


def ctest_names(build_dir: Path, config: str) -> list[str]:
    result = run(["ctest", "--test-dir", str(build_dir), "-C", config, "-N"], timeout=300)
    return re.findall(r"Test\s+#\d+:\s+(\S+)", result.stdout)


def module_tests(m: Module, names: list[str]) -> list[str]:
    exact = f"abi2_{m.name}_tests"
    if exact in names:
        return [exact]
    return [n for n in names if n.startswith(m.name + "_") and n.endswith("_tests")]


# --------------------------------------------------------------------------- steps

class Session:
    def __init__(self, args: argparse.Namespace, modules: list[Module]):
        self.args = args
        self.lab = Lab()
        self.modules = modules
        self.sanitize = getattr(args, "sanitize", False)
        self.config = getattr(args, "config", "Release")
        self.variant = "sanitize" if self.sanitize else self.config.lower()
        self.scratch = ROOT / "build" / f"scratch-{self.lab.sha7}"
        self.build_dir = ROOT / "build" / f"wstest-{self.lab.sha7}" / self.variant
        self.out = ROOT / "build" / "wstest-out" / self.variant
        self.tree: dict = {}

    def classify(self) -> None:
        self.lab.verify()
        names = [m.name for m in self.modules]
        if len(set(names)) != len(names):
            raise Refused("a module name is given twice")
        for m in self.modules:
            m.kind = "override" if (self.lab.dir / "modules" / m.name).is_dir() else "new"
            m.cls = registration_class(m.folder)

    def register(self) -> None:
        start = time.monotonic()
        try:
            self.classify()
            self.tree = prepare_tree(self.lab, self.scratch, self.modules)
        except Refused as error:
            for m in self.modules:
                m.record("register", "fail", time.monotonic() - start, str(error))
            raise
        for m in self.modules:
            m.record("register", "pass", time.monotonic() - start,
                     f"{m.kind}; {'registered (pre-MB2)' if m.kind == 'new' and self.lab.pre_mb2 else 'folder copied'}")

    def check(self, m: Module) -> None:
        start = time.monotonic()
        report_dir = self.build_dir.parent / "wsmod" / m.name
        result = run([sys.executable, str(self.scratch / "tools/module/wsmod.py"), "check",
                      str(self.scratch / "modules" / m.name), "--json", "--keep-build", str(report_dir)],
                     timeout=900)
        try:
            report = json.loads(result.stdout.strip().splitlines()[-1])
        except (ValueError, IndexError):
            raise StepFailed("check", "wsmod printed no JSON report:\n" + tail(result.stdout + result.stderr))
        m.module_id = (report.get("module") or {}).get("id")
        status: dict[str, set] = {}
        for item in report.get("results", []):
            status.setdefault(item["test"], set()).add(item["status"])
        problems = []
        for test in ("C1", "C2", "C3", "C4"):
            if status.get(test) != {"pass"}:
                problems.append(f"{test}={sorted(status.get(test, {'missing'}))}")
        c5 = status.get("C5", {"missing"})
        if not (c5 == {"pass"} or (c5 == {"skip"} and not (m.folder / "module.json").is_file())):
            problems.append(f"C5={sorted(c5)}")
        fails = [f"{r['test']} {r.get('where')}: {r.get('what')}" for r in report.get("results", [])
                 if r["status"] == "fail"]
        self.write(m, "check.json", report)
        if problems or not m.module_id:
            raise StepFailed("check", "wsmod check C1-C5 must pass: " + ", ".join(problems or ["no id"])
                             + ("\n" + "\n".join(fails) if fails else ""))
        m.record("check", "pass", time.monotonic() - start,
                 f"{m.module_id}; C6 (advisory) {sorted(status.get('C6', {'missing'}))}")

    def write(self, m: Module, name: str, data) -> None:
        path = self.out / m.name / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    def build(self, live: list[Module], extra_targets: list[str] | None = None) -> dict[str, list[str]]:
        start = time.monotonic()
        # Build the shared targets first; that configures, so CTest names (= test targets) are known.
        ok, detail = cmake_build(self.lab, self.scratch, self.build_dir, list(T0_TARGETS) if not
                                 self.sanitize else ["wirescape_render"], self.sanitize, self.config)
        tests: dict[str, list[str]] = {}
        if ok:
            names = ctest_names(self.build_dir, self.config)
            tests = {m.name: module_tests(m, names) for m in live}
            targets = sorted({t for ts in tests.values() for t in ts} | set(extra_targets or []))
            if targets:
                ok, detail = cmake_build(self.lab, self.scratch, self.build_dir, targets, self.sanitize,
                                         self.config)
        for m in live:
            m.record("build", "pass" if ok else "fail", time.monotonic() - start,
                     "" if ok else detail)
        return tests

    def tests(self, m: Module, names: list[str]) -> None:
        start = time.monotonic()
        if not names:
            raise StepFailed("tests", f"no CTest named abi2_{m.name}_tests (add it in the folder's "
                                      "CMakeLists.txt)")
        env = {"ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
               "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1"} if self.sanitize else None
        pattern = "^(" + "|".join(re.escape(n) for n in names) + ")$"
        result = run(["ctest", "--test-dir", str(self.build_dir), "-C", self.config, "-R", pattern,
                      "--output-on-failure", "--timeout", "300"], timeout=1800, env=env)
        ran = re.findall(r"Test\s+#\d+:\s+(\S+)", result.stdout)
        if result.returncode or sorted(set(ran)) != sorted(names):
            raise StepFailed("tests", tail(result.stdout + result.stderr, 60))
        m.record("tests", "pass", time.monotonic() - start, ", ".join(names))

    def demos(self, m: Module) -> None:
        start = time.monotonic()
        cases = sorted((m.folder / "demos").glob("*.json"))
        if not cases:
            raise StepFailed("demos", "every module needs at least one demos/*.json render case")
        render = binary(self.build_dir, "wirescape_render", self.config)
        evidence = self.lab.dir / "tools/render/evidence.py"
        results = []
        for case in cases:
            validate_case(case)
            out = self.out / m.name / "demos" / case.stem
            if out.exists():
                shutil.rmtree(out)
            result = run([sys.executable, str(evidence), "--render", str(render), "--case", str(case),
                          "--out", str(out), "--configuration", self.variant], timeout=600)
            results.append({"case": case.name, "exit": result.returncode})
            if result.returncode:
                detail = tail(result.stdout + result.stderr, 10)
                try:
                    detail = json.loads((out / "measurements.json").read_text())["error"]
                except (OSError, ValueError, KeyError):
                    pass
                raise StepFailed("demos", f"{case.name}: {detail}")
        m.record("demos", "pass", time.monotonic() - start, f"{len(cases)} case(s); WAVs in "
                 f"{(self.out / m.name / 'demos').relative_to(ROOT).as_posix()}")

    def cpu_probe(self, m: Module) -> None:
        start = time.monotonic()
        probe = binary(self.build_dir, "abi2_module_cpu_probe", self.config)
        seconds = getattr(self.args, "probe_seconds", 5.0)
        rows = []
        for channels in sorted(set(getattr(self.args, "probe_channels", None) or [1])):
            result = run([str(probe), "--module", m.module_id, "--instances", str(PROBE["instances"]),
                          "--rate", str(PROBE["rate"]), "--block", str(PROBE["block"]),
                          "--seconds", str(seconds), "--channels", str(channels)],
                         timeout=seconds * 20 + 120)
            line = next((l for l in result.stdout.splitlines() if l.startswith("module_cpu_probe ")), None)
            if result.returncode or not line:
                raise StepFailed("cpu_probe", tail(result.stdout + result.stderr, 10) or "probe failed")
            data = json.loads(line[len("module_cpu_probe "):])
            rows.append(data)
            if data.get("allocations") != 0 or data.get("faults") != 0:
                self.write(m, "cpu_probe.json", rows)
                raise StepFailed("cpu_probe", f"{channels} ch: allocations={data.get('allocations')}, "
                                              f"faults={data.get('faults')} (both must be 0)")
        self.write(m, "cpu_probe.json", rows)
        worst = max(float(r["render_ns"]["max"]) / float(r["period_ns"]) for r in rows)
        peaks = [r.get("peak") for r in rows]
        m.record("cpu_probe", "pass", time.monotonic() - start,
                 f"0 allocations, 0 faults; worst block {worst:.1%} of the period (diagnostic); "
                 f"peak {peaks}", worst_block_fraction=worst)

    # ----- identical (overrides, §2.4 step 6)

    def pristine(self) -> tuple[Path, Path]:
        source = ROOT / "build" / f"pristine-{self.lab.sha7}" / "src"
        build_dir = ROOT / "build" / f"pristine-{self.lab.sha7}" / self.variant
        prepare_tree(self.lab, source, [])
        ok, detail = cmake_build(self.lab, source, build_dir, ["wirescape_render"], self.sanitize,
                                 self.config)
        if not ok:
            raise StepFailed("identical", "pristine build failed: " + detail)
        return source, build_dir

    def identical(self, m: Module) -> None:
        start = time.monotonic()
        _, pristine_build = self.pristine()
        new_render = binary(self.build_dir, "wirescape_render", self.config)
        old_render = binary(pristine_build, "wirescape_render", self.config)
        added = appended_ids(self.lab.dir / "modules" / m.name, m.folder)
        cases = [(f"demos/{p.name}", json.loads(p.read_text(encoding="utf-8")))
                 for p in sorted((m.folder / "demos").glob("*.json"))]
        for p in sorted((self.lab.dir / "tools/render/cases").glob("*.json")):
            case = json.loads(p.read_text(encoding="utf-8"))
            if any(x.get("module") == m.module_id for x in case.get("patch", {}).get("modules", [])):
                cases.append((f"lab:{p.name}", case))
        work = self.out / m.name / "identical"
        if work.exists():
            shutil.rmtree(work)
        compared = []
        for label, case in cases:
            case = at_defaults(case, m.module_id, added)
            stem = re.sub(r"[^\w.-]", "_", label)
            a = render_case(old_render, case, work / f"{stem}.pristine")
            b = render_case(new_render, case, work / f"{stem}.override")
            if a != b:
                raise StepFailed("identical", f"{label}: pristine and override renders differ "
                                              f"({a[:12]} vs {b[:12]})")
            compared.append(label)
        if not compared:
            raise StepFailed("identical", "no render case uses the module")
        m.record("identical", "pass", time.monotonic() - start,
                 f"{len(compared)} case(s) byte-identical: {', '.join(compared)}")


def validate_case(path: Path) -> dict:
    try:
        case = json.loads(path.read_text(encoding="utf-8"))
    except ValueError as error:
        raise StepFailed("demos", f"{path.name}: invalid JSON: {error}")
    missing = [k for k in ("name", "rate", "block", "seconds", "seed", "peak_limit", "patch", "events",
                           "windows", "frequencies") if k not in case]
    if missing:
        raise StepFailed("demos", f"{path.name}: missing keys {missing} (lab tools/render/cases format)")
    return case


def appended_ids(lab_folder: Path, folder: Path) -> dict[str, set]:
    """Parameter and port IDs the override adds, from module.json (pre-MB2 metadata)."""
    added = {"parameters": set(), "inputs": set(), "outputs": set()}
    try:
        old = json.loads((lab_folder / "module.json").read_text(encoding="utf-8"))
        new = json.loads((folder / "module.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return added
    for key in added:
        added[key] = set(new.get(key, {})) - set(old.get(key, {}))
    return added


def at_defaults(case: dict, module_id: str, added: dict[str, set]) -> dict:
    """The case with the override's new parameters at their defaults and new ports unconnected."""
    case = copy.deepcopy(case)
    patch = case["patch"]
    nodes = {x["id"] for x in patch.get("modules", []) if x.get("module") == module_id}
    for x in patch.get("modules", []):
        if x["id"] in nodes:
            for pid in added["parameters"]:
                (x.get("parameters") or {}).pop(pid, None)
    def touches(end: dict, ports: set) -> bool:
        return end.get("module") in nodes and end.get("port") in ports
    patch["connections"] = [c for c in patch.get("connections", [])
                            if not touches(c.get("to", {}), added["inputs"] | added["parameters"])
                            and not touches(c.get("from", {}), added["outputs"])]
    return case


def render_case(render: Path, case: dict, out: Path, rate: int | None = None, block: int | None = None) -> str:
    out.mkdir(parents=True, exist_ok=True)
    (out / "patch.json").write_text(json.dumps(case["patch"]), encoding="utf-8")
    (out / "events.json").write_text(json.dumps(case["events"]), encoding="utf-8")
    result = run([str(render), "--patch", str(out / "patch.json"), "--script", str(out / "events.json"),
                  "--out", str(out / "audio.wav"), "--rate", str(rate or case["rate"]),
                  "--block", str(block or case["block"]), "--seconds", str(case["seconds"]),
                  "--seed", str(case["seed"])], timeout=600)
    if result.returncode:
        raise StepFailed("render", f"renderer exit {result.returncode}: {tail(result.stderr, 6)}")
    return sha256((out / "audio.wav").read_bytes())


# --------------------------------------------------------------------------- commands

def report(session: Session, wall: float, name: str) -> int:
    summary = {"tier": name, "variant": session.variant, "lab_commit": session.lab.sha,
               "platform": f"{platform.system().lower()}-{platform.machine().lower()}",
               "wall_seconds": round(wall, 1),
               "modules": {m.name: {"kind": m.kind, "id": m.module_id, "failed_step": m.failed,
                                    "steps": m.steps} for m in session.modules}}
    session.out.mkdir(parents=True, exist_ok=True)
    (session.out / f"{name}.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print()
    for m in session.modules:
        print(f"{m.name} ({m.kind}, {m.module_id}): {'FAIL at ' + m.failed if m.failed else 'PASS'}")
        for s in m.steps:
            first = s["detail"].splitlines()[0] if s["detail"] else ""
            print(f"  {s['status']:4} {s['step']:10} {s['seconds']:7.1f}s  {first}")
            if s["status"] == "fail" and "\n" in s["detail"]:
                rest = s["detail"].split("\n", 1)[1].strip("\n")
                print("       " + rest.replace("\n", "\n       "))
    print(f"\n{name} wall time: {wall:.1f}s ({session.variant}, lab {session.lab.sha7}); "
          f"summary: {(session.out / (name + '.json')).relative_to(ROOT).as_posix()}")
    step_summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if step_summary:
        with open(step_summary, "a", encoding="utf-8") as stream:
            stream.write(f"### wstest {name} ({session.variant}, {summary['platform']})\n\n"
                         f"Lab `{session.lab.sha7}` · wall time **{wall:.1f} s**\n\n"
                         "| Module | Kind | Result | Steps |\n|---|---|---|---|\n")
            for m in session.modules:
                steps = " · ".join(f"{s['step']} {s['status']} ({s['seconds']}s)" for s in m.steps)
                stream.write(f"| `{m.name}` | {m.kind} | {'fail at ' + m.failed if m.failed else 'pass'}"
                             f" | {steps} |\n")
            stream.write("\n")
    return 1 if any(m.failed for m in session.modules) else 0


def run_t0(args: argparse.Namespace, modules: list[Module]) -> tuple[Session, float]:
    start = time.monotonic()
    session = Session(args, modules)
    session.lab.fetch(git_deps=args.git_deps)
    try:
        session.register()
    except Refused:
        return session, time.monotonic() - start
    live = []
    for m in modules:
        try:
            session.check(m)
            live.append(m)
        except StepFailed as error:
            m.record(error.step, "fail", 0, error.detail)
    if args.command == "build" or getattr(args, "only_build", False):
        live = modules
    tests = session.build(live) if live else {}
    live = [m for m in live if not m.failed]
    steps = [("tests", lambda m: session.tests(m, tests.get(m.name, []))), ("demos", session.demos)]
    if not session.sanitize:
        steps.append(("cpu_probe", session.cpu_probe))
    if args.command == "t0":
        for m in live:
            for name, step in steps + ([("identical", session.identical)] if m.kind == "override" and
                                       not session.sanitize else []):
                t = time.monotonic()
                try:
                    step(m)
                except StepFailed as error:
                    m.record(name, "fail", time.monotonic() - t, f"{error.step}: {error.detail}"
                             if error.step != name else error.detail)
                    break
    verify_scratch(session.scratch, session.tree)
    return session, time.monotonic() - start


def cmd_fetch(args) -> int:
    Lab().fetch(git_deps=args.git_deps)
    log(f"lab ready at .lab/{Lab().sha}/")
    return 0


def cmd_t0(args) -> int:
    modules = [resolve_module(a) for a in args.modules]
    session, wall = run_t0(args, modules)
    return report(session, wall, args.command)


def cmd_identical(args) -> int:
    start = time.monotonic()
    modules = [resolve_module(a) for a in args.modules]
    args.command = "build"
    session, _ = run_t0(args, modules)
    for m in modules:
        if m.failed:
            continue
        if m.kind != "override":
            m.record("identical", "fail", 0, "only overrides of a lab folder have a pristine to compare")
            continue
        try:
            session.identical(m)
        except StepFailed as error:
            m.record("identical", "fail", 0, error.detail)
    return report(session, time.monotonic() - start, "identical")


def load_expected(sanitize: bool) -> dict:
    data = json.loads((ROOT / "tools/fixtures/expected.json").read_text(encoding="utf-8"))
    return data["sanitize" if sanitize else "default"]


def git_status() -> str:
    result = run(["git", "status", "--porcelain", "--untracked-files=all"], cwd=ROOT)
    return result.stdout if result.returncode == 0 else "<git status failed>"


def cmd_selftest(args) -> int:
    before = git_status()
    expected = load_expected(args.sanitize)
    modules = [resolve_module(f"tools/fixtures/{name}") for name in expected]
    args.command = "t0"
    session, wall = run_t0(args, modules)
    code = report(session, wall, "selftest")
    problems = [f"{m.name}: expected {expected[m.name] or 'pass'}, got {m.failed or 'pass'}"
                for m in modules if m.failed != expected[m.name]]
    after = git_status()
    if after != before:
        problems.append("git status changed after t0 (the scratch tree or outputs leaked into the "
                        "repo):\n" + after)
    for p in problems:
        print("SELFTEST FAIL: " + p)
    if not problems:
        print(f"SELFTEST PASS: every fixture ended as expected; git status unchanged ({len(modules)} fixtures)")
    del code
    return 1 if problems else 0


def cmd_t2(args) -> int:
    """Full lab build and every lab CTest with the module registered, the demo matrix and probes."""
    start = time.monotonic()
    m = resolve_module(args.module)
    session = Session(args, [m])
    session.lab.fetch(git_deps=args.git_deps)
    result: dict = {"tier": "T2", "module": m.name, "lab_commit": session.lab.sha,
                    "platform": f"{platform.system().lower()}-{platform.machine().lower()}",
                    "checks": {}}
    def finish(code: int) -> int:
        result["wall_seconds"] = round(time.monotonic() - start, 1)
        result["passed"] = code == 0
        out = Path(args.out) if args.out else session.out / "t2.json"
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        log(f"T2 {'passed' if code == 0 else 'FAILED'}; wrote {out}")
        return code
    try:
        session.register()
        session.check(m)
    except (Refused, StepFailed) as error:
        result["checks"]["register_check"] = {"status": "fail", "detail": str(error)}
        return finish(1)
    result.update(kind=m.kind, id=m.module_id)
    ok, detail = cmake_build(session.lab, session.scratch, session.build_dir, None, False, session.config)
    result["checks"]["full_build"] = {"status": "pass" if ok else "fail", "detail": detail}
    if not ok:
        return finish(1)
    ctest = run(["ctest", "--test-dir", str(session.build_dir), "-C", session.config,
                 "--output-on-failure", "--timeout", "300"], timeout=7200)
    failed = re.findall(r"\d+ - (\S+) \(", ctest.stdout)
    total = re.search(r"tests passed, (\d+) tests failed out of (\d+)", ctest.stdout)
    changed = catalogue_changed(session.lab.dir, m)
    lab_ctest = classify_ctest(ctest.returncode, failed, changed)
    lab_ctest["summary"] = total.group(0) if total else tail(ctest.stdout, 3)
    lab_ctest["catalogue_changed"] = changed
    result["checks"]["lab_ctest"] = lab_ctest
    result["checks"]["s0_catalogue_others"] = s0_catalogue_others(session, m)
    render = binary(session.build_dir, "wirescape_render", session.config)
    limits = engine_limits(session, render)
    # The probe must itself render at the lab case's own settings, or nothing may be skipped.
    probe_ok = "48000/128" not in limits
    result["checks"]["engine_limits"] = {"status": "pass" if probe_ok else "fail", "refused": limits}
    if not probe_ok:
        limits = {}
    matrix, code = [], 0 if (lab_ctest["status"] == "pass" and probe_ok and
                             result["checks"]["s0_catalogue_others"]["status"] == "pass") else 1
    for path in sorted((m.folder / "demos").glob("*.json")):
        case = json.loads(path.read_text(encoding="utf-8"))
        for rate in T2_RATES:
            for block in T2_BLOCKS:
                row = {"case": path.name, "rate": rate, "block": block}
                work = session.out / m.name / "t2" / f"{path.stem}-{rate}-{block}"
                try:
                    if case["seconds"] * rate * 8 > 20 * 1024 * 1024:
                        row.update(status="skip", detail="over the renderer's 20 MiB WAV limit")
                    else:
                        a = render_case(render, case, work / "a", rate, block)
                        b = render_case(render, case, work / "b", rate, block)
                        peak, finite = wav_peak(work / "a" / "audio.wav")
                        row.update(sha256=a, deterministic=a == b, finite=finite, peak=peak,
                                   peak_limit=case["peak_limit"])
                        good = a == b and finite and peak <= case["peak_limit"]
                        row["status"] = "pass" if good else "fail"
                    shutil.rmtree(work / "b", ignore_errors=True)
                except StepFailed as error:
                    # §9.2: skip only when the lab case refuses this pair with the same reason.
                    reason = refusal_reason(error.detail)
                    refused = limits.get(f"{rate}/{block}")
                    status = demo_refusal_status(reason, refused)
                    row.update(status=status, detail=reason if status == "fail" else
                               f"the demo and the lab's {LIMIT_CASE} both refuse ({reason}); "
                               f"{FEEDBACK_BLOCK_ISSUE}, ends at the pin bump with its fix")
                code |= row["status"] == "fail"
                matrix.append(row)
    result["checks"]["demo_matrix"] = {"status": "fail" if any(r["status"] == "fail" for r in matrix)
                                       else "pass", "rows": matrix}
    result["checks"]["callgrind"] = callgrind(session, m, render)
    result["checks"]["cortex_m7"] = cortex_m7(session, m)
    return finish(1 if code else 0)


# Lab #652, final ruling (0024 §9.1, claude-planner; supersedes the interim deferral of the two UI
# CTests). ui_catalogue_sync and ui_patch_conformance gate T2: registration inserts the module's
# presentation.json and runs the pin's generator. host/sidecar/testdata/s0_catalogue.ndjson has no
# generator by design (I7 wire contract), so abi2_sidecar_snapshot_tests stays deferred by name until
# the module's promotion PR, for a module that changes the catalogue; the gating s0_catalogue_others
# check (compare_s0) stands in for it. The list is closed: adding a name is an Architect ruling.
T2_DEFERRED_TO_PROMOTION = ("abi2_sidecar_snapshot_tests",)


def catalogue_changed(lab_dir: Path, m: Module) -> bool:
    """True when registering the module changes the catalogue: a new module, or an override whose
    module.json or dsp.h (the descriptor: IDs, ranges, flags) differs from the pinned lab folder."""
    if m.kind != "override":
        return True
    base = lab_dir / "modules" / m.name
    for name in ("module.json", "dsp.h"):
        ours, theirs = m.folder / name, base / name
        if ours.is_file() != theirs.is_file():
            return True
        if ours.is_file() and ours.read_bytes() != theirs.read_bytes():
            return True
    return False


def classify_ctest(returncode: int, failed: list[str], catalogue_change: bool) -> dict:
    """lab_ctest verdict under the #652 ruling: only the named sync tests may be deferred, and only
    when the module changes the catalogue. A non-zero exit with no parsed failure stays a failure."""
    deferred = sorted(n for n in failed if catalogue_change and n in T2_DEFERRED_TO_PROMOTION)
    hard = sorted(n for n in failed if n not in deferred)
    unexplained = returncode != 0 and not failed
    status = "fail" if hard or unexplained else "pass"
    return {"status": status, "failed": hard, "deferred_to_promotion": deferred,
            **({"detail": "ctest failed without naming a test"} if unexplained else {})}


def compare_s0(fixture: dict, actual: dict, module_id: str, kind: str) -> tuple[bool, str]:
    """0024 §9.1 rule 4. new: removing the module's entry leaves the fixture exactly, in order.
    override: every other entry is unchanged; the module's own entry differs only by appended ports
    (per direction) or parameters."""
    top = lambda c: {k: v for k, v in c.items() if k != "modules"}
    if top(fixture) != top(actual):
        return False, "catalogue fields other than modules differ"
    fm, am = fixture.get("modules", []), actual.get("modules", [])
    others = lambda ms: [e for e in ms if e.get("id") != module_id]
    if others(fm) != others(am):
        return False, "another module's entry or the order changed"
    mine = [e for e in am if e.get("id") == module_id]
    if kind == "new":
        if len(mine) != 1 or any(e.get("id") == module_id for e in fm):
            return False, f"expected exactly one new entry for {module_id}"
        return True, f"fixture unchanged apart from the new {module_id} entry"
    old = [e for e in fm if e.get("id") == module_id]
    if len(mine) != 1 or len(old) != 1:
        return False, f"expected one {module_id} entry in both"
    a, f = mine[0], old[0]
    if {k: v for k, v in a.items() if k not in ("ports", "parameters")} != \
            {k: v for k, v in f.items() if k not in ("ports", "parameters")}:
        return False, f"{module_id}: fields other than ports and parameters changed"
    fp, ap = f.get("parameters", []), a.get("parameters", [])
    if ap[:len(fp)] != fp:
        return False, f"{module_id}: parameters are not the fixture's plus appended ones"
    for direction in ("input", "output"):
        fo = [x for x in f.get("ports", []) if x.get("direction") == direction]
        ao = [x for x in a.get("ports", []) if x.get("direction") == direction]
        if ao[:len(fo)] != fo:
            return False, f"{module_id}: {direction} ports are not the fixture's plus appended ones"
    return True, f"{module_id}: only appended ports or parameters"


def s0_catalogue_others(session: "Session", m: Module) -> dict:
    """Gating check: the scratch build's hello catalogue (wirescape-engine --null-audio, devices off)
    against the parsed pinned fixture."""
    try:
        engine = binary(session.build_dir, "wirescape-engine", session.config)
    except StepFailed as error:
        return {"status": "fail", "detail": error.detail}
    absent = session.build_dir / "s0-settings-not-present.json"
    if absent.exists():
        absent.unlink()
    proc = subprocess.Popen([str(engine), "--null-audio", "--rate", "48000", "--block", "256",
                             "--settings", str(absent)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    try:
        out, err = proc.communicate(input="", timeout=60)
    except subprocess.TimeoutExpired:
        proc.kill()
        out, err = proc.communicate()
    try:
        hello = json.loads(out.splitlines()[0])
        actual = hello["catalogue"]
        fixture = json.loads((session.lab.dir / S0_FIXTURE).read_text(encoding="utf-8").splitlines()[0])
    except (ValueError, IndexError, KeyError, TypeError) as error:
        return {"status": "fail", "detail": f"no hello catalogue ({error}): {tail(out + err, 5)}"}
    if not m.module_id:
        return {"status": "fail", "detail": "module id unknown"}
    if not catalogue_changed(session.lab.dir, m):
        ok, detail = (actual == fixture), "catalogue unchanged" if actual == fixture else "catalogue changed"
    else:
        ok, detail = compare_s0(fixture, actual, m.module_id, m.kind)
    return {"status": "pass" if ok else "fail", "detail": detail}


def demo_refusal_status(demo_reason: str, lab_reason: str | None) -> str:
    """0024 §9.2 rule 2: a demo row that refuses is skip only when the lab case refuses the same
    rate/block with the same reason; anything else fails."""
    return "skip" if lab_reason is not None and demo_reason == lab_reason else "fail"


def refusal_reason(detail: str) -> str:
    return detail.splitlines()[-1][-160:] if detail else ""


LIMIT_CASE = "tools/render/cases/adsr-notes.json"


def engine_limits(session: Session, render: Path) -> dict:
    """Rate/block pairs the pinned lab's own render case cannot prepare (the renderer's limits)."""
    case = json.loads((session.lab.dir / LIMIT_CASE).read_text(encoding="utf-8"))
    refused = {}
    for rate in T2_RATES:
        for block in T2_BLOCKS:
            try:
                render_case(render, case, session.out / "engine-limits" / f"{rate}-{block}", rate, block)
            except StepFailed as error:
                refused[f"{rate}/{block}"] = refusal_reason(error.detail)
    shutil.rmtree(session.out / "engine-limits", ignore_errors=True)
    return refused


def wav_peak(path: Path) -> tuple[float, bool]:
    import struct
    data = path.read_bytes()
    count = struct.unpack_from("<I", data, 40)[0] // 4
    values = struct.unpack_from(f"<{count}f", data, 44)
    finite = all(math.isfinite(v) for v in values)
    return (max((abs(v) for v in values), default=0.0) if finite else float("nan")), finite


def callgrind(session: Session, m: Module, render: Path) -> dict:
    """Advisory: callgrind instruction counts for each demo (linux-x64 only)."""
    if not (sys.platform.startswith("linux") and platform.machine() in ("x86_64", "AMD64")):
        return {"status": "skip", "detail": "linux-x64 only"}
    if not shutil.which("valgrind"):
        return {"status": "skip", "detail": "valgrind not installed"}
    rows = []
    for path in sorted((m.folder / "demos").glob("*.json")):
        case = json.loads(path.read_text(encoding="utf-8"))
        work = session.out / m.name / "callgrind" / path.stem
        work.mkdir(parents=True, exist_ok=True)
        (work / "patch.json").write_text(json.dumps(case["patch"]), encoding="utf-8")
        (work / "events.json").write_text(json.dumps(case["events"]), encoding="utf-8")
        out_file = work / "callgrind.out"
        result = run(["valgrind", "--tool=callgrind", f"--callgrind-out-file={out_file}", str(render),
                      "--patch", str(work / "patch.json"), "--script", str(work / "events.json"),
                      "--out", str(work / "audio.wav"), "--rate", str(case["rate"]), "--block",
                      str(case["block"]), "--seconds", str(case["seconds"]), "--seed", str(case["seed"])],
                     timeout=3600)
        total = re.search(r"Collected\s*:\s*(\d+)", result.stderr)
        rows.append({"case": path.name, "instructions": int(total.group(1)) if total else None,
                     "exit": result.returncode})
    return {"status": "advisory", "rows": rows}


def cortex_m7(session: Session, m: Module) -> dict:
    """Advisory: compile-only Cortex-M7 object with the lab portability flags; heap symbols."""
    compiler = shutil.which("arm-none-eabi-g++")
    nm = shutil.which("arm-none-eabi-nm")
    if not compiler or not nm:
        return {"status": "skip", "detail": "arm-none-eabi toolchain not installed"}
    obj = session.out / m.name / "m7" / "module.o"
    obj.parent.mkdir(parents=True, exist_ok=True)
    result = run([compiler, *M7_FLAGS, "-I", "sdk", "-I", "sdk/include", "-I", "sdk/examples", "-c",
                  f"modules/{m.name}/module.cpp", "-o", str(obj)], cwd=session.scratch, timeout=600)
    if result.returncode:
        return {"status": "advisory", "compiles": False, "first_error": tail(result.stderr, 3)}
    symbols = run([nm, "--undefined-only", str(obj)]).stdout.split()
    heap = sorted(s for s in symbols if s in ("malloc", "calloc", "realloc", "free")
                  or s.startswith(("_Znw", "_Zna", "_Zdl", "_Zda")))
    doubles = sorted(s for s in symbols if s.startswith("__aeabi_d"))
    return {"status": "advisory", "compiles": True, "heap_symbols": heap, "double_symbols": doubles}


# --------------------------------------------------------------------------- app and ui (local only)
# Developer conveniences, not test tiers: nothing here changes T0/T1/T2 or build/scratch-<sha7>/.

def app_modules(args, lab: Lab) -> list[Module]:
    modules = [resolve_module(a) for a in args.modules]
    names = [m.name for m in modules]
    if len(set(names)) != len(names):
        raise Refused("a module name is given twice")
    for m in modules:
        m.kind = "override" if (lab.dir / "modules" / m.name).is_dir() else "new"
        m.cls = registration_class(m.folder)
    return modules


def tool_path(name: str) -> str:
    found = shutil.which(name + (".cmd" if os.name == "nt" else "")) or shutil.which(name)
    if not found:
        raise Refused(f"{name} not found on PATH (Node 20+ is required)")
    return found


def npm_install(scratch: Path, sub: str) -> None:
    """npm ci in scratch/<sub> when node_modules is missing or package-lock.json changed."""
    folder = scratch / sub
    lock = folder / "package-lock.json"
    if not lock.is_file():
        raise Refused(f"{sub}/package-lock.json is missing in the pin")
    stamp_path = scratch.parent / f"{scratch.name}.npm.json"
    try:
        stamps = json.loads(stamp_path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        stamps = {}
    digest = sha256(lock.read_bytes())
    if (folder / "node_modules").is_dir() and stamps.get(sub) == digest:
        return
    log(f"npm ci in {sub} (first run or package-lock.json changed)")
    stamps.pop(sub, None)
    result = run([tool_path("npm"), "ci", "--no-audit", "--no-fund"], cwd=folder, timeout=1800, capture=False)
    if result.returncode:
        raise Refused(f"npm ci failed in {sub} (exit {result.returncode})")
    stamps[sub] = digest
    stamp_path.write_text(json.dumps(stamps), encoding="utf-8")


def prepare_app_tree(lab: Lab, modules: list[Module]) -> tuple[Path, dict]:
    scratch = lab.root / "build" / f"app-{lab.sha7}"
    return scratch, prepare_tree(lab, scratch, modules, panels=True, keep=APP_KEEP)


def refresh_app_files(lab: Lab, scratch: Path, modules: list[Module], tracked: list[str]) -> dict:
    """Re-apply only the edited files (panels, sidecar, regenerated catalogue) to a prepared app tree."""
    tree = expected_tree(lab, tracked, modules, panels=True)
    try:
        for rel, (kind, source) in tree.items():
            if kind == "edit":
                (scratch / rel).parent.mkdir(parents=True, exist_ok=True)
                (scratch / rel).write_bytes(source)
        if SIDECAR in tree and (scratch / UI_GENERATOR).is_file():
            tree[UI_CATALOGUE] = ("edit", run_ui_generator(scratch))
        verify_scratch(scratch, tree, APP_KEEP)
    except Refused as error:
        if "scratch tree differs" not in str(error):
            raise
        return prepare_app_tree(lab, modules)[1]  # e.g. a panel.json was removed: rebuild the tree
    return tree


def launch_command(scratch: Path, engine: Path) -> list[str]:
    electron = scratch / "apps/desktop/node_modules/.bin" / ("electron.cmd" if os.name == "nt" else "electron")
    return [str(electron), ".", "--engine", str(engine)]


def cmd_app(args) -> int:
    lab = Lab()
    lab.fetch(git_deps=args.git_deps)
    modules = app_modules(args, lab)
    scratch, _ = prepare_app_tree(lab, modules)
    build_dir = ROOT / "build" / f"wstest-{lab.sha7}" / "app"
    log("building wirescape-engine (CMake target wirescape_engine_sidecar) ...")
    ok, detail = cmake_build(lab, scratch, build_dir, ["wirescape_engine_sidecar"], False, args.config)
    if not ok:
        raise Refused(detail)
    engine = binary(build_dir, "wirescape-engine", args.config).resolve()
    npm_install(scratch, "apps/studio")
    npm_install(scratch, "apps/desktop")
    log("building Studio and the desktop app (npm run build in apps/desktop) ...")
    result = run([tool_path("npm"), "run", "build"], cwd=scratch / "apps/desktop", timeout=1800, capture=False)
    if result.returncode:
        raise Refused(f"npm run build failed in apps/desktop (exit {result.returncode})")
    command = launch_command(scratch, engine)
    printable = subprocess.list2cmdline(command) if os.name == "nt" else shlex.join(command)
    log(f"ready. Launch (from {scratch / 'apps/desktop'}):\n    {printable}")
    if not args.launch:
        log("not launching: the app can open audio devices; the owner adds --launch")
        return 0
    log("launching the desktop app (Ctrl+C or close the window to stop)")
    try:
        return subprocess.call(command, cwd=str(scratch / "apps/desktop"))
    except KeyboardInterrupt:
        return 0


def stop_process(proc: subprocess.Popen) -> None:
    if proc.poll() is not None:
        return
    try:
        if os.name == "nt":
            subprocess.run(["taskkill", "/PID", str(proc.pid), "/T", "/F"], capture_output=True, check=False)
        else:
            os.killpg(proc.pid, signal.SIGTERM)
        proc.wait(timeout=10)
    except (OSError, subprocess.TimeoutExpired):
        proc.kill()


def watched(modules: list[Module]) -> dict[str, int]:
    state = {}
    for m in modules:
        for name in (PANEL_FILE, "presentation.json"):
            path = m.folder / name
            state[str(path)] = path.stat().st_mtime_ns if path.is_file() else 0
    return state


def cmd_ui(args) -> int:
    lab = Lab()
    lab.fetch(git_deps=args.git_deps)
    modules = app_modules(args, lab)
    scratch, tree = prepare_app_tree(lab, modules)
    if not any((m.folder / "presentation.json").is_file() for m in modules):
        log("warning: no module has a presentation.json, so none is in the UI catalogue yet")
    log(f"app tree ready: build/app-{lab.sha7}/ (no C++ build; Studio uses its fake engine, no sound)")
    if args.no_serve:
        return 0
    npm_install(scratch, "apps/studio")
    command = [tool_path("npm"), "run", "dev", "--", "--port", str(args.port), "--strictPort"]
    options = {"start_new_session": True} if os.name != "nt" else {"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP}
    proc = subprocess.Popen(command, cwd=str(scratch / "apps/studio"), **options)
    log(f"Studio dev server: http://localhost:{args.port}/  (edit modules/<name>/panel.json or presentation.json; "
        "Ctrl+C stops)")
    tracked = lab.tracked()
    state = watched(modules)
    try:
        while proc.poll() is None:
            time.sleep(0.5)
            now = watched(modules)
            if now == state:
                continue
            state = now
            try:
                refresh_app_files(lab, scratch, modules, tracked)
                log("updated — reload the browser")
            except Refused as error:
                log(f"refused, the browser keeps the last good version: {error}")
        log(f"the dev server stopped (exit {proc.returncode})")
        return proc.returncode or 0
    except KeyboardInterrupt:
        log("stopping the dev server")
        return 0
    finally:
        stop_process(proc)


# --------------------------------------------------------------------------- CI scope

SCOPE_ALL = ("tools/", ".github/workflows/", "LAB_PIN")


def scope(changed: list[str], root: Path = ROOT) -> dict:
    """What a change needs: run (any code path), all (harness or pin), and the changed modules."""
    everything = any(p == "LAB_PIN" or p.startswith(SCOPE_ALL[:2]) for p in changed)
    touched = sorted({p.split("/")[1] for p in changed if p.startswith("modules/") and p.count("/") >= 2})
    present = sorted(p.name for p in (root / "modules").iterdir()
                     if p.is_dir() and NAME_RE.match(p.name) and (p / "module.cpp").is_file()
                     ) if (root / "modules").is_dir() else []
    modules = present if everything else [n for n in touched if n in present]
    return {"run": everything or bool(touched), "all": everything, "modules": modules}


def cmd_scope(args) -> int:
    if args.paths is not None:
        changed = [p for p in args.paths if p]
    elif args.base and args.head:
        diff = run(["git", "diff", "--name-only", f"{args.base}...{args.head}"], cwd=ROOT)
        if diff.returncode:
            # Fail closed: an unknown diff runs everything.
            changed = ["LAB_PIN"]
        else:
            changed = diff.stdout.split()
    else:
        changed = ["LAB_PIN"]  # push to main, dispatch: run everything
    result = scope(changed)
    print(json.dumps(result))
    output = os.environ.get("GITHUB_OUTPUT")
    if output:
        with open(output, "a", encoding="utf-8") as stream:
            stream.write(f"run={'true' if result['run'] else 'false'}\n")
            stream.write(f"all={'true' if result['all'] else 'false'}\n")
            stream.write(f"modules={' '.join(result['modules'])}\n")
    return 0


# --------------------------------------------------------------------------- main

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="wstest", description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    def common(p, modules: str | None = "+"):
        p.add_argument("--git-deps", action="store_true",
                       help="use the lab's tools/cloud/git_deps.py when codeload is blocked")
        p.add_argument("--config", default="Release")
        if modules:
            p.add_argument("modules", nargs=modules)
    p = sub.add_parser("fetch")
    p.add_argument("--git-deps", action="store_true")
    p.set_defaults(func=cmd_fetch)
    for name in ("build", "t0"):
        p = sub.add_parser(name)
        common(p)
        p.add_argument("--sanitize", action="store_true", help="ASan+UBSan (T1); steps 2-4 only")
        p.add_argument("--probe-seconds", type=float, default=5.0)
        p.add_argument("--probe-channels", type=int, action="append", choices=range(1, 17))
        p.set_defaults(func=cmd_t0)
    p = sub.add_parser("identical")
    common(p)
    p.set_defaults(func=cmd_identical, sanitize=False)
    p = sub.add_parser("selftest")
    common(p, None)
    p.add_argument("--sanitize", action="store_true")
    p.add_argument("--probe-seconds", type=float, default=2.0)
    p.add_argument("--probe-channels", type=int, action="append", choices=range(1, 17))
    p.set_defaults(func=cmd_selftest)
    p = sub.add_parser("t2")
    common(p, None)
    p.add_argument("module")
    p.add_argument("--out", help="where to write t2.json")
    p.set_defaults(func=cmd_t2, sanitize=False)
    p = sub.add_parser("app", help="local only: scratch lab copy with panel.json, engine + Studio + desktop "
                       "build; prints the Electron launch command")
    common(p)
    p.add_argument("--launch", action="store_true",
                   help="start the desktop app. It may open audio devices: only the owner passes this, "
                        "never agents or CI")
    p.set_defaults(func=cmd_app)
    p = sub.add_parser("ui", help="local only: Studio dev server (fake engine, no build) with save-and-reload "
                       "of panel.json and presentation.json")
    common(p)
    p.add_argument("--port", type=int, default=5173)
    p.add_argument("--no-serve", action="store_true", help="prepare the tree and exit (tests)")
    p.set_defaults(func=cmd_ui)
    p = sub.add_parser("scope")
    p.add_argument("--base")
    p.add_argument("--head")
    p.add_argument("--paths", nargs="*")
    p.set_defaults(func=cmd_scope)
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except Refused as error:
        print(f"wstest refused: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
