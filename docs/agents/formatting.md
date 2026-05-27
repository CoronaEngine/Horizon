# Horizon Formatting Context
<!-- AGENT_DOCS_FORMATTING_ZH_CN_SHA256: 31a61df6b3163f8edc419596d9d06f7e3a0fb0bd7b5de0e5c4244219667a75f6 -->

Load this file only for formatting, clang-format, style, or include-hygiene tasks.

## Formatting

The project uses `.clang-format`.

The formatting script is `tools/code-format.ps1`; the unified `tools/dev.ps1 format-check` / `format` commands forward to it.

Known style signals:

- C++ uses 4-space indentation.
- Pointer and reference markers bind to the type side, for example `int* value` and `auto& device`.
- Namespace bodies are indented.
- Short inline functions may stay on one line.
- Namespace closing comments are not required.
- Avoid broad unrelated formatting.
- Do not format third-party code unless explicitly requested.

## Commands

Check C/C++ files in the current Git changes:

```powershell
.\tools\dev.ps1 format-check
# or
.\tools\code-format.ps1 -Check
```

Automatically format C/C++ files in the current Git changes:

```powershell
.\tools\dev.ps1 format
# or
.\tools\code-format.ps1
```

Check or format explicit files/directories:

```powershell
.\tools\dev.ps1 format-check src/hardware_wrapper_vulkan/hardware_context.h
.\tools\dev.ps1 format src/hardware_wrapper_vulkan
.\tools\code-format.ps1 -Check src/hardware_wrapper_vulkan/hardware_context.h
.\tools\code-format.ps1 src/hardware_wrapper_vulkan
```

By default, formatting only includes C/C++ files from current Git changes under `include/`, `src/hardware_wrapper_vulkan/`, `src/Helicon/`, `examples/`, `tests/`, and `tools/`. Format only the requested scope; do not casually include `third-party/`, `modules/`, or historical mirror trees.

## Known Limit

`clang-format` may not preserve every hand-cleaned nuance in `.cpp` files, especially some reference spacing and braced initializer line breaks. Treat those as tool limits unless the user asks for manual style repair.
