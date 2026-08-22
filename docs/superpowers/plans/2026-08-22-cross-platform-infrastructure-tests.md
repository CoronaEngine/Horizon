# 跨平台基础设施测试实施计划

> **供自动化执行者使用：** 必须使用子技能 `superpowers:subagent-driven-development`（推荐）或 `superpowers:executing-plans`，逐任务执行本计划。所有步骤使用复选框（`- [ ]`）跟踪。

**目标：** 将 `horizon-core`、`horizon-math`、`horizon-ast`、`horizon-dsl` 与完整引擎解耦构建，并在 Windows、Linux、macOS 上运行全部已注册的基础设施测试。

**架构：** 增加明确的引擎构建边界，使基础设施 target 不配置 Helicon，也不解析引擎专用 Conan 依赖。为 Core 提供完整的 Windows/POSIX 平台后端，再让现有开发脚本自动选择本机 Conan profile，使三个 CI 平台使用同一个 `tools/dev.py` 入口。

**技术栈：** C++20、CMake 4.x、Conan 2、Ninja Multi-Config、Python 3.12 `unittest`、CTest、GitHub Actions、Win32 API、POSIX `dlopen`/`backtrace` API。

**设计说明：** `docs/superpowers/specs/2026-08-22-cross-platform-infrastructure-tests-design.md`

## 全局约束

- 源码依赖保持 `horizon-dsl -> horizon-ast -> horizon-math -> horizon-core`，不得增加反向依赖。
- 四个基础设施 target 固定命名为 `horizon-core`、`horizon-math`、`horizon-ast`、`horizon-dsl`。
- `HORIZON_BUILD_ENGINE` 和 Conan `with_engine` 默认开启，保持现有完整引擎工作流不变。
- `core` 目标族必须关闭引擎并开启测试，且不得解析 Slang、Vulkan、SPIR-V、Tracy、Helicon、Ocarina 或 CUDA 依赖。
- Windows、Linux、macOS 必须提供同一组 Core 平台 API，不允许生成故意缺少平台符号的库。
- CI 只构建 `horizon-tests`、检查支持 32 个元素的 tuple 生成结果，并运行全部已注册 CTest。
- 不得查阅或修改 `modules/ocarina/`。
- 保留与本任务无关的工作区修改以及已有的 tuple 设计文档。

## 文件结构

### 新建文件

- `cmake/horizon_engine_dependencies.cmake`：查找并建立 Helicon/完整引擎专用依赖别名。
- `src/core/runtime/platform_windows.cpp`：承载现有 Win32 平台实现。
- `src/core/runtime/platform_posix.cpp`：提供 Linux/macOS 平台实现。
- `tests/core/test_dynamic_module.cpp`：测试 Core 公共平台 API。
- `tests/core/test_dynamic_module_library.cpp`：生成供动态加载测试使用的模块。

### 修改文件

- `CMakeLists.txt`：声明并约束引擎构建边界。
- `CMakePresets.json`：增加完整引擎的 `engine` 目标族 preset。
- `cmake/horizon_dev_bootstrap.cmake`：接受 `engine` 目标族。
- `cmake/horizon_core_dependencies.cmake`：只保留基础设施依赖。
- `src/CMakeLists.txt`：始终创建四个基础模块，仅按需创建 Helicon/Horizon。
- `src/core/CMakeLists.txt`：选择唯一平台后端并链接 `${CMAKE_DL_LIBS}`。
- `src/core/header.h`：消除 Windows 头泄漏并提供可移植 API 宏。
- `conanfile.py`：增加 `with_engine` 并按边界声明依赖。
- `tools/dev.py`：使 `core` 关闭引擎，并将 `Horizon` 映射到 `engine`。
- `tools/workflow.py`：检测非 Windows Conan profile 并加载 POSIX 构建环境。
- `tools/test_workflow.py`：验证目标族、profile、环境解析和 CI 矩阵。
- `tests/CMakeLists.txt`：让聚合测试 target 覆盖四个基础模块。
- `tests/core/CMakeLists.txt`：注册平台模块及加载测试。
- `.github/workflows/core-tests.yml`：运行三平台测试矩阵。
- `docs/architecture/overview.md`：记录引擎边界和跨平台能力范围。

---

### 任务 1：建立不含引擎的基础设施构建边界

**文件：**

- 修改：`tools/test_workflow.py`
- 修改：`tools/dev.py`
- 修改：`tools/workflow.py`
- 修改：`conanfile.py`
- 修改：`CMakePresets.json`
- 修改：`cmake/horizon_dev_bootstrap.cmake`
- 修改：`CMakeLists.txt`
- 修改：`src/CMakeLists.txt`

**接口：**

- 输入：现有 `core` 目标族和 Conan `with_tests` 选项。
- 输出：Conan `with_engine: bool`、CMake `HORIZON_BUILD_ENGINE: BOOL`、新目标族 `engine`，以及 `Horizon -> engine` 的目标映射。

- [ ] **步骤 1：先修改测试，固定目标族契约**

在 `test_target_families_select_conan_options` 中加入：

```python
self.assertEqual(
    conan_options("core"),
    ["&:with_engine=False", "&:with_tests=True"],
)
self.assertEqual(conan_options("engine"), ["&:with_engine=True"])
self.assertEqual(target_family_for_target("Horizon"), "engine")
self.assertEqual(target_family_slug("engine"), "engine")
```

- [ ] **步骤 2：运行测试并确认 RED**

```powershell
python -m unittest tools/test_workflow.py
```

预期：测试失败，因为 `engine` 尚未注册，且 `core` 尚未传入 `with_engine=False`。

- [ ] **步骤 3：增加 Conan 选项和目标族映射**

在 `conanfile.py` 的 `options` 与 `default_options` 中分别增加：

```python
"with_engine": [True, False],
"with_engine": True,
```

在 `conanfile.py` 的 `_target_families`、`tools/workflow.py` 的 `TARGET_FAMILIES`、`cmake/horizon_dev_bootstrap.cmake` 的目标族列表中，将 `engine` 放在 `core` 后面。

在 `tools/dev.py` 中使用：

```python
TARGET_FAMILY_OPTIONS = {
    "core": ("&:with_engine=False", "&:with_tests=True"),
    "engine": ("&:with_engine=True",),
    "tools": ("&:with_tools=True",),
    "examples": ("&:with_examples=True",),
    "ocarina": ("&:with_ocarina=True", "&:with_cuda=True"),
    "ocarina-tests": (
        "&:with_ocarina=True",
        "&:with_cuda=True",
        "&:with_ocarina_tests=True",
    ),
    "vision-hotfix": (
        "&:with_ocarina=True",
        "&:with_cuda=True",
        "&:with_vision_hotfix=True",
    ),
}
```

并在 `target_family_for_target()` 的最前面加入：

```python
if target == "Horizon":
    return "engine"
```

- [ ] **步骤 4：增加 `engine` preset**

在 `CMakePresets.json` 增加 `engine-debug`、`engine-relwithdebinfo`、`engine-release`、`engine-minsizerel` configure preset。它们分别使用：

```json
{
  "name": "engine-debug",
  "displayName": "Engine - Debug",
  "generator": "Ninja Multi-Config",
  "binaryDir": "${sourceDir}/build/conan/engine/debug",
  "cacheVariables": {
    "HORIZON_DEV_CONFIGURATION": "Debug",
    "HORIZON_DEV_TARGET_FAMILY": "engine"
  }
}
```

其余三个配置保持相同结构，仅替换配置名称、显示名称和目录。再增加四个同名 build preset，分别指向对应 configure preset 并保持 `Debug`、`RelWithDebInfo`、`Release`、`MinSizeRel` 的准确大小写。

- [ ] **步骤 5：拒绝无效的 Conan/CMake 组合**

在 `conanfile.py::validate()` 中加入：

```python
engine_features = {
    "with_tools": bool(self.options.with_tools),
    "with_examples": bool(self.options.with_examples),
    "with_ocarina": bool(self.options.with_ocarina),
    "with_vision_hotfix": bool(self.options.with_vision_hotfix),
}
enabled_engine_features = [name for name, enabled in engine_features.items() if enabled]
if enabled_engine_features and not bool(self.options.with_engine):
    raise ConanInvalidConfiguration(
        "with_engine=False is incompatible with: " + ", ".join(enabled_engine_features)
    )
```

在 `generate()` 中加入：

```python
variables["HORIZON_BUILD_ENGINE"] = bool(self.options.with_engine)
```

在根 `CMakeLists.txt` 中加入：

```cmake
option(HORIZON_BUILD_ENGINE "Build Helicon and the complete Horizon engine target" ON)

if(NOT HORIZON_BUILD_ENGINE AND
   (HORIZON_BUILD_TOOLS OR HORIZON_BUILD_EXAMPLES OR HORIZON_BUILD_OCARINA OR HORIZON_BUILD_VISION_HOTFIX))
    message(FATAL_ERROR
        "HORIZON_BUILD_ENGINE=OFF is incompatible with tools, examples, Ocarina, and Vision hotfix targets")
endif()
```

- [ ] **步骤 6：让四个基础 target 始终存在**

`src/CMakeLists.txt` 顶层始终按以下顺序执行：

```cmake
add_subdirectory(core)
add_subdirectory(math)
add_subdirectory(ast)
add_subdirectory(dsl)
```

将 hardware wrapper 的源文件收集、`PUBLIC_HEADERS`、`add_subdirectory(Helicon)`、`add_library(Horizon ...)`、Horizon 链接和 Tracy 设置整体改为：

```cmake
if(HORIZON_BUILD_ENGINE)
    file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/hardware_wrapper/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/hardware_wrapper/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/hardware_wrapper/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/hardware_wrapper/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/hardware_wrapper_vulkan/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/hardware_wrapper_vulkan/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/hardware_wrapper_vulkan/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/hardware_wrapper_vulkan/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/kernel/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/kernel/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/kernel/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/kernel/*.h"
    )

    set(PUBLIC_HEADERS
        "${PROJECT_SOURCE_DIR}/include/horizon.h"
        "${PROJECT_SOURCE_DIR}/include/corona/kernel/core/i_logger.h"
        "${PROJECT_SOURCE_DIR}/include/corona/kernel/utils/stack_trace.h"
        "${PROJECT_SOURCE_DIR}/include/corona/kernel/utils/storage.h"
        "${PROJECT_SOURCE_DIR}/include/corona/pal/cfw_platform.h"
    )

    add_subdirectory(Helicon)
    add_library(Horizon STATIC ${SOURCE_FILES} ${PUBLIC_HEADERS})
    target_include_directories(Horizon
        PUBLIC "${PROJECT_SOURCE_DIR}/include"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(Horizon PUBLIC
        Helicon
        quill::quill
        horizon::volk
        horizon::vulkan_headers
        horizon::vma
    )

    if(HORIZON_ENABLE_TRACY)
        target_link_libraries(Horizon PRIVATE Tracy::TracyClient)
        target_compile_definitions(Horizon PRIVATE HORIZON_TRACY_ENABLED=1)
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(Horizon PRIVATE -Wno-nullability-completeness)
    endif()
endif()
```

根目录的 MSVC 设置改为：

```cmake
if(MSVC)
    add_compile_options(/Zi)
    add_link_options(/INCREMENTAL:NO)
    if(TARGET Helicon)
        target_compile_options(Helicon PUBLIC /utf-8)
    endif()
endif()
```

- [ ] **步骤 7：验证并提交构建边界**

```powershell
python -m unittest tools/test_workflow.py
python tools/dev.py configure --configuration Debug --target-family core
python tools/dev.py configure --configuration Debug --target-family engine
```

预期：Python 测试通过；Core 构建含四个基础 target，但没有 `Helicon`/`Horizon`；Engine 使用独立目录且仍包含 `Horizon`。

```powershell
git add CMakeLists.txt CMakePresets.json cmake/horizon_dev_bootstrap.cmake src/CMakeLists.txt conanfile.py tools/dev.py tools/workflow.py tools/test_workflow.py
git commit -m "build: separate infrastructure from engine targets"
```

---

### 任务 2：拆分基础设施依赖与引擎依赖

**文件：**

- 修改：`cmake/horizon_core_dependencies.cmake`
- 新建：`cmake/horizon_engine_dependencies.cmake`
- 修改：`CMakeLists.txt`
- 修改：`conanfile.py`
- 修改：`tools/test_workflow.py`

**接口：**

- 输入：任务 1 的 `HORIZON_BUILD_ENGINE` 与 `with_engine`。
- 输出：基础设施别名 `horizon::fmt`、`horizon::spdlog`、`horizon::xxhash`，以及仅供引擎使用的 ktm、pfr、SPIR-V、Vulkan、Volk、VMA 别名。

- [ ] **步骤 1：增加依赖边界测试并确认 RED**

```python
def test_core_conan_graph_excludes_engine_packages(self) -> None:
    recipe = Path("conanfile.py").read_text(encoding="utf-8")
    self.assertIn("if bool(self.options.with_engine):", recipe)
    self.assertIn('self.requires("slang/2026.10"', recipe)
    self.assertIn('self.requires("quill/11.0.2"', recipe)
```

```powershell
python -m unittest tools/test_workflow.py
```

预期：测试失败，因为 Slang 等引擎依赖仍是无条件需求。

- [ ] **步骤 2：收敛基础设施依赖文件**

`cmake/horizon_core_dependencies.cmake` 保留现有别名辅助函数，只查找：

```cmake
find_package(quill CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(xxHash CONFIG REQUIRED)

_horizon_alias_target(horizon::fmt fmt::fmt-header-only fmt::fmt)
_horizon_alias_target(horizon::spdlog spdlog::spdlog spdlog)
_horizon_alias_target(horizon::xxhash xxHash::xxhash xxhash::xxhash xxhash)

_horizon_require_target(quill::quill "Quill is required by Horizon Core logging")
_horizon_require_target(horizon::fmt "fmt is required by Horizon Core formatting")
_horizon_require_target(horizon::spdlog "spdlog is required by Horizon Core logging")
_horizon_require_target(horizon::xxhash "xxHash is required by Horizon Core hashing")
```

- [ ] **步骤 3：建立引擎依赖文件**

新建 `cmake/horizon_engine_dependencies.cmake`，包含 `include_guard(GLOBAL)`，并从原文件迁入以下完整发现与别名逻辑：

```cmake
find_package(ktm CONFIG REQUIRED)
_horizon_alias_target(horizon::ktm ktm::ktm)
_horizon_require_target(horizon::ktm "ktm is required by Helicon public headers")

find_package(pfr CONFIG REQUIRED)
_horizon_alias_target(horizon::pfr pfr::pfr pfr)
_horizon_require_target(horizon::pfr "Boost.PFR headers are required by Helicon")

find_package(SPIRV-Tools CONFIG REQUIRED)
_horizon_alias_target(horizon::spirv_tools_link SPIRV-Tools-link spirv-tools::spirv-tools-link)
_horizon_require_target(horizon::spirv_tools_link "SPIRV-Tools link target is required by Helicon")

if(MSVC AND TARGET SPIRV-Tools-opt)
    get_target_property(_horizon_spirv_tools_opt_imported SPIRV-Tools-opt IMPORTED)
endif()
if(MSVC AND TARGET SPIRV-Tools-opt AND NOT _horizon_spirv_tools_opt_imported)
    target_compile_options(SPIRV-Tools-opt PRIVATE /Wv:18)
    target_compile_options(SPIRV-Tools-opt PRIVATE /wd4717 /wd5232)
endif()
unset(_horizon_spirv_tools_opt_imported)

find_package(VulkanHeaders CONFIG REQUIRED)
_horizon_alias_target(horizon::vulkan_headers vulkan-headers::vulkan-headers Vulkan-Headers)
find_package(volk CONFIG REQUIRED)
_horizon_alias_target(horizon::volk volk::volk volk)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
_horizon_alias_target(horizon::vma GPUOpen::VulkanMemoryAllocator vulkan-memory-allocator::vulkan-memory-allocator VulkanMemoryAllocator)
```

- [ ] **步骤 4：按引擎开关加载 CMake 依赖**

```cmake
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/horizon_core_dependencies.cmake)

if(HORIZON_BUILD_ENGINE)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/horizon_engine_dependencies.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/horizon_slang.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/horizon_tracy.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/horizon_runtime_deps.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/HeliconShaderCompile.cmake)
endif()
```

- [ ] **步骤 5：按 `with_engine` 拆分 Conan requirements**

无条件保留：

```python
self.requires("quill/11.0.2", transitive_headers=True, transitive_libs=True)
self.requires("fmt/12.1.0")
self.requires("spdlog/1.17.0")
self.requires("xxhash/0.8.3")
```

仅在 `if bool(self.options.with_engine):` 下声明：

```python
self.requires("ktm/0.2.14", transitive_headers=True)
self.requires("pfr/1.91.0", transitive_headers=True)
self.requires("spirv-tools/1.4.350.0", transitive_headers=True, transitive_libs=True)
self.requires("volk/1.4.350.0", transitive_headers=True, transitive_libs=True)
self.requires("vulkan-headers/1.4.350.0", transitive_headers=True)
self.requires("vulkan-memory-allocator/3.4.0", transitive_headers=True)
self.requires("slang/2026.10", transitive_headers=True, transitive_libs=True)
if bool(self.options.with_tracy):
    self.requires("tracy/0.13.1", options={"on_demand": True})
```

- [ ] **步骤 6：验证依赖图并提交**

```powershell
python -m unittest tools/test_workflow.py
conan graph info . -pr:a conan/profiles/windows-msvc-debug -pr:b conan/profiles/windows-msvc-debug -c:h user.horizon:target_family=core -o "&:with_engine=False" -o "&:with_tests=True" --format=json
```

预期：依赖图包含 quill、fmt、spdlog、xxhash，不包含 slang、Vulkan、SPIR-V、Tracy、ktm、pfr。

```powershell
git add CMakeLists.txt cmake/horizon_core_dependencies.cmake cmake/horizon_engine_dependencies.cmake conanfile.py tools/test_workflow.py
git commit -m "build: isolate engine-only dependencies"
```

---

### 任务 3：实现完整的 Windows 与 POSIX Core 平台后端

**文件：**

- 重命名：`src/core/runtime/platform.cpp` → `src/core/runtime/platform_windows.cpp`
- 新建：`src/core/runtime/platform_posix.cpp`
- 修改：`src/core/CMakeLists.txt`
- 修改：`src/core/header.h`
- 修改：`tools/test_workflow.py`
- 新建：`tests/core/test_dynamic_module.cpp`
- 新建：`tests/core/test_dynamic_module_library.cpp`
- 修改：`tests/core/CMakeLists.txt`

**接口：**

- 输入：`src/core/runtime/platform.h` 中的现有声明。
- 输出：三个平台上完整实现动态模块加载、符号查找、模块命名、traceback；测试模块导出 `extern "C" int horizon_test_value()`。

- [ ] **步骤 1：先建立动态模块行为测试**

`tests/core/test_dynamic_module_library.cpp`：

```cpp
#include "core/header.h"

OC_EXPORT_API int horizon_test_value() noexcept {
    return 42;
}
```

`tests/core/test_dynamic_module.cpp` 必须完成以下行为：

```cpp
#include "core/runtime/platform.h"

#include <filesystem>
#include <iostream>

namespace {
int failures = 0;
void expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
}

int main(int argc, char **argv) {
    expect(argc == 2, "the test module path is provided");
    if (argc != 2) { return 1; }

    void *module = horizon::core::dynamic_module_load(std::filesystem::path{argv[1]});
    expect(module != nullptr, "the test module loads through the Core API");

    using ValueFunction = int (*)() noexcept;
    void *symbol = module == nullptr ? nullptr
        : horizon::core::dynamic_module_find_symbol(module, "horizon_test_value");
    auto value = reinterpret_cast<ValueFunction>(symbol);
    expect(value != nullptr && value() == 42, "the exported symbol resolves and executes");
    horizon::core::dynamic_module_destroy(module);

#if defined(_WIN32)
    expect(horizon::core::dynamic_module_name("sample") == "sample.dll", "Windows suffix");
#elif defined(__APPLE__)
    expect(horizon::core::dynamic_module_name("sample") == "libsample.dylib", "macOS suffix");
#else
    expect(horizon::core::dynamic_module_name("sample") == "libsample.so", "Linux suffix");
#endif
    expect(!horizon::core::traceback().empty(), "traceback returns frames");
    return failures == 0 ? 0 : 1;
}
```

- [ ] **步骤 2：注册测试并确认缺少实现时会失败**

在 `tests/core/CMakeLists.txt` 增加：

```cmake
add_library(horizon-test-dynamic-module MODULE test_dynamic_module_library.cpp)
target_include_directories(horizon-test-dynamic-module PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_features(horizon-test-dynamic-module PRIVATE cxx_std_20)

add_executable(horizon-test-core-platform test_dynamic_module.cpp)
target_link_libraries(horizon-test-core-platform PRIVATE horizon-core)
target_compile_features(horizon-test-core-platform PRIVATE cxx_std_20)

add_dependencies(horizon-tests horizon-test-dynamic-module horizon-test-core-platform)
add_test(NAME horizon.core.platform
         COMMAND horizon-test-core-platform $<TARGET_FILE:horizon-test-dynamic-module>)
```

临时从 `SOURCE_FILES` 移除当前 `runtime/platform.cpp`，然后运行：

```powershell
python tools/dev.py build horizon-test-core-platform --configuration Debug --target-family core
```

预期：链接因平台函数未定义而失败，证明测试能捕获缺失后端。后续正式平台选择逻辑替换该临时移除。

- [ ] **步骤 3：迁移 Windows 实现并修正公共头**

将 `platform.cpp` 重命名为 `platform_windows.cpp`，保持 Win32 行为不变，并让 `oc_windows.h`、`dbghelp.h` 只出现在该实现文件中。

从 `src/core/header.h` 移除 `core/runtime/oc_windows.h`，定义：

```cpp
#if defined(_MSC_VER) && !defined(OC_STATIC_LINK)
#define OC_DLL_EXPORT __declspec(dllexport)
#define OC_DLL_IMPORT __declspec(dllimport)
#elif defined(__GNUC__) && !defined(OC_STATIC_LINK)
#define OC_DLL_EXPORT [[gnu::visibility("default")]]
#define OC_DLL_IMPORT
#else
#define OC_DLL_EXPORT
#define OC_DLL_IMPORT
#endif
```

所有现有模块 API 宏改为使用 `OC_DLL_EXPORT`/`OC_DLL_IMPORT`。`horizon-core` 通过 `target_compile_definitions(horizon-core PUBLIC OC_STATIC_LINK=1)` 将静态链接模式传递给消费者。更新 `test_core_header_keeps_api_macros_portable`，检查非 MSVC 可见性宏、静态链接分支，并确认公共头不再包含 `oc_windows.h`。

- [ ] **步骤 4：实现 POSIX 后端**

`src/core/runtime/platform_posix.cpp` 使用以下结构和 API：

```cpp
#include "platform.h"
#include "core/util/logging.h"
#include "fmt/format.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>

namespace horizon::core {
namespace detail {
string dynamic_loader_error() {
    const char *message = ::dlerror();
    return message == nullptr ? "unknown dynamic-loader error" : string{message};
}
string demangle_symbol(const char *name) {
    if (name == nullptr) { return "???"; }
    int status = 0;
    char *demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    string result = status == 0 && demangled != nullptr ? demangled : name;
    std::free(demangled);
    return result;
}
}// namespace detail

void *dynamic_module_load(const fs::path &path) noexcept {
    const string path_string = path.string();
    ::dlerror();
    void *module = ::dlopen(path_string.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (module == nullptr) {
        OC_ERROR_FORMAT("Failed to load dynamic module '{}', reason: {}.",
                        path_string, detail::dynamic_loader_error());
    }
    return module;
}
void dynamic_module_destroy(void *handle) noexcept {
    if (handle != nullptr) { ::dlclose(handle); }
}
void *dynamic_module_find_symbol(void *handle, string_view name_view) noexcept {
    if (handle == nullptr) { return nullptr; }
    const string name{name_view};
    ::dlerror();
    void *symbol = ::dlsym(handle, name.c_str());
    const char *error = ::dlerror();
    if (error != nullptr) {
        OC_INFO_FORMAT("Failed to load symbol '{}', reason: {}.", name, error);
        return nullptr;
    }
    return symbol;
}
string dynamic_module_name(string_view name) noexcept {
#if defined(__APPLE__)
    return "lib" + string{name} + ".dylib";
#else
    return "lib" + string{name} + ".so";
#endif
}
vector<TraceItem> traceback(int top) noexcept {
    void *frames[100]{};
    const int frame_count = ::backtrace(frames, 100);
    const int first_frame = std::clamp(top + 1, 0, frame_count);
    vector<TraceItem> trace;
    trace.reserve(static_cast<size_t>(frame_count - first_frame));
    for (int index = first_frame; index < frame_count; ++index) {
        Dl_info info{};
        TraceItem item{};
        item.address = reinterpret_cast<uint64_t>(frames[index]);
        if (::dladdr(frames[index], &info) != 0) {
            item.module = info.dli_fname == nullptr ? "???" : info.dli_fname;
            item.symbol = detail::demangle_symbol(info.dli_sname);
            item.offset = info.dli_saddr == nullptr ? 0u : static_cast<size_t>(
                reinterpret_cast<uintptr_t>(frames[index]) -
                reinterpret_cast<uintptr_t>(info.dli_saddr));
        } else {
            item.module = "???";
            item.symbol = "???";
            item.offset = 0u;
        }
        trace.emplace_back(std::move(item));
    }
    return trace;
}
string traceback_string(int top) noexcept {
    string result;
    const vector<TraceItem> trace = traceback(top + 1);
    for (size_t index = 0; index < trace.size(); ++index) {
        const TraceItem &item = trace[index];
        result += fmt::format("\n    {:>2}: {} :: {} + {}",
                              index, item.module, item.symbol, item.offset);
    }
    return result;
}
}// namespace horizon::core
```

- [ ] **步骤 5：在 CMake 中只选择一个平台后端**

```cmake
list(FILTER SOURCE_FILES EXCLUDE REGEX [[/runtime/platform_(windows|posix)\.cpp$]])
if(WIN32)
    list(APPEND SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/runtime/platform_windows.cpp")
elseif(APPLE OR UNIX)
    list(APPEND SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/runtime/platform_posix.cpp")
else()
    message(FATAL_ERROR "horizon-core has no platform backend for ${CMAKE_SYSTEM_NAME}")
endif()
```

```cmake
target_link_libraries(horizon-core PRIVATE
    ${CMAKE_DL_LIBS}
    $<$<PLATFORM_ID:Windows>:dbghelp>
)
```

- [ ] **步骤 6：验证并提交平台后端**

```powershell
python tools/dev.py build horizon-tests --configuration Debug --target-family core
ctest --test-dir build/conan/core/debug -C Debug -R horizon.core.platform --output-on-failure
ctest --test-dir build/conan/core/debug -C Debug --output-on-failure
```

预期：平台测试与全部基础设施测试均通过。

```powershell
git add src/core/header.h src/core/CMakeLists.txt src/core/runtime/platform_windows.cpp src/core/runtime/platform_posix.cpp tests/core/CMakeLists.txt tests/core/test_dynamic_module.cpp tests/core/test_dynamic_module_library.cpp tools/test_workflow.py
git commit -m "feat(core): add Windows and POSIX platform backends"
```

---

### 任务 4：让开发脚本原生支持 Windows、Linux、macOS

**文件：**

- 修改：`tools/workflow.py`
- 修改：`tools/test_workflow.py`

**接口：**

- 输入：四种现有配置名称。
- 输出：`conan_profile(repo_root: Path, configuration: str, system_name: str | None = None) -> str`，以及跨平台 `load_conan_build_environment(...) -> dict[str, str]`。

- [ ] **步骤 1：增加 profile 选择测试并确认 RED**

```python
def test_conan_profiles_are_native_to_the_host(self) -> None:
    root = Path("C:/repo")
    self.assertEqual(
        conan_profile(root, "Debug", "Windows"),
        str(root / "conan" / "profiles" / "windows-msvc-debug"),
    )
    self.assertEqual(conan_profile(root, "Debug", "Linux"), "horizon-linux-debug")
    self.assertEqual(conan_profile(root, "Release", "Darwin"), "horizon-darwin-release")
```

```powershell
python -m unittest tools/test_workflow.py
```

预期：因 `conan_profile` 尚不存在而失败。

- [ ] **步骤 2：实现本机 profile 选择**

```python
def conan_profile(
    repo_root: Path,
    configuration: str,
    system_name: str | None = None,
) -> str:
    system_name = system_name or platform.system()
    slug = configuration_slug(configuration)
    if system_name == "Windows":
        return str(repo_root / "conan" / "profiles" / f"windows-msvc-{slug}")
    if system_name not in {"Linux", "Darwin"}:
        raise RuntimeError(f"Unsupported host operating system: {system_name}")
    return f"horizon-{system_name.lower()}-{slug}"
```

Windows profile 不存在时明确报错；POSIX 在 `conan install` 前执行：

```python
run_command(("conan", "profile", "detect", "--force", "--name", profile), cwd=repo_root)
```

安装命令显式固定与项目 preset 相同的多配置生成器，并覆盖配置：

```python
command.extend(("-c:a", "tools.cmake.cmaketoolchain:generator=Ninja Multi-Config"))
command.extend(("-s:a", f"build_type={configuration}"))
command.extend(("-s:b", f"build_type={configuration}"))
command.extend(("-s:a", "compiler.cppstd=20"))
```

- [ ] **步骤 3：增加 POSIX JSON 环境解析测试并确认 RED**

```python
def test_json_environment_is_parsed_without_losing_values(self) -> None:
    environment = _parse_json_environment(
        '{"PATH": "/usr/bin", "TOKEN": "left=right", "MULTILINE": "a\\nb"}'
    )
    self.assertEqual(environment["TOKEN"], "left=right")
    self.assertEqual(environment["MULTILINE"], "a\nb")
```

预期：因 `_parse_json_environment` 尚不存在而失败。

- [ ] **步骤 4：按平台加载 Conan 构建环境**

Windows 保持 `.bat` 加 `set` 的现有路径。Linux/macOS 要求存在 `conanbuild.sh`，source 后使用当前 Python 将环境编码为 JSON：

```python
environment_script = "import json, os; print(json.dumps(dict(os.environ)))"
result = subprocess.run(
    (
        "/bin/sh",
        "-c",
        f". {shlex.quote(str(script))} >/dev/null && "
        f"{shlex.quote(sys.executable)} -c {shlex.quote(environment_script)}",
    ),
    cwd=repo_root,
    text=True,
    capture_output=True,
    check=False,
)
```

使用 `json.loads` 校验并解析字符串键值映射，再合并到 `os.environ` 副本。shell 返回非零时抛出 `CommandError`。生成 CMake 环境时，所有平台写入存在的 `PATH`、`CC`、`CXX`、`PKG_CONFIG_PATH`；Windows 继续写入 Visual Studio 相关变量。

- [ ] **步骤 5：验证并提交开发流程**

```powershell
python -m unittest tools/test_workflow.py
```

预期：Windows profile、Linux/macOS profile、JSON 环境解析、安全删除和目标族测试全部通过。

```powershell
git add tools/workflow.py tools/test_workflow.py
git commit -m "build: support native Conan profiles on Unix hosts"
```

---

### 任务 5：增加三平台基础设施 CI 矩阵

**文件：**

- 修改：`.github/workflows/core-tests.yml`
- 修改：`tools/test_workflow.py`

**接口：**

- 输入：任务 4 的跨平台 `tools/dev.py` 和聚合 target `horizon-tests`。
- 输出：`Windows Core Tests`、`Linux Core Tests`、`macOS Core Tests` 三个任务。

- [ ] **步骤 1：加强工作流结构测试并确认 RED**

```python
self.assertIn("fail-fast: false", workflow)
self.assertIn("python tools/dev.py build horizon-tests", workflow)
self.assertIn("python src/core/generate_tuple.py --max-arity 32 --check", workflow)
self.assertIn("ctest --test-dir build/conan/core/debug", workflow)
self.assertIn("if: matrix.os == 'windows-latest'", workflow)
self.assertIn('HORIZON_CONAN_EXPORT_LOCAL_RECIPES: "false"', workflow)
```

```powershell
python -m unittest tools/test_workflow.py
```

预期：当前工作流尚未禁用本地引擎 recipe 导出，因此至少一个断言失败。

- [ ] **步骤 2：定义矩阵和基础设施环境**

```yaml
env:
  HORIZON_CONAN_EXPORT_LOCAL_RECIPES: "false"

jobs:
  test:
    name: ${{ matrix.name }} Core Tests
    runs-on: ${{ matrix.os }}
    strategy:
      fail-fast: false
      matrix:
        include:
          - name: Windows
            os: windows-latest
          - name: Linux
            os: ubuntu-latest
          - name: macOS
            os: macos-latest
```

`ilammy/msvc-dev-cmd@v1` 仅在 `matrix.os == 'windows-latest'` 时运行。

- [ ] **步骤 3：限制每个平台只构建基础设施测试**

```yaml
- name: Build all registered infrastructure test targets
  run: python tools/dev.py build horizon-tests --configuration Debug --target-family core

- name: Verify generated tuple header
  run: python src/core/generate_tuple.py --max-arity 32 --check

- name: Run all registered infrastructure tests
  run: ctest --test-dir build/conan/core/debug -C Debug --output-on-failure
```

不得增加 `all`、`Horizon`、`Helicon`、示例、工具或 Ocarina 构建。

- [ ] **步骤 4：验证并提交 CI 矩阵**

```powershell
python -c "import yaml; yaml.safe_load(open('.github/workflows/core-tests.yml', encoding='utf-8'))"
python -m unittest tools/test_workflow.py
```

预期：YAML 解析成功，全部 Python 测试通过。

```powershell
git add .github/workflows/core-tests.yml tools/test_workflow.py
git commit -m "ci: test infrastructure on Windows Linux and macOS"
```

---

### 任务 6：更新架构文档并完成最终验证

**文件：**

- 修改：`docs/architecture/overview.md`
- 修改：`tests/CMakeLists.txt`
- 测试：`tests/` 下全部测试。

**接口：**

- 输入：任务 1–5 的全部 target 与工作流。
- 输出：准确的跨平台能力声明和最终验证证据。

- [ ] **步骤 1：让聚合 target 编译四个基础模块**

在四个模块 target 均已声明后加入：

```cmake
add_dependencies(horizon-tests
    horizon-core
    horizon-math
    horizon-ast
    horizon-dsl
)
```

保留 `horizon-tests` 对每个测试可执行文件的现有依赖。

- [ ] **步骤 2：更新架构文档**

`docs/architecture/overview.md` 必须明确说明：

- 四个基础设施 target 支持 Windows、Linux、macOS host 构建。
- `HORIZON_BUILD_ENGINE=OFF` 只配置基础设施层及其测试。
- 这不代表 Helicon、Vulkan、Slang、Ocarina、示例和工具已完整跨平台。
- Core 平台运行时由 CMake 选择 Windows 或 POSIX 后端。

- [ ] **步骤 3：执行全新 Windows 构建**

```powershell
python tools/dev.py rebuild horizon-tests --configuration Debug --target-family core
```

预期：Conan 只安装基础设施依赖；CMake 不创建 Helicon/Horizon；`horizon-tests` 构建四个模块和全部测试可执行文件。

- [ ] **步骤 4：执行完整本地检查**

```powershell
python src/core/generate_tuple.py --max-arity 32 --check
python -m unittest tools/test_workflow.py
ctest --test-dir build/conan/core/debug -C Debug --output-on-failure
git diff --check
```

预期：生成检查退出码为零；Python 与 CTest 零失败；`git diff --check` 无输出。

- [ ] **步骤 5：审核变更范围**

```powershell
git status --short
git diff --stat
git diff -- CMakeLists.txt CMakePresets.json src cmake conanfile.py tests tools .github docs/architecture/overview.md
```

确认没有修改 `modules/ocarina/`、构建产物、IDE 目录或无关文档。

- [ ] **步骤 6：提交文档与聚合覆盖**

```powershell
git add tests/CMakeLists.txt docs/architecture/overview.md
git commit -m "docs: define cross-platform infrastructure test scope"
```

- [ ] **步骤 7：推送并持续观察三平台 CI**

```powershell
git push origin refactor_dsl
```

预期：Windows、Linux、macOS 三个 Core Tests 任务全部成功。若任一任务失败，保留首个有因果价值的错误；尽可能在本地或等价容器复现；先增加回归测试，再实施一个有明确根因的修复；重新运行受影响平台的完整流程；提交、推送并继续观察，直到三个任务全部通过。

## 最终验收清单

- [ ] `horizon-core`、`horizon-math`、`horizon-ast`、`horizon-dsl` 在无引擎目标族中编译通过。
- [ ] Core 具有完整的 Windows 和 POSIX 平台实现。
- [ ] Core-only Conan 依赖图不包含引擎专用包。
- [ ] `tools/dev.py` 支持 Windows profile 和自动检测的 Linux/macOS profile。
- [ ] Tuple 生成检查通过。
- [ ] Python 工作流测试通过。
- [ ] 全部注册 CTest 通过。
- [ ] GitHub Actions 的 Windows、Ubuntu、macOS 均为绿色。
- [ ] 架构文档没有夸大完整引擎的跨平台能力。
- [ ] `git diff --check` 通过。
