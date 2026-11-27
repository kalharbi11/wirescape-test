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
    def test_direct_override_keeps_host_tests_and_executes_module_test(self):
        folder = self.new_module("groove").folder
        regression = "abi2_groove_tests"
        write(folder / "tests.cpp", "// fixture\n")
        write(folder / "CMakeLists.txt",
              f'add_custom_target({regression})\n'
              f'add_test(NAME {regression} COMMAND "${{CMAKE_COMMAND}}" -E true)\n')
        override = wstest.Module("groove", folder, "override", "ns::Thing")
        edits = wstest.registration_edits(self.lab.dir, [override])
        self.assertEqual(set(edits), {wstest.ENGINE_CMAKE})

        scratch = self.root / "scratch"
        write(scratch / "CMakeLists.txt",
              'cmake_minimum_required(VERSION 3.20)\nproject(Fixture NONE)\nenable_testing()\n'
              'add_custom_target(groove_runtime_tests)\n'
              'add_test(NAME groove_runtime_tests COMMAND "${CMAKE_COMMAND}" -E true)\n'
              'add_subdirectory(engine)\n')
        engine = edits[wstest.ENGINE_CMAKE].replace(
            "add_subdirectory(../modules/existing existing)\n", "")
        write(scratch / wstest.ENGINE_CMAKE, engine)
        write(scratch / "modules/groove/CMakeLists.txt", (folder / "CMakeLists.txt").read_text())
        build = self.root / "build"
        subprocess.run(["cmake", "-S", str(scratch), "-B", str(build)], check=True,
                       capture_output=True, text=True)
        names = wstest.ctest_names(build, "Release")
        selected = wstest.module_tests(override, names)
        self.assertCountEqual(selected, [regression, "groove_runtime_tests"])
        result = subprocess.run(["ctest", "--test-dir", str(build), "-C", "Release",
                                 "-R", "^(" + "|".join(selected) + ")$", "--no-tests=error"],
                                check=True, capture_output=True, text=True)
        self.assertRegex(result.stdout, r"Test\s+#\d+: abi2_groove_tests\s+\.+\s+Passed")
        self.assertRegex(result.stdout, r"Test\s+#\d+: groove_runtime_tests\s+\.+\s+Passed")
        self.assertIn(regression, wstest.module_tests(override, ["groove_runtime_tests"]))

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

    def test_new_module_defers_only_the_s0_snapshot(self):
        # §9.1: the two UI CTests gate again; only the S0 snapshot defers.
        verdict = wstest.classify_ctest(8, self.SYNC, True)
        self.assertEqual(verdict["status"], "fail")
        self.assertEqual(verdict["failed"], ["ui_catalogue_sync", "ui_patch_conformance"])
        verdict = wstest.classify_ctest(8, ["abi2_sidecar_snapshot_tests"], True)
        self.assertEqual(verdict["status"], "pass")
        self.assertEqual(verdict["deferred_to_promotion"], ["abi2_sidecar_snapshot_tests"])
        verdict = wstest.classify_ctest(8, ["abi2_sidecar_snapshot_tests", "abi2_engine_tests"], True)
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


class PresentationTests(FakeLab):
    """0024 §9.1: presentation.json goes into the scratch sidecar and the pin's generator runs."""
    SIDECAR = {"SPDX-License-Identifier": "GPL-3.0-only", "note": "n",
               "modules": [{"id": "essentials.existing", "name": "Existing", "ports": [], "parameters": []}]}
    GENERATOR = ("# SPDX-License-Identifier: GPL-3.0-only\nimport json, pathlib\n"
                 "root = pathlib.Path(__file__).resolve().parents[1]\n"
                 "d = json.loads((root / 'apps/studio/public/catalogue-presentation.json').read_text())\n"
                 "(root / 'apps/studio/public/wirescape_modules.json').write_text("
                 "json.dumps([m['id'] for m in d['modules']]) + '\\n')\n")

    def setUp(self):
        super().setUp()
        lab = self.upstream
        write(lab / wstest.SIDECAR, json.dumps(self.SIDECAR, indent=1) + "\n")
        write(lab / wstest.UI_CATALOGUE, '["essentials.existing"]\n')
        write(lab / wstest.UI_GENERATOR, self.GENERATOR)
        write(lab / wstest.PANELS_DIR / "index.json", '["essentials.existing"]\n')
        write(lab / wstest.PANELS_DIR / "essentials.existing.json", '{"width": 10}\n')
        write(lab / "modules/existing/dsp.h", '// SPDX-License-Identifier: GPL-3.0-only\n#pragma once\n'
              'static constexpr ws::Info info{"essentials.existing","Existing",1};\n')
        git(lab, "add", ".")
        git(lab, "commit", "-q", "-m", "sidecar")
        self.sha = git(lab, "rev-parse", "HEAD").strip()
        write(self.root / "LAB_PIN", f"repo=example/lab\ncommit={self.sha}\npinned=2026-11-26\n")
        self.lab = wstest.Lab(self.root)
        self.lab.fetch()

    def thing(self, presentation: dict | None, ident: str = "test.thing"):
        m = self.new_module()
        write(m.folder / "dsp.h", f'// SPDX-License-Identifier: GPL-3.0-only\n#pragma once\n'
              f'static constexpr ws::Info info{{"{ident}","Thing",1}};\n')
        if presentation is not None:
            write(m.folder / "presentation.json", json.dumps(presentation))
        return m

    def sidecar(self, scratch):
        return json.loads((scratch / wstest.SIDECAR).read_text(encoding="utf-8"))

    def test_new_module_is_appended_and_the_generator_runs(self):
        m = self.thing({"SPDX-License-Identifier": "GPL-3.0-only", "id": "test.thing", "name": "Thing"})
        scratch = self.root / "build/scratch"
        wstest.prepare_tree(self.lab, scratch, [m])
        mods = self.sidecar(scratch)["modules"]
        self.assertEqual([x["id"] for x in mods], ["essentials.existing", "test.thing"])
        self.assertNotIn("SPDX-License-Identifier", mods[1])
        self.assertEqual(json.loads((scratch / wstest.UI_CATALOGUE).read_text()),
                         ["essentials.existing", "test.thing"])

    # ---- panel.json (wstest app / ui; never T0)
    def panel(self, panel: dict | str, ident: str = "test.thing", presentation: bool = True):
        m = self.thing({"id": ident, "name": "Thing"} if presentation else None, ident)
        write(m.folder / "panel.json", panel if isinstance(panel, str) else json.dumps(panel))
        return m

    def app_tree(self, m):
        scratch = self.root / "build/app"
        wstest.prepare_tree(self.lab, scratch, [m], panels=True, keep=wstest.APP_KEEP)
        return scratch

    def test_panel_is_written_without_spdx_and_indexed_once(self):
        m = self.panel({"SPDX-License-Identifier": "GPL-3.0-only", "width": 7, "hidden": ["a"]})
        scratch = self.app_tree(m)
        panels = scratch / wstest.PANELS_DIR
        self.assertEqual(json.loads((panels / "test.thing.json").read_text()), {"width": 7, "hidden": ["a"]})
        self.assertEqual(json.loads((panels / "index.json").read_text()), ["essentials.existing", "test.thing"])
        self.assertFalse((scratch / "modules/thing/panel.json").exists())
        self.app_tree(m)  # rerun: no duplicate
        self.assertEqual(json.loads((panels / "index.json").read_text()), ["essentials.existing", "test.thing"])

    def test_panel_for_an_override_replaces_the_lab_file(self):
        folder = self.root / "modules/existing"
        module(folder, "existing::Existing")
        write(folder / "dsp.h", '#pragma once\nstatic constexpr ws::Info info{"essentials.existing","E",1};\n')
        write(folder / "panel.json", '{"width": 99}')
        m = wstest.Module("existing", folder, "override", "existing::Existing")
        scratch = self.app_tree(m)
        panels = scratch / wstest.PANELS_DIR
        self.assertEqual(json.loads((panels / "essentials.existing.json").read_text()), {"width": 99})
        self.assertEqual(json.loads((panels / "index.json").read_text()), ["essentials.existing"])

    def test_bad_panels_are_refused(self):
        with self.assertRaisesRegex(wstest.Refused, "id does not match"):
            self.app_tree(self.panel({"id": "other.id"}))
        with self.assertRaisesRegex(wstest.Refused, "not JSON"):
            self.app_tree(self.panel("{nope"))
        with self.assertRaisesRegex(wstest.Refused, r"unknown keys \['colour'\]"):
            self.app_tree(self.panel({"colour": "red"}))
        with self.assertRaisesRegex(wstest.Refused, "one PanelOverride object"):
            self.app_tree(self.panel("[1]"))

    def test_t0_tree_ignores_panel_json(self):
        m = self.panel({"width": 7})
        with_panel = wstest.prepare_tree(self.lab, self.root / "build/scratch", [m])
        (m.folder / "panel.json").unlink()
        without = wstest.prepare_tree(self.lab, self.root / "build/scratch2", [m])
        self.assertEqual(sorted(with_panel), sorted(without))
        self.assertFalse(any(k.startswith(wstest.PANELS_DIR + "/test.") for k in with_panel))
        self.assertFalse((self.root / "build/scratch/modules/thing/panel.json").exists())
        self.assertEqual(json.loads((self.root / "build/scratch" / wstest.PANELS_DIR / "index.json").read_text()),
                         ["essentials.existing"])

    def test_app_refresh_keeps_installs_and_follows_edits(self):
        m = self.panel({"width": 7})
        scratch, _ = wstest.prepare_app_tree(self.lab, [m])
        write(scratch / "apps/studio/node_modules/x/index.js", "// installed")
        tracked = self.lab.tracked()
        write(m.folder / "panel.json", '{"width": 8}')
        write(m.folder / "presentation.json", json.dumps({"id": "test.thing", "name": "Renamed"}))
        wstest.refresh_app_files(self.lab, scratch, [m], tracked)
        self.assertEqual(json.loads((scratch / wstest.PANELS_DIR / "test.thing.json").read_text()), {"width": 8})
        self.assertEqual(self.sidecar(scratch)["modules"][-1]["name"], "Renamed")
        (m.folder / "panel.json").unlink()  # removed: the tree is rebuilt, node_modules stays
        wstest.refresh_app_files(self.lab, scratch, [m], tracked)
        self.assertFalse((scratch / wstest.PANELS_DIR / "test.thing.json").exists())
        self.assertTrue((scratch / "apps/studio/node_modules/x/index.js").is_file())

    def test_without_presentation_nothing_changes(self):
        scratch = self.root / "build/scratch"
        wstest.prepare_tree(self.lab, scratch, [self.thing(None)])
        self.assertEqual((scratch / wstest.UI_CATALOGUE).read_text(), '["essentials.existing"]\n')

    def test_override_replaces_its_entry(self):
        folder = self.root / "modules/existing"
        import shutil
        shutil.copytree(self.lab.dir / "modules/existing", folder)
        write(folder / "presentation.json", json.dumps({"id": "essentials.existing", "name": "Renamed"}))
        m = wstest.Module("existing", folder, "override", "existing::Existing")
        scratch = self.root / "build/scratch"
        wstest.prepare_tree(self.lab, scratch, [m])
        mods = self.sidecar(scratch)["modules"]
        self.assertEqual(len(mods), 1)
        self.assertEqual(mods[0]["name"], "Renamed")

    def test_wrong_id_or_duplicate_is_refused(self):
        with self.assertRaises(wstest.Refused):
            wstest.prepare_tree(self.lab, self.root / "build/s1", [self.thing({"id": "test.other"})])
        with self.assertRaises(wstest.Refused):
            wstest.prepare_tree(self.lab, self.root / "build/s2",
                                [self.thing({"id": "essentials.existing"}, "essentials.existing")])

    def test_generator_touching_another_file_is_refused(self):
        write(self.upstream / wstest.UI_GENERATOR, self.GENERATOR + "(root / 'README.md').write_text('x')\n")
        git(self.upstream, "commit", "-qam", "bad generator")
        sha = git(self.upstream, "rev-parse", "HEAD").strip()
        write(self.root / "LAB_PIN", f"repo=example/lab\ncommit={sha}\npinned=2026-11-26\n")
        lab = wstest.Lab(self.root)
        lab.fetch()
        with self.assertRaises(wstest.Refused):
            wstest.prepare_tree(lab, self.root / "build/s3", [self.thing({"id": "test.thing"})])


class S0OthersTests(unittest.TestCase):
    """0024 §9.1 rule 4: the gating s0_catalogue_others check."""
    A = {"id": "a", "ports": [{"id": "in", "direction": "input"}, {"id": "out", "direction": "output"}],
         "parameters": [{"id": "p"}], "name": "A"}
    B = {"id": "b", "ports": [], "parameters": [], "name": "B"}
    FIXTURE = {"modules": [A, B]}

    def test_new_module_must_leave_the_fixture_exactly(self):
        new = {"id": "n", "ports": [], "parameters": []}
        self.assertTrue(wstest.compare_s0(self.FIXTURE, {"modules": [self.A, new, self.B]}, "n", "new")[0])
        self.assertFalse(wstest.compare_s0(self.FIXTURE, {"modules": [self.B, self.A, new]}, "n", "new")[0])
        changed = dict(self.B, name="B2")
        self.assertFalse(wstest.compare_s0(self.FIXTURE, {"modules": [self.A, changed, new]}, "n", "new")[0])
        self.assertFalse(wstest.compare_s0(self.FIXTURE, {"modules": [self.A, self.B]}, "n", "new")[0])

    def test_override_may_only_append(self):
        grown = dict(self.A, parameters=[{"id": "p"}, {"id": "q"}],
                     ports=[{"id": "in", "direction": "input"}, {"id": "in2", "direction": "input"},
                            {"id": "out", "direction": "output"}])
        self.assertTrue(wstest.compare_s0(self.FIXTURE, {"modules": [grown, self.B]}, "a", "override")[0])
        reordered = dict(self.A, parameters=[{"id": "q"}, {"id": "p"}])
        self.assertFalse(wstest.compare_s0(self.FIXTURE, {"modules": [reordered, self.B]}, "a", "override")[0])
        renamed = dict(self.A, name="A2")
        self.assertFalse(wstest.compare_s0(self.FIXTURE, {"modules": [renamed, self.B]}, "a", "override")[0])
        other = dict(self.B, name="B2")
        self.assertFalse(wstest.compare_s0(self.FIXTURE, {"modules": [self.A, other]}, "a", "override")[0])


class BlockSkipTests(unittest.TestCase):
    """0024 §9.2: blocks 16, 17, 48, 128, 512; skip only when the demo refuses like the lab case."""
    def test_blocks(self):
        self.assertEqual(wstest.T2_BLOCKS, (16, 17, 48, 128, 512))

    def test_skip_rule(self):
        reason = "renderer exit 1: wirescape_render: patch load failed: Cannot prepare graph"
        self.assertEqual(wstest.demo_refusal_status(reason, reason), "skip")
        self.assertEqual(wstest.demo_refusal_status(reason, None), "fail")
        self.assertEqual(wstest.demo_refusal_status("renderer exit 1: other", reason), "fail")


if __name__ == "__main__":
    unittest.main()
