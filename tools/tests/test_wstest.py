# SPDX-License-Identifier: GPL-3.0-only
"""Fast wstest unit tests on a tiny fake lab (no CMake build). Run:
python -m unittest discover -s tools/tests -v"""
from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import wstest  # noqa: E402

CATALOG_H = """// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "../../modules/existing/dsp.h"
#include "../../sdk/examples/random_source/dsp.h"
#include "module_catalog.inc"
"""


def git(cwd: Path, *args: str) -> str:
    env = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@example.invalid",
               GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@example.invalid")
    return subprocess.run(["git", *args], cwd=cwd, env=env, check=True, capture_output=True,
                          text=True).stdout


def write(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def module(folder: Path, cls: str = "ns::Thing", extra: str = "") -> Path:
    write(folder / "dsp.h", "// SPDX-License-Identifier: GPL-3.0-only\n#pragma once\n")
    write(folder / "module.cpp", f'// SPDX-License-Identifier: GPL-3.0-only\n#include "dsp.h"\n'
                                 f"WS_REGISTER_MODULE({cls})\n{extra}")
    return folder


class FakeLab(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        base = Path(self.tmp.name)
        self.upstream = base / "upstream"
        write(self.upstream / "engine/CMakeLists.txt", "add_subdirectory(../modules/existing existing)\n")
        write(self.upstream / "engine/control/module_catalog.h", CATALOG_H)
        write(self.upstream / "engine/control/module_catalog.inc", "WS_APP_MODULE(existing::Existing)\n")
        module(self.upstream / "modules/existing", "existing::Existing")
        write(self.upstream / "modules/existing/module.json", json.dumps(
            {"id": "essentials.existing", "parameters": {"a": 0}, "inputs": {"in": "signal"},
             "outputs": {"out": "signal"}}))
        write(self.upstream / "README.md", "lab\n")
        git(self.upstream, "init", "-q")
        git(self.upstream, "add", ".")
        git(self.upstream, "commit", "-q", "-m", "lab")
        self.sha = git(self.upstream, "rev-parse", "HEAD").strip()
        self.root = base / "test-repo"
        write(self.root / "LAB_PIN", f"repo=example/lab\ncommit={self.sha}\npinned=2026-11-26\n")
        os.environ["WSTEST_LAB_URL"] = str(self.upstream)
        self.lab = wstest.Lab(self.root)
        self.lab.fetch()

    def tearDown(self):
        os.environ.pop("WSTEST_LAB_URL", None)
        self.tmp.cleanup()

    def new_module(self, name="thing", cls="ns::Thing") -> wstest.Module:
        folder = module(self.root / "modules" / name, cls)
        m = wstest.Module(name, folder, "new", wstest.registration_class(folder))
        return m


class RegistrationTests(FakeLab):
    def test_single_registration_line_names_the_class(self):
        self.assertEqual(wstest.registration_class(self.new_module().folder), "ns::Thing")

    def test_zero_or_two_registration_lines_refuse(self):
        none = self.root / "modules/none"
        write(none / "module.cpp", "// SPDX-License-Identifier: GPL-3.0-only\n// WS_REGISTER_MODULE(X)\n")
        with self.assertRaisesRegex(wstest.Refused, "found 0"):
            wstest.registration_class(none)
        two = module(self.root / "modules/two", "a::A", "WS_REGISTER_MODULE(b::B)\n")
        with self.assertRaisesRegex(wstest.Refused, "found 2"):
            wstest.registration_class(two)

    def test_new_module_gets_exactly_three_edits(self):
        edits = wstest.registration_edits(self.lab.dir, [self.new_module()])
        self.assertEqual(sorted(edits), sorted([wstest.ENGINE_CMAKE, wstest.CATALOG_H, wstest.CATALOG_INC]))
        self.assertTrue(edits[wstest.ENGINE_CMAKE].endswith("add_subdirectory(../modules/thing thing)\n"))
        self.assertIn('#include "../../modules/existing/dsp.h"\n#include "../../modules/thing/dsp.h"\n',
                      edits[wstest.CATALOG_H])
        self.assertTrue(edits[wstest.CATALOG_INC].endswith("WS_APP_MODULE(ns::Thing)\n"))

    def test_override_adds_nothing_to_the_catalogue(self):
        folder = module(self.root / "modules/existing", "existing::Existing")
        m = wstest.Module("existing", folder, "override", "existing::Existing")
        self.assertEqual(wstest.registration_edits(self.lab.dir, [m]), {})

    def test_after_mb2_copies_only(self):
        (self.lab.dir / wstest.CATALOG_INC).unlink()
        self.assertEqual(wstest.registration_edits(self.lab.dir, [self.new_module()]), {})


class ScratchTests(FakeLab):
    def test_scratch_is_pin_plus_folder_plus_edits(self):
        scratch = self.root / "build/scratch"
        wstest.prepare_tree(self.lab, scratch, [self.new_module()])
        self.assertTrue((scratch / "modules/thing/module.cpp").is_file())
        self.assertEqual((scratch / "README.md").read_text(), "lab\n")

    def test_any_other_scratch_change_is_refused(self):
        scratch = self.root / "build/scratch"
        tree = wstest.prepare_tree(self.lab, scratch, [self.new_module()])
        (scratch / "README.md").write_text("patched\n")
        with self.assertRaisesRegex(wstest.Refused, "changed: README.md"):
            wstest.verify_scratch(scratch, tree)
        (scratch / "README.md").write_text("lab\n")
        write(scratch / "engine/extra.cpp", "")
        with self.assertRaisesRegex(wstest.Refused, "unexpected: engine/extra.cpp"):
            wstest.verify_scratch(scratch, tree)

    def test_scratch_is_recreated_every_run(self):
        scratch = self.root / "build/scratch"
        m = self.new_module()
        wstest.prepare_tree(self.lab, scratch, [m])
        write(scratch / "stray.txt", "left over")
        wstest.prepare_tree(self.lab, scratch, [m])
        self.assertFalse((scratch / "stray.txt").exists())

    def test_override_replaces_the_lab_folder(self):
        folder = self.root / "modules/existing"
        module(folder, "existing::Existing")
        m = wstest.Module("existing", folder, "override", "existing::Existing")
        scratch = self.root / "build/scratch"
        wstest.prepare_tree(self.lab, scratch, [m])
        self.assertFalse((scratch / "modules/existing/module.json").exists())

    def test_pinned_lab_must_be_at_the_pin_and_clean(self):
        (self.lab.dir / "README.md").write_text("edited\n")
        with self.assertRaisesRegex(wstest.Refused, "modified tracked files"):
            self.lab.verify()


class HelperTests(unittest.TestCase):
    def test_scope(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name in ("a", "b"):
                write(root / "modules" / name / "module.cpp", "")
            write(root / "modules/README.md", "")
            self.assertEqual(wstest.scope(["README.md", "modules/README.md"], root),
                             {"run": False, "all": False, "modules": []})
            self.assertEqual(wstest.scope(["modules/b/dsp.h"], root),
                             {"run": True, "all": False, "modules": ["b"]})
            self.assertEqual(wstest.scope(["modules/gone/dsp.h"], root),
                             {"run": True, "all": False, "modules": []})
            for path in ("tools/wstest.py", "LAB_PIN", ".github/workflows/t0.yml"):
                self.assertEqual(wstest.scope([path], root), {"run": True, "all": True, "modules": ["a", "b"]})

    def test_at_defaults_drops_new_parameters_and_ports(self):
        case = {"patch": {"modules": [{"id": 1, "module": "x.m", "parameters": {"old": 1, "new": 2}},
                                      {"id": 2, "module": "x.other", "parameters": {"new": 3}}],
                          "connections": [{"from": {"module": 2, "port": "o"}, "to": {"module": 1, "port": "new_in"}},
                                          {"from": {"module": 2, "port": "o"}, "to": {"module": 1, "port": "old_in"}}]}}
        out = wstest.at_defaults(case, "x.m", {"parameters": {"new"}, "inputs": {"new_in"}, "outputs": set()})
        self.assertEqual(out["patch"]["modules"][0]["parameters"], {"old": 1})
        self.assertEqual(out["patch"]["modules"][1]["parameters"], {"new": 3})
        self.assertEqual([c["to"]["port"] for c in out["patch"]["connections"]], ["old_in"])
        self.assertEqual(case["patch"]["modules"][0]["parameters"]["new"], 2)

    def test_fixture_expectations_name_steps(self):
        data = json.loads((wstest.ROOT / "tools/fixtures/expected.json").read_text())
        self.assertEqual(data["default"], {"gain": None, "nan_gain": "demos", "alloc_gain": "cpu_probe"})


class T2CtestRulingTests(FakeLab):
    """Lab #652 ruling: only the named catalogue-sync CTests defer, only for a catalogue change."""
    SYNC = ["ui_catalogue_sync", "ui_patch_conformance", "abi2_sidecar_snapshot_tests"]

    def test_new_module_defers_only_the_named_sync_tests(self):
        verdict = wstest.classify_ctest(8, self.SYNC, True)
        self.assertEqual(verdict["status"], "pass")
        self.assertEqual(verdict["deferred_to_promotion"], sorted(self.SYNC))
        verdict = wstest.classify_ctest(8, self.SYNC + ["abi2_engine_tests"], True)
        self.assertEqual((verdict["status"], verdict["failed"]), ("fail", ["abi2_engine_tests"]))

    def test_no_catalogue_change_defers_nothing(self):
        verdict = wstest.classify_ctest(8, ["ui_catalogue_sync"], False)
        self.assertEqual((verdict["status"], verdict["deferred_to_promotion"]), ("fail", []))

    def test_unexplained_ctest_failure_fails(self):
        self.assertEqual(wstest.classify_ctest(8, [], True)["status"], "fail")
        self.assertEqual(wstest.classify_ctest(0, [], False)["status"], "pass")

    def test_catalogue_changed(self):
        self.assertTrue(wstest.catalogue_changed(self.lab.dir, self.new_module()))
        folder = self.root / "modules/existing"
        import shutil
        shutil.copytree(self.lab.dir / "modules/existing", folder)
        override = wstest.Module("existing", folder, "override")
        self.assertFalse(wstest.catalogue_changed(self.lab.dir, override))
        write(folder / "dsp.h", "// SPDX-License-Identifier: GPL-3.0-only\n#pragma once\n// edit\n")
        self.assertTrue(wstest.catalogue_changed(self.lab.dir, override))


if __name__ == "__main__":
    unittest.main()
