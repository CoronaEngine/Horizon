# Horizon Push Constant Context
<!-- AGENT_DOCS_PUSH_CONSTANTS_ZH_CN_SHA256: b5a542d7e188ae591b0be9444161f6072d230ecacba1ed33d6645837e9143c11 -->

Load this file only for push constant layout, reflection fields, generated bindings, or runtime push constant writes.

## Source of Truth

The effective source for push constant layout is Slang program/module reflection.

Do not restore the removed SPIRV-Cross reflection path. Read block size, member offsets, and member sizes from Slang type and variable layouts.

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

1. `ShaderLanguageConverter` collects push constant block and member layouts through Slang reflection.
2. `ShaderResources` stores block and member metadata.
3. `tools/main.cpp` generates binding code.
4. `include/horizon.h` forwards user assignment data into runtime writes.
5. Vulkan pipelines allocate push constant backing storage and call `vkCmdPushConstants`.
