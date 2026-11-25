"""Reject incomplete or skipped CI evidence and exercise a real CTest run."""

import importlib.util
import contextlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET


REPO_ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("ocarina_ci_suite", REPO_ROOT / "src/tests/run_ci_suite.py")
suite = importlib.util.module_from_spec(spec)
spec.loader.exec_module(suite)


class CiEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.executable = self.root / "test-sample"
        self.executable.touch()
        self.manifest = [{"target": "test-sample", "path": str(self.executable), "benchmark": False}]
        self.inventory = {"tests": [{"name": "ocarina.test-sample", "command": [str(self.executable)]}]}

    def validate(self):
        return suite.validate_inventory(self.manifest, self.inventory, expected_targets=1, expected_cases=1)

    def results(self, *, status="run", output="actual result=PASS", tag=None, name="ocarina.test-sample"):
        root = ET.Element("testsuite")
        case = ET.SubElement(root, "testcase", name=name, status=status, time="0.125")
        ET.SubElement(case, "system-out").text = output
        if tag:
            ET.SubElement(case, tag).text = "intentional diagnostic"
        path = self.root / "results.xml"
        ET.ElementTree(root).write(path, encoding="utf-8")
        return suite.read_results(path, self.validate()["tests"], self.root)

    def test_valid_inventory_and_results_preserve_command_and_stdout(self):
        cases, errors = self.results()
        self.assertEqual(errors, [])
        self.assertEqual(cases[0]["status"], "passed")
        self.assertEqual(cases[0]["target"], "test-sample")
        self.assertEqual(cases[0]["command"], [str(self.executable)])
        self.assertEqual(Path(cases[0]["log"]).read_text(), "actual result=PASS")

    def test_missing_executable_is_rejected(self):
        self.executable.unlink()
        with self.assertRaisesRegex(ValueError, "missing executable"):
            self.validate()

    def test_wrong_inventory_counts_and_duplicate_names_are_rejected(self):
        for manifest, inventory in (([], self.inventory), (self.manifest, {"tests": []})):
            with self.subTest(manifest=manifest, inventory=inventory):
                with self.assertRaisesRegex(ValueError, "Expected"):
                    suite.validate_inventory(manifest, inventory, expected_targets=1, expected_cases=1)
        duplicate = {"tests": self.inventory["tests"] * 2}
        with self.assertRaisesRegex(ValueError, "duplicate CTest"):
            suite.validate_inventory(self.manifest, duplicate, expected_targets=1, expected_cases=2)

    def test_named_test_cannot_claim_an_executable_it_does_not_run(self):
        self.inventory["tests"][0]["command"] = [sys.executable, "fake.py"]
        with self.assertRaisesRegex(ValueError, "no direct CTest"):
            self.validate()

    def test_missing_or_unexpected_junit_case_is_rejected(self):
        cases, errors = self.results(name="ocarina.other")
        self.assertEqual(cases[0]["status"], "missing")
        self.assertTrue(any("unexpected" in error for error in errors))
        self.assertTrue(any("missing" in error for error in errors))

    def test_xml_skips_failure_error_and_notrun_are_rejected(self):
        for status, tag in (("run", "skipped"), ("run", "failure"), ("run", "error"), ("notrun", None)):
            with self.subTest(status=status, tag=tag):
                cases, errors = self.results(status=status, tag=tag)
                self.assertNotEqual(cases[0]["status"], "passed")
                self.assertEqual(cases[0]["executed"], status == "run" and tag != "skipped")
                self.assertTrue(errors)

    def test_feature_skip_cannot_hide_in_a_zero_exit_result(self):
        for marker in ("SKIPPED (vulkan-compute): no ray queries", "SKIP: missing feature"):
            with self.subTest(marker=marker):
                cases, errors = self.results(output="prefix\n" + marker + "\nother checks passed")
                self.assertEqual(cases[0]["status"], "skipped")
                self.assertTrue(cases[0]["executed"])
                self.assertTrue(errors)

    def test_truncated_stdout_is_not_complete_evidence(self):
        cases, errors = self.results(output="[This part of the test output was removed since it exceeds the threshold of 1024 bytes.]")
        self.assertEqual(cases[0]["status"], "failed")
        self.assertTrue(any("truncated" in error for error in errors))

    def test_duplicate_junit_case_is_rejected(self):
        self.results()
        path = self.root / "results.xml"
        tree = ET.parse(path)
        tree.getroot().append(ET.fromstring(ET.tostring(tree.getroot()[0])))
        tree.write(path)
        _, errors = suite.read_results(path, self.validate()["tests"], self.root)
        self.assertTrue(any("duplicate" in error for error in errors))

    def test_nonfinite_duration_is_not_valid_execution_evidence(self):
        for duration in ("nan", "inf", "-1"):
            with self.subTest(duration=duration):
                self.results()
                path = self.root / "results.xml"
                tree = ET.parse(path)
                tree.getroot()[0].set("time", duration)
                tree.write(path)
                cases, errors = suite.read_results(path, self.validate()["tests"], self.root)
                self.assertEqual(cases[0]["status"], "failed")
                self.assertTrue(errors)

    def test_failed_run_keeps_failure_report_and_cannot_reuse_old_junit(self):
        output = self.root / "failed-evidence"
        output.mkdir()
        stale = output / "results.xml"
        stale.write_text('<testsuite><testcase name="old" status="run"/></testsuite>')
        with contextlib.redirect_stdout(io.StringIO()):
            report = suite.run_suite(self.root, "Release", output, self.root / "absent.json",
                                     expected_targets=1, expected_cases=1, ctest="not-executed")
        self.assertEqual(report["status"], "failed")
        self.assertIsNone(report["ctest_returncode"])
        self.assertFalse(stale.exists())
        self.assertEqual(json.loads((output / "summary.json").read_text())["status"], "failed")
        self.assertTrue((output / "summary.md").is_file())

    def test_pr_revision_and_driver_metadata_are_recorded_without_copying_event(self):
        event = self.root / "event.json"
        event.write_text(json.dumps({"pull_request": {"head": {"sha": "a" * 40}},
                                     "private_test_field": "should-not-copy"}))
        driver = self.root / "driver.json"
        driver_data = {"name": "MoltenVK", "version": "test-version", "commit": "driver-commit"}
        driver.write_text(json.dumps(driver_data))
        metadata_env = dict(os.environ, GITHUB_EVENT_PATH=str(event), GITHUB_SHA="b" * 40,
                            GITHUB_RUN_ID="123", GITHUB_RUN_ATTEMPT="2",
                            HORIZON_CI_DRIVER_METADATA=str(driver))
        with patch.object(os, "environ", metadata_env):
            metadata = suite.execution_metadata()
        self.assertEqual(metadata["source_revision"], "a" * 40)
        self.assertRegex(metadata["checkout_revision"], r"^[0-9a-f]{40}$")
        self.assertEqual(metadata["github"]["GITHUB_RUN_ID"], "123")
        self.assertEqual(metadata["github"]["GITHUB_RUN_ATTEMPT"], "2")
        self.assertEqual(metadata["driver"], driver_data)
        self.assertNotIn("should-not-copy", json.dumps(metadata))


class RealCTestEvidenceTests(unittest.TestCase):
    def test_real_ctest_failure_counts_as_executed_but_missing_command_does_not(self):
        cmake, ctest, ninja = (shutil.which(name) for name in ("cmake", "ctest", "ninja"))
        if not all((cmake, ctest, ninja)):
            self.skipTest("CMake, CTest and Ninja required")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 4.0)\nproject(FailureEvidence LANGUAGES NONE)\n'
                'enable_testing()\n'
                'add_test(NAME ocarina.failed COMMAND "${CMAKE_COMMAND}" -E false)\n'
                'add_test(NAME ocarina.missing COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/absent-executable")\n',
                encoding="utf-8")
            build, junit = root / "build", root / "results.xml"
            configured = subprocess.run([cmake, "-S", str(root), "-B", str(build),
                                         "-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={ninja}"],
                                        capture_output=True, text=True, timeout=60)
            self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
            result = subprocess.run([ctest, "--test-dir", str(build), "--output-junit", str(junit)],
                                    capture_output=True, text=True, timeout=60)
            self.assertNotEqual(result.returncode, 0)
            tests = [{"name": "ocarina.failed", "command": [cmake, "-E", "false"]},
                     {"name": "ocarina.missing", "command": [str(root / "absent-executable")]}]
            cases, errors = suite.read_results(junit, tests, root)
            by_name = {case["name"]: case for case in cases}
            self.assertTrue(by_name["ocarina.failed"]["executed"])
            self.assertEqual(by_name["ocarina.failed"]["status"], "failed")
            self.assertFalse(by_name["ocarina.missing"]["executed"])
            self.assertNotEqual(by_name["ocarina.missing"]["status"], "passed")
            self.assertEqual(len(errors), 2)

    def test_real_ctest_produces_complete_target_and_case_evidence(self):
        cmake, ctest, ninja = (shutil.which(name) for name in ("cmake", "ctest", "ninja"))
        if not all((cmake, ctest, ninja)):
            self.skipTest("CMake, CTest and Ninja required")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "sample.cpp").write_text(
                '#include <iostream>\nint main(){ for(int i=0;i<300;++i) '
                'std::cout << "record " << i << " retains complete output\\n"; return 0; }\n', encoding="utf-8")
            (root / "bench.cpp").write_text(
                '#include <iostream>\nint main(){ volatile int value=0; for(int i=0;i<100;++i) value+=i; '
                'std::cout << "measurement=" << value << "\\n"; return value==4950?0:1; }\n', encoding="utf-8")
            (root / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 4.0)\nproject(Evidence LANGUAGES CXX)\nenable_testing()\n'
                'add_executable(test-sample sample.cpp)\nadd_executable(test-bench bench.cpp)\n'
                'add_test(NAME ocarina.test-sample COMMAND test-sample)\n'
                'add_test(NAME ocarina.test-sample.again COMMAND test-sample)\n'
                'add_test(NAME ocarina.test-bench COMMAND test-bench)\n'
                'file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/targets.json" CONTENT '
                '"[{\\\"target\\\":\\\"test-sample\\\",\\\"path\\\":\\\"$<TARGET_FILE:test-sample>\\\",\\\"benchmark\\\":false},'
                '{\\\"target\\\":\\\"test-bench\\\",\\\"path\\\":\\\"$<TARGET_FILE:test-bench>\\\",\\\"benchmark\\\":true}]\\n")\n',
                encoding="utf-8")
            build = root / "build"
            for command in ([cmake, "-S", str(root), "-B", str(build), "-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={ninja}"],
                            [cmake, "--build", str(build)]):
                result = subprocess.run(command, capture_output=True, text=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            output = root / "evidence"
            with contextlib.redirect_stdout(io.StringIO()):
                report = suite.run_suite(build, "Release", output, build / "targets.json",
                                         expected_targets=2, expected_cases=3, ctest=ctest)
            self.assertEqual(report["status"], "passed", report["errors"])
            self.assertEqual(len(report["cases"]), 3)
            self.assertTrue(all(target["passed"] for target in report["targets"]))
            self.assertTrue(all(target["executed"] for target in report["targets"]))
            self.assertEqual(sum(target["benchmark"] for target in report["targets"]), 1)
            self.assertIn("record 299", Path(report["cases"][0]["log"]).read_text())
            self.assertEqual(json.loads((output / "summary.json").read_text())["status"], "passed")
            self.assertTrue((output / "summary.md").is_file())


if __name__ == "__main__":
    unittest.main()
