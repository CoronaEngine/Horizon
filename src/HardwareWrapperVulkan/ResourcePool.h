#pragma once

#include "HardwareWrapperVulkan/DisplayVulkan/DisplayManager.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceManager.h"
#include "HardwareWrapperVulkan/PipelineVulkan/ComputePipeline.h"
#include "HardwareWrapperVulkan/PipelineVulkan/RasterizerPipeline.h"
#include "horizon/core/storage.h"

extern horizon::core::Storage<ResourceManager::BufferHardwareWrap> globalBufferStorages;
extern horizon::core::Storage<ResourceManager::ImageHardwareWrap> globalImageStorages;

// Control wrappers are heap-owned; their reference counts are owner-thread only.
struct RasterizerPipelineWrap
{
    RasterizerPipelineVulkan *impl = nullptr;
    uint64_t refCount = 1;
};

struct ComputePipelineWrap
{
    ComputePipelineVulkan *impl = nullptr;
    uint64_t refCount = 1;
};

struct DisplayerHardwareWrap
{
    void *displaySurface = nullptr;
    std::shared_ptr<DisplayManager> displayManager;
    uint64_t refCount = 1;
};

struct ExecutorWrap
{
    HardwareExecutorVulkan *impl = nullptr;
    uint64_t refCount = 1;
};

struct PushConstantWrap
{
    uint8_t *data{nullptr};
    uint64_t size{0};
    uint64_t refCount{1};
    bool isSub{false};
};
