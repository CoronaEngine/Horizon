# Horizon Helicon Context
<!-- AGENT_DOCS_HELICON_ZH_CN_SHA256: 382d961f19491f7796e6d1fcf1bc8ef7e4e00721256434c46b624a305873f8cc -->

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

## Debug / Release Hardcoded Shaders

- Debug uses per-instance compiled outputs. Release must not switch to the hardcoded shader cache merely because `Compiler/HardcodeShaders/HardcodeShaders.h` exists locally.
- `Compiler/HardcodeShaders/HardcodeShaders*.cpp` files are generated local artifacts. Normal builds must exclude them from recursive source globs; consume them only when `HORIZON_ENABLE_HARDCODE_SHADERS` is explicitly enabled.
- If Debug succeeds but Release reports C++ syntax errors inside generated shader files, first check whether the Release build graph contains `HardcodeShaders*.cpp.obj`. Then inspect generator append offsets, string escaping, and cache variant types instead of changing shader syntax first.

## Validation

For shader compiler, reflection, or generated binding changes:

```powershell
cmake --build --preset msvc-debug --target ShaderCompileScripts
cmake --build --preset msvc-debug --target Horizon
cmake --build --preset msvc-release --target ShaderCompileScripts
cmake --build --preset msvc-release --target HorizonExamples
```

Run examples only if the change affects runtime/example behavior.
