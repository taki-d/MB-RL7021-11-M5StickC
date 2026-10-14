"""Exercise capture-window accounting without hardware or wall-clock waits."""
import contextlib
import io
import json
from pathlib import Path
import runpy
import types
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "measure_polling.py"


class CaptureTests(unittest.TestCase):
    def capture(self, warmup, seconds):
        clock = [0.0]
        lines = iter([
            b"999\n",
            b"METER request t=20 tid=1 epc=E7 opc=1\n",
            b"METER response t=120 tid=1 epc=E7 elapsed=100 loop_gap=10\n",
            b"256\n",
            b"METER request t=130 tid=2 epc=E7 opc=1\n",
        ])

        class FakeSerial:
            def __init__(self, *args, **kwargs):
                pass

            def __enter__(self):
                return self

            def __exit__(self, *args):
                pass

            def readline(self):
                clock[0] += 1
                return next(lines)

        output = io.StringIO()
        argv = [str(SCRIPT), "fake", "--seconds", str(seconds),
                "--warmup-seconds", str(warmup)]
        with patch.dict("sys.modules", {"serial": types.SimpleNamespace(Serial=FakeSerial)}), \
                patch("sys.argv", argv), patch("time.monotonic", lambda: clock[0]), \
                contextlib.redirect_stdout(output):
            runpy.run_path(str(SCRIPT), run_name="__main__")
        return [json.loads(line) for line in output.getvalue().splitlines()]

    def test_warmup_excludes_samples_and_rebases_time(self):
        rows = self.capture(warmup=2, seconds=3)
        samples = [row for row in rows if row["event"] == "sample"]
        self.assertEqual(samples, [{"event": "sample", "at": 2.0, "watts": 256}])
        summary = next(row for row in rows if row["event"] == "summary")
        self.assertEqual(summary["warmup_s"], 2)
        self.assertEqual(summary["duration_s"], 3)
        self.assertEqual(summary["responses_ms"]["E7"]["n"], 1)
        self.assertEqual(summary["inflight_at_end"], 1)

    def test_zero_warmup_keeps_all_samples(self):
        rows = self.capture(warmup=0, seconds=5)
        samples = [row["watts"] for row in rows if row["event"] == "sample"]
        self.assertEqual(samples, [999, 256])


if __name__ == "__main__":
    unittest.main()
