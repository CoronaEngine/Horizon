# Horizon 基础模块 Quill 日志迁移设计

## 1. 背景

Horizon 当前维护中的基础模块为 `src/core`、`src/math`、`src/ast`、`src/dsl` 和
`src/runtime`。这些模块统一通过 `src/core/util/logging.h` 提供的 `OC_*` 宏和
`horizon::core` 日志函数记录日志，但底层实现仍使用 spdlog。

工程的 Kernel 日志已经使用 Quill。为了统一维护中的 Horizon 日志后端，本次将上述
五个模块及其相关测试迁移到 Quill。旧 Ocarina 不在修改范围内，其可选构建可以继续
条件性使用 spdlog。

## 2. 目标

- `src/core`、`src/math`、`src/ast`、`src/dsl`、`src/runtime` 不再包含或链接 spdlog。
- 保留现有 `OC_*` 宏和 `horizon::core::debug`、`info`、`warning`、`error` 等接口及
  参数拼接语义。
- Quill 类型和宏只出现在 Core 日志实现中，不扩散到其他基础模块的公共接口。
- 普通 core、engine 和 tests 依赖图不再安装或查找 spdlog。
- 仅当 `with_ocarina=True` 时保留旧 Ocarina 所需的 spdlog Conan/CMake 依赖。
- Windows、Linux、macOS 上的基础模块测试继续通过。

## 3. 非目标

- 不修改 `modules/ocarina` 中的代码。
- 不重命名现有 `OC_*` 日志宏。
- 不将基础模块日志改为 Kernel 的 `CoronaLogger`，避免 Core 反向依赖 Kernel。
- 不为本次迁移增加文件日志、日志轮转或运行时 sink 配置等新功能。
- 不改变现有 `OC_ERROR` 的终止语义以及 `OC_EXCEPTION` 的异常语义。

## 4. 架构设计

### 4.1 依赖方向

日志实现归属于 `horizon-core`：

```text
math ─┐
ast  ─┼──> core logging wrapper ──> Quill
dsl  ─┤
runtime┘
```

其他基础模块只依赖 Core 提供的日志接口，不直接包含 Quill 头文件。Core 不依赖
math、ast、dsl 或 runtime，从而保持既有单向依赖。

### 4.2 公共接口

`src/core/util/logging.h` 保留以下兼容接口：

- `debug`、`info`、`warning`、`warning_if`、`warning_if_not`
- `exception`、`exception_if`、`exception_if_not`
- `error`、`error_if`、`error_if_not`
- `log_level_debug`、`log_level_info`、`log_level_warning`、`log_level_error`
- 全部现有 `OC_*` 宏

同时正式声明已有实现但尚未在头文件声明的 `log_flush()`。

公共头文件不暴露 `quill::Logger`。模板函数继续使用 `serialize()` 生成完整字符串，
再调用 Core 内部的非模板日志写入函数，因此调用方原有的多参数拼接行为不变。

### 4.3 Quill 实现

`src/core/util/logging.cpp` 负责：

- 通过 `quill::Backend::start()` 启动进程级异步后端。
- 创建 Core 专用控制台 sink 和名为 `horizon_core` 的 logger。
- Debug 构建使用 Debug 级别，其他构建使用 Info 级别。
- 将 Core 的 debug、info、warning 和 error 写入分别映射到 Quill 对应级别。
- 使用 `quill::Logger::flush_log()` 实现显式刷新。
- 在 `OC_ERROR` 触发断言或进程退出前刷新错误日志。

Quill 11 的 Backend 启动由库内的进程级 `std::call_once` 保护，因此 Core logger 与
现有 Kernel logger 重复调用 `Backend::start()` 是安全的。Core 不调用
`quill::Backend::stop()`，避免提前停止其他 logger 共用的全局后端。

### 4.4 构建与依赖

- `horizon-core` 移除对 `horizon::spdlog` 的链接，仅链接 Quill。
- `cmake/horizon_core_dependencies.cmake` 始终查找 Quill；只有启用
  `HORIZON_BUILD_OCARINA` 时才查找并创建 `horizon::spdlog`。
- `conanfile.py` 始终声明 Quill；只有 `with_ocarina=True` 时才声明 spdlog。
- `cmake/horizon_ocarina.cmake` 和 `modules/ocarina` 保持不变。

这保证默认 core、engine 和 tests 构建完全不需要 spdlog，同时不破坏旧 Ocarina 的
可选构建入口。

## 5. 测试设计

### 5.1 Core 日志行为测试

在 `tests/core` 新增独立日志测试 target，覆盖：

- 首次日志调用能够初始化 Quill backend。
- Debug、Info 和 Warning 消息能够写入并刷新。
- 四个日志级别切换函数均可调用，切换后 logger 保持可用。
- `log_flush()` 能在异步后端运行时完成。
- `OC_EXCEPTION` 保持抛出 `std::runtime_error`，且异常消息保留参数拼接和源码位置。

`OC_ERROR` 会主动终止进程，不在普通单进程单元测试中直接调用；其日志刷新顺序由
实现审查和编译覆盖保证，避免让测试依赖平台特定的子进程退出行为。

### 5.2 构建边界测试

新增嵌套 CMake Configure 测试：关闭 engine 与 tests，明确设置
`CMAKE_DISABLE_FIND_PACKAGE_spdlog=TRUE`，并要求普通基础模块仍能完成配置。该测试
直接约束默认构建不能查找或链接 spdlog。

随后在不包含 spdlog 的 core Conan 依赖图下构建全部 `horizon-tests`。如果
`src/core`、`src/math`、`src/ast`、`src/dsl` 或 `src/runtime` 重新引入 spdlog 头文件
或类型，编译会因为没有 spdlog include path 而失败。配置测试与实际编译共同覆盖
包发现、target 链接和源码依赖，且不扫描 `modules/ocarina`。

### 5.3 构建验证

- 重新执行 core 目标族的 Conan 安装和 CMake Configure，验证默认依赖图不含
  spdlog。
- 构建 `horizon-tests`。
- 运行 tests 下全部已注册 CTest。
- 运行现有 Python 工作流测试和生成器检查。
- 执行 `git diff --check`。
- 推送后观察 Windows、Linux、macOS 三平台 CI；只有用户明确要求时才提交和推送。

## 6. 兼容性与风险

- Quill 为异步日志后端，错误退出前必须显式刷新，否则最后一条错误日志可能丢失。
- 原 `horizon::core::logger()` 直接暴露 spdlog 类型，与“Core 封装 Quill、不向调用方
  暴露后端类型”的目标冲突，因此本次有意移除。仓库内没有该接口的调用方；外部代码
  若曾直接调用它，应迁移到 `debug`、`info`、`warning`、`error`、日志级别函数和
  `log_flush()`，而不是依赖具体日志库。
- Core 与 Kernel 共用 Quill 的进程级 Backend。Backend 启动是幂等的，但首次启动者
  决定全局 Backend 配置；本次不调整 Kernel 配置，以控制迁移范围。
- 现有日志先经 `serialize()` 生成字符串，因此本次不获得 Quill 延迟格式化的性能
  优势，但可以完整保留调用兼容性。未来若要采用原生格式化接口，应作为独立 API
  变更处理。
- spdlog 仍可能出现在旧 Ocarina 的条件依赖和源码中，这不表示维护中的 Horizon
  基础模块仍依赖 spdlog。

## 7. 完成标准

- 五个目标目录中不存在 spdlog 使用。
- 默认 core/engine/tests 配置不需要 spdlog 包或 CMake target。
- 旧 Ocarina 的条件依赖仍可解析，且本次不修改其源码。
- 新增日志行为测试和架构约束测试通过。
- tests 下全部已注册测试通过。
- CI 的 Windows、Linux、macOS 基础测试全部通过。
