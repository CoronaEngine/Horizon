#include "ResourcePool.h"

horizon::core::Storage<ResourceManager::BufferHardwareWrap> globalBufferStorages;
horizon::core::Storage<ResourceManager::ImageHardwareWrap> globalImageStorages;
horizon::core::Storage<RasterizerPipelineWrap> gRasterizerPipelineStorage;
horizon::core::Storage<DisplayerHardwareWrap> globalDisplayerStorages;
horizon::core::Storage<ComputePipelineWrap> gComputePipelineStorage;
horizon::core::Storage<ExecutorWrap> gExecutorStorage;
horizon::core::Storage<PushConstantWrap> globalPushConstantStorages;