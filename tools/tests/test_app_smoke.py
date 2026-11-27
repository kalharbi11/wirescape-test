# SPDX-License-Identifier: GPL-3.0-only
"""Device-free tests for the Linux app smoke orchestration."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import app_smoke


class AppSmokeTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        (self.root / "LAB_PIN").write_text("abcdef0123456789\n")
        for name in ("panel", "plain", "untouched"):
            folder = self.root / "modules" / name
            folder.mkdir(parents=True)
            (folder / "module.cpp").touch()
            if name != "plain":
                (folder / "panel.json").write_text("{}")

    def test_scoped_panel_only_and_build_only(self):
        run = Mock()
        app_smoke.smoke(["plain", "panel"], self.root, run)
        calls = run.call_args_list
        self.assertEqual(len(calls), 4)
        commands = [c.args[0] for c in calls]
        self.assertEqual(commands[0][2:], ["ui", "panel", "--no-serve"])
        self.assertEqual(commands[1][2:], ["app", "panel"])
        self.assertEqual(calls[1].kwargs["env"]["ELECTRON_SKIP_BINARY_DOWNLOAD"], "1")
        self.assertEqual(commands[2], ["npx", "playwright", "install", "--with-deps", "chromium"])
        self.assertEqual(calls[2].kwargs["cwd"], self.root / "build/app-abcdef0/apps/studio")
        self.assertEqual(commands[3][2:5], ["shot", "panel", "--out"])
        self.assertEqual(Path(commands[3][-1]), self.root / "build/shots/panel.png")
        self.assertTrue(all(c.kwargs["check"] for c in calls))
        self.assertFalse(any("--launch" in command for command in commands))

    def test_nonpanel_and_empty_scope_skip(self):
        run = Mock()
        app_smoke.smoke(["plain"], self.root, run)
        app_smoke.smoke([], self.root, run)
        run.assert_not_called()

    def test_wide_scope_runs_all_panels_and_installs_browser_once(self):
        run = Mock()
        app_smoke.smoke(["panel", "plain", "untouched"], self.root, run)
        commands = [c.args[0] for c in run.call_args_list]
        self.assertEqual([c[3] for c in commands if len(c) > 3 and c[2] == "shot"],
                         ["panel", "untouched"])
        self.assertEqual(sum(c[0] == "npx" for c in commands), 1)

    def test_each_failure_propagates(self):
        for index in range(4):
            with self.subTest(index=index):
                failure = subprocess.CalledProcessError(7, ["fake"])
                run = Mock(side_effect=[None] * index + [failure])
                with self.assertRaises(subprocess.CalledProcessError):
                    app_smoke.smoke(["panel", "untouched"], self.root, run)
                self.assertEqual(run.call_count, index + 1)

    def test_invalid_scope_refused(self):
        run = Mock()
        with self.assertRaises(ValueError):
            app_smoke.smoke(["../outside"], self.root, run)
        run.assert_not_called()
