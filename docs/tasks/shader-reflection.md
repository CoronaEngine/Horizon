# Shader Reflection Task Notes
<!-- TASK_DOCS_SHADER_REFLECTION_ZH_CN_SHA256: ebab355737ebea872cc806e076a09dfae4ca1329faba8d1372f2b795a8d05f11 -->

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
uv run --frozen python tools/dev.py build ShaderCompileScripts
```
