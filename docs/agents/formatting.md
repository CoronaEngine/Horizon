# Horizon Formatting Context
<!-- AGENT_DOCS_FORMATTING_ZH_CN_SHA256: 85243218fd1e3760ee5cf13517a520863f337d92bebec8b3004eb737c975f10e -->

Load this file only for formatting, clang-format, style, or include-hygiene tasks.

## Formatting

The project uses `.clang-format`.

Known style signals:

- C++ uses 4-space indentation.
- Pointer and reference markers bind to the type side, for example `int* value` and `auto& device`.
- Namespace bodies are indented.
- Short inline functions may stay on one line.
- Namespace closing comments are not required.
- Avoid broad unrelated formatting.
- Do not format third-party code unless explicitly requested.

## Commands

```powershell
.\tools\code-format.ps1 -Check
.\tools\code-format.ps1
clang-format --style=file --dry-run --Werror src/hardware_wrapper_vulkan/hardware_context.h
```

## Known Limit

`clang-format` may not preserve every hand-cleaned nuance in `.cpp` files, especially some reference spacing and braced initializer line breaks. Treat those as tool limits unless the user asks for manual style repair.
