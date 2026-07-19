---
name: clion-build
description: Build the HorizonExamples CMake target with the repository's existing CLion Debug build tree. Use when Codex is asked to build, rebuild, compile, or verify HorizonExamples exactly as CLion does with cmake-build-debug and the configured CLion toolchain.
---

# CLion Build

Build `HorizonExamples` through the existing CLion Debug cache without reconfiguring the project.

## Workflow

1. Run from the repository root:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .agents/skills/clion-build/scripts/build-horizon-examples.ps1
   ```

2. Report the build result and the first actionable compiler or linker error when the build fails.

## Build Contract

- Use only `cmake-build-debug` as the build directory.
- Build only the `HorizonExamples` target.
- Use 30 parallel jobs, matching the CLion build invocation.
- Read the CMake executable and MSVC installation from `cmake-build-debug/CMakeCache.txt` so the build uses the same tools selected by CLion.
- Do not configure CMake, use a preset, choose another generator, clean the build tree, or create another build directory.
- If the cache or required toolchain entry is missing, stop and report that the CLion Debug profile must be loaded successfully first.
