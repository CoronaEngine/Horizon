# Horizon Push Constant 上下文

仅在处理 push constant layout、反射字段、generated binding 或 runtime push constant 写入时加载。

## 真实来源

push constant layout 的有效来源是 SPIR-V 反射，尤其是 `spirv-cross`。

不要假设 Slang reflection 能提供完整 push constant 信息。

## 必要数据

Vulkan/runtime 侧当前关心：

- Push constant block size。
- Member name。
- Member byte offset。
- Member type size。
- Bind type。
- Resource-like value 是 32-bit 还是 64-bit handle。

## 需要检查的字段

- `ShaderResources::pushConstantSize`
- `ShaderResources::pushConstantName`
- `ShaderBindInfo::byteOffset`
- `ShaderBindInfo::typeSize`
- `ShaderBindInfo::bindType`
- Generated binding structs。
- Compute/rasterizer pipeline 的 push constant storage 和 `vkCmdPushConstants` 调用。

## 消费路径

典型路径：

1. `ShaderLanguageConverter` 反射 SPIR-V push constant buffers。
2. `ShaderResources` 存储 block 和 member metadata。
3. `tools/main.cpp` 生成绑定代码。
4. `include/Horizon.h` 把用户赋值转发到 runtime 写入。
5. Vulkan pipelines 分配 push constant backing storage 并调用 `vkCmdPushConstants`。
