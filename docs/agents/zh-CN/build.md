# 构建与验证

## 统一入口

开发机需要 uv、CMake、Ninja 和 Visual Studio C++ 工具链。Conan 由 uv 根据 `uv.lock` 安装，不要求系统级 Conan。

```text
uv run --frozen python tools/dev.py status
uv run --frozen python tools/dev.py configure
uv run --frozen python tools/dev.py build
uv run --frozen python tools/dev.py build ShaderCompileScripts
uv run --frozen python tools/dev.py build HorizonExamples
```

默认配置是 `RelWithDebInfo`，默认目标是 `HorizonExamples`。其他配置使用 `--configuration Debug|Release|RelWithDebInfo|MinSizeRel`。

`build` 会安装依赖、配置并编译；`build-fast` 只复用现有构建树；`rebuild` 删除当前配置目录后重建；`clean` 删除项目生成目录。

## IDE

VS、VSCode CMake Tools 和 CLion 直接选择根目录 `CMakePresets.json` 的稳定 preset。首次 configure 会在 `project()` 前调用 uv/Python bootstrap，生成 `build/conan/<configuration>` 下的 Conan toolchain。

## Conan 边界

`conanfile.py` 是开发依赖和 toolchain manifest，不是 Horizon package recipe。不要运行 `conan create`，也不要新增 package、editable 或导出源码流程。

目标名决定需要启用的开发依赖选项：

- `HorizonExamples` 启用 examples。
- `ShaderCompileScripts` 启用 tools。
- `HorizonTests` 启用 tests。
- `HorizonSmokeBenchmarks` 启用 benchmarks。
- `ocarina*` 与 `vision-hotfix*` 目标按需启用 CUDA/Ocarina；此时要求 `CUDA_PATH`。

Slang 与其他本地 recipe 会在 install/configure/build 前导出。CMake 不提供 `HORIZON_SLANG_ROOT` 或 `third-party/slang` fallback。

## 验证

常用验证顺序：

```text
uv run --frozen python -m unittest tools/test_workflow.py
uv run --frozen python tools/dev.py configure
uv run --frozen python tools/dev.py build-fast HorizonExamples
```

只有排查底层问题时才直接调用 Conan 或 CMake。
