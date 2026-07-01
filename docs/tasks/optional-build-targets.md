# Optional Build Targets
<!-- TASK_DOCS_OPTIONAL_BUILD_TARGETS_ZH_CN_SHA256: b19fd3fed3f0f7f8610c9b4521a7535d9adca6919de66ca8ee159506f1c50c85 -->

## When To Use

- A newcomer configures the repo and sees too many targets or artifacts.
- You only want the core Horizon library, without examples, tests, tools, ocarina, or third-party CLIs by default.
- You need to temporarily enable one target family for debugging or validation.

## Default Behavior

- This note describes direct CMake preset defaults; the `tools/dev.ps1` Conan workflow may have different root package defaults.
- Direct CMake default configure favors a clean build and enables only targets required by the core library.
- `tools/`, `examples/`, `tests/`, `benchmarks/`, and `modules/ocarina` are off by default.
- SPIRV-Tools-style third-party command line tools and third-party install rules are off by default.
- Old build directories may cache previous ON values. If behavior looks stale, reconfigure with explicit OFF values or delete the affected `build/<preset>`.

## Enable On Demand

```powershell
# Core library only
cmake --preset ninja-msvc
cmake --build --preset msvc-debug --target Horizon

# Shader/codegen tool
cmake --preset ninja-msvc -DHORIZON_BUILD_TOOLS=ON
cmake --build --preset msvc-debug --target ShaderCompileScripts

# Examples; also enables tools/
cmake --preset ninja-msvc -DHORIZON_BUILD_EXAMPLES=ON
cmake --build --preset msvc-debug --target HorizonExamples

# Unified Horizon tests
cmake --preset ninja-msvc -DHORIZON_BUILD_TESTS=ON
cmake --build --preset msvc-debug --target HorizonTests
ctest --test-dir build/ninja-msvc -C Debug -R HorizonTests --output-on-failure

# Benchmarks
cmake --preset ninja-msvc -DHORIZON_BUILD_BENCHMARKS=ON

# ocarina; also requires CUDA_PATH
cmake --preset ninja-msvc -DHORIZON_BUILD_OCARINA=ON

# ocarina tests; also requires ocarina
cmake --preset ninja-msvc -DHORIZON_BUILD_OCARINA=ON -DHORIZON_BUILD_OCARINA_TESTS=ON

# Third-party command line tools such as spirv-*; only enable when needed
cmake --preset ninja-msvc -DHORIZON_BUILD_DEPENDENCY_TOOLS=ON

# Third-party install rules; usually unnecessary for daily development
cmake --preset ninja-msvc -DHORIZON_ENABLE_DEPENDENCY_INSTALL=ON
```

## Restore Clean Configure

```powershell
cmake --preset ninja-msvc `
  -DHORIZON_BUILD_TOOLS=OFF `
  -DHORIZON_BUILD_EXAMPLES=OFF `
  -DHORIZON_BUILD_TESTS=OFF `
  -DHORIZON_BUILD_BENCHMARKS=OFF `
  -DHORIZON_BUILD_OCARINA=OFF `
  -DHORIZON_BUILD_OCARINA_TESTS=OFF `
  -DHORIZON_BUILD_DEPENDENCY_TOOLS=OFF `
  -DHORIZON_ENABLE_DEPENDENCY_INSTALL=OFF
```

If old targets or artifacts are still visible, first check whether they are files left by an earlier build. Disabling CMake targets does not delete existing `.exe` / `.dll` files.

## Validation

```powershell
cmake --preset ninja-msvc
cmake --build --preset msvc-debug --target Horizon
ninja -C build/ninja-msvc -f build-Debug.ninja -t query all:Debug
```

In a clean default build, `all:Debug` should only pull library-related targets. It should not pull `ShaderCompileScripts`, `HorizonExamples`, `HorizonTests`, `ocarina`, or `spirv-*` CLI targets by default.
