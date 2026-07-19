---
name: run-horizon-baseline
description: Launch the repository's existing Debug HorizonExamples executable with baseline as its only program argument. Use when Codex is asked to start, run, open, or launch the Horizon baseline example from cmake-build-debug.
---

# Run Horizon Baseline

Launch the existing `HorizonExamples` Debug artifact in `baseline` mode.

## Workflow

1. Run from the repository root:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .agents/skills/run-horizon-baseline/scripts/run-baseline.ps1
   ```

2. Report the process ID returned by the script.

## Run Contract

- Use `cmake-build-debug/examples/HorizonExamples.exe`.
- Set the child process working directory to `cmake-build-debug/examples` so relative shader paths resolve.
- Pass exactly one program argument: `baseline`.
- Add `cmake-build-debug/bin` to the child process PATH when that directory exists so hotfix runtime DLLs can resolve.
- Start the interactive application visibly and return without waiting for it to close.
- Do not build, reconfigure, attach a debugger, choose another executable, or create another output directory.
- If the executable or baseline shaders are missing, stop and report that the Debug target must be built first.
