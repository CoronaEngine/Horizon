# 热更新跨平台接口

公共接口使用 `horizon::hotfix` 命名空间，依赖 C++20 标准库。
Windows、POSIX 进程执行器和 CMake 构建后端已实现对应接口。
共享运行时保留 `vision::hotfix` 的对象替换和版本管理接口，统一使用 CMake 构建后端。
Windows、Linux 和 macOS 宿主使用相同的热更新流程；旧编译器插件、构建规则解析器和常驻命令会话已移除。

## 接口与职责

| 接口 | 输入 | 输出 | 后续实现 |
| --- | --- | --- | --- |
| `IProcessRunner::run()` | `ProcessRequest` | `ProcessResult` | Windows、Linux、macOS 已实现 |
| `IBuildBackend::build()` | `BuildRequest` | `BuildResult` | CMake 已实现 |

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
- 默认 `exit_code` 为空，表示尚未取得系统报告的退出码。启动和等待失败通过
  `error_message` 返回。
- `succeeded()` 要求退出码为零且无执行错误。stderr 可以包含编译器警告。

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
该目标在关闭 Engine/Ocarina 时也可用。

Windows 消费者链接 `horizon-hotfix-process-windows`，并从
`horizon/hotfix/windows_process_runner.h` 使用 `WindowsProcessRunner`。
执行器使用 MSVC CRT 规则转义参数，按 Windows 的大小写规则覆盖环境变量。
子进程的标准输入为 EOF，stdout 和 stderr 保留各自的原始字节。
执行器在直接子进程退出后收集已进入管道的数据并返回，后代持有写端时也能完成。
退出码保留 Windows 返回的 32 位值，以有符号整数表示。

Linux 和 macOS 消费者链接 `horizon-hotfix-process-posix`，并从
`horizon/hotfix/posix_process_runner.h` 使用 `PosixProcessRunner`。
环境变量名区分大小写。子进程的标准输入为 EOF，stdout 和 stderr 分别保留原始字节。
子进程因信号终止时，`exit_code` 为空，`error_message` 包含信号编号。

### CMake 构建后端

在项目中注册已有的 `MODULE` 或 `SHARED` 目标。

```cmake
include(horizon_hotfix_manifest)
horizon_generate_hotfix_manifest(my_hotfix_module)
```

配置时生成 `hotfix-manifests/<目标>/configurations.txt`，并在每个配置的子目录
生成 `module.txt`。后者使用 `$<TARGET_FILE:...>` 记录模块绝对路径。
`BuildRequest` 使用已配置的构建目录和原样的目标、配置名称。配置为空时，对应
未设置构建类型的单配置构建树。名称使用字母、数字、下划线、加号、点或连字符，
以字母、数字、下划线或加号开头，结尾不用点。

消费者链接 `horizon-hotfix-build-cmake` 和对应平台的进程执行器目标，
将执行器和 CMake 可执行文件路径传给 `CMakeBuildBackend`。
构造时可以传入构建环境覆盖值。调用方负责串行使用同一个构建树。

后端执行指定目标和配置的 `cmake --build`。构建成功后，后端读取清单并确认模块文件存在且非空。
构建失败通过 `BuildResult` 返回诊断和空产物。

### 接入运行时

宿主链接 `horizon-hotfix`、`horizon-hotfix-build-cmake` 和对应平台的进程执行器。
macOS 和 Linux 使用 `horizon-hotfix-process-posix`，Windows 使用 `horizon-hotfix-process-windows`。
创建执行器和共享的 `CMakeBuildBackend`，调用
`HotfixSystem::set_build_backend(backend, build_directory, configuration)` 选择后端。
在主线程配置，调用返回 `true` 后使用 `register_target` 登记要监控的源文件及其 CMake
目标名。执行器必须存活到后台构建结束并销毁 `HotfixSystem`。

用 `horizon_register_hotfix_module(my_hotfix_module)` 注册可更新的 CMake `MODULE`。
该函数为目标编译模块入口、隔离模块内的工厂注册表并生成产物清单。
模块源码用 `VS_REGISTER_HOTFIX` 注册对象工厂。宿主管理观察者和被监控的目标，
并注册初始对象的工厂，供首次加载和回退使用。模块和宿主使用相同的工具链、配置及兼容的对象接口。
Core 构建提供 CMake 热更新运行时，不依赖 Ocarina。`horizon-hotfix` 在三个平台均选择 `core` 目标族。

普通 baseline 示例通过 `HORIZON_ENABLE_EXAMPLE_HOTFIX=ON` 开启热更新。
配置好的 `examples` 构建树会注册 `horizon-baseline-hotfix-patch` 模块，并将构建目录、配置、CMake
路径与原始源码传给宿主。修改 `examples/example_baseline/baseline_hotfix_test.cpp` 后按 H 构建并应用新版本，
F9 回退历史版本。构建和加载均复用同一跨平台运行时；窗口验证仍使用独立的源码副本和测试控制文件。
完整引擎示例的 CI 覆盖 Windows 和 macOS；当前引擎使用的 Slang 包配方不支持 Linux，
Linux 使用不依赖引擎的 Core 和 baseline-window 用例验证热更新。

`check_and_build()` 负责触发后台构建。整批构建成功后，模块按原文件名复制到
构建目录下的 `hotfix-staged/<会话>/generation_<序号>/`。主线程调用
`execute_callback()` 验证模块和工厂后应用更新。加载使用副本的绝对路径，已加载的
版本保留到进程退出，以支持回退以及仍引用旧版本代码的对象。构建、复制或模块验证失败时保留当前版本和版本历史，
下一次构建仍会处理未应用的源码修改。

## 验证

在 Linux 下可直接构建运行时和端到端测试。`dev.py` 会为这些目标选择 `core` 构建树。

```bash
python3 build_tools/dev.py build horizon-hotfix horizon-test-hotfix-runtime --configuration Debug
ctest --test-dir build/conan/core/debug -C Debug -R '^horizon\.hotfix\.runtime$' --no-tests=error --output-on-failure
```

如需运行所有热更新测试，先构建聚合目标 `horizon-tests`。

```powershell
python build_tools/dev.py build-fast horizon-tests --configuration Debug --target-family core
ctest --test-dir build/conan/core/debug -C Debug -R horizon.hotfix --output-on-failure
```

以上 `build-fast` 需要已经配置的 core Debug 构建树；首次使用可将 `build-fast` 换为 `build`。

测试通过独立头文件编译检查接口可实现性，并验证默认失败、非零退出、执行错误、
stderr 警告、构建失败携带旧路径、成功但缺失模块、可选附属产物和平台无关命名。
Windows、Linux 和 macOS 测试另外启动真实子进程，验证参数、环境覆盖、工作目录、两路输出和退出码。
Windows Ninja 构建还运行一个独立 MODULE 项目，验证初次构建、增量重建、编译失败和产物路径。
三端 Core CI 都运行 `horizon.hotfix.runtime`。用例用真实 CMake 模块验证两次连续更新和对象状态恢复、
回退到已加载版本、编译失败时保留当前对象及版本历史，以及失败后重新切到之前的最新版本。

`horizon.hotfix.baseline_window` 使用实际的 `example_baseline` 渲染循环和资源。
它在同一个进程里检查 GLFW 窗口标题、成功呈现的帧数、对象身份和版本副本。
Linux CI 使用 Xvfb 和 lavapipe，macOS CI 使用 Cocoa 和 MoltenVK。
本地有 GLFW 3.4、GLM、Volk、Vulkan 加载器及可用的图形会话时，可以构建并运行这个用例。

```bash
python3 build_tools/dev.py install horizon-test-baseline-window --configuration Release --target-family core
HORIZON_DEV_BOOTSTRAP_ACTIVE=1 cmake --preset core-release -DHORIZON_BUILD_BASELINE_WINDOW_TEST=ON
python3 build_tools/dev.py build-fast horizon-test-baseline-window --configuration Release --target-family core
ctest --test-dir build/conan/core/release -C Release -L '^window$' --no-tests=error --verbose
```

## 后续迁移与验收

- [x] Windows 进程执行器及真实子进程测试。
- [x] POSIX 进程执行器及 Linux/macOS 真实子进程测试。
- [x] CMake 后端及小模块构建、增量、失败和产物测试。
- [x] Windows、Linux 和 macOS 运行时接入 CMake 后端及版本副本，验证连续更新、版本回退和编译失败保护。

进程执行器继承宿主环境并应用显式覆盖值。工具链环境由已配置的 CMake 构建树准备，
不再依赖常驻 cmd 会话保存环境。
