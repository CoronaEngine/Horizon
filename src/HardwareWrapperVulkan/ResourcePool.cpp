#include "ResourcePool.h"

Corona::Kernel::Utils::Storage<ResourceManager::BufferHardwareWrap> globalBufferStorages;
Corona::Kernel::Utils::Storage<ResourceManager::ImageHardwareWrap> globalImageStorages;
Corona::Kernel::Utils::Storage<RasterizerPipelineWrap> gRasterizerPipelineStorage;
Corona::Kernel::Utils::Storage<DisplayerHardwareWrap> globalDisplayerStorages;
Corona::Kernel::Utils::Storage<ComputePipelineWrap> gComputePipelineStorage;
Corona::Kernel::Utils::Storage<ExecutorWrap> gExecutorStorage;
Corona::Kernel::Utils::Storage<PushConstantWrap> globalPushConstantStorages;