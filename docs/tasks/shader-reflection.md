# Shader Reflection Task Notes

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
