#pragma once

#include <cstdint>
#include <memory>

#include "hardware_wrapper_vulkan/HardwareVulkan/ResourceManager.h"
#include "corona/kernel/utils/storage.h"

class DisplayManager;
struct ComputePipelineVulkan;
struct HardwareExecutorVulkan;
struct RasterizerPipelineVulkan;

struct BufferWrap
{
    HardwareBufferDesc desc;

    uint64_t allocation_size{0}; // 实际分配大小。外部导入或者对齐后，可能大于 desc.byte_size。
    uint64_t ref_count{1};

    VkBuffer buffer_handle{VK_NULL_HANDLE};
    VkBufferUsageFlags buffer_usage{0};
    VmaAllocation buffer_alloc{VK_NULL_HANDLE};
    VmaAllocationInfo buffer_alloc_info{};

    bool host_imported_manual_bind{false};
    bool imported{false};

    int32_t bindless_index{-1};

    DeviceManager *device_manager{nullptr};
    ResourceManager *resource_manager{nullptr};
};

using BufferHardwareStorage = Corona::Kernel::Utils::Storage<ResourceManager::BufferHardwareWrap>;
using ImageHardwareStorage = Corona::Kernel::Utils::Storage<ResourceManager::ImageHardwareWrap>;

struct RasterizerPipelineWrap
{
    RasterizerPipelineVulkan *impl{nullptr};
    uint64_t refCount{1};
};

using RasterizerPipelineStorage = Corona::Kernel::Utils::Storage<RasterizerPipelineWrap>;

struct ComputePipelineWrap
{
    ComputePipelineVulkan *impl{nullptr};
    uint64_t refCount{1};
};

using ComputePipelineStorage = Corona::Kernel::Utils::Storage<ComputePipelineWrap>;

struct DisplayerHardwareWrap
{
    void *displaySurface{nullptr};
    std::shared_ptr<DisplayManager> displayManager;
    uint64_t refCount{1};
};

using DisplayerHardwareStorage = Corona::Kernel::Utils::Storage<DisplayerHardwareWrap>;

struct ExecutorWrap
{
    HardwareExecutorVulkan *impl{nullptr};
    uint64_t refCount{1};
};

using ExecutorStorage = Corona::Kernel::Utils::Storage<ExecutorWrap>;

struct PushConstantWrap
{
    uint8_t *data{nullptr};
    uint64_t size{0};
    uint64_t refCount{1};
    bool isSub{false};
};

using PushConstantStorage = Corona::Kernel::Utils::Storage<PushConstantWrap>;

extern BufferHardwareStorage globalBufferStorages;
extern ImageHardwareStorage globalImageStorages;
extern RasterizerPipelineStorage gRasterizerPipelineStorage;
extern ComputePipelineStorage gComputePipelineStorage;
extern DisplayerHardwareStorage globalDisplayerStorages;
extern ExecutorStorage gExecutorStorage;
extern PushConstantStorage globalPushConstantStorages;
