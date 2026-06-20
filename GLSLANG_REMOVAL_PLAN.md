# Horizon glslang Removal Plan

## Summary

Horizon's shader path now uses Slang for GLSL/HLSL/Slang source input, target
generation, and module-based compilation. Khronos `glslang` is no longer part of
the active Helicon compile path, so the repository should not fetch, link, or
expose it.

This cleanup removes only the Khronos `glslang` dependency and dead adapter code.
It does not remove GLSL/HLSL as source languages.

## Implementation Scope

- Remove the `glslang` FetchContent declaration and related cache options from
  `cmake/HorizonCoreDependencies.cmake`.
- Remove `glslang` and `glslang-default-resource-limits` from the `Helicon`
  target link libraries.
- Remove `ShaderLanguageConverter::glslangSpirvCompiler`, its glslang headers,
  and its local include resolver helper.
- Remove the commented historical `ShaderCodeCompiler::compile` branch that still
  referenced the old glslang path.

## Preserved Boundaries

- Keep `ShaderLanguage::GLSL` and `ShaderLanguage::HLSL`; Slang still accepts
  them through `CompilerOptionName::Language`.
- Keep SPIRV-Cross. It remains the source of truth for SPIR-V reflection data
  needed by push constants and generated bindings.
- Keep SPIRV-Tools. `SPIRV-Tools-link` still backs SPIR-V linking and validation
  helpers.
- Do not change `modules/` or historical `src/HardwareWrapperVulkan/` code as
  part of this cleanup.

## Validation

Run:

```powershell
rg -n "glslang|GLSLANG|Glslang" -S cmake src/Helicon CMakeLists.txt
cmd.exe /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --preset ninja-msvc'
cmd.exe /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset msvc-debug --target ShaderCompileScripts && cmake --build --preset msvc-debug --target Horizon'
git diff --check
```

Expected result: active CMake and Helicon code contain no `glslang` references,
configure succeeds without creating a `glslang` target, and the Helicon tool and
Horizon library builds pass.
