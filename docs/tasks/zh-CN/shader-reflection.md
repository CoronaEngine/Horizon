# Shader Reflection 任务说明

## 相关路径

- `src/Helicon/Compiler/`

## 必须检查

- `src/Helicon/Compiler/ShaderCodeCompiler.h`
- `src/Helicon/Compiler/ShaderLanguageConverter.cpp`

## Push Constant 字段

修改 push constant reflection 时，注意：

- `size`
- `offset`
- `typeSize`
- `name`
- `bindType`

## 验证

```powershell
uv run --frozen python tools/dev.py build ShaderCompileScripts
```
