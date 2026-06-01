#if defined(_WIN32) || defined(_WIN64)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

#include "resource_manager.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#endif

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
#if VMA_EXTERNAL_MEMORY
            return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#else
            return 0;
#endif
#else
            return 0;
#endif
        }

        [[nodiscard]] VkExternalMemoryHandleTypeFlagBits to_vk_external_memory_handle_type(ExternalMemoryHandleType type) noexcept
        {
            switch (type)
            {
            case ExternalMemoryHandleType::OpaqueWin32:
#if defined(_WIN32) || defined(_WIN64)
                return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
                return static_cast<VkExternalMemoryHandleTypeFlagBits>(0);
#endif
            case ExternalMemoryHandleType::OpaqueFd:
#if defined(__linux__) && VMA_EXTERNAL_MEMORY
                return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#else
                return static_cast<VkExternalMemoryHandleTypeFlagBits>(0);
#endif
            case ExternalMemoryHandleType::None:
                break;
            }

            return static_cast<VkExternalMemoryHandleTypeFlagBits>(0);
        }

        void throw_if_failed(VkResult result, const char* operation)
        {
            if (result != VK_SUCCESS)
            {
                throw std::runtime_error(std::string(operation) + " failed. VkResult=" + std::to_string(static_cast<int>(result)));
            }
        }

#if defined(_WIN32) || defined(_WIN64)
        void close_win32_handle(void*& handle) noexcept
        {
            HANDLE native = static_cast<HANDLE>(handle);
            if (native != nullptr && native != INVALID_HANDLE_VALUE)
            {
                CloseHandle(native);
            }
            handle = nullptr;
        }

        [[nodiscard]] HANDLE duplicate_win32_handle(HANDLE source)
        {
            HANDLE duplicate = nullptr;
            HANDLE current_process = GetCurrentProcess();
            if (!DuplicateHandle(current_process, source, current_process, &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS))
            {
                throw std::runtime_error("DuplicateHandle failed. GetLastError=" + std::to_string(GetLastError()));
            }

            return duplicate;
        }
#endif

        void require_external_buffer_feature(VkPhysicalDevice physical_device,
                                             const VkBufferCreateInfo& create_info,
                                             VkExternalMemoryHandleTypeFlagBits handle_type,
                                             VkExternalMemoryFeatureFlagBits feature,
                                             const char* operation)
        {
            VkPhysicalDeviceExternalBufferInfo external_info {};
            external_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO;
            external_info.flags = create_info.flags;
            external_info.usage = create_info.usage;
            external_info.handleType = handle_type;

            VkExternalBufferProperties external_properties {};
            external_properties.sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES;
            vkGetPhysicalDeviceExternalBufferProperties(physical_device, &external_info, &external_properties);

            if ((external_properties.externalMemoryProperties.externalMemoryFeatures & feature) == 0)
            {
                throw std::runtime_error(std::string(operation) + " is not supported for this HardwareBuffer usage.");
            }
        }

        [[nodiscard]] uint32_t clamp_descriptor_capacity(uint32_t preferred, uint32_t limit) noexcept
        {
            if (limit == 0)
            {
                return 0;
            }

            return std::max(1u, std::min(preferred, limit));
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

        destroy_descriptors_unlocked();

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

        throw_if_failed(vmaCreateAllocator(&create_info, &allocator_), "vmaCreateAllocator");
    }

    void ResourceManager::create_storage_buffer_descriptors()
    {
        if (storage_buffer_descriptors_.set != VK_NULL_HANDLE)
        {
            return;
        }

        if (device_manager_ == nullptr || device_manager_->logical_device() == VK_NULL_HANDLE)
        {
            throw std::runtime_error("ResourceManager::store_descriptor called before initialize().");
        }

        VkPhysicalDeviceDescriptorIndexingProperties indexing_properties {};
        indexing_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;

        VkPhysicalDeviceProperties2 properties {};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties.pNext = &indexing_properties;
        vkGetPhysicalDeviceProperties2(device_manager_->physical_device(), &properties);

        VkPhysicalDeviceVulkan12Features features12 {};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

        VkPhysicalDeviceFeatures2 features {};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &features12;
        vkGetPhysicalDeviceFeatures2(device_manager_->physical_device(), &features);

        VkDescriptorBindingFlags binding_flags = 0;
        VkDescriptorSetLayoutCreateFlags layout_flags = 0;
        VkDescriptorPoolCreateFlags pool_flags = 0;
        uint32_t descriptor_limit = properties.properties.limits.maxDescriptorSetStorageBuffers;

        if (features12.descriptorBindingStorageBufferUpdateAfterBind == VK_TRUE)
        {
            binding_flags |= VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
            layout_flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
            pool_flags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
            descriptor_limit = indexing_properties.maxDescriptorSetUpdateAfterBindStorageBuffers;
        }

        constexpr uint32_t preferred_descriptor_count = 4096;
        const uint32_t descriptor_count = clamp_descriptor_capacity(preferred_descriptor_count, descriptor_limit);
        if (descriptor_count == 0)
        {
            throw std::runtime_error("Storage buffer descriptors are not supported by this Vulkan device.");
        }

        VkDescriptorSetLayoutBinding binding {};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.descriptorCount = descriptor_count;
        binding.stageFlags = VK_SHADER_STAGE_ALL;

        VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info {};
        binding_flags_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        binding_flags_info.bindingCount = 1;
        binding_flags_info.pBindingFlags = &binding_flags;

        VkDescriptorSetLayoutCreateInfo layout_info {};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.flags = layout_flags;
        layout_info.bindingCount = 1;
        layout_info.pBindings = &binding;
        layout_info.pNext = binding_flags != 0 ? &binding_flags_info : nullptr;

        throw_if_failed(vkCreateDescriptorSetLayout(device_manager_->logical_device(),
                                                    &layout_info,
                                                    nullptr,
                                                    &storage_buffer_descriptors_.layout),
                        "vkCreateDescriptorSetLayout");

        VkDescriptorPoolSize pool_size {};
        pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        pool_size.descriptorCount = descriptor_count;

        VkDescriptorPoolCreateInfo pool_info {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = pool_flags;
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;

        try
        {
            throw_if_failed(vkCreateDescriptorPool(device_manager_->logical_device(),
                                                   &pool_info,
                                                   nullptr,
                                                   &storage_buffer_descriptors_.pool),
                            "vkCreateDescriptorPool");

            VkDescriptorSetAllocateInfo alloc_info {};
            alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            alloc_info.descriptorPool = storage_buffer_descriptors_.pool;
            alloc_info.descriptorSetCount = 1;
            alloc_info.pSetLayouts = &storage_buffer_descriptors_.layout;

            throw_if_failed(vkAllocateDescriptorSets(device_manager_->logical_device(),
                                                     &alloc_info,
                                                     &storage_buffer_descriptors_.set),
                            "vkAllocateDescriptorSets");
        }
        catch (...)
        {
            destroy_descriptors_unlocked();
            throw;
        }

        storage_buffer_descriptors_.capacity = descriptor_count;
        next_storage_buffer_descriptor_ = 0;
    }

    void ResourceManager::destroy_descriptors_unlocked() noexcept
    {
        if (device_manager_ != nullptr && device_manager_->logical_device() != VK_NULL_HANDLE)
        {
            if (storage_buffer_descriptors_.pool != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(device_manager_->logical_device(), storage_buffer_descriptors_.pool, nullptr);
            }

            if (storage_buffer_descriptors_.layout != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(device_manager_->logical_device(), storage_buffer_descriptors_.layout, nullptr);
            }
        }

        storage_buffer_descriptors_ = {};
        next_storage_buffer_descriptor_ = 0;
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
            require_external_buffer_feature(device_manager_->physical_device(),
                                            create_info,
                                            static_cast<VkExternalMemoryHandleTypeFlagBits>(handle_type),
                                            VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT,
                                            "Exportable HardwareBuffer");
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

    BufferWrap ResourceManager::import_buffer(const ExternalMemoryHandle& handle, const HardwareBufferDesc& desc)
    {
        std::lock_guard lock(mutex_);

        if (device_manager_ == nullptr || allocator_ == VK_NULL_HANDLE)
        {
            throw std::runtime_error("ResourceManager::import_buffer called before initialize().");
        }

        if (!handle)
        {
            throw std::invalid_argument("ResourceManager::import_buffer requires a valid external memory handle.");
        }

        const uint64_t logical_size = desc.byte_size();
        if (logical_size == 0)
        {
            return {};
        }

        const VkExternalMemoryHandleTypeFlagBits handle_type = to_vk_external_memory_handle_type(handle.type);
        if (handle_type == 0)
        {
            throw std::runtime_error("External memory handle type is not supported on this platform.");
        }

        const uint64_t allocation_size = handle.allocation_size != 0 ? handle.allocation_size : logical_size;
        const BufferRange memory_range = handle.memory_range.resolve(allocation_size);
        if (memory_range.byte_offset != 0)
        {
            throw std::invalid_argument("HardwareBuffer external import only supports zero-offset dedicated memory.");
        }

        if (memory_range.byte_size < logical_size)
        {
            throw std::invalid_argument("HardwareBuffer external memory range is smaller than the requested buffer.");
        }

        BufferWrap buffer;
        buffer.desc = desc;
        buffer.buffer_usage = to_vk_buffer_usage(desc.usage);
        buffer.device_manager = device_manager_;
        buffer.resource_manager = this;

        std::vector<uint32_t> queue_family_indices;
        VkBufferCreateInfo create_info = buffer_info(desc, queue_family_indices);
        create_info.size = memory_range.byte_size;

        VkExternalMemoryBufferCreateInfo external_buffer {};
        external_buffer.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
        external_buffer.handleTypes = handle_type;
        create_info.pNext = &external_buffer;

        require_external_buffer_feature(device_manager_->physical_device(),
                                        create_info,
                                        handle_type,
                                        VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT,
                                        "Imported HardwareBuffer");

        VmaAllocationCreateInfo alloc_info = allocation_info(desc);
        alloc_info.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

#if defined(_WIN32) || defined(_WIN64)
        VkImportMemoryWin32HandleInfoKHR import_info {};
        import_info.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
        import_info.handleType = handle_type;
        import_info.handle = static_cast<HANDLE>(handle.handle);
        void* import_next = &import_info;
#elif defined(__linux__) && VMA_EXTERNAL_MEMORY
        VkImportMemoryFdInfoKHR import_info {};
        import_info.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
        import_info.handleType = handle_type;
        import_info.fd = handle.fd;
        void* import_next = &import_info;
#else
        void* import_next = nullptr;
#endif

        if (import_next == nullptr)
        {
            throw std::runtime_error("External memory import is not supported on this platform.");
        }

        const VkResult result = vmaCreateDedicatedBuffer(allocator_,
                                                         &create_info,
                                                         &alloc_info,
                                                         import_next,
                                                         &buffer.buffer_handle,
                                                         &buffer.buffer_alloc,
                                                         &buffer.buffer_alloc_info);
        throw_if_failed(result, "vmaCreateDedicatedBuffer(import)");

        buffer.allocation_size = allocation_size;
        buffer.imported = true;
        buffer.host_imported_manual_bind = false;
        name_buffer(device_manager_->logical_device(), allocator_, buffer);
        return buffer;
    }

    ExternalMemoryHandle ResourceManager::export_buffer(BufferWrap& buffer)
    {
        std::lock_guard lock(mutex_);

        if (device_manager_ == nullptr || allocator_ == VK_NULL_HANDLE)
        {
            throw std::runtime_error("ResourceManager::export_buffer called before initialize().");
        }

        if (!buffer.valid() || buffer.buffer_alloc == VK_NULL_HANDLE)
        {
            throw std::invalid_argument("ResourceManager::export_buffer requires a valid HardwareBuffer allocation.");
        }

        if (!buffer.desc.exportable)
        {
            throw std::logic_error("HardwareBuffer was not created with exportable=true.");
        }

        const uint64_t allocation_size = buffer.allocation_size != 0
            ? buffer.allocation_size
            : static_cast<uint64_t>(buffer.buffer_alloc_info.size);
        const BufferRange memory_range { 0, buffer.logical_size() };

#if defined(_WIN32) || defined(_WIN64)
        if (buffer.exported_win32_handle == nullptr)
        {
            VmaAllocationInfo allocation_info {};
            vmaGetAllocationInfo(allocator_, buffer.buffer_alloc, &allocation_info);

            auto get_memory_win32_handle =
                reinterpret_cast<PFN_vkGetMemoryWin32HandleKHR>(vkGetDeviceProcAddr(device_manager_->logical_device(), "vkGetMemoryWin32HandleKHR"));
            if (get_memory_win32_handle == nullptr)
            {
                throw std::runtime_error("vkGetMemoryWin32HandleKHR is not available on this Vulkan device.");
            }

            VkMemoryGetWin32HandleInfoKHR handle_info {};
            handle_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
            handle_info.memory = allocation_info.deviceMemory;
            handle_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

            HANDLE exported_handle = nullptr;
            const VkResult result = get_memory_win32_handle(device_manager_->logical_device(), &handle_info, &exported_handle);
            throw_if_failed(result, "vkGetMemoryWin32HandleKHR");
            buffer.exported_win32_handle = exported_handle;
        }

        HANDLE duplicate = duplicate_win32_handle(static_cast<HANDLE>(buffer.exported_win32_handle));
        return ExternalMemoryHandle::win32(duplicate, allocation_size, memory_range);
#elif defined(__linux__) && VMA_EXTERNAL_MEMORY
        VmaAllocationInfo allocation_info {};
        vmaGetAllocationInfo(allocator_, buffer.buffer_alloc, &allocation_info);

        VkMemoryGetFdInfoKHR fd_info {};
        fd_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
        fd_info.memory = allocation_info.deviceMemory;
        fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

        int fd = -1;
        const VkResult result = vkGetMemoryFdKHR(device_manager_->logical_device(), &fd_info, &fd);
        throw_if_failed(result, "vkGetMemoryFdKHR");
        return ExternalMemoryHandle::opaque_fd(fd, allocation_size, memory_range);
#else
        throw std::runtime_error("External memory export is not supported on this platform.");
#endif
    }

    uint32_t ResourceManager::store_descriptor(BufferWrap& buffer)
    {
        std::lock_guard lock(mutex_);

        if (device_manager_ == nullptr || allocator_ == VK_NULL_HANDLE)
        {
            throw std::runtime_error("ResourceManager::store_descriptor called before initialize().");
        }

        if (!buffer.valid())
        {
            throw std::invalid_argument("ResourceManager::store_descriptor requires a valid HardwareBuffer.");
        }

        create_storage_buffer_descriptors();

        if (buffer.bindless_index < 0)
        {
            if (next_storage_buffer_descriptor_ >= storage_buffer_descriptors_.capacity)
            {
                throw std::runtime_error("Storage buffer descriptor array is full.");
            }

            buffer.bindless_index = static_cast<std::int32_t>(next_storage_buffer_descriptor_++);
        }

        const uint32_t descriptor_index = static_cast<uint32_t>(buffer.bindless_index);
        if (descriptor_index >= storage_buffer_descriptors_.capacity)
        {
            throw std::runtime_error("HardwareBuffer descriptor index exceeds the storage buffer descriptor array.");
        }

        VkDescriptorBufferInfo buffer_info {};
        buffer_info.buffer = buffer.buffer_handle;
        buffer_info.offset = 0;
        buffer_info.range = buffer.logical_size();

        VkWriteDescriptorSet write {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = storage_buffer_descriptors_.set;
        write.dstBinding = 0;
        write.dstArrayElement = descriptor_index;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &buffer_info;

        vkUpdateDescriptorSets(device_manager_->logical_device(), 1, &write, 0, nullptr);
        return descriptor_index;
    }

    void ResourceManager::destroy_buffer(BufferWrap& buffer) noexcept
    {
        std::lock_guard lock(mutex_);

#if defined(_WIN32) || defined(_WIN64)
        if (buffer.desc.exportable)
        {
            close_win32_handle(buffer.exported_win32_handle);
        }
#endif

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
