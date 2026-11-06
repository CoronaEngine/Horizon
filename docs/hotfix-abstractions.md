# 热更新跨平台接口：第一阶段

本阶段只建立接口和结果契约，保留 `vision::hotfix` 原有 MSVC 实现。
新接口使用 `horizon::hotfix` 命名空间，只依赖 C++20 标准库。
它们暂未接入 `HotfixSystem`，也不代表运行时热更新已跨平台。

## 接口与职责

| 接口 | 输入 | 输出 | 后续实现 |
| --- | --- | --- | --- |
| `IProcessRunner::run()` | `ProcessRequest` | `ProcessResult` | Windows / POSIX 进程执行器 |
| `IBuildBackend::build()` | `BuildRequest` | `BuildResult` | CMake 构建后端 |

两个调用均同步完成。后台线程、任务串行化、回调和对象替换由上层调度。
实现类应把预期的进程/构建失败放进结果；接口未承诺 `noexcept`，例如内存分配
仍可能抛出异常。接口也未承诺跨编译器、跨标准库的二进制兼容性。

### 进程契约

- `executable` 使用原生 `std::filesystem::path`；参数和环境变量字符串使用 UTF-8。
- `arguments` 是独立参数列表。执行器负责平台编码和参数转义，不自动通过 shell 解析。
- 空 `working_directory` 表示继承工作目录；相对可执行文件和工作目录相对调用者
  的工作目录解释。需要搜索 PATH 时应由调用者先解析可执行文件。
- `environment_overrides` 与继承环境合并。它不是完整环境替换，也不表达删除变量。
  不应修改宿主进程自己的环境。
- 默认 `exit_code` 为空，表示没有正常退出结果。启动失败、等待失败和异常终止
  应提供 `error_message`，不能伪造正常退出零。
- `succeeded()` 要求正常退出码为零且无执行错误。stderr 中存在编译器警告不算失败。

### 构建契约

- 输入是已配置的构建目录、目标、配置和修改文件提示。
- 工具链、Conan 依赖和 SDK 的准备独立于本次构建；修改文件列表不能替代头文件依赖分析。
- 默认状态为 `not_started`。预期构建失败返回 `failed` 和诊断信息，不发布新模块。
- 成功时，后端返回已验证存在、完整产物的绝对 `module_path`；不推测平台后缀。
- 调试符号、导入库是可选附属产物；Linux/macOS 不需要伪造 `.pdb` 或 `.lib`。
- `succeeded()` 只检查成功状态和非空模块路径，不访问文件系统或检查 ABI。
  后端负责实际产物验证；未来加载器负责入口、协议和 ABI 检查。
- 版本副本的复制、加载和旧模块生命周期属于运行时，不由后端自动执行。

## CMake 使用

消费者链接 header-only 目标即可继承公开头文件路径和 C++20 要求：

```cmake
target_link_libraries(my_backend PRIVATE horizon-hotfix-api)
```

头文件分别为 `horizon/hotfix/process_runner.h` 和 `horizon/hotfix/build_backend.h`。
该目标在关闭 Engine/Ocarina 时也可用。旧 `horizon-hotfix` 公开链接新目标，
但旧的 `Compiler`、`CmdProcess`、插件工厂和回调签名均未改变。

## 本阶段验证

```powershell
python build_tools/dev.py build-fast horizon-test-hotfix-process-contract horizon-test-hotfix-build-contract --configuration Debug --target-family core
ctest --test-dir build/conan/core/debug -C Debug -R horizon.hotfix --output-on-failure
```

以上 `build-fast` 需要已经配置的 core Debug 构建树；首次使用可将 `build-fast` 换为 `build`。

测试通过独立头文件编译检查接口可实现性，并验证默认失败、非零退出、执行错误、
stderr 警告、构建失败携带旧路径、成功但缺失模块、可选附属产物和平台无关命名。
它们不启动真实子进程、不调用 CMake 后端，也不覆盖对象状态迁移。
没有用空实现的返回值冒充端到端构建成功。

## 后续迁移与验收

- [ ] Windows 进程执行器：真实子进程的参数、UTF-8、环境覆盖、工作目录、输出和退出码测试。
- [ ] POSIX 进程执行器：Linux/macOS 实机验证，同一组契约测试。
- [ ] CMake 后端：用真实微型 MODULE 目标验证增量构建、头文件依赖、失败重试和产物路径。
- [ ] 接入旧 MSVC 热更新：保持编译环境与宿主一致，验证连续更新和失败后继续运行。
- [ ] 模块加载与状态迁移：版本副本、符号隔离、安全点、旧对象析构和版本回退。

原有 MSVC 路径使用常驻 cmd 会话保存环境。接入新的逐进程执行器时，必须显式传递
完整继承环境及覆盖值，不能把一次 `vcvarsall.bat` 调用当成后续进程的永久环境初始化。
