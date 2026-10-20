"""Run isolated Vulkan AST/IR capability regressions and compare their outputs.

Build the four *-capability-probe / *-float32-policy-probe executables first.
Example: python run_capability_audit.py --runtime <bin/Release> --output <logs>
GPU work is deliberately serialized. Validation can be enabled with the usual
VK_INSTANCE_LAYERS / VK_LAYER_PATH / VK_LAYER_SETTINGS_PATH environment variables.
CPU oracles and matching AST/IR observation rows are required. The
float32-subnormal policy case records GPU behavior for exact AST/IR
comparison; it does not impose a separate CPU denormal-arithmetic policy.
Exit 1 means a process, validation or observation-capture failure; exit 2 means
exact numeric differences in the selected deterministic regression cases.
"""

import argparse
import ctypes
import datetime
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import time


CASES = {
    "test-runtime-codegen-capability-probe": (
        "inverse2", "determinant2", "callable-storage", "callable-alias", "fmod-mod", "ulong-makers"),
    "test-runtime-backend-capability-probe": (
        "empty-accel", "fast-build", "mesh-rebuild", "same-batch-accel", "multistream-ray-params"),
    "test-resources-resource-capability-probe": (
        "texture-buffer-view-offset", "texture-narrow-channels", "texture-linear-fraction", "texture-uint-reinterpret"),
    "test-runtime-float32-policy-probe": (
        "float32-subnormal", "hyperbolic-stability", "half-intermediate", "half-inverse"),
}
VALIDATION = re.compile(r"Validation Error|Validation Warning|VUID-|SYNC-HAZARD")
OBSERVATIONS = {"NARROW": ("direct", "bindless"), "FILTER": ("direct", "bindless"),
                "FLOAT_BITS": ("actual",), "FLOAT_MATH": ("actual",), "HALF_MATH": ("actual", "actual_bits"),
                "HALF_INVERSE": ("actual", "actual_bits"), "REINTERPRET": ("tested_words", "oracle_failures")}
EXPECTED_ROWS = {"texture-narrow-channels": 192, "texture-linear-fraction": 80,
                 "float32-subnormal": 80, "hyperbolic-stability": 40,
                 "half-intermediate": 9, "half-inverse": 727, "texture-uint-reinterpret": 54}


def compare_observations(results):
    """Exact numeric comparisons are observations, not universal error tolerances."""
    comparisons = []
    lookup = {(r["case"], r["codegen"]): r for r in results}
    for case in sorted({key[0] for key in lookup}):
        if (case, "ast") not in lookup or (case, "ir") not in lookup:
            continue
        rows, parse_errors = [], []
        for codegen in ("ast", "ir"):
            records = {}
            for line in Path(lookup[(case, codegen)]["log"]).read_text(encoding="utf-8", errors="replace").splitlines():
                prefix = line.split(" ", 1)[0]
                if prefix not in OBSERVATIONS:
                    continue
                fields = dict(re.findall(r"(\w+)=(\S+)", line))
                if not all(k in fields for k in OBSERVATIONS[prefix]):
                    parse_errors.append(dict(codegen=codegen, line=line))
                    continue
                # Row/index/format/dimension/op/input/channel uniquely identify
                # these diagnostic observations. Preserve input bits verbatim.
                identity = (prefix,) + tuple((k, fields[k]) for k in
                    ("dimension", "format", "word_channels", "index", "channel", "row", "column", "component", "op", "input") if k in fields)
                # Hyperbolic inputs may repeat (acosh(1)); number occurrences.
                occurrence = sum(key[:-1] == identity for key in records)
                try:
                    records[identity + (occurrence,)] = {k: float(fields[k]) for k in OBSERVATIONS[prefix]}
                except ValueError:
                    parse_errors.append(dict(codegen=codegen, line=line))
            rows.append(records)
        if not rows[0] and not rows[1] and case not in EXPECTED_ROWS:
            continue
        mismatches, examples, largest = 0, [], 0.0
        for identity in sorted(rows[0].keys() | rows[1].keys()):
            first, second = rows[0].get(identity), rows[1].get(identity)
            equal = first is not None and second is not None and all(
                first[k] == second[k] or math.isnan(first[k]) and math.isnan(second[k]) for k in first)
            if not equal:
                mismatches += 1
                if len(examples) < 8:
                    examples.append(dict(identity=identity, ast=first, ir=second))
                if first and second:
                    for key in first:
                        if key != "actual_bits" and math.isfinite(first[key]) and math.isfinite(second[key]):
                            largest = max(largest, abs(first[key] - second[key]))
        comparisons.append(dict(case=case, ast_rows=len(rows[0]), ir_rows=len(rows[1]),
                                different_rows=mismatches, max_numeric_delta=largest, expected_rows=EXPECTED_ROWS.get(case),
                                parse_errors=parse_errors, examples=examples))
    return comparisons


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--case", action="append", dest="cases",
                        choices=[case for cases in CASES.values() for case in cases])
    parser.add_argument("--timeout", type=float, default=300)
    args = parser.parse_args()
    runtime, output = args.runtime.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    selected = set(args.cases or [case for cases in CASES.values() for case in cases])
    if os.name == "nt":
        # Shader compilation regressions can abort. Do not leave modal Windows
        # crash dialogs hanging after the isolated child process exits.
        ctypes.windll.kernel32.SetErrorMode(0x8003)
    report = {
        "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "runtime": str(runtime),
        "notes": [
            "All selected cases must pass on both Vulkan codegen paths.",
            "float32-subnormal compares observed GPU policy across Vulkan codegen paths; other cases also use CPU oracles.",
            "empty-accel traces zero and cleared acceleration structures.",
            "Deterministic observation rows are compared exactly across Vulkan codegen paths.",
            "Different AS build actions are recorded separately from hit correctness.",
        ],
        "results": [],
    }
    result_file = output / "results.json"
    for target, cases in CASES.items():
        for case in cases:
            if case not in selected:
                continue
            executable = runtime / (target + (".exe" if os.name == "nt" else ""))
            if not executable.is_file():
                parser.error(f"missing executable: {executable}")
            for backend in ("vulkan-compute",):
                for codegen in ("ast", "ir"):
                    env = os.environ.copy()
                    env.update(OCARINA_TEST_CODEGEN=codegen)
                    log = output / f"{case}-{backend}-{codegen}.log"
                    start = time.monotonic()
                    command = [str(executable), "--case", case]
                    with log.open("wb") as stream:
                        try:
                            process = subprocess.run(
                                command, cwd=runtime, env=env, stdout=stream,
                                stderr=subprocess.STDOUT, timeout=args.timeout,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
                            returncode = process.returncode
                        except subprocess.TimeoutExpired:
                            returncode = "timeout"
                    contents = log.read_text(encoding="utf-8", errors="replace")
                    validation_lines = [line for line in contents.splitlines() if VALIDATION.search(line)]
                    result = dict(case=case, backend=backend, codegen=codegen,
                                  returncode=returncode, seconds=round(time.monotonic() - start, 3),
                                  validation_messages=len(validation_lines), log=str(log))
                    report["results"].append(result)
                    result_file.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
                    print(json.dumps(result), flush=True)
    failed = sum(item["returncode"] != 0 or item["validation_messages"] != 0 for item in report["results"])
    report["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    report["processes"] = len(report["results"])
    report["failed_or_validation_processes"] = failed
    report["observations"] = compare_observations(report["results"])
    report["incomplete_observations"] = sum(bool(c["parse_errors"]) or any(
        c[key] != c["expected_rows"] for key in ("ast_rows", "ir_rows"))
        for c in report["observations"])
    report["different_observation_rows"] = sum(c["different_rows"] for c in report["observations"])
    result_file.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    if failed or report["incomplete_observations"]:
        return 1
    return 2 if report["different_observation_rows"] else 0


if __name__ == "__main__":
    sys.exit(main())
