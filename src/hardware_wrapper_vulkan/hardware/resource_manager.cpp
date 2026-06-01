#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

#include "resource_manager.h"

#include <stdexcept>
#include <string>
#include <vector>

#include "corona/kernel/core/i_logger.h"
#include "device_manager.h"

namespace Corona::Horizon
{
    namespace
    {
        [[nodiscard]] VkBufferUsageFlags to_vk_buffer_usage(BufferUsageFlags usage) noexcept
        {
            VkBufferUsageFlags result = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

            if (has_flag(usage, BufferUsageFlags::Vertex))
                result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

            if (has_flag(usage, BufferUsageFlags::Index))
                result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

            if (has_flag(usage, BufferUsageFlags::Uniform))
                result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

            if (has_flag(usage, BufferUsageFlags::Storage))
                result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

            return result;
        }

        [[nodiscard]] VkExternalMemoryHandleTypeFlags external_memory_handle_type() noexcept
        {
#if defined(_WIN32) || defined(_WIN64)
            return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#elif defined(__linux__)
            return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#else
            return 0;
#endif
        }

        void throw_if_failed(VkResult result, const char* operation)
        {
            if (result != VK_SUCCESS)
            {
                throw std::runtime_error(std::string(operation) + " failed. VkResult=" + std::to_string(static_cast<int>(result)));
            }
        }

        void name_buffer(VkDevice device, VmaAllocator allocator, const BufferWrap& buffer) noexcept
        {
            if (buffer.desc.debug_name.empty())
            {
                return;
            }

            if (buffer.buffer_alloc != VK_NULL_HANDLE)
            {
                vmaSetAllocationName(allocator, buffer.buffer_alloc, buffer.desc.debug_name.c_str());
            }

            if (vkSetDebugUtilsObjectNameEXT == nullptr || buffer.buffer_handle == VK_NULL_HANDLE)
            {
                return;
            }

            VkDebugUtilsObjectNameInfoEXT name_info {};
            name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            name_info.objectType = VK_OBJECT_TYPE_BUFFER;
            name_info.objectHandle = reinterpret_cast<uint64_t>(buffer.buffer_handle);
            name_info.pObjectName = buffer.desc.debug_name.c_str();
            (void)vkSetDebugUtilsObjectNameEXT(device, &name_info);
        }
    }

    ResourceManager::ResourceManager()
    {
    }

    ResourceManager::~ResourceManager()
    {
        shutdown();
    }

    void ResourceManager::initialize(DeviceManager& device_manager)
    {
        std::lock_guard lock(mutex_);

        if (device_manager_ == &device_manager && allocator_ != VK_NULL_HANDLE)
        {
            return;
        }

        shutdown_unlocked();

        if (device_manager.instance() == VK_NULL_HANDLE ||
            device_manager.physical_device() == VK_NULL_HANDLE ||
            device_manager.logical_device() == VK_NULL_HANDLE)
        {
            throw std::runtime_error("ResourceManager requires an initialized DeviceManager.");
        }

        device_manager_ = &device_manager;
        create_allocator();
    }

    void ResourceManager::shutdown() noexcept
    {
        std::lock_guard lock(mutex_);
        shutdown_unlocked();
    }

    void ResourceManager::shutdown_unlocked() noexcept
    {
        if (device_manager_ != nullptr && device_manager_->logical_device() != VK_NULL_HANDLE)
        {
            (void)vkDeviceWaitIdle(device_manager_->logical_device());
        }

        if (allocator_ != VK_NULL_HANDLE)
        {
            vmaDestroyAllocator(allocator_);
            allocator_ = VK_NULL_HANDLE;
        }

        device_manager_ = nullptr;
    }

    bool ResourceManager::initialized() const noexcept
    {
        std::lock_guard lock(mutex_);
        return device_manager_ != nullptr && allocator_ != VK_NULL_HANDLE;
    }

    VmaAllocationCreateInfo ResourceManager::allocation_info(const HardwareBufferDesc& desc) const noexcept
    {
        VmaAllocationCreateInfo alloc_info {};

        switch (desc.cpu_access)
        {
        case CpuAccessMode::None:
            alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            break;
        case CpuAccessMode::Read:
            alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
        case CpuAccessMode::Write:
            alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
        case CpuAccessMode::ReadWrite:
            alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
        }

        if (desc.dedicated || desc.exportable)
        {
            alloc_info.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        }

        return alloc_info;
    }

    VkBufferCreateInfo ResourceManager::buffer_info(const HardwareBufferDesc& desc, std::vector<uint32_t>& queue_family_indices) const
    {
        VkBufferCreateInfo create_info {};
        create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        create_info.size = desc.byte_size();
        create_info.usage = to_vk_buffer_usage(desc.usage);

        queue_family_indices.clear();
        if (device_manager_ != nullptr)
        {
            const auto& queue_families = device_manager_->queue_families();
            queue_family_indices.reserve(queue_families.size());
            for (const QueueFamilyInfo& family : queue_families)
            {
                queue_family_indices.push_back(family.family_index);
            }
        }

        if (queue_family_indices.size() > 1)
        {
            create_info.sharingMode = VK_SHARING_MODE_CONCURRENT;
            create_info.queueFamilyIndexCount = static_cast<uint32_t>(queue_family_indices.size());
            create_info.pQueueFamilyIndices = queue_family_indices.data();
        }
        else
        {
            create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        return create_info;
    }

    void ResourceManager::create_allocator()
    {
        VmaVulkanFunctions vulkan_functions {};
        vulkan_functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        vulkan_functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

        VmaAllocatorCreateInfo create_info {};
        create_info.vulkanApiVersion = VK_API_VERSION_1_4;
        create_info.instance = device_manager_->instance();
        create_info.physicalDevice = device_manager_->physical_device();
        create_info.device = device_manager_->logical_device();
        create_info.pVulkanFunctions = &vulkan_functions;

#if defined(_WIN32) || defined(_WIN64)
        create_info.flags |= VMA_ALLOCATOR_CREATE_KHR_EXTERNAL_MEMORY_WIN32_BIT;
#endif

        throw_if_failed(vmaCreateAllocator(&create_info, &allocator_), "vmaCreateAllocator");
    }

    BufferWrap ResourceManager::create_buffer(const HardwareBufferDesc& desc)
    {
        std::lock_guard lock(mutex_);

        if (device_manager_ == nullptr || allocator_ == VK_NULL_HANDLE)
        {
            throw std::runtime_error("ResourceManager::create_buffer called before initialize().");
        }

        const uint64_t logical_size = desc.byte_size();
        if (logical_size == 0)
        {
            return {};
        }

        BufferWrap buffer;
        buffer.desc = desc;
        buffer.buffer_usage = to_vk_buffer_usage(desc.usage);
        buffer.device_manager = device_manager_;
        buffer.resource_manager = this;

        std::vector<uint32_t> queue_family_indices;
        VkBufferCreateInfo create_info = buffer_info(desc, queue_family_indices);

        VkExternalMemoryBufferCreateInfo external_buffer {};
        if (desc.exportable)
        {
            const VkExternalMemoryHandleTypeFlags handle_type = external_memory_handle_type();
            if (handle_type == 0)
            {
                throw std::runtime_error("Exportable HardwareBuffer is not supported on this platform.");
            }

            external_buffer.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
            external_buffer.handleTypes = handle_type;
            create_info.pNext = &external_buffer;
        }

        VmaAllocationCreateInfo alloc_info = allocation_info(desc);
        VkResult result = VK_SUCCESS;

        if (desc.dedicated || desc.exportable)
        {
            VkExportMemoryAllocateInfo export_info {};
            void* memory_next = nullptr;

            if (desc.exportable)
            {
                export_info.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
                export_info.handleTypes = external_memory_handle_type();
                memory_next = &export_info;
            }

            result = vmaCreateDedicatedBuffer(allocator_,
                                              &create_info,
                                              &alloc_info,
                                              memory_next,
                                              &buffer.buffer_handle,
                                              &buffer.buffer_alloc,
                                              &buffer.buffer_alloc_info);
        }
        else
        {
            result = vmaCreateBuffer(allocator_,
                                     &create_info,
                                     &alloc_info,
                                     &buffer.buffer_handle,
                                     &buffer.buffer_alloc,
                                     &buffer.buffer_alloc_info);
        }

        throw_if_failed(result, "vmaCreateBuffer");

        buffer.allocation_size = buffer.buffer_alloc_info.size != 0 ? static_cast<uint64_t>(buffer.buffer_alloc_info.size) : logical_size;
        buffer.imported = false;
        name_buffer(device_manager_->logical_device(), allocator_, buffer);
        return buffer;
    }

    void ResourceManager::destroy_buffer(BufferWrap& buffer) noexcept
    {
        std::lock_guard lock(mutex_);

        if (buffer.buffer_handle == VK_NULL_HANDLE)
        {
            buffer.clear_handles();
            return;
        }

        if (allocator_ != VK_NULL_HANDLE && buffer.buffer_alloc != VK_NULL_HANDLE)
        {
            if (buffer.host_imported_manual_bind)
            {
                if (device_manager_ != nullptr && device_manager_->logical_device() != VK_NULL_HANDLE)
                {
                    vkDestroyBuffer(device_manager_->logical_device(), buffer.buffer_handle, nullptr);
                }
                vmaFreeMemory(allocator_, buffer.buffer_alloc);
            }
            else
            {
                vmaDestroyBuffer(allocator_, buffer.buffer_handle, buffer.buffer_alloc);
            }
        }
        else if (device_manager_ != nullptr && device_manager_->logical_device() != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device_manager_->logical_device(), buffer.buffer_handle, nullptr);
        }

        buffer.clear_handles();
    }
}
