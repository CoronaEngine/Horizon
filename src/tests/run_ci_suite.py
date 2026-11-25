"""Execute the Ocarina CI CTest suite and retain verifiable execution evidence.

Every selected case must execute and pass. XML skips, feature skips printed by
a successful process, and truncated output are failures of complete coverage.
"""

import argparse
import ctypes
import datetime
import json
import math
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import xml.etree.ElementTree as ET


EXPECTED_TARGETS = 51
EXPECTED_CASES = 70
OUTPUT_LIMIT = 16 * 1024 * 1024
HIDDEN_SKIP = re.compile(r"\bSKIPPED\b|\bSKIP:", re.IGNORECASE)
TRUNCATED_OUTPUT = "This part of the test output was removed since it exceeds the threshold"
REPO_ROOT = Path(__file__).resolve().parents[2]


def canonical_path(path):
    return os.path.normcase(str(Path(path).resolve()))


def validate_inventory(manifest, inventory, *, expected_targets=EXPECTED_TARGETS,
                       expected_cases=EXPECTED_CASES):
    if not isinstance(manifest, list) or len(manifest) != expected_targets:
        raise ValueError(f"Expected {expected_targets} executable targets in the manifest")
    targets, by_path = {}, {}
    for item in manifest:
        if (not isinstance(item, dict) or not isinstance(item.get("target"), str)
                or not item["target"] or not isinstance(item.get("path"), str)
                or not item["path"] or not isinstance(item.get("benchmark"), bool)):
            raise ValueError("Each manifest entry needs target, path and boolean benchmark")
        name, path = item["target"], canonical_path(item["path"])
        if name in targets or path in by_path:
            raise ValueError(f"duplicate executable target or path: {name}")
        if not Path(path).is_file():
            raise ValueError(f"missing executable: {name}: {path}")
        targets[name] = dict(item, path=path, executed=False, passed=False)
        by_path[path] = name
    if not isinstance(inventory, dict):
        raise ValueError("CTest inventory must be a JSON object")
    tests = inventory.get("tests", [])
    if not isinstance(tests, list) or len(tests) != expected_cases:
        raise ValueError(f"Expected {expected_cases} CTest cases")
    names, directly_tested, normalized = set(), set(), []
    for test in tests:
        if not isinstance(test, dict):
            raise ValueError("Each CTest inventory entry must be an object")
        name, command = test.get("name"), test.get("command")
        if not isinstance(name, str) or not name.startswith("ocarina."):
            raise ValueError(f"unexpected CTest name: {name}")
        if name in names:
            raise ValueError(f"duplicate CTest name: {name}")
        names.add(name)
        if not isinstance(command, list) or not command or not all(isinstance(arg, str) for arg in command):
            raise ValueError(f"missing CTest command: {name}")
        target = by_path.get(canonical_path(command[0]))
        if target:
            directly_tested.add(target)
        else:
            # Failure probes run CMake with the executable as a -D argument.
            # Associate those results without counting them as direct execution.
            target = next((candidate for candidate in targets
                           if name == f"ocarina.{candidate}" or name.startswith(f"ocarina.{candidate}.")), None)
        normalized.append(dict(test, command=list(command), target=target))
    absent = set(targets) - directly_tested
    if absent:
        raise ValueError("no direct CTest execution for targets: " + ", ".join(sorted(absent)))
    return {"targets": list(targets.values()), "tests": normalized}


def read_results(path, tests, output):
    root = ET.parse(path).getroot()
    entries = root.findall(".//testcase")
    expected = {test["name"]: test for test in tests}
    actual, errors = {}, []
    for entry in entries:
        name = entry.get("name")
        if name not in expected:
            errors.append(f"unexpected JUnit case: {name}")
        elif name in actual:
            errors.append(f"duplicate JUnit case: {name}")
        else:
            actual[name] = entry
    logs = Path(output) / "cases"
    logs.mkdir(parents=True, exist_ok=True)
    cases = []
    for index, test in enumerate(tests, 1):
        name = test["name"]
        result = {"name": name, "command": test["command"], "target": test.get("target"),
                  "status": "missing", "executed": False, "seconds": None,
                  "log": None, "diagnostics": []}
        entry = actual.get(name)
        if entry is None:
            result["diagnostics"].append("missing JUnit result")
        else:
            stdout = "\n".join((child.text or "") for child in entry if child.tag in ("system-out", "system-err"))
            log_path = logs / f"{index:03d}-{re.sub(r'[^a-zA-Z0-9_.-]', '_', name)}.log"
            log_path.write_text(stdout, encoding="utf-8")
            result.update(log=str(log_path.resolve()), junit_status=entry.get("status"))
            # CTest marks a started process with a nonzero exit as "fail";
            # commands that could not start are "notrun". A failed execution
            # must remain distinguishable from missing execution.
            result["executed"] = entry.get("status") in ("run", "fail") and entry.find("skipped") is None
            try:
                result["seconds"] = float(entry.get("time", "nan"))
                if not math.isfinite(result["seconds"]) or result["seconds"] < 0:
                    result["seconds"] = None
                    raise ValueError("non-finite or negative duration")
            except ValueError:
                result["diagnostics"].append("invalid JUnit duration")
            status = "passed" if entry.get("status") == "run" else "notrun"
            for tag in ("skipped", "failure", "error"):
                element = entry.find(tag)
                if element is not None:
                    status = "skipped" if tag == "skipped" else "failed"
                    result["diagnostics"].append(f"JUnit {tag}: {element.text or element.get('message', '')}")
            hidden_skips = [line for line in stdout.splitlines() if HIDDEN_SKIP.search(line)]
            if hidden_skips:
                status = "skipped"
                result["diagnostics"].extend(hidden_skips)
            if TRUNCATED_OUTPUT in stdout:
                status = "failed"
                result["diagnostics"].append("truncated output cannot prove complete execution")
            if result["diagnostics"] and status == "passed":
                status = "failed"
            result["status"] = status
        if result["status"] != "passed":
            errors.append(f"{name}: {result['status']}" +
                          (": " + "; ".join(result["diagnostics"]) if result["diagnostics"] else ""))
        cases.append(result)
    return cases, errors


def utc_now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def execution_metadata():
    checkout = subprocess.run(["git", "-C", str(REPO_ROOT), "rev-parse", "HEAD"],
                              capture_output=True, text=True, timeout=15, check=True).stdout.strip()
    source = os.environ.get("GITHUB_SHA") or checkout
    event_path = os.environ.get("GITHUB_EVENT_PATH")
    if event_path:
        event = json.loads(Path(event_path).read_text(encoding="utf-8"))
        if not isinstance(event, dict):
            raise ValueError("GitHub event metadata must be a JSON object")
        source = event.get("pull_request", {}).get("head", {}).get("sha") or source
    metadata = {
        "checkout_revision": checkout, "source_revision": source,
        "tracked_worktree_dirty": bool(subprocess.run(
            ["git", "-C", str(REPO_ROOT), "status", "--porcelain", "--untracked-files=no"],
            capture_output=True, text=True, timeout=15, check=True).stdout.strip()),
        "github": {key: os.environ.get(key) for key in (
            "GITHUB_RUN_ID", "GITHUB_RUN_ATTEMPT", "GITHUB_JOB", "GITHUB_EVENT_NAME", "GITHUB_REF", "RUNNER_OS")},
    }
    driver_path = os.environ.get("HORIZON_CI_DRIVER_METADATA")
    if driver_path:
        metadata["driver"] = json.loads(Path(driver_path).read_text(encoding="utf-8"))
    return metadata


def write_report(output, report):
    (output / "summary.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    cases = report.get("cases", [])
    lines = ["# Ocarina CI execution evidence", "", f"Status: **{report['status']}**", "",
             f"Source revision: `{report.get('source_revision', 'unknown')}`",
             f"Checkout revision: `{report.get('checkout_revision', 'unknown')}`",
             f"Platform: {report['platform']} / {report['machine']}",
             f"Passed cases: {sum(case['status'] == 'passed' for case in cases)}/{report['expected']['cases']}",
             f"Executed targets: {sum(target.get('executed', False) for target in report['targets'])}/{report['expected']['targets']}",
             f"Passed targets: {sum(target.get('passed', False) for target in report['targets'])}/{report['expected']['targets']}", ""]
    if report["errors"]:
        lines.extend(["## Errors", ""] + [f"- {error}" for error in report["errors"]] + [""])
    lines.extend(["| Case | Target | Status | Seconds | Output |", "|---|---|---|---:|---|"])
    for case in cases:
        log = Path(case["log"]).relative_to(output).as_posix() if case.get("log") else None
        lines.append(f"| `{case['name']}` | `{case.get('target') or 'audit/driver'}` | {case['status']} | "
                     f"{case['seconds']} | " + (f"[log]({log}) |" if log else "unavailable |"))
    (output / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def run_suite(build_dir, configuration, output, manifest_path, *,
              expected_targets=EXPECTED_TARGETS, expected_cases=None, ctest="ctest",
              without_int64_atomics=False):
    if expected_cases is None:
        expected_cases = EXPECTED_CASES - int(without_int64_atomics)
    build_dir, output, manifest_path = (Path(path).resolve() for path in (build_dir, output, manifest_path))
    output.mkdir(parents=True, exist_ok=True)
    # A previous successful run must not supply results after an early failure.
    junit = output / "results.xml"
    junit.unlink(missing_ok=True)
    base = [str(ctest), "--test-dir", str(build_dir), "-C", configuration, "-R", r"^ocarina\."]
    if without_int64_atomics:
        base += ["-LE", "^int64-atomics$"]
    inventory_command = base + ["--show-only=json-v1"]
    run_command = base + ["--no-tests=error", "--verbose", "--output-on-failure",
                          "--test-output-size-passed", str(OUTPUT_LIMIT),
                          "--test-output-size-failed", str(OUTPUT_LIMIT), "--output-junit", str(junit)]
    report = {"schema_version": 1, "status": "running", "started_utc": utc_now(),
              "platform": platform.system(), "machine": platform.machine(), "configuration": configuration,
              "build_dir": str(build_dir), "manifest": str(manifest_path),
              "expected": {"targets": expected_targets, "cases": expected_cases},
              "commands": {"inventory": inventory_command, "ctest": run_command},
              "targets": [], "cases": [], "errors": [], "ctest_returncode": None}
    write_report(output, report)
    try:
        report.update(execution_metadata())
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        inventory_result = subprocess.run(inventory_command, capture_output=True, text=True,
                                          encoding="utf-8", errors="replace", timeout=60)
        (output / "inventory.json").write_text(inventory_result.stdout, encoding="utf-8")
        (output / "inventory-stderr.log").write_text(inventory_result.stderr, encoding="utf-8")
        if inventory_result.returncode:
            raise ValueError(f"CTest inventory failed with exit {inventory_result.returncode}")
        validated = validate_inventory(manifest, json.loads(inventory_result.stdout),
                                       expected_targets=expected_targets, expected_cases=expected_cases)
        report["targets"] = validated["targets"]
        write_report(output, report)
        if os.name == "nt":
            ctypes.windll.kernel32.SetErrorMode(0x8003)
        with (output / "ctest.log").open("w", encoding="utf-8") as log:
            with subprocess.Popen(run_command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  text=True, encoding="utf-8", errors="replace") as process:
                for line in process.stdout:
                    log.write(line)
                    log.flush()
                    print(line, end="", flush=True)
                report["ctest_returncode"] = process.wait()
        report["cases"], result_errors = read_results(junit, validated["tests"], output)
        report["errors"].extend(result_errors)
        for target in report["targets"]:
            related = [case for case in report["cases"] if case["target"] == target["target"]]
            target["cases"] = [case["name"] for case in related]
            target["executed"] = bool(related) and all(case["executed"] for case in related)
            target["passed"] = bool(related) and all(case["status"] == "passed" for case in related)
        if report["ctest_returncode"]:
            report["errors"].append(f"CTest exited with {report['ctest_returncode']}")
    except (OSError, ValueError, KeyError, TypeError, ET.ParseError, subprocess.SubprocessError) as error:
        report["errors"].append(f"{type(error).__name__}: {error}")
    report["status"] = "failed" if report["errors"] else "passed"
    report["finished_utc"] = utc_now()
    write_report(output, report)
    print(f"Ocarina CI evidence: {report['status']}; {output / 'summary.json'}", flush=True)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--configuration", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--without-int64-atomics", action="store_true",
                        help="Exclude the int64 buffer atomics case on runners lacking that capability")
    args = parser.parse_args()
    manifest = args.manifest or args.build_dir / "src/tests/ci" / args.configuration / "targets.json"
    report = run_suite(args.build_dir, args.configuration, args.output, manifest,
                       without_int64_atomics=args.without_int64_atomics)
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    sys.exit(main())
