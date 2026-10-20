"""Run isolated Vulkan EDSL capability regressions and capture observations.

Build the four *-capability-probe / *-float32-policy-probe executables first.
Example: python run_capability_audit.py --runtime <bin/Release> --output <logs>
GPU work is deliberately serialized. Validation can be enabled with the usual
VK_INSTANCE_LAYERS / VK_LAYER_PATH / VK_LAYER_SETTINGS_PATH environment variables.
Each selected case runs once. Probe executables check their CPU oracles;
this runner also checks validation messages and complete observation capture.
The float32-subnormal case records GPU behavior without imposing a separate
CPU denormal-arithmetic policy. Exit 1 means a process, validation or
observation-capture failure.
"""

import argparse
import ctypes
import datetime
import json
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


def collect_observations(results):
    """Check capture completeness; numeric correctness is checked by each probe."""
    observations = []
    for result in results:
        rows, parse_errors = 0, []
        for line in Path(result["log"]).read_text(encoding="utf-8", errors="replace").splitlines():
            prefix = line.split(" ", 1)[0]
            if prefix not in OBSERVATIONS:
                continue
            fields = dict(re.findall(r"(\w+)=(\S+)", line))
            try:
                # NaN/Inf are valid observations in the floating-point policy probes.
                for key in OBSERVATIONS[prefix]:
                    float(fields[key])
            except (KeyError, ValueError):
                parse_errors.append(line)
            else:
                rows += 1
        observations.append(dict(case=result["case"], rows=rows,
                                 expected_rows=EXPECTED_ROWS.get(result["case"]),
                                 parse_errors=parse_errors))
    return observations


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
            "All selected cases must pass using the Vulkan EDSL path.",
            "float32-subnormal records GPU policy; other cases also use CPU oracles.",
            "empty-accel traces zero and cleared acceleration structures.",
            "Observation rows must be parseable and complete.",
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
            log = output / f"{case}-vulkan-compute.log"
            start = time.monotonic()
            command = [str(executable), "--case", case]
            with log.open("wb") as stream:
                try:
                    process = subprocess.run(
                        command, cwd=runtime, stdout=stream,
                        stderr=subprocess.STDOUT, timeout=args.timeout,
                        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
                    returncode = process.returncode
                except subprocess.TimeoutExpired:
                    returncode = "timeout"
            contents = log.read_text(encoding="utf-8", errors="replace")
            validation_lines = [line for line in contents.splitlines() if VALIDATION.search(line)]
            result = dict(case=case, backend="vulkan-compute",
                          returncode=returncode, seconds=round(time.monotonic() - start, 3),
                          validation_messages=len(validation_lines), log=str(log))
            report["results"].append(result)
            result_file.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
            print(json.dumps(result), flush=True)
    failed = sum(item["returncode"] != 0 or item["validation_messages"] != 0 for item in report["results"])
    report["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    report["processes"] = len(report["results"])
    report["failed_or_validation_processes"] = failed
    report["observations"] = collect_observations(report["results"])
    report["incomplete_observations"] = sum(
        bool(c["parse_errors"]) or (c["expected_rows"] is not None and c["rows"] != c["expected_rows"])
        for c in report["observations"])
    result_file.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    if failed or report["incomplete_observations"]:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
