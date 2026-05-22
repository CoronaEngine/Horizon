#pragma once

#include <cstdint>
#include <memory>

#include "hardware_wrapper_vulkan/resource_registry.h"
#include "hardware_wrapper_vulkan/hardware/resource_manager.h"

struct ComputePipelineVulkan;
struct RasterizerPipelineVulkan;
struct HardwareExecutorVulkan;

namespace Corona::Horizon::Vulkan
{
    struct RasterizerPipelineWrap
    {
        RasterizerPipelineVulkan* impl = nullptr;
        uint64_t refCount = 1;
        uint64_t ref_count = 1;
    };

    struct ComputePipelineWrap
    {
        ComputePipelineVulkan* impl = nullptr;
        uint64_t refCount = 1;
        uint64_t ref_count = 1;
    };

    struct DisplayerHardwareWrap
    {
        void* display_surface = nullptr;
        void* displaySurface = nullptr;
        std::shared_ptr<void> display_manager;
        std::shared_ptr<void> displayManager;
        uint64_t refCount = 1;
        uint64_t ref_count = 1;
    };

    struct ExecutorWrap
    {
        HardwareExecutorVulkan* impl = nullptr;
        uint64_t refCount = 1;
        uint64_t ref_count = 1;
    };

    struct PushConstantWrap
    {
        uint8_t* data = nullptr;
        uint64_t size = 0;
        bool is_sub = false;
        bool isSub = false;
        uint64_t refCount = 1;
        uint64_t ref_count = 1;
    };

    using BufferStorage = StorageCompat<ResourceManager::BufferHardwareWrap>;
    using ImageStorage = StorageCompat<ResourceManager::ImageHardwareWrap>;
    using RasterizerPipelineStorage = StorageCompat<RasterizerPipelineWrap>;
    using ComputePipelineStorage = StorageCompat<ComputePipelineWrap>;
    using DisplayerStorage = StorageCompat<DisplayerHardwareWrap>;
    using ExecutorStorage = StorageCompat<ExecutorWrap>;
    using PushConstantStorage = StorageCompat<PushConstantWrap>;

    extern BufferStorage global_buffer_storages;
    extern ImageStorage global_image_storages;
    extern RasterizerPipelineStorage rasterizer_pipeline_storage;
    extern ComputePipelineStorage compute_pipeline_storage;
    extern DisplayerStorage global_displayer_storages;
    extern ExecutorStorage executor_storage;
    extern PushConstantStorage global_push_constant_storages;

    bool retain_buffer(std::uintptr_t id) noexcept;
    bool release_buffer(std::uintptr_t id) noexcept;
    bool is_buffer_alive(std::uintptr_t id) noexcept;

    bool retain_image(std::uintptr_t id) noexcept;
    bool release_image(std::uintptr_t id) noexcept;
    bool is_image_alive(std::uintptr_t id) noexcept;
}

namespace Corona::Horizon
{
    using Id = std::uintptr_t;
    using Vulkan::ComputePipelineWrap;
    using Vulkan::DisplayerHardwareWrap;
    using Vulkan::ExecutorWrap;
    using Vulkan::PushConstantWrap;
    using Vulkan::RasterizerPipelineWrap;
    using ResourceManager = Vulkan::ResourceManager;

    inline Vulkan::BufferStorage& global_buffer_storages = Vulkan::global_buffer_storages;
    inline Vulkan::ImageStorage& global_image_storages = Vulkan::global_image_storages;
    inline Vulkan::RasterizerPipelineStorage& rasterizer_pipeline_storage = Vulkan::rasterizer_pipeline_storage;
    inline Vulkan::ComputePipelineStorage& compute_pipeline_storage = Vulkan::compute_pipeline_storage;
    inline Vulkan::DisplayerStorage& global_displayer_storages = Vulkan::global_displayer_storages;
    inline Vulkan::ExecutorStorage& executor_storage = Vulkan::executor_storage;
    inline Vulkan::PushConstantStorage& global_push_constant_storages = Vulkan::global_push_constant_storages;

    inline Vulkan::BufferStorage& globalBufferStorages = Vulkan::global_buffer_storages;
    inline Vulkan::ImageStorage& globalImageStorages = Vulkan::global_image_storages;
    inline Vulkan::RasterizerPipelineStorage& gRasterizerPipelineStorage = Vulkan::rasterizer_pipeline_storage;
    inline Vulkan::ComputePipelineStorage& gComputePipelineStorage = Vulkan::compute_pipeline_storage;
    inline Vulkan::DisplayerStorage& globalDisplayerStorages = Vulkan::global_displayer_storages;
    inline Vulkan::ExecutorStorage& gExecutorStorage = Vulkan::executor_storage;
    inline Vulkan::PushConstantStorage& globalPushConstantStorages = Vulkan::global_push_constant_storages;

    inline bool retainBuffer(std::uintptr_t id) noexcept { return Vulkan::retain_buffer(id); }
    inline bool releaseBuffer(std::uintptr_t id) noexcept { return Vulkan::release_buffer(id); }
    inline bool isBufferAlive(std::uintptr_t id) noexcept { return Vulkan::is_buffer_alive(id); }

    inline bool retain_buffer(std::uintptr_t id) noexcept { return Vulkan::retain_buffer(id); }
    inline bool release_buffer(std::uintptr_t id) noexcept { return Vulkan::release_buffer(id); }
    inline bool is_buffer_alive(std::uintptr_t id) noexcept { return Vulkan::is_buffer_alive(id); }

    inline bool retainImage(std::uintptr_t id) noexcept { return Vulkan::retain_image(id); }
    inline bool releaseImage(std::uintptr_t id) noexcept { return Vulkan::release_image(id); }
    inline bool isImageAlive(std::uintptr_t id) noexcept { return Vulkan::is_image_alive(id); }

    inline bool retain_image(std::uintptr_t id) noexcept { return Vulkan::retain_image(id); }
    inline bool release_image(std::uintptr_t id) noexcept { return Vulkan::release_image(id); }
    inline bool is_image_alive(std::uintptr_t id) noexcept { return Vulkan::is_image_alive(id); }
}
