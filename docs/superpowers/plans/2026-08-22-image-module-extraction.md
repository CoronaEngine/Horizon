# Horizon Image Module Extraction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Move the host-side image container and PNG/JPEG/BMP/TGA/HDR/EXR codecs from `src/dsl/image` into an independently buildable `horizon-image` module under `src/image`, with regression tests preserving the supported formats.

**Architecture:** `horizon-image` owns CPU image storage and codec operations in `horizon::image`; it publicly depends on `horizon-core` and `horizon-math`, and privately consumes Conan-provided STB and TinyEXR. `core::PixelStorage` remains in Core, while DSL, AST, RHI, and old Ocarina remain outside the module dependency graph.

**Tech Stack:** C++20, CMake 4, Ninja Multi-Config, Conan 2, STB `cci.20240531`, TinyEXR `1.0.7`, CTest.

**Spec:** `docs/architecture/image-module.md`

## Global Constraints

- Modify Horizon source only under `src/`; old `modules/ocarina` is reference-only and must not be changed.
- Preserve PNG, JPEG/JPG, BMP, TGA, HDR, and EXR loading/saving behavior.
- Keep `core::PixelStorage` in `src/core/image/image_format.h`; move all host image classes to `horizon::image`.
- Enforce `horizon-image -> horizon-math -> horizon-core` and forbid DSL, AST, RHI, or Vulkan dependencies from Image.
- Manage STB and TinyEXR through Conan and CMake `find_package`; do not use `FetchContent` or old Ocarina extension paths.
- Do not commit, push, rebase, or discard existing worktree changes; leave implementation changes uncommitted for user review.
- Reconfigure after dependency and target changes; `ninja: no work to do` is not verification.

---

### Task 1: Lock the public Image contract with a failing test target

**Files:**
- Create: `tests/image/CMakeLists.txt`
- Create: `tests/image/test_image.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: existing `core::PixelStorage`, `math::uint2`, `math::float4`.
- Produces: executable target `horizon-test-image`, CTest name `horizon.image`, and the expected public include `#include "image/image.h"`.

- [x] **Step 1: Write the failing in-memory and format regression test**

  Add a standalone test executable following the existing `failures`/`expect` pattern. Use a temporary directory below `std::filesystem::temp_directory_path()` and a deterministic 2x2 RGBA fixture. Cover:

  ```cpp
  using horizon::core::PixelStorage;
  using horizon::image::ColorSpace;
  using horizon::image::Image;
  using horizon::math::float4;
  using horizon::math::make_uint2;

  const std::array<float4, 4> hdr_pixels{{
      {0.25f, 0.5f, 1.0f, 1.0f},
      {1.5f, 0.25f, 0.0f, 1.0f},
      {0.0f, 2.0f, 0.5f, 1.0f},
      {1.0f, 1.0f, 1.0f, 1.0f},
  }};
  Image image = Image::from_data(hdr_pixels.data(), make_uint2(2u, 2u));
  ```

  Assert in-memory resolution/storage/size/view; lossless PNG pixels; BMP/TGA/JPEG dimensions and `BYTE4`; HDR/EXR dimensions, `FLOAT4`, finite channels, and approximate source values; unsupported `.gif` save throws with the extension/path in the message. Delete only files created by this test through explicit `std::filesystem::remove` calls.

- [x] **Step 2: Register the test target**

  Add `tests/image/CMakeLists.txt`:

  ```cmake
  add_executable(horizon-test-image test_image.cpp)
  target_link_libraries(horizon-test-image PRIVATE horizon-image)
  target_compile_features(horizon-test-image PRIVATE cxx_std_20)
  add_dependencies(horizon-tests horizon-test-image)
  add_test(NAME horizon.image COMMAND horizon-test-image)
  ```

  Add `add_subdirectory(image)` and `horizon-image` to the aggregate dependencies in `tests/CMakeLists.txt`.

- [x] **Step 3: Configure to prove the test is red**

  Run:

  ```powershell
  conda run -n horizon-dev --no-capture-output python tools/dev.py configure --configuration Debug --target-family core
  ```

  Expected: configure fails because `horizon-image` and/or `image/image.h` does not exist yet. Record the first causal error.

---

### Task 2: Add Conan/CMake dependency ownership and the Image target

**Files:**
- Modify: `conanfile.py`
- Create: `cmake/horizon_image_dependencies.cmake`
- Modify: `cmake/horizon_example_dependencies.cmake`
- Modify: `CMakeLists.txt`
- Modify: `src/CMakeLists.txt`
- Create: `src/image/CMakeLists.txt`

**Interfaces:**
- Consumes: Conan packages `stb/cci.20240531`, `tinyexr/1.0.7`; targets `horizon-core`, `horizon-math`.
- Produces: aliases `horizon::stb`, `horizon::tinyexr`; static target `horizon-image` with public source include root.

- [x] **Step 1: Promote codec packages to base requirements**

  In `conanfile.py::requirements`, add STB and TinyEXR beside the unconditional Core dependencies and remove the examples-only STB requirement:

  ```python
  self.requires("stb/cci.20240531")
  self.requires("tinyexr/1.0.7")
  ```

- [x] **Step 2: Create stable codec aliases**

  Create `cmake/horizon_image_dependencies.cmake` with an include guard, a local alias helper, `find_package(stb CONFIG REQUIRED)`, and `find_package(tinyexr CONFIG REQUIRED)`. Accept package target candidates `stb::stb`/`stb` and `tinyexr::tinyexr`/`tinyexr`; emit `FATAL_ERROR` when either Horizon alias is missing.

- [x] **Step 3: Make the alias owner unconditional**

  Include `cmake/horizon_image_dependencies.cmake` immediately after `horizon_core_dependencies.cmake` in the root CMake file. Remove only STB discovery/alias creation from `horizon_example_dependencies.cmake`; examples continue linking `horizon::stb`.

- [x] **Step 4: Register the module target in dependency order**

  Add `add_subdirectory(image)` after Math and before AST in `src/CMakeLists.txt`. Create `src/image/CMakeLists.txt`:

  ```cmake
  file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS *.h*)
  file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS *.cpp)
  add_library(horizon-image STATIC ${HEADER_FILES} ${SOURCE_FILES})
  target_include_directories(horizon-image PUBLIC ${PROJECT_SOURCE_DIR}/src)
  target_compile_features(horizon-image PUBLIC cxx_std_20)
  target_link_libraries(horizon-image
      PUBLIC horizon-core horizon-math
      PRIVATE horizon::stb horizon::tinyexr)
  ```

- [x] **Step 5: Reconfigure to validate dependency discovery**

  Run the Task 1 configure command. Expected: Conan installs/resolves both codec packages and CMake advances past both `find_package` calls; a missing source/header failure is still acceptable until Task 3.

---

### Task 3: Move Image out of DSL and remove its DSL-only math dependency

**Files:**
- Move: `src/dsl/image/image_base.h` -> `src/image/image_base.h`
- Move: `src/dsl/image/image.h` -> `src/image/image.h`
- Move: `src/dsl/image/image.cpp` -> `src/image/image.cpp`
- Modify: `src/core/header.h`
- Modify: `src/dsl/CMakeLists.txt`

**Interfaces:**
- Consumes: `core::PixelStorage`, Core allocation/logging helpers, Math vector types.
- Produces: `image::ImageBase`, `image::ImageView`, `image::Image`, `image::ColorSpace`, `image::EToneMap`, `image::ImageWrap`; `OC_IMAGE_API` export annotation.

- [x] **Step 1: Move the three source files without copying them**

  Use filesystem moves so `src/dsl/image` disappears. Preserve public class/function names and signatures except for namespace ownership.

- [x] **Step 2: Establish the real namespace and export owner**

  Add `OC_IMAGE_API` in `src/core/header.h` using the existing DLL macro pattern. Change all moved declarations/definitions from `namespace horizon::core` to `namespace horizon::image`, replace `OC_CORE_API` with `OC_IMAGE_API`, and qualify or narrowly import needed Core/Math names inside `horizon::image`.

- [x] **Step 3: Replace the DSL math include with host-only Image helpers**

  Remove `#include "dsl/math/base.h"`. Add private functions in `src/image/image.cpp` for the exact host operations used by codecs:

  ```cpp
  template<typename T>
  [[nodiscard]] T lerp(float t, const T &a, const T &b) noexcept {
      return (1.0f - t) * a + t * b;
  }

  template<typename T>
  [[nodiscard]] T srgb_to_linear(const T &value) {
      return horizon::math::select(
          value <= T{0.04045f}, value / T{12.92f},
          horizon::math::pow((value + T{0.055f}) / T{1.055f}, T{2.4f}));
  }
  ```

  Provide scalar/vector-compatible byte packing helpers equivalent to the existing `make_8bit` and `make_rgba`. Keep them private to Image so Math does not acquire an image codec concern.

- [x] **Step 4: Point codec implementation at Conan headers**

  Keep `STB_IMAGE_IMPLEMENTATION` and `STB_IMAGE_WRITE_IMPLEMENTATION` defined only in `src/image/image.cpp`. Include `<stb_image.h>`, `<stb_image_write.h>`, and `<tinyexr.h>`; link TinyEXR through its Conan target and do not include an old `ext/` path.

- [x] **Step 5: Remove obsolete DSL build filtering**

  Delete only the `image/image.cpp` exclusion from `src/dsl/CMakeLists.txt`; do not link `horizon-dsl` to `horizon-image`.

- [x] **Step 6: Build to expose migration defects**

  Run:

  ```powershell
  conda run -n horizon-dev --no-capture-output python tools/dev.py build horizon-test-image --configuration Debug --target-family core
  ```

  Expected: compile/link succeeds, or fails at the first concrete source incompatibility. Any defect revealed here must receive a focused regression assertion in `tests/image/test_image.cpp` before its implementation is changed.

---

### Task 4: Make every preserved format test pass

**Files:**
- Modify: `src/image/image.cpp`
- Modify: `tests/image/test_image.cpp`

**Interfaces:**
- Consumes: the public Image API from Task 3.
- Produces: validated PNG/JPEG/BMP/TGA/HDR/EXR dispatch and explicit unsupported-extension errors.

- [x] **Step 1: Run only the Image regression test and capture the first failure**

  Run:

  ```powershell
  ctest --test-dir build/conan/core/debug --build-config Debug --output-on-failure -R "^horizon\.image$"
  ```

  Expected: any remaining format/conversion defect produces a precise failing assertion or exception.

- [x] **Step 2: Fix one observed behavior at a time**

  Apply the smallest implementation correction required by the failing test. Preserve these invariants:

  ```text
  BYTE1/BYTE2/BYTE4 -> FLOAT1/FLOAT2/FLOAT4
  FLOAT1/FLOAT2/FLOAT4 -> BYTE1/BYTE2/BYTE4
  .png/.bmp/.tga/.jpg/.jpeg -> STB 8-bit path
  .hdr -> STB float path
  .exr -> TinyEXR float path
  every other extension -> exception naming the extension/path
  ```

  Ensure failed STB calls and TinyEXR calls free their library-owned error/image/header data before throwing. EXR channel mapping must read only `num_channels` entries and map named R/G/B/A channels correctly.

- [x] **Step 3: Re-run after each minimal correction**

  Run the Image CTest command after each change. Expected final result: `horizon.image` passes with all six format families exercised.

---

### Task 5: Enforce module boundaries and finish verification

**Files:**
- Create: `tests/architecture/check_image_module_boundary.cmake`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/architecture/image-module.md`
- Modify: `docs/architecture/overview.md`

**Interfaces:**
- Consumes: final filesystem layout and target graph.
- Produces: CTest `horizon.architecture.image_module_boundary` and architecture docs distinguishing implemented state from future GPU/RHI work.

- [x] **Step 1: Write the failing boundary check**

  Add a CMake script that recursively scans `src/image` and fails if content contains `dsl/`, `ast/`, `rhi/`, `vulkan`, or `modules/ocarina`; also fail if `src/dsl/image` still exists or if `src/dsl/CMakeLists.txt` mentions `horizon-image`. Register it with:

  ```cmake
  add_test(
      NAME horizon.architecture.image_module_boundary
      COMMAND ${CMAKE_COMMAND}
              -DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
              -P ${CMAKE_CURRENT_SOURCE_DIR}/architecture/check_image_module_boundary.cmake)
  ```

- [x] **Step 2: Run the boundary checks**

  Run:

  ```powershell
  ctest --test-dir build/conan/core/debug --build-config Debug --output-on-failure -R "horizon\.architecture\.(core_math|image_module)_boundary"
  ```

  Expected: both architecture tests pass.

- [x] **Step 3: Update architecture status**

  Change the Image design status from `待实施` to `已实施`, document the actual target/namespace/dependency paths, and add Image to `docs/architecture/overview.md` without describing future GPU/RHI integration as implemented.

- [x] **Step 4: Run the full relevant verification suite**

  Run:

  ```powershell
  conda run -n horizon-dev --no-capture-output python tools/dev.py build horizon-tests --configuration Debug --target-family core
  ctest --test-dir build/conan/core/debug --build-config Debug --output-on-failure
  git diff --check
  git status --short
  ```

  Expected: build succeeds, all registered Core/Math/AST/DSL/Image and architecture tests pass, `git diff --check` emits no errors, and only this task's files plus the user's pre-existing untracked planning files remain changed/untracked.
