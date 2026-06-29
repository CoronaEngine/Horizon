# Horizon Build Context
<!-- AGENT_DOCS_BUILD_ZH_CN_SHA256: 38bdd5bfc550cd3dece5e94bcb6af3ff820ec0b1b9e8db7c2294fca39c968e6a -->

Load this file only for CMake, preset, build, CI, or validation-command work.

## Targets

- `Horizon`: main static library.
- `ShaderCompileScripts`: shader/codegen tool target under `tools/`, enabled by `HORIZON_BUILD_TOOLS`.
- `HorizonExamples`: examples target, enabled by `HORIZON_BUILD_EXAMPLES`; enabling examples also enables `tools/`.
- `HorizonTests`: unified correctness test entry under `tests/`, enabled by `HORIZON_BUILD_TESTS`.

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
cmake --build --preset msvc-debug --target HorizonTests
ctest --test-dir build/ninja-msvc -C Debug -R HorizonTests --output-on-failure
```

Default configure favors a clean build: only library-required targets are enabled. `tools/`, `examples/`, `tests/`, `benchmarks/`, `modules/ocarina`, third-party command line tools, and third-party install rules are off by default. Enable them explicitly when needed:

```powershell
cmake --preset ninja-msvc -DHORIZON_BUILD_TOOLS=ON
cmake --preset ninja-msvc -DHORIZON_BUILD_EXAMPLES=ON
cmake --preset ninja-msvc -DHORIZON_BUILD_TESTS=ON
cmake --preset ninja-msvc -DHORIZON_BUILD_BENCHMARKS=ON
cmake --preset ninja-msvc -DHORIZON_BUILD_OCARINA=ON
cmake --preset ninja-msvc -DHORIZON_BUILD_OCARINA=ON -DHORIZON_BUILD_OCARINA_TESTS=ON
cmake --preset ninja-msvc -DHORIZON_BUILD_DEPENDENCY_TOOLS=ON
cmake --preset ninja-msvc -DHORIZON_ENABLE_DEPENDENCY_INSTALL=ON
```

For newcomer on-demand target selection, load `docs/tasks/optional-build-targets.md` first; it contains the short command table, clean-configuration reset, and validation steps.

## Rules

- Root `CMakeLists.txt` owns project options, dependencies, and subdirectories.
- `src/CMakeLists.txt` defines `Horizon`.
- `tools/CMakeLists.txt` defines `ShaderCompileScripts`.
- `examples/CMakeLists.txt` defines `HorizonExamples`.
- `tests/CMakeLists.txt` defines `HorizonTests`.
- `include/` is the public include surface.
- Keep Vulkan, VMA, Windows, and implementation-only types out of public headers unless truly required.
- After CMake changes, run configure plus the smallest relevant build.
- The common entry point for newcomers and agents is `tools/dev.ps1`; use direct `cmake` commands only when investigating lower-level issues.
- Each configure preset uses its own `build/<preset>` directory; do not assume all generators share `build/`.
- FetchContent dependencies use a required shared source cache and preset-local build directories.
- Slang is a required local input. CMake does not download it during configure; set `HORIZON_SLANG_ROOT` or pre-populate `third-party/slang/download/slang-<version>-windows-<arch>.zip`.

## Presets And Directories

- Presets are defined in `CMakePresets.json`.
- Windows defaults to `ninja-msvc` / `msvc-debug`; `ninja-clang` and `vs2022` also exist.
- Linux presets are `ninja-linux-gcc` and `ninja-linux-clang`; macOS uses `ninja-macos`.
- Run `cmake --list-presets` when local preset availability is uncertain.
- Common build directories are `build/ninja-msvc`, `build/ninja-clang`, `build/vs2022`, `build/ninja-linux-gcc`, `build/ninja-linux-clang`, and `build/ninja-macos`.
- `HORIZON_BUILD_TOOLS` controls `tools/`; disabling it removes `ShaderCompileScripts`.
- `HORIZON_BUILD_EXAMPLES` controls `examples/` and example dependencies; disabling it removes `HorizonExamples`.
- `HORIZON_BUILD_TESTS` controls `tests/`; the test entry is `HorizonTests`, and `HorizonTests.exe --list` explains what it covers.
- `HORIZON_BUILD_OCARINA` controls `modules/ocarina`; it also requires `CUDA_PATH`. `HORIZON_BUILD_OCARINA_TESTS` separately controls ocarina's own tests.
- `HORIZON_BUILD_DEPENDENCY_TOOLS` controls SPIRV-Tools-style third-party command line tools; `HORIZON_ENABLE_DEPENDENCY_INSTALL` controls third-party install rules.

## FetchContent Rules

- Shared source cache: `build/_deps/*-src`.
- Shared download/update temp directories: `build/_deps/*-tmp`.
- Preset-local third-party build directories: `build/<preset>/deps/*-build`.
- Prefer `horizon_fetchcontent_declare(...)` for new FetchContent dependencies.
- Do not call bare `FetchContent_Declare(...)` unless the dependency truly needs custom `SOURCE_DIR` or `BINARY_DIR`.
- `HORIZON_FETCHCONTENT_REQUIRE_SOURCE_CACHE` defaults to `ON`; missing source checkouts fail instead of being populated during configure. Set it to `OFF` only for an explicit bootstrap run.
- If `build/<preset>/_deps` appears, first check stale cache or a new dependency bypassing `horizon_fetchcontent_declare(...)`.
- For daily cleanup, delete the specific `build/<preset>` directory; delete `build/_deps` only when third-party sources must be re-cloned.

## Visual Studio / MSVC

- In Visual Studio, open the repository root, not a single `.cpp` file.
- VS 2026 daily debugging uses `ninja-msvc` / `msvc-debug`; the dropdown commonly shows `Debug (ninja-msvc-msvc-debug)`.
- The debug target is usually `HorizonExamples`, with `examples/main.cpp` as the entry point.
- The executable is usually `build/ninja-msvc/examples/Debug/HorizonExamples.exe`.
- If VS does not show `HorizonExamples`, confirm configure succeeded and `HORIZON_BUILD_EXAMPLES` was not disabled.
- For command-line MSVC reproduction, prefer the Visual Studio Developer Command Prompt; plain PowerShell may miss the MSVC include/lib environment and fail on standard headers such as `<cstdint>`.

```powershell
cmd.exe /d /s /c "`"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat`" -arch=x64 -host_arch=x64 && cmake --build --preset msvc-debug --target Horizon"
```

Replace the target as needed; use `Horizon` for library validation and `HorizonExamples` when reproducing example debugging.

Use the unified test entry for test validation:

```powershell
cmd.exe /d /s /c "`"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat`" -arch=x64 -host_arch=x64 && cmake --build --preset msvc-debug --target HorizonTests && ctest --test-dir build/ninja-msvc -C Debug -R HorizonTests --output-on-failure"
```

Run `build\ninja-msvc\tests\Debug\HorizonTests.exe --list` to see what the tests cover.

## MSVC UTF-8 And Preprocessor Diagnostics

- When diagnosing MSVC failures from Chinese comments or UTF-8 source, generate `compile_commands.json` first and check whether the real compile commands include `/source-charset:utf-8`, `/execution-charset:utf-8`, or equivalent `/utf-8`.
- Put final fixes in target-level compile options; do not treat global `CL=/utf-8` as a project fix because it can mix with existing charset flags and trigger `D8016`.
- MSVC still parses skipped `#elif` condition text; put compiler built-ins such as `__has_attribute(...)` inside nested `#if` blocks after the compiler branch is known, otherwise MSVC can emit `C4067`.

## Notes

- Current preset flow is Windows / MSVC / Ninja focused.
- Docs-only changes normally do not need CMake builds.
