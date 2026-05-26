# Horizon Build Context
<!-- AGENT_DOCS_BUILD_ZH_CN_SHA256: 50455579e907b1a8e5dd9d1f58f70cd3312845176bb79273c603feeff9d064a4 -->

Load this file only for CMake, preset, build, CI, or validation-command work.

## Targets

- `Horizon`: main static library.
- `ShaderCompileScripts`: shader/codegen tool target under `tools/`.
- `HorizonExamples`: examples target, enabled by `HORIZON_BUILD_EXAMPLES`.

## Common Commands

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

## Notes

- Current preset flow is Windows / MSVC / Ninja focused.
- Docs-only changes normally do not need CMake builds.
