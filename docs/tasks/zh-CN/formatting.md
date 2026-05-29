# 格式化任务说明

## 风格来源

- 使用 `.clang-format`。

## 命令

检查当前 C/C++ 改动：

```powershell
.\tools\dev.ps1 format-check
# 或
.\tools\code-format.ps1 -Check
```

对当前 C/C++ 改动应用自动格式化：

```powershell
.\tools\dev.ps1 format
# 或
.\tools\code-format.ps1
```

检查或格式化显式路径：

```powershell
.\tools\dev.ps1 format-check src/hardware_wrapper_vulkan/hardware_context.h
.\tools\dev.ps1 format src/hardware_wrapper_vulkan
.\tools\code-format.ps1 -Check src/hardware_wrapper_vulkan/hardware_context.h
.\tools\code-format.ps1 src/hardware_wrapper_vulkan
```

## 范围

- 不要手动大范围重排第三方代码。
- 常规 feature 或 bug-fix 任务中避免大范围无关格式化。
