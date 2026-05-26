# Horizon 格式化上下文

仅在处理格式化、clang-format、代码风格或 include hygiene 时加载。

## 格式

项目使用 `.clang-format`。

已知风格信号：

- C++ 使用 4 空格缩进。
- 指针和引用靠近类型侧，例如 `int* value`、`auto& device`。
- namespace 内部缩进。
- 短 inline 函数可以保持单行。
- 不要求 namespace 结尾注释。
- 不要大范围格式化无关文件。
- 不要格式化第三方代码，除非明确要求。

## 命令

```powershell
.\tools\code-format.ps1 -Check
.\tools\code-format.ps1
clang-format --style=file --dry-run --Werror src/hardware_wrapper_vulkan/hardware_context.h
```

## 已知限制

`clang-format` 可能无法保留 `.cpp` 里所有手工清理过的细节，尤其是某些引用空格和 braced initializer 换行。除非用户要求手工修风格，否则把这些视为工具限制。
