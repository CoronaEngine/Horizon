#include "hardware_wrapper_vulkan/hardware/resource_manager.h"

#include <algorithm>
#include <array>
#include <limits>

#include "HardwareWrapperVulkan/HardwareVulkan/DeviceManager.h"

namespace Corona::Horizon::Vulkan
{
    namespace
    {
        VkBufferCreateInfo make_buffer_info(uint32_t element_count, uint32_t element_size, VkBufferUsageFlags usage)
        {
            VkBufferCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            info.size = VkDeviceSize{element_count} * VkDeviceSize{element_size};
            info.usage = usage;
            info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            return info;
        }

        VmaAllocationCreateInfo make_allocation_info(bool host_visible_mapped, bool dedicated)
        {
            VmaAllocationCreateInfo info{};
            info.usage = host_visible_mapped ? VMA_MEMORY_USAGE_AUTO : VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            if (host_visible_mapped)
            {
                info.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                              VMA_ALLOCATION_CREATE_MAPPED_BIT;
            }
            if (dedicated)
            {
                info.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
            }
            return info;
        }

        VkImageAspectFlags aspect_from_format(VkFormat format)
        {
            switch (format)
            {
            case VK_FORMAT_D16_UNORM:
            case VK_FORMAT_D32_SFLOAT:
                return VK_IMAGE_ASPECT_DEPTH_BIT;
            case VK_FORMAT_D24_UNORM_S8_UINT:
            case VK_FORMAT_D32_SFLOAT_S8_UINT:
                return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            default:
                return VK_IMAGE_ASPECT_COLOR_BIT;
            }
        }

        void mirror_buffer_aliases(ResourceManager::BufferHardwareWrap& buffer)
        {
            buffer.elementCount = buffer.element_count;
            buffer.elementSize = buffer.element_size;
            buffer.refCount = buffer.ref_count;
            buffer.bufferHandle = buffer.buffer;
            buffer.bufferUsage = buffer.usage;
            buffer.bufferAlloc = buffer.allocation;
            buffer.bufferAllocInfo = buffer.allocation_info;
            buffer.hostImportedManualBind = buffer.host_imported_manual_bind;
            buffer.bindlessIndex = buffer.bindless_index;
            buffer.resourceManager = buffer.resource_manager;
        }

        void mirror_image_aliases(ResourceManager::ImageHardwareWrap& image)
        {
            image.refCount = image.ref_count;
            image.imageHandle = image.image;
            image.imageView = image.image_view;
            image.imageLayout = image.layout;
            image.imageFormat = image.format;
            image.imageUsage = image.usage;
            image.aspectMask = image.aspect_mask;
            image.arrayLayers = image.array_layers;
            image.mipLevels = image.mip_levels;
            image.clearValue = image.clear_value;
            image.imageAlloc = image.allocation;
            image.imageAllocInfo = image.allocation_info;
            image.bindlessIndex = image.bindless_index;
            image.resourceManager = image.resource_manager;
        }

        ResourceManager::BindlessDescriptorSet& descriptor_set_pair(ResourceManager& manager, uint32_t index)
        {
            auto& primary = manager.bindless_descriptors[index];
            auto& compat = manager.bindlessDescriptors[index];
            if (primary.descriptor_set == VK_NULL_HANDLE && compat.descriptorSet != VK_NULL_HANDLE)
            {
                primary.descriptor_pool = compat.descriptorPool;
                primary.descriptorPool = compat.descriptorPool;
                primary.descriptor_set_layout = compat.descriptorSetLayout;
                primary.descriptorSetLayout = compat.descriptorSetLayout;
                primary.descriptor_set = compat.descriptorSet;
                primary.descriptorSet = compat.descriptorSet;
            }
            if (compat.descriptorSet == VK_NULL_HANDLE && primary.descriptor_set != VK_NULL_HANDLE)
            {
                compat.descriptorPool = primary.descriptor_pool;
                compat.descriptor_pool = primary.descriptor_pool;
                compat.descriptorSetLayout = primary.descriptor_set_layout;
                compat.descriptor_set_layout = primary.descriptor_set_layout;
                compat.descriptorSet = primary.descriptor_set;
                compat.descriptor_set = primary.descriptor_set;
            }
            return primary;
        }
    }

    ResourceManager::~ResourceManager()
    {
        cleanup();
    }

    void ResourceManager::init(DeviceManager& device_manager)
    {
        cleanup();
        device_ = &device_manager;
        create_allocator();
    }

    void ResourceManager::cleanup() noexcept
    {
        destroy_allocator();
        device_ = nullptr;
        next_descriptor_index_ = 0;
    }

    VkDevice ResourceManager::logical_device() const noexcept
    {
        return device_ ? device_->getLogicalDevice() : VK_NULL_HANDLE;
    }

    VkPhysicalDevice ResourceManager::physical_device() const noexcept
    {
        return device_ ? device_->getPhysicalDevice() : VK_NULL_HANDLE;
    }

    void ResourceManager::create_allocator()
    {
        if (!device_ || logical_device() == VK_NULL_HANDLE || physical_device() == VK_NULL_HANDLE)
        {
            throw std::runtime_error("ResourceManager requires an initialized DeviceManager.");
        }

        VmaAllocatorCreateInfo info{};
        info.physicalDevice = physical_device();
        info.device = logical_device();
        info.instance = VK_NULL_HANDLE;
        info.vulkanApiVersion = VK_API_VERSION_1_3;
        coronaHardwareCheck(vmaCreateAllocator(&info, &allocator_));
    }

    void ResourceManager::destroy_allocator() noexcept
    {
        if (allocator_ != VK_NULL_HANDLE)
        {
            vmaDestroyAllocator(allocator_);
            allocator_ = VK_NULL_HANDLE;
        }
    }

    ResourceManager::BufferHardwareWrap ResourceManager::create_buffer(uint32_t element_count,
                                                                       uint32_t element_size,
                                                                       VkBufferUsageFlags usage,
                                                                       bool host_visible_mapped,
                                                                       bool dedicated)
    {
        if (!initialized())
        {
            throw std::runtime_error("ResourceManager::create_buffer called before init.");
        }
        if (element_count == 0 || element_size == 0)
        {
            throw std::invalid_argument("Buffer element_count and element_size must be non-zero.");
        }

        BufferHardwareWrap buffer{};
        buffer.element_count = element_count;
        buffer.elementCount = element_count;
        buffer.element_size = element_size;
        buffer.elementSize = element_size;
        buffer.usage = usage;
        buffer.device = device_;
        buffer.resource_manager = this;

        VkBufferCreateInfo buffer_info = make_buffer_info(element_count, element_size, usage);
        VmaAllocationCreateInfo alloc_info = make_allocation_info(host_visible_mapped, dedicated);
        coronaHardwareCheck(vmaCreateBuffer(allocator_,
                                            &buffer_info,
                                            &alloc_info,
                                            &buffer.buffer,
                                            &buffer.allocation,
                                            &buffer.allocation_info));

        mirror_buffer_aliases(buffer);
        return buffer;
    }

    ResourceManager::BufferHardwareWrap ResourceManager::import_buffer_memory(const ExternalMemoryHandle&,
                                                                              uint32_t element_count,
                                                                              uint32_t element_size,
                                                                              uint32_t,
                                                                              VkBufferUsageFlags usage)
    {
        BufferHardwareWrap buffer = create_buffer(element_count, element_size, usage, true, true);
        buffer.host_imported_manual_bind = true;
        mirror_buffer_aliases(buffer);
        return buffer;
    }

    ResourceManager::ExternalMemoryHandle ResourceManager::export_buffer_memory(BufferHardwareWrap&)
    {
        return {};
    }

    void ResourceManager::destroy_buffer(BufferHardwareWrap& buffer) noexcept
    {
        if (allocator_ != VK_NULL_HANDLE && buffer.buffer != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(allocator_, buffer.buffer, buffer.allocation);
        }
        buffer = {};
    }

    ResourceManager::ImageHardwareWrap ResourceManager::create_image(uint32_t width,
                                                                     uint32_t height,
                                                                     VkFormat format,
                                                                     VkImageUsageFlags usage,
                                                                     uint32_t layers,
                                                                     uint32_t mip_levels)
    {
        if (!initialized())
        {
            throw std::runtime_error("ResourceManager::create_image called before init.");
        }

        ImageHardwareWrap image{};
        image.width = width;
        image.height = height;
        image.depth = 1;
        image.format = format;
        image.usage = usage;
        image.aspect_mask = aspect_from_format(format);
        image.array_layers = std::max(1u, layers);
        image.mip_levels = std::max(1u, mip_levels);
        image.layout = VK_IMAGE_LAYOUT_UNDEFINED;
        image.device = device_;
        image.resource_manager = this;

        VkImageCreateInfo image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.extent = {width, height, 1};
        image_info.mipLevels = image.mip_levels;
        image_info.arrayLayers = image.array_layers;
        image_info.format = format;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.usage = usage;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        coronaHardwareCheck(vmaCreateImage(allocator_,
                                           &image_info,
                                           &alloc_info,
                                           &image.image,
                                           &image.allocation,
                                           &image.allocation_info));
        mirror_image_aliases(image);
        return image;
    }

    VkImageView ResourceManager::create_image_view(ImageHardwareWrap& image, uint32_t layer, uint32_t mip_level)
    {
        if (logical_device() == VK_NULL_HANDLE || image.image == VK_NULL_HANDLE)
        {
            return VK_NULL_HANDLE;
        }

        const bool whole_image = layer == std::numeric_limits<uint32_t>::max() &&
                                 mip_level == std::numeric_limits<uint32_t>::max();

        VkImageViewCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = image.image;
        info.viewType = image.array_layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        info.format = image.format;
        info.subresourceRange.aspectMask = image.aspect_mask;
        info.subresourceRange.baseArrayLayer = whole_image ? 0u : layer;
        info.subresourceRange.layerCount = whole_image ? image.array_layers : 1u;
        info.subresourceRange.baseMipLevel = whole_image ? 0u : mip_level;
        info.subresourceRange.levelCount = whole_image ? image.mip_levels : 1u;

        VkImageView view = VK_NULL_HANDLE;
        coronaHardwareCheck(vkCreateImageView(logical_device(), &info, nullptr, &view));

        if (whole_image)
        {
            image.image_view = view;
        }
        else
        {
            const uint64_t key = uint64_t{layer} << 32u | uint64_t{mip_level};
            image.sub_views[key] = view;
            image.allSubViews[key] = view;
        }
        mirror_image_aliases(image);
        return view;
    }

    void ResourceManager::destroy_image(ImageHardwareWrap& image) noexcept
    {
        const VkDevice device = logical_device();
        if (device != VK_NULL_HANDLE)
        {
            std::unordered_set<VkImageView> destroyed;
            for (auto& [_, view] : image.sub_views)
            {
                if (view != VK_NULL_HANDLE && destroyed.insert(view).second)
                {
                    vkDestroyImageView(device, view, nullptr);
                }
            }
            for (auto& [_, view] : image.allSubViews)
            {
                if (view != VK_NULL_HANDLE && destroyed.insert(view).second)
                {
                    vkDestroyImageView(device, view, nullptr);
                }
            }
            if (image.image_view != VK_NULL_HANDLE && destroyed.insert(image.image_view).second)
            {
                vkDestroyImageView(device, image.image_view, nullptr);
            }
        }

        if (allocator_ != VK_NULL_HANDLE && image.image != VK_NULL_HANDLE)
        {
            vmaDestroyImage(allocator_, image.image, image.allocation);
        }
        image = {};
    }

    int32_t ResourceManager::store_descriptor(BufferHardwareWrap& buffer)
    {
        if (buffer.bindless_index < 0)
        {
            buffer.bindless_index = static_cast<int32_t>(next_descriptor_index_++);
            mirror_buffer_aliases(buffer);
        }
        return store_descriptor_at(buffer, static_cast<uint32_t>(buffer.bindless_index))
                   ? buffer.bindless_index
                   : -1;
    }

    int32_t ResourceManager::store_descriptor(ImageHardwareWrap& image)
    {
        if (image.bindless_index < 0)
        {
            image.bindless_index = static_cast<int32_t>(next_descriptor_index_++);
            mirror_image_aliases(image);
        }
        return store_descriptor_at(image, static_cast<uint32_t>(image.bindless_index))
                   ? image.bindless_index
                   : -1;
    }

    bool ResourceManager::store_descriptor_at(BufferHardwareWrap& buffer, uint32_t descriptor_index)
    {
        buffer.bindless_index = static_cast<int32_t>(descriptor_index);
        mirror_buffer_aliases(buffer);
        if (buffer.buffer == VK_NULL_HANDLE || logical_device() == VK_NULL_HANDLE)
        {
            return false;
        }

        auto& set = descriptor_set_pair(*this, 1u);
        if (set.descriptor_set == VK_NULL_HANDLE)
        {
            return true;
        }

        VkDescriptorBufferInfo buffer_info{};
        buffer_info.buffer = buffer.buffer;
        buffer_info.offset = 0;
        buffer_info.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set.descriptor_set;
        write.dstBinding = 0;
        write.dstArrayElement = descriptor_index;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &buffer_info;

        vkUpdateDescriptorSets(logical_device(), 1, &write, 0, nullptr);
        return true;
    }

    bool ResourceManager::store_descriptor_at(ImageHardwareWrap& image, uint32_t descriptor_index)
    {
        image.bindless_index = static_cast<int32_t>(descriptor_index);
        mirror_image_aliases(image);
        if (image.image == VK_NULL_HANDLE || logical_device() == VK_NULL_HANDLE)
        {
            return false;
        }

        if (image.image_view == VK_NULL_HANDLE)
        {
            create_image_view(image);
        }

        const bool storage_image = (image.usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0;
        auto& set = descriptor_set_pair(*this, storage_image ? 2u : 0u);
        if (set.descriptor_set == VK_NULL_HANDLE)
        {
            return true;
        }

        VkDescriptorImageInfo image_info{};
        image_info.imageLayout = storage_image ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image_info.imageView = image.image_view;
        image_info.sampler = VK_NULL_HANDLE;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set.descriptor_set;
        write.dstBinding = 0;
        write.dstArrayElement = descriptor_index;
        write.descriptorCount = 1;
        write.descriptorType = storage_image ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        write.pImageInfo = &image_info;

        vkUpdateDescriptorSets(logical_device(), 1, &write, 0, nullptr);
        return true;
    }
}
