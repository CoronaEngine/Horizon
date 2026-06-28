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
- Windows MSVC Debug/Release profiles live in `conan/profiles/`.

## Validation

```powershell
conan install . --profile:host conan/profiles/windows-msvc-debug --profile:build conan/profiles/windows-msvc-debug --build=missing
conan graph info . --profile:host conan/profiles/windows-msvc-debug --profile:build conan/profiles/windows-msvc-debug
```

## Boundaries

- This is not yet a complete release package.
- Existing FetchContent dependency migration is deferred.
- Stable CMake package config/export targets are deferred.
- `conan create` should be validated only after the package dependency surface is made explicit.
