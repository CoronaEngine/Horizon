import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock


_path = Path(__file__).with_name("run_capability_audit.py")
_spec = importlib.util.spec_from_file_location("capability_audit", _path)
audit = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(audit)


class CapabilityAuditTests(unittest.TestCase):
    def collect(self, lines, case="numeric-probe"):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "probe.log"
            log.write_text("\n".join(lines), encoding="utf-8")
            return audit.collect_observations([dict(case=case, log=str(log))])[0]

    def test_policy_observations_allow_nonfinite_values(self):
        observation = self.collect([
            "FLOAT_MATH op=round input=0.5 actual=1",
            "FLOAT_MATH op=acosh input=-1 actual=nan",
            "FLOAT_MATH op=sinh input=100 actual=inf",
        ])
        self.assertEqual(observation["rows"], 3)
        self.assertEqual(observation["parse_errors"], [])

    def test_unparseable_or_missing_observation_field_is_an_error(self):
        observation = self.collect([
            "FLOAT_MATH op=round input=0.5 actual=invalid",
            "NARROW direct=1",
        ])
        self.assertEqual(observation["rows"], 0)
        self.assertEqual(len(observation["parse_errors"]), 2)

    def run_audit(self, case, lines=(), returncode=0):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = next(name for name, cases in audit.CASES.items() if case in cases)
            (root / (target + (".exe" if audit.os.name == "nt" else ""))).touch()

            def run(command, **kwargs):
                kwargs["stdout"].write("\n".join(lines).encode("utf-8"))
                return subprocess.CompletedProcess(command, returncode)

            argv = ["audit", "--runtime", str(root), "--output", str(root / "output"), "--case", case]
            with mock.patch.object(audit.sys, "argv", argv), \
                 mock.patch.object(audit.subprocess, "run", side_effect=run) as process, \
                 mock.patch("builtins.print"):
                status = audit.main()
            self.assertEqual(process.call_count, 1)
            report = json.loads((root / "output/results.json").read_text(encoding="utf-8"))
            self.assertEqual(report["processes"], 1)
            return status, report

    def test_case_without_observations_runs_once(self):
        status, report = self.run_audit("empty-accel")
        self.assertEqual(status, 0)
        self.assertEqual(report["incomplete_observations"], 0)

    def test_missing_expected_observations_fail(self):
        status, report = self.run_audit("half-intermediate")
        self.assertEqual(status, 1)
        self.assertEqual(report["incomplete_observations"], 1)

    def test_complete_observations_pass(self):
        rows = [f"HALF_MATH op=add input={i} actual=1 actual_bits=15360" for i in range(9)]
        status, report = self.run_audit("half-intermediate", rows)
        self.assertEqual(status, 0)
        self.assertEqual(report["observations"][0]["rows"], 9)

    def test_probe_or_validation_failure_fails_audit(self):
        for lines, returncode in (((), 1), (("Validation Error: VUID-test",), 0)):
            with self.subTest(lines=lines, returncode=returncode):
                status, report = self.run_audit("empty-accel", lines, returncode)
                self.assertEqual(status, 1)
                self.assertEqual(report["failed_or_validation_processes"], 1)


if __name__ == "__main__":
    unittest.main()
