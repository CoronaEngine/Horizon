# 按需开启构建目标

## 适用场景

- 新人第一次 configure 后发现目标或产物很多，不确定哪些必须构建。
- 只想构建 Horizon 库本体，不想默认生成示例、测试、工具、ocarina 或第三方 CLI。
- 需要临时开启某一类目标进行调试或验证。

## 默认行为

- 默认 configure 偏向干净构建，只启用库本体需要的目标。
- `tools/`、`examples/`、`tests/`、`benchmarks/`、`modules/ocarina` 默认关闭。
- SPIRV-Tools 等第三方命令行工具和第三方安装规则默认关闭。
- 旧 build 目录可能缓存过旧的 ON 值；如果行为不符合预期，重新 configure 并显式传入 OFF，或删除对应 `build/<preset>`。

## 按需开启

```powershell
# 只构建库本体
cmake --preset ninja-msvc
cmake --build --preset msvc-debug --target Horizon

# shader/codegen 工具
cmake --preset ninja-msvc -DHORIZON_BUILD_TOOLS=ON
cmake --build --preset msvc-debug --target ShaderCompileScripts

# 示例；会同时启用 tools/
cmake --preset ninja-msvc -DHORIZON_BUILD_EXAMPLES=ON
cmake --build --preset msvc-debug --target HorizonExamples

# Horizon 统一测试
cmake --preset ninja-msvc -DHORIZON_BUILD_TESTS=ON
cmake --build --preset msvc-debug --target HorizonTests
ctest --test-dir build/ninja-msvc -C Debug -R HorizonTests --output-on-failure

# benchmark
cmake --preset ninja-msvc -DHORIZON_BUILD_BENCHMARKS=ON

# ocarina；还需要 CUDA_PATH
cmake --preset ninja-msvc -DHORIZON_BUILD_OCARINA=ON

# ocarina 自测；同时需要启用 ocarina
cmake --preset ninja-msvc -DHORIZON_BUILD_OCARINA=ON -DHORIZON_BUILD_OCARINA_TESTS=ON

# 第三方命令行工具，例如 spirv-*；仅在确实需要这些工具时开启
cmake --preset ninja-msvc -DHORIZON_BUILD_DEPENDENCY_TOOLS=ON

# 第三方 install 规则；日常开发通常不需要
cmake --preset ninja-msvc -DHORIZON_ENABLE_DEPENDENCY_INSTALL=ON
```

## 恢复干净配置

```powershell
cmake --preset ninja-msvc `
  -DHORIZON_BUILD_TOOLS=OFF `
  -DHORIZON_BUILD_EXAMPLES=OFF `
  -DHORIZON_BUILD_TESTS=OFF `
  -DHORIZON_BUILD_BENCHMARKS=OFF `
  -DHORIZON_BUILD_OCARINA=OFF `
  -DHORIZON_BUILD_OCARINA_TESTS=OFF `
  -DHORIZON_BUILD_DEPENDENCY_TOOLS=OFF `
  -DHORIZON_ENABLE_DEPENDENCY_INSTALL=OFF
```

如果仍看到旧目标或旧产物，先确认它们是不是历史构建留下的文件；CMake 关闭目标不会自动删除已生成的 `.exe` / `.dll`。

## 验证

```powershell
cmake --preset ninja-msvc
cmake --build --preset msvc-debug --target Horizon
ninja -C build/ninja-msvc -f build-Debug.ninja -t query all:Debug
```

干净默认构建下，`all:Debug` 应只拉库本体相关目标，不应默认拉 `ShaderCompileScripts`、`HorizonExamples`、`HorizonTests`、`ocarina` 或 `spirv-*` CLI。
