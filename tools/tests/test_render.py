# SPDX-License-Identifier: GPL-3.0-only
"""Render preview CLI tests: real scratch checks, fake native renderer."""
import json
import struct
from pathlib import Path
from unittest.mock import patch

from test_wstest import FakeLab, write, wstest


class RenderTests(FakeLab):
    def setUp(self):
        super().setUp()
        self.mod = self.new_module()
        self.demo = self.mod.folder / "demos"
        for name in ("a", "b"):
            write(self.demo / (name + ".json"), json.dumps(dict(
                name=name, rate=48000, block=128, seconds=1, seed=7,
                peak_limit=1, patch={}, events=[], windows=[], frequencies=[])))
        self.addCleanup(patch.stopall)
        patch.object(wstest, "ROOT", self.root).start()
        patch.object(wstest, "Lab", return_value=self.lab).start()
        patch.object(wstest, "resolve_module", return_value=self.mod).start()
        self.build = patch.object(wstest, "cmake_build", return_value=(True, "")).start()
        self.binary = patch.object(wstest, "binary", return_value=Path("renderer")).start()
        self.render = patch.object(wstest, "render_case", side_effect=self.fake_render).start()
        self.check = patch.object(wstest.Session, "check", side_effect=AssertionError("check")).start()
        self.tests = patch.object(wstest.Session, "tests", side_effect=AssertionError("tests")).start()
        self.probe = patch.object(wstest.Session, "cpu_probe", side_effect=AssertionError("probe")).start()
        self.messages = patch.object(wstest, "log").start()
        self.out = self.root / "build/wstest-out/render/thing"

    def fake_render(self, render, case, out, rate=None, block=None):
        out.mkdir(parents=True, exist_ok=True)
        samples = struct.pack("<2f", 0.25, -0.5)
        header = struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", 36 + len(samples),
                             b"WAVE", b"fmt ", 16, 3, 2, rate or case["rate"],
                             (rate or case["rate"]) * 8, 8, 32, b"data", len(samples))
        (out / "audio.wav").write_bytes(header + samples)
        return "hash"

    def test_all_demos_and_only_renderer_target(self):
        self.assertEqual(wstest.main(["render", "thing"]), 0)
        self.assertEqual(self.build.call_count, 1)
        self.assertEqual(self.build.call_args.args[3], ["wirescape_render"])
        self.assertEqual(self.render.call_count, 2)
        self.assertTrue((self.out / "a/audio.wav").is_file())
        self.assertTrue((self.out / "b/audio.wav").is_file())
        self.assertIn("peak=0.5", str(self.messages.call_args_list))
        self.assertIn("finite=true", str(self.messages.call_args_list))
        for m in (self.check, self.tests, self.probe):
            m.assert_not_called()

    def test_selected_demo_and_rate_block(self):
        self.assertEqual(wstest.main(["render", "thing", "--demo", "b.json",
                                      "--rate", "44100", "--block", "48"]), 0)
        self.render.assert_called_once()
        args = self.render.call_args
        self.assertEqual(args.args[1]["name"], "b")
        self.assertEqual(args.kwargs, {"rate": 44100, "block": 48})
        self.assertFalse((self.out / "a").exists())

    def test_missing_demo_refused_before_build(self):
        self.assertNotEqual(wstest.main(["render", "thing", "--demo", "missing.json"]), 0)
        self.build.assert_not_called()

    def test_invalid_rate_refused_before_build(self):
        self.assertNotEqual(wstest.main(["render", "thing", "--rate", "0"]), 0)
        self.build.assert_not_called()

    def test_nonfinite_output_fails(self):
        def nonfinite(render, case, out, rate=None, block=None):
            self.fake_render(render, case, out, rate, block)
            path = out / "audio.wav"
            path.write_bytes(path.read_bytes()[:44] + struct.pack("<2f", float("nan"), 0))
            return "hash"
        self.render.side_effect = nonfinite
        self.assertNotEqual(wstest.main(["render", "thing", "--demo", "a.json"]), 0)
        self.assertIn("finite=false", str(self.messages.call_args_list))

    def test_scratch_mutation_refused_even_on_render_failure(self):
        def corrupt(*args, **kwargs):
            scratch = self.root / "build" / f"scratch-{self.lab.sha7}"
            write(scratch / "README.md", "changed")
            raise wstest.StepFailed("render", "fake failure")
        self.render.side_effect = corrupt
        self.assertEqual(wstest.main(["render", "thing", "--demo", "a.json"]), 2)
