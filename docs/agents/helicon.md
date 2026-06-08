# Horizon Helicon Context
<!-- AGENT_DOCS_HELICON_ZH_CN_SHA256: 0270bd8569d9ea5e0187976d4983be805cdb0e0b4f81cfffd09c6b34030d5f12 -->

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

## Generated Binding Compatibility

- Keep `BindingKey` and `BoundField` in `src/Helicon/Codegen/VariateProxy.h`, and `AutoBindEntry` in `ComputePipelineObject.h` / `RasterizedPipelineObject.h`, in the current four-field compatibility shape: `byteOffset`, `typeSize`, `bindType`, and `location`. Do not add `set` / `binding` to these Helicon metadata structures just to adapt the Vulkan backend.
- Older generated direct-resource metadata stores the descriptor binding in the fourth `location` slot. Runtime bridge types may map a missing `binding` to `location`, and Vulkan consumers may fill a default set while consuming old metadata. Only change Helicon metadata shape when shader reflection, generator output, public runtime, and Vulkan consumers migrate together.

## Validation

For shader compiler, reflection, or generated binding changes:

```powershell
cmake --build --preset msvc-debug --target ShaderCompileScripts
cmake --build --preset msvc-debug --target Horizon
```

Run examples only if the change affects runtime/example behavior.
