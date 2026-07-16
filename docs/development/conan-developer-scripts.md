# Python 开发工作流

Horizon 的唯一开发入口是 `tools/dev.py`。Python 与 Conan 版本由 `uv.lock` 固定；开发机只需要预装 uv、CMake、Ninja 和 Visual Studio C++ 工具链，不需要单独安装 Conan。

默认配置为 `RelWithDebInfo`，默认构建目标为 `HorizonExamples`：

```text
uv run --frozen python tools/dev.py status
uv run --frozen python tools/dev.py install
uv run --frozen python tools/dev.py configure
uv run --frozen python tools/dev.py build
uv run --frozen python tools/dev.py build-fast
uv run --frozen python tools/dev.py rebuild
uv run --frozen python tools/dev.py update
uv run --frozen python tools/dev.py clean
```

可在命令后指定目标，并用 `--configuration Debug|Release|RelWithDebInfo|MinSizeRel` 选择配置。例如：

```text
uv run --frozen python tools/dev.py build ShaderCompileScripts
uv run --frozen python tools/dev.py build HorizonExamples --configuration Release
```

`build` 会安装依赖、配置并编译；`build-fast` 只复用已配置的构建树；`rebuild` 只删除当前配置目录后重建；`clean` 删除仓库生成的构建/安装目录。

Horizon 的 `conanfile.py` 只描述开发依赖、选项与 CMake toolchain，不再包含 `exports_sources`、`build()`、`package()`、`package_info()` 或 editable/package 发布能力。

## IDE / CMake Presets

VS、VSCode CMake Tools 和 CLion 直接选择根目录 `CMakePresets.json` 中的 `relwithdebinfo`、`debug`、`release` 或 `minsizerel`。首次 CMake configure 会在 `project()` 前通过 uv 自动完成 Conan bootstrap。各配置目录为 `build/conan/<configuration>`。

## Conan cache

全局 Conan cache 维护入口是 `tools/conan_cache.py`：

```text
uv run --frozen python tools/conan_cache.py list
uv run --frozen python tools/conan_cache.py update slang --version 2026.10
uv run --frozen python tools/conan_cache.py remove slang/2026.10 --dry-run
uv run --frozen python tools/conan_cache.py clear --dry-run
```

`remove` 默认要求输入 `YES`，也可使用 `--force`。`clear` 影响当前用户的整个 Conan cache，应先使用 `--dry-run`。
