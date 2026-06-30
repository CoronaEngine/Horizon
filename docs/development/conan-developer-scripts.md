# Conan 开发脚本使用说明

本文说明 `tools/` 目录下两个 PowerShell 辅助脚本的用途和用法：

- `tools/dev.ps1`：日常开发入口，用于安装依赖、配置 CMake、构建目标，以及检查或应用代码格式化。
- `tools/conan-cache.ps1`：本地 Conan 缓存维护工具，用于查看、更新、删除缓存条目。

建议在仓库根目录下用 PowerShell 运行这些脚本。脚本默认假设 `git`、`conan`、`cmake` 已经在 `PATH` 中，并使用 `conan/profiles/` 下的 Windows MSVC profile。

## `tools/dev.ps1`

`dev.ps1` 封装了 Horizon 常用的 Conan + CMake 工作流。它会导出仓库内的 Conan 本地 recipe，把依赖图安装到 `build/conan`，导入 Conan 生成的构建环境，然后通过 CMake 配置或构建目标。

基本形式：

```powershell
.\tools\dev.ps1 <command> [target-or-path] [-Configuration Debug|Release|RelWithDebInfo|MinSizeRel]
```

默认值：

- 命令：`status`
- 目标：`Horizon`
- 配置：`Debug`
- 构建目录：`build/conan`
- CMake configure preset：`conan-default`

### 命令

| 命令 | 行为 | 适用场景 |
| --- | --- | --- |
| `status` | 输出 `git status --short --branch`、`conan --version`、`cmake --list-presets`。 | 快速检查环境和工作区状态。 |
| `install` | 导出本地 recipe，并用所选 MSVC profile 执行 `conan install .`。 | 只刷新依赖，不重新配置 CMake。 |
| `configure` | 执行 `install`，导入 `build/conan/generators/conanbuild.bat`，然后运行 `cmake --preset conan-default`。 | 生成或刷新 CMake 构建树。 |
| `build` | 执行 `install`、`configure`，然后在 `build/conan` 中构建指定目标。 | 依赖或 CMake 配置可能变化后的常规构建路径。 |
| `build-fast` | 只导入现有 Conan 构建环境并构建目标，不执行 install/configure。 | 依赖和 CMake cache 已经有效时的快速重编译。 |
| `rebuild` | 删除 `build/` 和 `install/`，再执行 `install`、`configure`、`build`。 | 处理陈旧 CMake cache、生成器变化、构建树损坏等问题。 |
| `update` | 执行带 `--update` 的 Conan install，然后重新配置 CMake。 | 拉取依赖的新修订版本，但不立即构建。 |
| `format-check` | 执行 `tools/code-format.ps1 -Check`。 | 只检查格式，不改写文件。 |
| `format` | 执行 `tools/code-format.ps1`。 | 应用代码格式化。 |

常用示例：

```powershell
.\tools\dev.ps1 status
.\tools\dev.ps1 install
.\tools\dev.ps1 configure
.\tools\dev.ps1 build Horizon
.\tools\dev.ps1 build ShaderCompileScripts
.\tools\dev.ps1 build HorizonExamples
.\tools\dev.ps1 build HorizonTests -Configuration Release
.\tools\dev.ps1 build-fast Horizon
.\tools\dev.ps1 rebuild Horizon
.\tools\dev.ps1 update
.\tools\dev.ps1 format-check
.\tools\dev.ps1 format
.\tools\dev.ps1 format src/Helicon
```

### Target 与 Configuration

`-Configuration` 同时决定 Conan profile 和 CMake 构建配置：

| Configuration | Conan profile |
| --- | --- |
| `Debug` | `conan/profiles/windows-msvc-debug` |
| `Release` | `conan/profiles/windows-msvc-release` |
| `RelWithDebInfo` | `conan/profiles/windows-msvc-relwithdebinfo` |
| `MinSizeRel` | `conan/profiles/windows-msvc-minsizerel` |

命令后的第一个位置参数是构建命令使用的 CMake target：

```powershell
.\tools\dev.ps1 build HorizonExamples -Configuration RelWithDebInfo
```

脚本会把剩余参数作为 target 数组接收，但真正传给 CMake build 的是 `Target[0]`。完整 target 数组会用于 Conan option 推导，也会用于格式化命令的路径参数。

### 根据 Target 自动推导的 Conan Options

Horizon 会针对特定 target 自动开启根包选项：

| target 值 | 自动添加的 Conan option |
| --- | --- |
| `ShaderCompileScripts` | `-o &:with_tools=True` |
| `HorizonExamples` | `-o &:with_examples=True` |
| `HorizonTests` | `-o &:with_tests=True` |

例如：

```powershell
.\tools\dev.ps1 build ShaderCompileScripts
```

会先导出本地 recipes，用 `with_tools=True` 安装依赖图，配置 CMake，然后构建 `ShaderCompileScripts`。

### 本地 Recipes

每次 Conan install 前，脚本都会导出以下本地 recipes：

- `conan/recipes/ktm`
- `conan/recipes/pfr`
- `conan/recipes/slang`
- `conan/recipes/vulkan-memory-allocator`

### 构建树安全检查

`build-fast` 会检查 `build/conan/CMakeCache.txt` 是否存在，并确认该 cache 属于当前仓库。如果 cache 指向其他源码目录或其他 cache 目录，脚本会要求改用 `rebuild`。

`rebuild` 只会删除 `build/` 和 `install/`。删除前脚本会解析绝对路径，并拒绝删除仓库根目录之外的路径。

### 格式化入口

`format-check` 和 `format` 都委托给 `tools/code-format.ps1`。

不传显式路径时，默认 target 值是 `Horizon`；脚本会把这个默认值转换为空参数，让 `code-format.ps1` 自己决定默认格式化范围：

```powershell
.\tools\dev.ps1 format-check
.\tools\dev.ps1 format
```

传入显式值时，这些值会继续传给 `code-format.ps1`：

```powershell
.\tools\dev.ps1 format src/Helicon
.\tools\dev.ps1 format include tools
```

## `tools/conan-cache.ps1`

`conan-cache.ps1` 用于直接维护本地 Conan cache。它不会配置或构建 CMake。它操作的是当前用户账号的全局 Conan cache，因此删除操作需要谨慎。

基本形式：

```powershell
.\tools\conan-cache.ps1 <list|update|remove> [package-or-reference] [options]
```

默认值：

- 命令：`list`
- 配置：`Debug`
- 不传 package/reference 时的匹配模式：`*`

### 常用示例

```powershell
.\tools\conan-cache.ps1 list
.\tools\conan-cache.ps1 list slang
.\tools\conan-cache.ps1 list slang -Version 2026.10
.\tools\conan-cache.ps1 list -Reference slang/2026.10
.\tools\conan-cache.ps1 list slang -Version 2026.10 -PackageId "*"

.\tools\conan-cache.ps1 update
.\tools\conan-cache.ps1 update slang
.\tools\conan-cache.ps1 update slang -Version 2026.10
.\tools\conan-cache.ps1 update -Reference slang/2026.10

.\tools\conan-cache.ps1 remove slang/2026.10 -DryRun
.\tools\conan-cache.ps1 remove slang/2026.10
.\tools\conan-cache.ps1 remove slang -Version 2026.10 -Force
```

### Reference 与匹配规则

脚本会根据输入构造 Conan cache 匹配模式：

| 输入 | 生成的匹配模式 |
| --- | --- |
| 不传 package，也不传 `-Reference` | `*` |
| `slang` | `slang/*` |
| `slang -Version 2026.10` | `slang/2026.10` |
| `slang -Version 2026.10 -User user -Channel channel` | `slang/2026.10@user/channel` |
| `slang -Version 2026.10 -PackageId "*"` | `slang/2026.10:*` |
| `-Reference slang/2026.10` | `slang/2026.10` |

如果位置参数本身已经包含 `/`、`@`、`*`、`#` 或 `:`，脚本会把它视为 reference-like pattern，并直接使用。

输入限制：

- 位置参数 package 和 `-Reference` 不能同时使用。
- `-Reference` 不能和 `-Version`、`-User`、`-Channel` 同时使用。
- `-User` 和 `-Channel` 必须成对提供。
- `-PackageId` 必须搭配 package 或 `-Reference`。
- `-PackageId` 和 `-PackageQuery` 只支持 `list` 和 `remove`，不支持 `update`。

### `list`

`list` 执行：

```powershell
conan list <pattern> --cache
```

如果提供了 `-PackageQuery`，脚本会继续传递为 `--package-query`。

示例：

```powershell
.\tools\conan-cache.ps1 list
.\tools\conan-cache.ps1 list vulkan-memory-allocator
.\tools\conan-cache.ps1 list slang -Version 2026.10 -PackageId "*"
```

### `update`

`update` 会先导出与 `dev.ps1` 相同的本地 recipes，然后执行带 `--update` 的 Conan install。

如果输入可以解析为一个具体 reference，脚本会直接安装该 reference：

```powershell
conan install --requires <reference> ...
```

如果输入为空、包含通配符、包含 revision，或包含 package id，脚本会改为更新 Horizon 的完整依赖图：

```powershell
conan install . ...
```

如果可以从输入中推导出包名，脚本会传递 `--update <name>`；否则传递普通的 `--update`。

### `remove`

`remove` 会先列出匹配的 cache 条目，然后执行：

```powershell
conan remove <pattern> --confirm
```

安全行为：

- 不带 `-Force` 或 `-DryRun` 时，脚本会要求输入 `YES`。
- `-DryRun` 会追加 Conan 的 `--dry-run`，不会真正删除条目。
- `-Force` 会跳过确认并删除匹配项。

当 pattern 中包含 `*`，或你不确定会匹配多少条目时，建议先使用 `-DryRun`。

### 应该使用哪个脚本

| 需求 | 推荐命令 |
| --- | --- |
| 常规依赖安装 | `.\tools\dev.ps1 install` |
| 常规配置 | `.\tools\dev.ps1 configure` |
| 常规构建 | `.\tools\dev.ps1 build <target>` |
| 快速本地重编译 | `.\tools\dev.ps1 build-fast <target>` |
| 清理构建树后重建 | `.\tools\dev.ps1 rebuild <target>` |
| 更新完整依赖图并重新配置 | `.\tools\dev.ps1 update` |
| 检查或应用格式化 | `.\tools\dev.ps1 format-check` / `.\tools\dev.ps1 format` |
| 查看本地 Conan cache | `.\tools\conan-cache.ps1 list ...` |
| 更新某个包或 reference | `.\tools\conan-cache.ps1 update ...` |
| 删除陈旧 cache 条目 | `.\tools\conan-cache.ps1 remove ...` |
