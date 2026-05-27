# Horizon Build Context
<!-- AGENT_DOCS_BUILD_ZH_CN_SHA256: bc328c29aecf54f3c295686180ab13d7b0a55abec474527c6e4e7db3099289e1 -->

Load this file only for CMake, preset, build, CI, or validation-command work.

## Targets

- `Horizon`: main static library.
- `ShaderCompileScripts`: shader/codegen tool target under `tools/`.
- `HorizonExamples`: examples target, enabled by `HORIZON_BUILD_EXAMPLES`.

## Common Commands

Prefer the unified development entry point:

```powershell
.\tools\dev.ps1 status
.\tools\dev.ps1 configure
.\tools\dev.ps1 build Horizon
.\tools\dev.ps1 build ShaderCompileScripts
.\tools\dev.ps1 build HorizonExamples
```

`tools/dev.ps1` only wraps existing commands. When debugging CMake or CI issues, run the underlying commands directly:

```powershell
cmake --preset ninja-msvc
cmake --build --preset msvc-debug --target Horizon
cmake --build --preset msvc-debug --target ShaderCompileScripts
cmake --build --preset msvc-debug --target HorizonExamples
```

## Rules

- Root `CMakeLists.txt` owns project options, dependencies, and subdirectories.
- `src/CMakeLists.txt` defines `Horizon`.
- `tools/CMakeLists.txt` defines `ShaderCompileScripts`.
- `examples/CMakeLists.txt` defines `HorizonExamples`.
- `include/` is the public include surface.
- Keep Vulkan, VMA, Windows, and implementation-only types out of public headers unless truly required.
- After CMake changes, run configure plus the smallest relevant build.
- The common entry point for newcomers and agents is `tools/dev.ps1`; use direct `cmake` commands only when investigating lower-level issues.
- Each configure preset uses its own `build/<preset>` directory; do not assume all generators share `build/`.
- FetchContent dependencies use a shared source cache and preset-local build directories; see `docs/cmake-usage.md`.

## Deep References

Load these longer docs only when the task needs them:

- CMake structure, presets, build directories, optional modules, troubleshooting: `docs/cmake-usage.md`
- Visual Studio / VS CMake debugging, executable paths, command-line reproduction: `docs/vs-debugging.md`

## Notes

- Current preset flow is Windows / MSVC / Ninja focused.
- Docs-only changes normally do not need CMake builds.
