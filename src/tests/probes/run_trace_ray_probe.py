"""Run the native traceRay probe; capability-only results never count as passing."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess


REPO_ROOT = Path(__file__).resolve().parents[3]


def validate_result(report, returncode):
    errors = []
    if returncode != 0:
        errors.append(f"probe exited with {returncode}")
    if not isinstance(report, dict):
        return errors + ["probe report must be an object"]
    if report.get("status") != "passed":
        errors.append("native traceRay execution did not pass")
    if report.get("ray_query_enabled") is not False:
        errors.append("ray query must not be enabled for this probe")
    if report.get("submitted") is not True or report.get("completed") is not True:
        errors.append("GPU submission and completion must both be observed")
    if report.get("ray_count") != 8 or report.get("trace_ray_instructions") != 2:
        errors.append("expected eight rays and two native TraceRay instructions")
    cases = report.get("cases", [])
    if (not isinstance(cases, list) or len(cases) != 2
            or {c.get("name") for c in cases if isinstance(c, dict)} != {"closest-hit", "occlusion"}):
        errors.append("closest-hit and occlusion must both have results")
    elif any(c.get("status") != "passed" or c.get("verified_rays") != 8 for c in cases):
        errors.append("both cases must verify all eight GPU results")
    return errors


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    executable, output = args.executable.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    raw = output / "probe-result.json"
    log = output / "probe.log"
    # A failed process must never inherit a previous successful result.
    raw.unlink(missing_ok=True)
    for name in ("raygenMain.spv", "missMain.spv", "closestMain.spv"):
        (output / name).unlink(missing_ok=True)
    returncode = -1
    process_error = None
    try:
        with log.open("w", encoding="utf-8") as stream:
            process = subprocess.run([str(executable), str(raw)], cwd=executable.parent,
                                     stdout=stream, stderr=subprocess.STDOUT, timeout=180)
        returncode = process.returncode
    except (OSError, subprocess.TimeoutExpired) as error:
        process_error = str(error)
    try:
        report = json.loads(raw.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        report = {"status": "failed", "phase": "process", "error": "probe report missing or invalid"}
    errors = validate_result(report, returncode)
    if process_error:
        errors.append(process_error)
    checkout = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=REPO_ROOT, text=True).strip()
    result = {"verification_status": "failed" if errors else "passed", "source_revision": os.getenv("GITHUB_SHA") or checkout,
              "checkout_revision": checkout, "tracked_worktree_dirty": bool(subprocess.check_output(
                  ["git", "status", "--porcelain", "--untracked-files=no"], cwd=REPO_ROOT, text=True).strip()),
              "platform": platform.platform(), "machine": platform.machine(),
              "github_run_id": os.getenv("GITHUB_RUN_ID"), "github_run_attempt": os.getenv("GITHUB_RUN_ATTEMPT"),
              "probe": report, "returncode": returncode, "errors": errors,
              "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest() if executable.is_file() else None,
              "spirv_sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in output.glob("*.spv")}}
    metadata = os.getenv("HORIZON_CI_DRIVER_METADATA")
    if metadata:
        result["driver"] = json.loads(Path(metadata).read_text(encoding="utf-8"))
    (output / "summary.json").write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(log.read_text(encoding="utf-8", errors="replace") if log.exists() else "Probe did not produce a log")
    print(f"TraceRay CI verification: {result['verification_status']}; evidence: {output}")
    for error in errors:
        print(f"ERROR: {error}")
    return bool(errors)


if __name__ == "__main__":
    raise SystemExit(main())
