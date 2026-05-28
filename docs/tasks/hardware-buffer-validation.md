# Hardware Buffer Validation Task Notes

## Boundary

- `HardwareBufferDesc` owns data defaults and pure derived properties such as `byte_size()`.
- Do not add `HardwareBufferDesc::validate()`; keep policy checks in `src/hardware_wrapper/validation`.
- Keep hard safety invariants always on: zero element size, byte-size overflow, and upload data exceeding buffer byte size must not depend on optional validation.
- `HORIZON_ENABLE_VALIDATION` is the compile-time switch for optional validation diagnostics and should be exported with `target_compile_definitions(... PUBLIC ...)`.
- `HardwareValidationMode` is the runtime policy switch: `Disabled`, `Log`, or `Throw`.

## Placement

- Add buffer desc, copy, host read, and host write policy checks under `src/hardware_wrapper/validation`.
- Keep pure size math out of the validation module when public templates or disabled-validation builds still need it.
- If `HardwareBufferDesc::byte_size()` becomes non-inline, move it to a consistently compiled implementation file, not the optional validation module.
- Keep Vulkan, VMA, Windows, and other backend-only details out of public descriptor checks unless the public API explicitly exposes them.

## Validation

For docs or comment-only updates:

```powershell
git diff --check
```

For C++ or CMake changes:

```powershell
cmd.exe /d /s /c "`"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat`" -arch=x64 -host_arch=x64 && cmake --build --preset msvc-debug --target Horizon"
```
