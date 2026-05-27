# Formatting Task Notes

## Style Source

- Use `.clang-format`.

## Commands

Check current C/C++ changes:

```powershell
.\tools\dev.ps1 format-check
# or
.\tools\code-format.ps1 -Check
```

Apply automatic formatting to current C/C++ changes:

```powershell
.\tools\dev.ps1 format
# or
.\tools\code-format.ps1
```

Check or format explicit paths:

```powershell
.\tools\dev.ps1 format-check src/hardware_wrapper_vulkan/hardware_context.h
.\tools\dev.ps1 format src/hardware_wrapper_vulkan
.\tools\code-format.ps1 -Check src/hardware_wrapper_vulkan/hardware_context.h
.\tools\code-format.ps1 src/hardware_wrapper_vulkan
```

## Scope

- Do not manually reformat large areas of third-party code.
- Avoid broad unrelated formatting in normal feature or bug-fix tasks.
