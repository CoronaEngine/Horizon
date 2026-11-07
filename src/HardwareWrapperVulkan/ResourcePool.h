#pragma once

#include "HardwareWrapperVulkan/DisplayVulkan/DisplayManager.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceManager.h"
#include "HardwareWrapperVulkan/PipelineVulkan/ComputePipeline.h"
#include "HardwareWrapperVulkan/PipelineVulkan/RasterizerPipeline.h"
#include "horizon/core/storage.h"

extern horizon::core::Storage<ResourceManager::BufferHardwareWrap> globalBufferStorages;
extern horizon::core::Storage<ResourceManager::ImageHardwareWrap> globalImageStorages;

struct RasterizerPipelineWrap
{
    RasterizerPipelineVulkan *impl = nullptr;
    uint64_t refCount = 1;
};

extern horizon::core::Storage<RasterizerPipelineWrap> gRasterizerPipelineStorage;

struct ComputePipelineWrap
{
    ComputePipelineVulkan *impl = nullptr;
    uint64_t refCount = 1;
};

extern horizon::core::Storage<ComputePipelineWrap> gComputePipelineStorage;

struct DisplayerHardwareWrap
{
    void *displaySurface = nullptr;
    std::shared_ptr<DisplayManager> displayManager;
    uint64_t refCount = 1;
};

extern horizon::core::Storage<DisplayerHardwareWrap> globalDisplayerStorages;

struct ExecutorWrap
{
    HardwareExecutorVulkan *impl = nullptr;
    uint64_t refCount = 1;
};

extern horizon::core::Storage<ExecutorWrap> gExecutorStorage;

struct PushConstantWrap
{
    uint8_t *data{nullptr};
    uint64_t size{0};
    uint64_t refCount{1};
    bool isSub{false};
};

extern horizon::core::Storage<PushConstantWrap> globalPushConstantStorages;