//#pragma once
//
//#include "corona/kernel/utils/storage.h"
//
//namespace Corona::Horizon
//{
//    class DisplayManager;
//    class ComputePipelineVulkan;
//    class HardwareExecutorVulkan;
//    class RasterizerPipelineVulkan;
//
//    struct BufferWrap
//    {
//        HardwareBufferDesc desc;
//
//        uint64_t allocation_size{0}; // 实际分配大小。外部导入或者对齐后，可能大于 desc.byte_size。
//        uint64_t ref_count{1};
//
//        VkBuffer buffer_handle{VK_NULL_HANDLE};
//        VkBufferUsageFlags buffer_usage{0};
//        VmaAllocation buffer_alloc{VK_NULL_HANDLE};
//        VmaAllocationInfo buffer_alloc_info{};
//
//        bool host_imported_manual_bind{false};
//        bool imported{false};
//
//        int32_t bindless_index{-1};
//
//        DeviceManager* device_manager{nullptr};
//        ResourceManager* resource_manager{nullptr};
//    };
//
//    extern Corona::Kernel::Utils::Storage<ResourceManager::BufferWrap> g_buffer_storages;
//
//    struct RasterizerPipelineWrap
//    {
//        RasterizerPipelineVulkan *impl{nullptr};
//        uint64_t refCount{1};
//    };
//
//    struct ComputePipelineWrap
//    {
//        ComputePipelineVulkan *impl{nullptr};
//        uint64_t refCount{1};
//    };
//
//    struct DisplayerHardwareWrap
//    {
//        void *displaySurface{nullptr};
//        std::shared_ptr<DisplayManager> displayManager;
//        uint64_t refCount{1};
//    };
//
//    struct ExecutorWrap
//    {
//        HardwareExecutorVulkan *impl{nullptr};
//        uint64_t refCount{1};
//    };
//
//}
