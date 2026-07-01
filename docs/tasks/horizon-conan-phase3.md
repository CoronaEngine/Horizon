# Horizon Conan Phase 3

This task note records the first Horizon Conan package scaffold used by CoronaEngine's build-governance migration.

## Current Scope

- `conanfile.py` defines `horizon/<version>`.
- The default migration version is `0.5.0`; CI/release jobs may override it with `HORIZON_CONAN_VERSION`.
- The recipe exposes the first package options:
  - `shared`
  - `with_ocarina`
  - `with_vision_hotfix`
  - `with_cuda`
  - `with_tools`
  - `with_examples`
  - `with_tests`
  - `with_benchmarks`
  - `with_ocarina_tests`
  - `with_ocarina_vulkan`
  - `with_hardcode_shaders`
- The Conan default path enables `with_ocarina=True`, `with_cuda=True`, and `with_vision_hotfix=True`; direct CMake presets still keep Horizon's original CMake defaults.
- The recipe generates `CMakeToolchain` and `CMakeDeps`.
- The recipe maps options to existing CMake cache variables without changing Horizon's default CMake build.
- When ocarina or vision-hotfix are enabled, `conan create` builds the `ocarina` and `vision-hotfix-all` targets in addition to `Horizon`.
- The recipe exports `cmake/HeliconShaderCompile.cmake` as a CMake build module so consumers can keep calling `helicon_compile_shaders()` after `find_package(Horizon CONFIG)`.
- When `with_tools=True`, the recipe builds/packages `ShaderCompileScripts` and exports `HORIZON_SHADER_COMPILE_SCRIPTS_EXECUTABLE` so the build module can create an imported tool target for package consumers.
- Consumer-side `conan install` writes `horizon_BUILD_MODULES_PATHS_<CONFIG>` with the Helicon build module path. `conan create` is the normal way to generate the Horizon library artifact and optional `ShaderCompileScripts` tool package in the local cache.
- Windows MSVC Debug/Release profiles live in `conan/profiles/`.

## Validation

```powershell
conan install . --profile:host conan/profiles/windows-msvc-debug --profile:build conan/profiles/windows-msvc-debug --build=missing
conan graph info . --profile:host conan/profiles/windows-msvc-debug --profile:build conan/profiles/windows-msvc-debug
.\tools\dev.ps1 package ShaderCompileScripts
```

## Boundaries

- The package is the consumer-facing dependency surface for CoronaEngine and should be published to a Conan remote for machines that do not have a Horizon checkout.
- CoronaEngine should consume `horizon/<version>` from a Conan cache or remote, not through editable mode or a sibling-repo bridge recipe.
- Build/package with `with_tools=True` when consumers need `helicon_compile_shaders()` to invoke `ShaderCompileScripts`.
