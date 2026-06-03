# Horizon 格式化上下文

仅在处理格式化、clang-format、代码风格或 include hygiene 时加载。

## 格式

项目使用 `.clang-format`。

格式化脚本是 `tools/code-format.ps1`；统一开发入口 `tools/dev.ps1 format-check` / `format` 会转发到该脚本。

已知风格信号：

- C++ 使用 4 空格缩进。
- 指针和引用靠近类型侧，例如 `int* value`、`auto& device`。
- namespace 内部缩进。
- 短 inline 函数可以保持单行。
- 不要求 namespace 结尾注释。
- 不要大范围格式化无关文件。
- 不要格式化第三方代码，除非明确要求。

## 命令

检查当前 Git 改动中的 C/C++ 文件：

```powershell
.\tools\dev.ps1 format-check
# 或
.\tools\code-format.ps1 -Check
```

自动格式化当前 Git 改动中的 C/C++ 文件：

```powershell
.\tools\dev.ps1 format
# 或
.\tools\code-format.ps1
```

检查或格式化明确指定的文件/目录：

```powershell
.\tools\dev.ps1 format-check src/hardware_wrapper_vulkan/hardware_context.h
.\tools\dev.ps1 format src/hardware_wrapper_vulkan
.\tools\code-format.ps1 -Check src/hardware_wrapper_vulkan/hardware_context.h
.\tools\code-format.ps1 src/hardware_wrapper_vulkan
```

默认格式化范围只包含当前 Git 改动中位于 `include/`、`src/hardware_wrapper_vulkan/`、`src/Helicon/`、`examples/`、`tests/` 和 `tools/` 下的 C/C++ 文件。只格式化用户要求的范围；不要把 `third-party/`、`modules/` 或历史镜像树一起顺手格式化。

## 已知限制

`clang-format` 可能无法保留 `.cpp` 里所有手工清理过的细节，尤其是某些引用空格和 braced initializer 换行。除非用户要求手工修风格，否则把这些视为工具限制。
