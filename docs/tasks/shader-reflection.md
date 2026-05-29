# Shader Reflection Task Notes
<!-- TASK_DOCS_SHADER_REFLECTION_ZH_CN_SHA256: 99abb372478871d3d500a18e6703fc398df50bc6882b2aaf55b13e719016eb86 -->

## Related Paths

- `src/Helicon/Compiler/`

## Must Check

- `src/Helicon/Compiler/ShaderCodeCompiler.h`
- `src/Helicon/Compiler/ShaderLanguageConverter.cpp`

## Push Constant Fields

When changing push constant reflection, pay attention to:

- `size`
- `offset`
- `typeSize`
- `name`
- `bindType`

## Validation

```powershell
.\tools\dev.ps1 build ShaderCompileScripts
```
