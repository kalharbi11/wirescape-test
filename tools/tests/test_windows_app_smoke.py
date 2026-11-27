# SPDX-License-Identifier: GPL-3.0-only
"""Fast tests for Windows build/lifecycle orchestration (no devices)."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import windows_app_smoke
import wstest


class WindowsSmokeTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        for name in ("panel", "plain", "untouched"):
            folder = self.root / "modules" / name
            folder.mkdir(parents=True)
            (folder / "module.cpp").touch()
            if name != "plain":
                (folder / "panel.json").touch()

    def test_build_check_and_lifecycle_only_scoped_panels(self):
        run = Mock()
        windows_app_smoke.smoke(["plain", "panel"], self.root, run, console_flag=16)
        calls = run.call_args_list
        self.assertEqual(len(calls), 3)
        self.assertEqual(calls[0].args[0][2:], ["app", "panel"])
        self.assertEqual(calls[0].kwargs["env"]["ELECTRON_SKIP_BINARY_DOWNLOAD"], "1")
        self.assertEqual(calls[1].args[0][2:], ["ui", "panel", "--check"])
        self.assertEqual(calls[2].args[0][2:], ["--interrupt-worker", "panel"])
        self.assertEqual(calls[2].kwargs["creationflags"], 16)
        self.assertTrue(all(c.kwargs["check"] for c in calls))
        self.assertFalse(any("--launch" in c.args[0] for c in calls))

    def test_empty_nonpanel_and_bad_scope(self):
        run = Mock()
        windows_app_smoke.smoke([], self.root, run, console_flag=16)
        windows_app_smoke.smoke(["plain"], self.root, run, console_flag=16)
        with self.assertRaises(ValueError):
            windows_app_smoke.smoke(["panel", "../outside"], self.root, run, console_flag=16)
        run.assert_not_called()

    def test_fail_fast_each_command(self):
        for i in range(3):
            with self.subTest(i=i):
                run = Mock(side_effect=[None] * i + [subprocess.CalledProcessError(1, ["fake"])])
                with self.assertRaises(subprocess.CalledProcessError):
                    windows_app_smoke.smoke(["panel", "untouched"], self.root, run, console_flag=16)
                self.assertEqual(run.call_count, i + 1)

    def test_windows_taskkill_tree_and_wait(self):
        proc = Mock(pid=123)
        proc.poll.return_value = None
        with patch.object(wstest.os, "name", "nt"), patch.object(wstest.subprocess, "run") as run:
            run.return_value = subprocess.CompletedProcess([], 0, b"", b"")
            wstest.stop_process(proc)
        self.assertEqual(run.call_args.args[0], ["taskkill", "/PID", "123", "/T", "/F"])
        proc.wait.assert_called_once_with(timeout=10)
        proc.kill.assert_not_called()

    def test_windows_taskkill_failure_is_not_silent(self):
        proc = Mock(pid=123)
        proc.poll.return_value = None
        with patch.object(wstest.os, "name", "nt"), patch.object(wstest.subprocess, "run") as run:
            run.return_value = subprocess.CompletedProcess([], 1, b"", b"denied")
            with self.assertRaises(wstest.Refused):
                wstest.stop_process(proc)
        proc.kill.assert_called_once()
        proc.wait.assert_called_once_with(timeout=10)
