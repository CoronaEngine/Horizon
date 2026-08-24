# Horizon Quill 日志迁移实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 Horizon 维护中的五个基础模块及相关测试从 spdlog 迁移到由 Core 封装的 Quill 日志后端。

**Architecture:** `horizon-core` 在实现文件中独占 Quill 类型和日志宏，对外继续提供现有 `OC_*` 与 `horizon::core` 兼容接口。普通 core、engine 和 tests 构建只解析 Quill；spdlog 仅在启用旧 Ocarina 时作为条件依赖存在。

**Tech Stack:** C++20、Quill 11.0.2、CMake 4、Conan 2、CTest、Ninja Multi-Config

**Spec:** `docs/superpowers/specs/2026-08-24-quill-logging-migration-design.md`

## Global Constraints

- 修改范围为 `src/core`、`src/math`、`src/ast`、`src/dsl`、`src/runtime`、相关 `tests`、`cmake/horizon_core_dependencies.cmake` 和 `conanfile.py`。
- 不读取或修改 `modules/ocarina`；旧 Ocarina 继续条件性使用 spdlog。
- 保留全部现有 `OC_*` 宏、日志级别函数、参数拼接语义、`OC_ERROR` 终止语义和 `OC_EXCEPTION` 异常语义。
- Quill 类型不得出现在五个基础模块的公共头文件中。
- Core 不调用 `quill::Backend::stop()`。
- 使用 C++20，并验证 Windows、Linux、macOS CI 所运行的基础测试集合。
- 未经用户额外授权不提交、不推送。

---

### Task 1: 建立 spdlog 构建边界回归测试

**Files:**
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: 当前 core Conan toolchain、`CMAKE_DISABLE_FIND_PACKAGE_spdlog=TRUE` 和关闭 engine/tests 的嵌套 CMake Configure。
- Produces: CTest `horizon.architecture.quill_logging_boundary`，证明普通基础模块可以在 spdlog 包不可用时完成配置。

- [ ] **Step 1: 写入失败的架构测试**

在 `tests/CMakeLists.txt` 注册一个嵌套 Configure。关闭嵌套工程的 engine 与 tests，复用 core toolchain，并明确禁用 spdlog 包发现：

```cmake
set(_quill_logging_boundary_build_dir
    "${CMAKE_CURRENT_BINARY_DIR}/quill-logging-boundary")
add_test(
    NAME horizon.architecture.quill_logging_boundary
    COMMAND ${CMAKE_COMMAND} -E env CORONA_DEV_BOOTSTRAP_ACTIVE=1
            ${CMAKE_COMMAND}
            -S ${PROJECT_SOURCE_DIR}
            -B ${_quill_logging_boundary_build_dir}
            -G ${CMAKE_GENERATOR}
            -DHORIZON_DEV_CONFIGURATION=${HORIZON_DEV_CONFIGURATION}
            -DHORIZON_DEV_TARGET_FAMILY=core
            -DHORIZON_BUILD_ENGINE=OFF
            -DHORIZON_BUILD_TESTS=OFF
            -DCMAKE_DISABLE_FIND_PACKAGE_spdlog=TRUE
)
```

- [ ] **Step 2: 运行测试并确认按预期失败**

Run:

```powershell
cmake --build build/conan/core/debug --config Debug --target horizon-tests
ctest --test-dir build/conan/core/debug -C Debug -R horizon.architecture.quill_logging_boundary --output-on-failure
```

Expected: FAIL，错误来自 `find_package(spdlog CONFIG REQUIRED)` 与 `CMAKE_DISABLE_FIND_PACKAGE_spdlog=TRUE` 的冲突。

- [ ] **Step 3: 检查本任务 diff**

Run: `git diff --check -- tests/CMakeLists.txt`

Expected: exit 0。

---

### Task 2: 建立 Core 日志兼容行为测试

**Files:**
- Create: `tests/core/test_logging.cpp`
- Modify: `tests/core/CMakeLists.txt`

**Interfaces:**
- Consumes: `horizon::core::debug/info/warning`、四个 `log_level_*` 函数、`OC_EXCEPTION`。
- Produces: 可执行目标 `horizon-test-core-logging` 和 CTest `horizon.core.logging`；要求新增 `horizon::core::log_flush()` 公共声明。

- [ ] **Step 1: 写入失败的日志测试**

测试程序采用现有 `expect` 风格。首次日志调用前把 stdout 重定向到测试临时文件，
执行并刷新以下日志：

```cpp
horizon::core::log_level_debug();
horizon::core::debug("debug value ", 1);
horizon::core::log_level_info();
horizon::core::info("info value ", 2);
horizon::core::log_level_warning();
horizon::core::warning("warning value ", 3);
horizon::core::info("suppressed info");
horizon::core::log_level_error();
horizon::core::warning("suppressed warning");
horizon::core::log_level_debug();
horizon::core::log_flush();
```

关闭输出文件后读取实际内容，断言 debug/info/warning 三条允许消息存在，两个被当前
级别过滤的消息不存在。

随后调用 `OC_EXCEPTION("logging exception ", 42)`，断言捕获到
`std::runtime_error`，且 `what()` 同时包含 `logging exception 42` 和当前测试文件名。

在 `tests/core/CMakeLists.txt` 中链接 `horizon-core`、加入 `horizon-tests` 依赖并注册 CTest。

- [ ] **Step 2: 构建并确认测试按预期失败**

Run:

```powershell
cmake --build build/conan/core/debug --config Debug --target horizon-test-core-logging
```

Expected: 编译失败，错误指出 `horizon::core::log_flush` 尚未在 `logging.h` 声明。

- [ ] **Step 3: 检查本任务 diff**

Run: `git diff --check -- tests/core/CMakeLists.txt tests/core/test_logging.cpp`

Expected: exit 0。

---

### Task 3: 用 Quill 实现 Core 日志封装

**Files:**
- Modify: `src/core/util/logging.h`
- Modify: `src/core/util/logging.cpp`

**Interfaces:**
- Consumes: Quill `Backend`、`Frontend`、`Logger`、`LogMacros` 和 `ConsoleSink`。
- Produces: 不暴露 Quill 类型的兼容日志 API；新增 `void log_flush() noexcept`；内部消息入口接收拥有所有权的 `std::string`。

- [ ] **Step 1: 修改公共头文件的最小实现**

移除 spdlog includes 与 `spdlog::logger &logger()`。在 `horizon::core::detail` 声明四个导出函数：

```cpp
OC_CORE_API void log_debug_message(std::string message) noexcept;
OC_CORE_API void log_info_message(std::string message) noexcept;
OC_CORE_API void log_warning_message(std::string message) noexcept;
OC_CORE_API void log_error_message(std::string message) noexcept;
```

模板函数继续调用 `serialize()`，并把返回字符串传给对应内部函数。声明：

```cpp
OC_CORE_API void log_flush() noexcept;
```

`error()` 必须按“记录 Error → `log_flush()` → `OC_ASSERT` → `std::exit(-1)`”顺序执行。

- [ ] **Step 2: 实现 Core 私有 Quill logger**

在 `logging.cpp` 的匿名命名空间中实现 `quill::Logger *quill_logger() noexcept`：调用
`quill::Backend::start()`，创建 `horizon_core_console` ConsoleSink 和 `horizon_core`
logger，并按 `NDEBUG` 设置 Debug/Info 默认级别。

四个内部消息函数分别使用：

```cpp
LOG_DEBUG(quill_logger(), "{}", message);
LOG_INFO(quill_logger(), "{}", message);
LOG_WARNING(quill_logger(), "{}", message);
LOG_ERROR(quill_logger(), "{}", message);
```

四个级别函数映射到 `quill::LogLevel::Debug/Info/Warning/Error`；`log_flush()` 调用
`quill_logger()->flush_log()`。不得停止全局 Backend。

- [ ] **Step 3: 构建并运行日志测试**

Run:

```powershell
cmake --build build/conan/core/debug --config Debug --target horizon-test-core-logging
ctest --test-dir build/conan/core/debug -C Debug -R horizon.core.logging --output-on-failure
```

Expected: build exit 0，CTest 1/1 PASS。

- [ ] **Step 4: 运行构建边界测试并观察仍有依赖违规**

Run:

```powershell
ctest --test-dir build/conan/core/debug -C Debug -R horizon.architecture.quill_logging_boundary --output-on-failure
```

Expected: CMake 依赖文件尚未清理时测试继续 FAIL，错误仍来自无条件 `find_package(spdlog CONFIG REQUIRED)`。

---

### Task 4: 条件化 spdlog 构建依赖

**Files:**
- Modify: `src/core/CMakeLists.txt`
- Modify: `cmake/horizon_core_dependencies.cmake`
- Modify: `conanfile.py`

**Interfaces:**
- Consumes: `HORIZON_BUILD_OCARINA` CMake 选项和 `with_ocarina` Conan 选项。
- Produces: 普通目标族只解析 `quill::quill`；Ocarina 目标族额外解析 `horizon::spdlog`。

- [ ] **Step 1: 清理 horizon-core 链接接口**

把 `quill::quill` 移入 `horizon-core` 的 PRIVATE 链接依赖，并删除
`$<$<TARGET_EXISTS:horizon::spdlog>:horizon::spdlog>`。保留 fmt 和 xxHash 的原有公共依赖。

- [ ] **Step 2: 条件化 CMake 包发现**

`cmake/horizon_core_dependencies.cmake` 始终 `find_package(quill CONFIG REQUIRED)`；将
spdlog 的 `find_package`、alias 和 require 三步放入：

```cmake
if(HORIZON_BUILD_OCARINA)
    find_package(spdlog CONFIG REQUIRED)
    _horizon_alias_target(horizon::spdlog spdlog::spdlog spdlog)
    _horizon_require_target(horizon::spdlog "spdlog is required by legacy Ocarina")
endif()
```

- [ ] **Step 3: 条件化 Conan requirement**

删除无条件 spdlog requirement，并在 `requirements()` 的 Ocarina 条件中加入：

```python
if bool(self.options.with_ocarina):
    self.requires("spdlog/1.17.0")
```

该条件与 Ocarina 现有 CUDA 校验保持一致，不修改旧模块源码。

- [ ] **Step 4: 从干净配置路径重新生成 core 依赖图**

Run:

```powershell
conda run -n horizon-dev --no-capture-output python tools/dev.py configure --configuration Debug --target-family core
```

Expected: exit 0；生成的 core Conan graph 和 CMake 配置不要求 spdlog。

- [ ] **Step 5: 构建并运行两项新增测试**

Run:

```powershell
conda run -n horizon-dev --no-capture-output python tools/dev.py build-fast horizon-tests --configuration Debug --target-family core
ctest --test-dir build/conan/core/debug -C Debug -R "horizon\.(core\.logging|architecture\.quill_logging_boundary)" --output-on-failure
```

Expected: build exit 0，CTest 2/2 PASS。

---

### Task 5: 完整回归验证

**Files:**
- Verify only: all files changed by Tasks 1–4

**Interfaces:**
- Consumes: `horizon-tests` 聚合目标和全部已注册 CTest。
- Produces: 可复核的构建、测试、生成器和格式检查证据。

- [ ] **Step 1: 构建全部基础测试目标**

Run:

```powershell
conda run -n horizon-dev --no-capture-output python tools/dev.py build-fast horizon-tests --configuration Debug --target-family core
```

Expected: exit 0。

- [ ] **Step 2: 运行全部 CTest**

Run:

```powershell
ctest --test-dir build/conan/core/debug -C Debug --output-on-failure
```

Expected: 0 failed。

- [ ] **Step 3: 运行工作流脚本测试与代码生成检查**

Run:

```powershell
conda run -n horizon-dev --no-capture-output python -m unittest tools/test_workflow.py
conda run -n horizon-dev --no-capture-output python src/core/generate_tuple.py --check
conda run -n horizon-dev --no-capture-output python src/math/swizzle_inl/generate_swizzles.py
conda run -n horizon-dev --no-capture-output python src/dsl/detail/swizzle/generate_swizzle.py
git diff --exit-code -- src/core/tuple.h src/math/swizzle_inl src/dsl/detail/swizzle
```

Expected: 工作流测试全部通过，tuple 检查 exit 0，两个 swizzle 生成器运行后没有产生源码差异。

- [ ] **Step 4: 检查残留依赖与 diff**

Run:

```powershell
rg -n "spdlog|SPDLOG_" src/core src/math src/dsl src/runtime src/ast tests
git diff --check
git status --short
```

Expected: 五个源码目录无 spdlog 命中；测试脚本仅可在错误说明中出现禁用名称；`git diff --check` exit 0；状态中保留用户原有未跟踪文档且不修改。

- [ ] **Step 5: 汇报结果**

列出修改文件、实际构建和测试结果、未验证项，并明确说明尚未提交或推送。只有用户随后明确要求时才执行 Git 提交、推送和 CI 监控。
