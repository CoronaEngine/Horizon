"""Exercise the rejection checker with synthetic process output, without using a GPU."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile


WRAPPER = Path(__file__).with_name("check_trace_mode_rejections.cmake")
INCOMPATIBLE = "RayTracingPipeline incompatible with Vulkan shader 'trace-local-rejection': callable 'trace-local-restriction': "
DIAGNOSTICS = {
    "workgroup": INCOMPATIBLE + "synchronize_block",
    "warp": INCOMPATIBLE + "warp operation",
    "unknown": INCOMPATIBLE + "custom header 'trace-mode-unknown.slang'",
    "limit-axis": "Ray dispatch dimension 129 exceeds axis 2 limit 128",
    "limit-product": "Ray dispatch 1024 x 1024 x 2 exceeds maxRayDispatchInvocationCount 1048576",
    "unavailable-query": "RayQuery unavailable for Vulkan shader 'trace-local-rejection'",
    "unavailable-pipeline": "RayTracingPipeline unavailable for Vulkan shader 'trace-local-rejection'",
}


def check(cmake, probe, reason, name, lines, acceptable, exit_code=1, signal=None):
    # Separate writes to stdout/stderr exercise the order observed by the wrapper.
    if os.name == "nt":
        script = ["@echo off", *(f"1>&{fd} echo({msg}" for fd, msg in lines), f"exit /b {exit_code}"]
    else:
        termination = f"kill -{signal} $$" if signal else f"exit {exit_code}"
        script = ["#!/bin/sh", "ulimit -c 0", *(f"printf '%s\\n' {shlex.quote(msg)} >&{fd}" for fd, msg in lines), termination]
    probe.write_text("\n".join(script) + "\n")
    probe.chmod(0o755)
    run = subprocess.run([cmake, f"-DTEST={probe}", f"-DREASON={reason}", "-P", str(WRAPPER)],
                         capture_output=True, text=True)
    if (run.returncode == 0) != acceptable:
        raise AssertionError(f"{name}: unexpected wrapper exit {run.returncode}\n{run.stdout}{run.stderr}")
    print(f"{name}: wrapper exit {run.returncode}")


def main(cmake):
    with tempfile.TemporaryDirectory() as directory:
        probe = Path(directory) / ("probe.cmd" if os.name == "nt" else "probe.sh")

        def banner(reason):
            return (1, f"attempting rejection {reason}")

        check(cmake, probe, "limit-axis", "banner and nonzero exit", [banner("limit-axis")], False)
        for reason, diagnostic in DIAGNOSTICS.items():
            message = (2, "[ERROR][logging.cpp:19] " + diagnostic)
            check(cmake, probe, reason, reason + " deliberate rejection", [banner(reason), message], True)
            check(cmake, probe, reason, reason + " startup stderr", [message, banner(reason)], False)
        check(cmake, probe, "unavailable-query", "diagnostic split across lines", [
            banner("unavailable-query"), (2, "RayQuery"), (2, "unavailable for Vulkan shader 'trace-local-rejection'")], False)
        check(cmake, probe, "warp", "unrelated diagnostic", [banner("warp"), (2, "shader contains warp operation")], False)
        check(cmake, probe, "limit-product", "success is not rejection", [
            banner("limit-product"), (1, DIAGNOSTICS["limit-product"])], False, exit_code=0)
        if os.name == "posix":
            reason = "unavailable-query"
            marker = banner(reason)
            complete = [marker, (2, DIAGNOSTICS[reason])]
            check(cmake, probe, reason, "Debug error followed by SIGABRT", complete, True, signal="ABRT")
            check(cmake, probe, reason, "bare SIGABRT", [marker], False, signal="ABRT")
            check(cmake, probe, reason, "unrelated diagnostic followed by SIGABRT",
                  [marker, (2, DIAGNOSTICS["limit-axis"])], False, signal="ABRT")
            check(cmake, probe, reason, "matching diagnostic followed by SIGSEGV", complete, False, signal="SEGV")


if __name__ == "__main__":
    main(sys.argv[1])
