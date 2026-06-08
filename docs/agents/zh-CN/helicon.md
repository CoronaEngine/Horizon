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

## Generated Binding 兼容性

- `src/Helicon/Codegen/VariateProxy.h` 中的 `BindingKey`、`BoundField`，以及 `ComputePipelineObject.h` / `RasterizedPipelineObject.h` 中的 `AutoBindEntry`，当前保持四字段兼容形态：`byteOffset`、`typeSize`、`bindType`、`location`。不要为了适配 Vulkan 后端临时给这些 Helicon metadata 结构添加 `set` / `binding`。
- 旧 generated direct-resource 约定把 descriptor binding 存在第四个 `location` 槽位。runtime 桥接类型可以把缺失的 `binding` 默认映射为 `location`，Vulkan 消费端也可以在消费旧 metadata 时补默认 set；只有在 shader reflection、generator、public runtime 和 Vulkan consumer 全链路一起迁移时，才修改 Helicon metadata 结构。

## 验证

修改 shader compiler、reflection 或 generated binding 时：

```powershell
cmake --build --preset msvc-debug --target ShaderCompileScripts
cmake --build --preset msvc-debug --target Horizon
```

只有影响 runtime/example 行为时才运行示例。
