# Horizon Helicon 上下文

仅在处理 shader DSL、AST、codegen、compiler、reflection、generated binding 或 `ShaderCompileScripts` 时加载。

## 关键路径

- `src/Helicon/Compiler/ShaderCodeCompiler.h`
- `src/Helicon/Compiler/ShaderCodeCompiler.cpp`
- `src/Helicon/Compiler/ShaderLanguageConverter.h`
- `src/Helicon/Compiler/ShaderLanguageConverter.cpp`
- `src/Helicon/Codegen/`
- `tools/main.cpp`

## Reflection 消费链

修改 shader reflection 时，沿完整消费链检查：

1. 反射数据从哪里产生。
2. `ShaderResources` 存储哪些字段。
3. `tools/main.cpp` 如何生成绑定代码。
4. `include/Horizon.h` 如何把用户赋值转为 runtime 写入。
5. Vulkan pipeline 如何使用这些数据创建 layout 或写入 push constant。

## 验证

修改 shader compiler、reflection 或 generated binding 时：

```powershell
cmake --build --preset msvc-debug --target ShaderCompileScripts
cmake --build --preset msvc-debug --target Horizon
```

只有影响 runtime/example 行为时才运行示例。
