# Corona Framework Getting Started (Build / Run / Test)

This document is a practical onboarding guide (Windows-first; other platforms follow the same preset pattern).

## Prerequisites

- CMake 4.0+ (repo requires `cmake_minimum_required(VERSION 4.0)`) 
- A C++20-capable compiler
  - Windows: MSVC (recommended) or LLVM Clang

## Configure and Build

### Option A: Use CMake Presets (recommended)

#### MSVC + Ninja Multi-Config

```powershell
cmake --preset ninja-msvc
cmake --build --preset msvc-debug
```

#### Clang + Ninja Multi-Config

```powershell
cmake --preset ninja-clang
cmake --build --preset clang-debug
```

#### Visual Studio 2022

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug
```

### Option B: Toggle examples/tests

Top-level `CMakeLists.txt` gates `examples/` and `tests/` via `CORONA_FRAMEWORK_BUILD_EXAMPLES` and `CORONA_FRAMEWORK_BUILD_TESTS`.

Example: disable tests, build libraries + examples

```powershell
cmake --preset ninja-msvc -D CORONA_FRAMEWORK_BUILD_TESTS=OFF
cmake --build --preset msvc-debug
```

## Run Examples

Example target names are defined in `examples/CMakeLists.txt`:

- `01_event_bus`
- `02_system_lifecycle`
- `03_event_stream`
- `04_vfs`
- `05_multithreading`
- `06_game_loop`
- `07_storage`

With multi-config generators (Ninja Multi-Config / Visual Studio), executables are typically under `build/examples/<Config>/`.

For example (Debug):

```powershell
.\build\examples\Debug\01_event_bus.exe
.\build\examples\Debug\06_game_loop.exe
```

Build a single example target:

```powershell
cmake --build build --config Debug --target 06_game_loop
```

## Run Tests

### Run all tests

```powershell
Set-Location .\build
ctest -C Debug
```

### Run a single test binary

Test executables are typically under `build/tests/<Config>/`.

```powershell
.\build\tests\Debug\kernel_event_stream_test.exe
```

Test target naming comes from `tests/CMakeLists.txt` via `corona_add_test(<target> <src...>)`; the produced binary name is `<target>`.

## Troubleshooting

### 1) Missing TBB runtime DLLs

The build copies TBB runtime artifacts to each target output directory (see `cmake/CoronaTbbConfig.cmake` and `corona_copy_tbb_runtime_artifacts()`).

If you still see missing DLL errors:
- Make sure you run the executable from its output directory
- Rebuild the target: `cmake --build build --config Debug`

### 2) Third-party fetch failures

- Logging library `quill` is fetched via `FetchContent` (see `cmake/CoronaThirdParty.cmake`).
- If your network is restricted, configure proxy/mirror or rely on a prepared dependency cache.

### 3) Regenerate build directory

```powershell
Remove-Item -Recurse -Force .\build
cmake --preset ninja-msvc
cmake --build --preset msvc-debug
```
