# Horizon Helicon Context
<!-- AGENT_DOCS_HELICON_ZH_CN_SHA256: ad03611a5b2b26f92428c93e8e0aa80ad0850b81b8a778ea02c69cadaa932952 -->

Load this file only for shader DSL, AST, codegen, compiler, reflection, generated binding, or `ShaderCompileScripts` work.

## Key Paths

- `src/Helicon/Compiler/ShaderCodeCompiler.h`
- `src/Helicon/Compiler/ShaderCodeCompiler.cpp`
- `src/Helicon/Compiler/ShaderLanguageConverter.h`
- `src/Helicon/Compiler/ShaderLanguageConverter.cpp`
- `src/Helicon/Codegen/`
- `tools/main.cpp`

## Reflection Consumer Chain

When changing shader reflection, trace the full consumer chain:

1. Where reflection data is produced.
2. Which fields are stored in `ShaderResources`.
3. How `tools/main.cpp` generates binding code.
4. How `include/Horizon.h` turns user assignments into runtime writes.
5. How the Vulkan pipeline uses the data to create layouts or write push constants.

## Validation

For shader compiler, reflection, or generated binding changes:

```powershell
cmake --build --preset msvc-debug --target ShaderCompileScripts
cmake --build --preset msvc-debug --target Horizon
```

Run examples only if the change affects runtime/example behavior.
