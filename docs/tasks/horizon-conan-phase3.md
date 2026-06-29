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
- The recipe generates `CMakeToolchain` and `CMakeDeps`.
- The recipe maps options to existing CMake cache variables without changing Horizon's default CMake build.
- The recipe exports `cmake/HeliconShaderCompile.cmake` as a CMake build module so consumers can keep calling `helicon_compile_shaders()` after `find_package(Horizon CONFIG)`.
- When `with_tools=True`, the recipe builds/packages `ShaderCompileScripts` and exports `HORIZON_SHADER_COMPILE_SCRIPTS_EXECUTABLE` so the build module can create an imported tool target for package consumers.
- Consumer-side `conan install` now writes `horizon_BUILD_MODULES_PATHS_<CONFIG>` with the Helicon build module path. Full `find_package(Horizon CONFIG)` smoke is still blocked until the Horizon library artifact is available in the package/editable layout.
- Windows MSVC Debug/Release profiles live in `conan/profiles/`.

## Validation

```powershell
conan install . --profile:host conan/profiles/windows-msvc-debug --profile:build conan/profiles/windows-msvc-debug --build=missing
conan graph info . --profile:host conan/profiles/windows-msvc-debug --profile:build conan/profiles/windows-msvc-debug
```

## Boundaries

- This is not yet a complete release package.
- Existing FetchContent dependency migration is deferred.
- Full component targets and package-library layout validation are deferred.
- `conan create` should be validated only after the package dependency surface is made explicit.
