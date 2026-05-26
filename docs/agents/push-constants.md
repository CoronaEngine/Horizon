# Horizon Push Constant Context
<!-- AGENT_DOCS_PUSH_CONSTANTS_ZH_CN_SHA256: 9e24a85141d478fac7c066eae60249fe9d2bd573c9ef41a55ddc5bffa1979391 -->

Load this file only for push constant layout, reflection fields, generated bindings, or runtime push constant writes.

## Source of Truth

The effective source for push constant layout is SPIR-V reflection, especially `spirv-cross`.

Do not assume Slang reflection provides complete push constant information.

## Required Data

The Vulkan/runtime side currently cares about:

- Push constant block size.
- Member name.
- Member byte offset.
- Member type size.
- Bind type.
- Whether resource-like values are represented as 32-bit or 64-bit handles.

## Fields to Inspect

- `ShaderResources::pushConstantSize`
- `ShaderResources::pushConstantName`
- `ShaderBindInfo::byteOffset`
- `ShaderBindInfo::typeSize`
- `ShaderBindInfo::bindType`
- Generated binding structs.
- Compute/rasterizer pipeline push constant storage and `vkCmdPushConstants` calls.

## Consumer Path

Typical path:

1. `ShaderLanguageConverter` reflects SPIR-V push constant buffers.
2. `ShaderResources` stores block and member metadata.
3. `tools/main.cpp` generates binding code.
4. `include/Horizon.h` forwards user assignment data into runtime writes.
5. Vulkan pipelines allocate push constant backing storage and call `vkCmdPushConstants`.
