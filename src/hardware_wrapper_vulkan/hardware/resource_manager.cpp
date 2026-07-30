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
#include <array>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#endif

#include "corona/kernel/core/i_logger.h"
#include "device_manager.h"
#include "hardware_wrapper/diagnostics.h"

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

            if (has_flag(usage, BufferUsageFlags::Indirect))
                result |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

            return result;
        }

        [[nodiscard]] VkFormat to_vk_format(Format format) noexcept
        {
            switch (format)
            {
            case Format::R8_UINT: return VK_FORMAT_R8_UINT;
            case Format::R8_SINT: return VK_FORMAT_R8_SINT;
            case Format::R8_UNORM: return VK_FORMAT_R8_UNORM;
            case Format::R8_SNORM: return VK_FORMAT_R8_SNORM;
            case Format::RG8_UINT: return VK_FORMAT_R8G8_UINT;
            case Format::RG8_SINT: return VK_FORMAT_R8G8_SINT;
            case Format::RG8_UNORM: return VK_FORMAT_R8G8_UNORM;
            case Format::RG8_SNORM: return VK_FORMAT_R8G8_SNORM;
            case Format::R16_UINT: return VK_FORMAT_R16_UINT;
            case Format::R16_SINT: return VK_FORMAT_R16_SINT;
            case Format::R16_UNORM: return VK_FORMAT_R16_UNORM;
            case Format::R16_SNORM: return VK_FORMAT_R16_SNORM;
            case Format::R16_FLOAT: return VK_FORMAT_R16_SFLOAT;
            case Format::BGRA4_UNORM: return VK_FORMAT_B4G4R4A4_UNORM_PACK16;
            case Format::B5G6R5_UNORM: return VK_FORMAT_B5G6R5_UNORM_PACK16;
            case Format::B5G5R5A1_UNORM: return VK_FORMAT_B5G5R5A1_UNORM_PACK16;
            case Format::RGBA8_UINT: return VK_FORMAT_R8G8B8A8_UINT;
            case Format::RGBA8_SINT: return VK_FORMAT_R8G8B8A8_SINT;
            case Format::RGBA8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
            case Format::RGBA8_SNORM: return VK_FORMAT_R8G8B8A8_SNORM;
            case Format::BGRA8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
            case Format::BGRX8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
            case Format::SRGBA8_UNORM: return VK_FORMAT_R8G8B8A8_SRGB;
            case Format::SBGRA8_UNORM: return VK_FORMAT_B8G8R8A8_SRGB;
            case Format::SBGRX8_UNORM: return VK_FORMAT_B8G8R8A8_SRGB;
            case Format::R10G10B10A2_UNORM: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
            case Format::R11G11B10_FLOAT: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
            case Format::RG16_UINT: return VK_FORMAT_R16G16_UINT;
            case Format::RG16_SINT: return VK_FORMAT_R16G16_SINT;
            case Format::RG16_UNORM: return VK_FORMAT_R16G16_UNORM;
            case Format::RG16_SNORM: return VK_FORMAT_R16G16_SNORM;
            case Format::RG16_FLOAT: return VK_FORMAT_R16G16_SFLOAT;
            case Format::R32_UINT: return VK_FORMAT_R32_UINT;
            case Format::R32_SINT: return VK_FORMAT_R32_SINT;
            case Format::R32_FLOAT: return VK_FORMAT_R32_SFLOAT;
            case Format::RGBA16_UINT: return VK_FORMAT_R16G16B16A16_UINT;
            case Format::RGBA16_SINT: return VK_FORMAT_R16G16B16A16_SINT;
            case Format::RGBA16_FLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
            case Format::RGBA16_UNORM: return VK_FORMAT_R16G16B16A16_UNORM;
            case Format::RGBA16_SNORM: return VK_FORMAT_R16G16B16A16_SNORM;
            case Format::RG32_UINT: return VK_FORMAT_R32G32_UINT;
            case Format::RG32_SINT: return VK_FORMAT_R32G32_SINT;
            case Format::RG32_FLOAT: return VK_FORMAT_R32G32_SFLOAT;
            case Format::RGB32_UINT: return VK_FORMAT_R32G32B32_UINT;
            case Format::RGB32_SINT: return VK_FORMAT_R32G32B32_SINT;
            case Format::RGB32_FLOAT: return VK_FORMAT_R32G32B32_SFLOAT;
            case Format::RGBA32_UINT: return VK_FORMAT_R32G32B32A32_UINT;
            case Format::RGBA32_SINT: return VK_FORMAT_R32G32B32A32_SINT;
            case Format::RGBA32_FLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
            case Format::D16: return VK_FORMAT_D16_UNORM;
            case Format::D24S8: return VK_FORMAT_D24_UNORM_S8_UINT;
            case Format::X24G8_UINT: return VK_FORMAT_S8_UINT;
            case Format::D32: return VK_FORMAT_D32_SFLOAT;
            case Format::D32S8: return VK_FORMAT_D32_SFLOAT_S8_UINT;
            case Format::X32G8_UINT: return VK_FORMAT_S8_UINT;
            case Format::BC1_UNORM: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
            case Format::BC1_UNORM_SRGB: return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
            case Format::BC2_UNORM: return VK_FORMAT_BC2_UNORM_BLOCK;
            case Format::BC2_UNORM_SRGB: return VK_FORMAT_BC2_SRGB_BLOCK;
            case Format::BC3_UNORM: return VK_FORMAT_BC3_UNORM_BLOCK;
            case Format::BC3_UNORM_SRGB: return VK_FORMAT_BC3_SRGB_BLOCK;
            case Format::BC4_UNORM: return VK_FORMAT_BC4_UNORM_BLOCK;
            case Format::BC4_SNORM: return VK_FORMAT_BC4_SNORM_BLOCK;
            case Format::BC5_UNORM: return VK_FORMAT_BC5_UNORM_BLOCK;
            case Format::BC5_SNORM: return VK_FORMAT_BC5_SNORM_BLOCK;
            case Format::BC6H_UFLOAT: return VK_FORMAT_BC6H_UFLOAT_BLOCK;
            case Format::BC6H_SFLOAT: return VK_FORMAT_BC6H_SFLOAT_BLOCK;
            case Format::BC7_UNORM: return VK_FORMAT_BC7_UNORM_BLOCK;
            case Format::BC7_UNORM_SRGB: return VK_FORMAT_BC7_SRGB_BLOCK;
            case Format::UNKNOWN:
            case Format::COUNT:
                break;
            }

            return VK_FORMAT_UNDEFINED;
        }

        [[nodiscard]] Format from_vk_format(VkFormat format) noexcept
        {
            switch (format)
            {
            case VK_FORMAT_R8G8B8A8_UNORM: return Format::RGBA8_UNORM;
            case VK_FORMAT_R8G8B8A8_SRGB: return Format::SRGBA8_UNORM;
            case VK_FORMAT_B8G8R8A8_UNORM: return Format::BGRA8_UNORM;
            case VK_FORMAT_B8G8R8A8_SRGB: return Format::SBGRA8_UNORM;
            default: break;
            }

            return Format::UNKNOWN;
        }

        [[nodiscard]] VkImageUsageFlags to_vk_image_usage(ImageUsageFlags usage) noexcept
        {
            VkImageUsageFlags result = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

            if (has_flag(usage, ImageUsageFlags::Sampled))
                result |= VK_IMAGE_USAGE_SAMPLED_BIT;

            if (has_flag(usage, ImageUsageFlags::Storage))
                result |= VK_IMAGE_USAGE_STORAGE_BIT;

            if (has_flag(usage, ImageUsageFlags::ColorAttachment))
                result |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

            if (has_flag(usage, ImageUsageFlags::DepthStencilAttachment))
                result |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

            return result;
        }

        [[nodiscard]] VkImageType to_vk_image_type(ImageDimension dimension) noexcept
        {
            switch (dimension)
            {
            case ImageDimension::Image1D:
                return VK_IMAGE_TYPE_1D;
            case ImageDimension::Image3D:
                return VK_IMAGE_TYPE_3D;
            case ImageDimension::Image2D:
            case ImageDimension::Image2DArray:
            case ImageDimension::Cube:
            case ImageDimension::CubeArray:
                return VK_IMAGE_TYPE_2D;
            }

            return VK_IMAGE_TYPE_2D;
        }

        [[nodiscard]] VkImageViewType to_vk_image_view_type(const HardwareImageDesc& desc, ImageSubresourceRange range) noexcept
        {
            const bool single_layer = range.layer_count == 1;
            switch (desc.dimension)
            {
            case ImageDimension::Image1D:
                return single_layer ? VK_IMAGE_VIEW_TYPE_1D : VK_IMAGE_VIEW_TYPE_1D_ARRAY;
            case ImageDimension::Image3D:
                return VK_IMAGE_VIEW_TYPE_3D;
            case ImageDimension::Cube:
                return single_layer ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_CUBE;
            case ImageDimension::CubeArray:
                return single_layer ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
            case ImageDimension::Image2DArray:
                return single_layer ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            case ImageDimension::Image2D:
                return single_layer ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            }

            return VK_IMAGE_VIEW_TYPE_2D;
        }

        [[nodiscard]] bool image_usage_allows_view(VkImageUsageFlags usage) noexcept
        {
            constexpr VkImageUsageFlags view_usage =
                VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_STORAGE_BIT |
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

            return (usage & view_usage) != 0;
        }

        [[nodiscard]] VkImageLayout sampled_descriptor_layout(const ImageWrap& image) noexcept
        {
            if (has_flag(image.desc.usage, ImageUsageFlags::Storage))
            {
                return VK_IMAGE_LAYOUT_GENERAL;
            }

            return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        [[nodiscard]] VkSampleCountFlagBits to_vk_sample_count(uint32_t sample_count) noexcept
        {
            switch (sample_count)
            {
            case 1: return VK_SAMPLE_COUNT_1_BIT;
            case 2: return VK_SAMPLE_COUNT_2_BIT;
            case 4: return VK_SAMPLE_COUNT_4_BIT;
            case 8: return VK_SAMPLE_COUNT_8_BIT;
            case 16: return VK_SAMPLE_COUNT_16_BIT;
            case 32: return VK_SAMPLE_COUNT_32_BIT;
            case 64: return VK_SAMPLE_COUNT_64_BIT;
            default: return VK_SAMPLE_COUNT_1_BIT;
            }
        }

        [[nodiscard]] VkImageAspectFlags aspect_mask(Format format) noexcept
        {
            switch (format)
            {
            case Format::D16:
            case Format::D32:
                return VK_IMAGE_ASPECT_DEPTH_BIT;
            case Format::D24S8:
            case Format::D32S8:
                return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            case Format::X24G8_UINT:
            case Format::X32G8_UINT:
                return VK_IMAGE_ASPECT_STENCIL_BIT;
            default:
                return VK_IMAGE_ASPECT_COLOR_BIT;
            }
        }

        [[nodiscard]] ImageSubresourceRange resolve_range(ImageSubresourceRange range, const HardwareImageDesc& desc) noexcept
        {
            if (range.layer_count == ImageSubresourceRange::remaining)
                range.layer_count = range.base_layer < desc.array_layers ? desc.array_layers - range.base_layer : 0;

            if (range.mip_count == ImageSubresourceRange::remaining)
                range.mip_count = range.base_mip < desc.mip_levels ? desc.mip_levels - range.base_mip : 0;

            return range;
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
                Diagnostics::write(Diagnostics::Level::Error,
                                   "VK_ERROR",
                                   std::string(operation) + " failed. VkResult=" + std::to_string(static_cast<int>(result)));
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

        void require_external_image_feature(VkPhysicalDevice physical_device,
                                            const VkImageCreateInfo& create_info,
                                            VkExternalMemoryHandleTypeFlagBits handle_type,
                                            VkExternalMemoryFeatureFlagBits feature,
                                            const char* operation)
        {
            VkPhysicalDeviceExternalImageFormatInfo external_info {};
            external_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
            external_info.handleType = handle_type;

            VkPhysicalDeviceImageFormatInfo2 format_info {};
            format_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
            format_info.pNext = &external_info;
            format_info.format = create_info.format;
            format_info.type = create_info.imageType;
            format_info.tiling = create_info.tiling;
            format_info.usage = create_info.usage;
            format_info.flags = create_info.flags;

            VkExternalImageFormatProperties external_properties {};
            external_properties.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;

            VkImageFormatProperties2 image_properties {};
            image_properties.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
            image_properties.pNext = &external_properties;

            const VkResult result = vkGetPhysicalDeviceImageFormatProperties2(physical_device, &format_info, &image_properties);
            throw_if_failed(result, operation);

            if ((external_properties.externalMemoryProperties.externalMemoryFeatures & feature) == 0)
            {
                throw std::runtime_error(std::string(operation) + " is not supported for this HardwareImage usage.");
            }
        }

        [[nodiscard]] std::string image_create_context(const VkImageCreateInfo& create_info, const HardwareImageDesc& desc)
        {
            std::ostringstream message;
            message << "format=" << static_cast<int>(create_info.format)
                    << ", type=" << static_cast<int>(create_info.imageType)
                    << ", tiling=" << static_cast<int>(create_info.tiling)
                    << ", usage=0x" << std::hex << create_info.usage << std::dec
                    << ", extent=" << create_info.extent.width << "x" << create_info.extent.height << "x" << create_info.extent.depth
                    << ", mips=" << create_info.mipLevels
                    << ", layers=" << create_info.arrayLayers
                    << ", samples=" << static_cast<int>(create_info.samples);

            if (!desc.debug_name.empty())
            {
                message << ", name=\"" << desc.debug_name << "\"";
            }

            return message.str();
        }

        void require_image_format_support(VkPhysicalDevice physical_device,
                                          const VkImageCreateInfo& create_info,
                                          const HardwareImageDesc& desc,
                                          const char* operation)
        {
            VkPhysicalDeviceImageFormatInfo2 format_info {};
            format_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
            format_info.format = create_info.format;
            format_info.type = create_info.imageType;
            format_info.tiling = create_info.tiling;
            format_info.usage = create_info.usage;
            format_info.flags = create_info.flags;

            VkImageFormatProperties2 image_properties {};
            image_properties.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;

            const VkResult result = vkGetPhysicalDeviceImageFormatProperties2(physical_device, &format_info, &image_properties);
            if (result == VK_ERROR_FORMAT_NOT_SUPPORTED)
            {
                throw std::runtime_error(std::string(operation) + " is not supported by this Vulkan device (" +
                                         image_create_context(create_info, desc) + ").");
            }
            throw_if_failed(result, operation);

            const VkImageFormatProperties& limits = image_properties.imageFormatProperties;
            if (create_info.extent.width > limits.maxExtent.width ||
                create_info.extent.height > limits.maxExtent.height ||
                create_info.extent.depth > limits.maxExtent.depth)
            {
                throw std::runtime_error(std::string(operation) + " extent exceeds this Vulkan device limit (" +
                                         image_create_context(create_info, desc) + ").");
            }
            if (create_info.mipLevels > limits.maxMipLevels)
            {
                throw std::runtime_error(std::string(operation) + " mip level count exceeds this Vulkan device limit (" +
                                         image_create_context(create_info, desc) + ").");
            }
            if (create_info.arrayLayers > limits.maxArrayLayers)
            {
                throw std::runtime_error(std::string(operation) + " array layer count exceeds this Vulkan device limit (" +
                                         image_create_context(create_info, desc) + ").");
            }
            if ((limits.sampleCounts & create_info.samples) == 0)
            {
                throw std::runtime_error(std::string(operation) + " sample count is not supported by this Vulkan device (" +
                                         image_create_context(create_info, desc) + ").");
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

        void name_vulkan_object(VkDevice device, VkObjectType object_type, uint64_t object_handle, const std::string& name) noexcept
        {
            if (vkSetDebugUtilsObjectNameEXT == nullptr || object_handle == 0 || name.empty())
            {
                return;
            }

            VkDebugUtilsObjectNameInfoEXT name_info {};
            name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            name_info.objectType = object_type;
            name_info.objectHandle = object_handle;
            name_info.pObjectName = name.c_str();
            (void)vkSetDebugUtilsObjectNameEXT(device, &name_info);
        }

        void name_image(VkDevice device, VmaAllocator allocator, const ImageWrap& image) noexcept
        {
            if (image.desc.debug_name.empty())
            {
                return;
            }

            if (image.image_alloc != VK_NULL_HANDLE)
            {
                vmaSetAllocationName(allocator, image.image_alloc, image.desc.debug_name.c_str());
            }

            if (vkSetDebugUtilsObjectNameEXT == nullptr || image.image_handle == VK_NULL_HANDLE)
            {
                return;
            }

            VkDebugUtilsObjectNameInfoEXT name_info {};
            name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            name_info.objectType = VK_OBJECT_TYPE_IMAGE;
            name_info.objectHandle = reinterpret_cast<uint64_t>(image.image_handle);
            name_info.pObjectName = image.desc.debug_name.c_str();
            (void)vkSetDebugUtilsObjectNameEXT(device, &name_info);

            if (image.image_view != VK_NULL_HANDLE)
            {
                name_vulkan_object(device,
                                   VK_OBJECT_TYPE_IMAGE_VIEW,
                                   reinterpret_cast<uint64_t>(image.image_view),
                                   image.desc.debug_name + ".view");
            }
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

    VmaAllocationCreateInfo ResourceManager::allocation_info(const HardwareImageDesc& desc) const noexcept
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

    VkImageCreateInfo ResourceManager::image_info(const HardwareImageDesc& desc) const
    {
        VkImageCreateInfo create_info {};
        create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        create_info.flags = 0;
        create_info.imageType = to_vk_image_type(desc.dimension);
        create_info.format = to_vk_format(desc.format);
        create_info.extent = { desc.extent.width, desc.extent.height, desc.extent.depth };
        create_info.mipLevels = desc.mip_levels;
        create_info.arrayLayers = desc.array_layers;
        create_info.samples = to_vk_sample_count(desc.sample_count);
        create_info.tiling = desc.cpu_access == CpuAccessMode::None ? VK_IMAGE_TILING_OPTIMAL : VK_IMAGE_TILING_LINEAR;
        create_info.usage = to_vk_image_usage(desc.usage);
        create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (desc.dimension == ImageDimension::Cube || desc.dimension == ImageDimension::CubeArray)
        {
            create_info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        }

        return create_info;
    }

    VkImageView ResourceManager::create_image_view(const ImageWrap& image, ImageSubresourceRange range) const
    {
        if (device_manager_ == nullptr || device_manager_->logical_device() == VK_NULL_HANDLE || image.image_handle == VK_NULL_HANDLE ||
            !image_usage_allows_view(image.image_usage))
        {
            return VK_NULL_HANDLE;
        }

        range = resolve_range(range, image.desc);
        if (range.layer_count == 0 || range.mip_count == 0)
        {
            return VK_NULL_HANDLE;
        }

        VkImageViewCreateInfo view_info {};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = image.image_handle;
        view_info.viewType = to_vk_image_view_type(image.desc, range);
        view_info.format = image.image_format;
        view_info.subresourceRange.aspectMask = image.aspect_mask;
        view_info.subresourceRange.baseArrayLayer = range.base_layer;
        view_info.subresourceRange.layerCount = range.layer_count;
        view_info.subresourceRange.baseMipLevel = range.base_mip;
        view_info.subresourceRange.levelCount = range.mip_count;

        VkImageView view = VK_NULL_HANDLE;
        throw_if_failed(vkCreateImageView(device_manager_->logical_device(), &view_info, nullptr, &view), "vkCreateImageView");
        return view;
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

    std::uint64_t ResourceManager::device_local_memory_size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (allocator_ == VK_NULL_HANDLE)
        {
            return 0;
        }

        const VkPhysicalDeviceMemoryProperties* mem_props = nullptr;
        vmaGetMemoryProperties(allocator_, &mem_props);
        if (mem_props == nullptr)
        {
            return 0;
        }

        std::uint64_t total = 0;
        for (uint32_t heap = 0; heap < mem_props->memoryHeapCount; ++heap)
        {
            if ((mem_props->memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
            {
                total += mem_props->memoryHeaps[heap].size;
            }
        }
        return total;
    }

    void ResourceManager::create_default_sampler()
    {
        if (default_sampler_ != VK_NULL_HANDLE)
        {
            return;
        }

        if (device_manager_ == nullptr || device_manager_->logical_device() == VK_NULL_HANDLE)
        {
            throw std::runtime_error("ResourceManager cannot create bindless sampler before initialize().");
        }

        VkSamplerCreateInfo sampler_info {};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.mipLodBias = 0.0f;
        sampler_info.minLod = 0.0f;
        sampler_info.maxLod = VK_LOD_CLAMP_NONE;
        VkPhysicalDeviceFeatures features {};
        vkGetPhysicalDeviceFeatures(device_manager_->physical_device(), &features);
        if (features.samplerAnisotropy == VK_TRUE)
        {
            sampler_info.anisotropyEnable = VK_TRUE;
            sampler_info.maxAnisotropy = device_manager_->properties().properties.limits.maxSamplerAnisotropy;
        }
        else
        {
            sampler_info.anisotropyEnable = VK_FALSE;
            sampler_info.maxAnisotropy = 1.0f;
        }
        sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        sampler_info.unnormalizedCoordinates = VK_FALSE;
        sampler_info.compareEnable = VK_FALSE;
        sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;

        throw_if_failed(vkCreateSampler(device_manager_->logical_device(),
                                        &sampler_info,
                                        nullptr,
                                        &default_sampler_),
                        "vkCreateSampler(bindless default)");
    }

    VkSampler ResourceManager::default_sampler()
    {
        std::lock_guard lock(mutex_);

        if (device_manager_ == nullptr || allocator_ == VK_NULL_HANDLE)
        {
            throw std::runtime_error("ResourceManager::default_sampler called before initialize().");
        }

        create_default_sampler();
        return default_sampler_;
    }

    void ResourceManager::create_bindless_descriptor_array(DescriptorArray& descriptors,
                                                           VkDescriptorType descriptor_type,
                                                           uint32_t descriptor_limit,
                                                           bool update_after_bind,
                                                           const char* label)
    {
        if (descriptors.set != VK_NULL_HANDLE)
        {
            return;
        }

        if (device_manager_ == nullptr || device_manager_->logical_device() == VK_NULL_HANDLE)
        {
            throw std::runtime_error("ResourceManager cannot create bindless descriptors before initialize().");
        }

        if (!update_after_bind)
        {
            throw std::runtime_error(std::string(label) + " bindless descriptors require update-after-bind support.");
        }

        constexpr uint32_t preferred_descriptor_count = 4096;
        const uint32_t descriptor_count = clamp_descriptor_capacity(preferred_descriptor_count, descriptor_limit);
        if (descriptor_count == 0)
        {
            throw std::runtime_error(std::string(label) + " bindless descriptors are not supported by this Vulkan device.");
        }

        constexpr VkDescriptorBindingFlags binding_flags =
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
            VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
            VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

        VkDescriptorSetLayoutBinding binding {};
        binding.binding = 0;
        binding.descriptorType = descriptor_type;
        binding.descriptorCount = descriptor_count;
        binding.stageFlags = VK_SHADER_STAGE_ALL;

        VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info {};
        binding_flags_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        binding_flags_info.bindingCount = 1;
        binding_flags_info.pBindingFlags = &binding_flags;

        VkDescriptorSetLayoutCreateInfo layout_info {};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        layout_info.bindingCount = 1;
        layout_info.pBindings = &binding;
        layout_info.pNext = &binding_flags_info;

        throw_if_failed(vkCreateDescriptorSetLayout(device_manager_->logical_device(),
                                                     &layout_info,
                                                     nullptr,
                                                     &descriptors.layout),
                        (std::string("vkCreateDescriptorSetLayout(") + label + ")").c_str());

        VkDescriptorPoolSize pool_size {};
        pool_size.type = descriptor_type;
        pool_size.descriptorCount = descriptor_count;

        VkDescriptorPoolCreateInfo pool_info {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;

        try
        {
            throw_if_failed(vkCreateDescriptorPool(device_manager_->logical_device(),
                                                   &pool_info,
                                                   nullptr,
                                                   &descriptors.pool),
                            (std::string("vkCreateDescriptorPool(") + label + ")").c_str());

            VkDescriptorSetVariableDescriptorCountAllocateInfo variable_count_info {};
            variable_count_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
            variable_count_info.descriptorSetCount = 1;
            variable_count_info.pDescriptorCounts = &descriptor_count;

            VkDescriptorSetAllocateInfo alloc_info {};
            alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            alloc_info.pNext = &variable_count_info;
            alloc_info.descriptorPool = descriptors.pool;
            alloc_info.descriptorSetCount = 1;
            alloc_info.pSetLayouts = &descriptors.layout;

            throw_if_failed(vkAllocateDescriptorSets(device_manager_->logical_device(),
                                                      &alloc_info,
                                                     &descriptors.set),
                            (std::string("vkAllocateDescriptorSets(") + label + ")").c_str());

            const std::string name_prefix = std::string("horizon.bindless.") + label;
            name_vulkan_object(device_manager_->logical_device(),
                               VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                               reinterpret_cast<uint64_t>(descriptors.layout),
                               name_prefix + ".layout");
            name_vulkan_object(device_manager_->logical_device(),
                               VK_OBJECT_TYPE_DESCRIPTOR_POOL,
                               reinterpret_cast<uint64_t>(descriptors.pool),
                               name_prefix + ".pool");
            name_vulkan_object(device_manager_->logical_device(),
                               VK_OBJECT_TYPE_DESCRIPTOR_SET,
                               reinterpret_cast<uint64_t>(descriptors.set),
                               name_prefix + ".set");
        }
        catch (...)
        {
            destroy_descriptors_unlocked();
            throw;
        }

        descriptors.capacity = descriptor_count;
    }

    void ResourceManager::create_combined_texture_descriptors()
    {
        if (combined_texture_descriptors_.set != VK_NULL_HANDLE)
        {
            return;
        }

        if (device_manager_ == nullptr || device_manager_->logical_device() == VK_NULL_HANDLE)
        {
            throw std::runtime_error("ResourceManager::store_descriptor called before initialize().");
        }

        create_default_sampler();

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

        if (features12.descriptorIndexing != VK_TRUE ||
            features12.runtimeDescriptorArray != VK_TRUE ||
            features12.descriptorBindingPartiallyBound != VK_TRUE ||
            features12.descriptorBindingVariableDescriptorCount != VK_TRUE ||
            features12.shaderSampledImageArrayNonUniformIndexing != VK_TRUE)
        {
            throw std::runtime_error("Combined texture bindless descriptors require descriptor indexing, runtime arrays, partial binding, variable descriptor counts, and sampled-image non-uniform indexing.");
        }

        const uint32_t descriptor_limit = std::min({
            indexing_properties.maxUpdateAfterBindDescriptorsInAllPools / bindless_descriptor_set_count,
            indexing_properties.maxPerStageUpdateAfterBindResources / bindless_descriptor_set_count,
            indexing_properties.maxPerStageDescriptorUpdateAfterBindSamplers,
            indexing_properties.maxPerStageDescriptorUpdateAfterBindSampledImages,
            indexing_properties.maxDescriptorSetUpdateAfterBindSamplers,
            indexing_properties.maxDescriptorSetUpdateAfterBindSampledImages,
        });

        create_bindless_descriptor_array(combined_texture_descriptors_,
                                         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                         descriptor_limit,
                                         features12.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE,
                                         "combined texture");
        combined_texture_descriptors_.reset_allocation();
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

        if (features12.descriptorIndexing != VK_TRUE ||
            features12.runtimeDescriptorArray != VK_TRUE ||
            features12.descriptorBindingPartiallyBound != VK_TRUE ||
            features12.descriptorBindingVariableDescriptorCount != VK_TRUE ||
            features12.shaderStorageBufferArrayNonUniformIndexing != VK_TRUE)
        {
            throw std::runtime_error("Storage buffer bindless descriptors require descriptor indexing, runtime arrays, partial binding, variable descriptor counts, and storage-buffer non-uniform indexing.");
        }

        const uint32_t descriptor_limit = std::min({
            indexing_properties.maxUpdateAfterBindDescriptorsInAllPools / bindless_descriptor_set_count,
            indexing_properties.maxPerStageUpdateAfterBindResources / bindless_descriptor_set_count,
            indexing_properties.maxPerStageDescriptorUpdateAfterBindStorageBuffers,
            indexing_properties.maxDescriptorSetUpdateAfterBindStorageBuffers,
        });

        create_bindless_descriptor_array(storage_buffer_descriptors_,
                                         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                         descriptor_limit,
                                         features12.descriptorBindingStorageBufferUpdateAfterBind == VK_TRUE,
                                         "storage buffer");
        storage_buffer_descriptors_.reset_allocation();
    }

    void ResourceManager::create_storage_image_descriptors()
    {
        if (storage_image_descriptors_.set != VK_NULL_HANDLE)
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

        if (features12.descriptorIndexing != VK_TRUE ||
            features12.runtimeDescriptorArray != VK_TRUE ||
            features12.descriptorBindingPartiallyBound != VK_TRUE ||
            features12.descriptorBindingVariableDescriptorCount != VK_TRUE ||
            features12.shaderStorageImageArrayNonUniformIndexing != VK_TRUE)
        {
            throw std::runtime_error("Storage image bindless descriptors require descriptor indexing, runtime arrays, partial binding, variable descriptor counts, and storage-image non-uniform indexing.");
        }

        const uint32_t descriptor_limit = std::min({
            indexing_properties.maxUpdateAfterBindDescriptorsInAllPools / bindless_descriptor_set_count,
            indexing_properties.maxPerStageUpdateAfterBindResources / bindless_descriptor_set_count,
            indexing_properties.maxPerStageDescriptorUpdateAfterBindStorageImages,
            indexing_properties.maxDescriptorSetUpdateAfterBindStorageImages,
        });

        create_bindless_descriptor_array(storage_image_descriptors_,
                                         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                         descriptor_limit,
                                         features12.descriptorBindingStorageImageUpdateAfterBind == VK_TRUE,
                                         "storage image");
        storage_image_descriptors_.reset_allocation();
    }

    void ResourceManager::ensure_bindless_descriptors_unlocked()
    {
        create_combined_texture_descriptors();
        create_storage_buffer_descriptors();
        create_storage_image_descriptors();
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

            if (combined_texture_descriptors_.pool != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(device_manager_->logical_device(), combined_texture_descriptors_.pool, nullptr);
            }

            if (combined_texture_descriptors_.layout != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(device_manager_->logical_device(), combined_texture_descriptors_.layout, nullptr);
            }

            if (storage_image_descriptors_.pool != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(device_manager_->logical_device(), storage_image_descriptors_.pool, nullptr);
            }

            if (storage_image_descriptors_.layout != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(device_manager_->logical_device(), storage_image_descriptors_.layout, nullptr);
            }

            if (default_sampler_ != VK_NULL_HANDLE)
            {
                vkDestroySampler(device_manager_->logical_device(), default_sampler_, nullptr);
            }
        }

        default_sampler_ = VK_NULL_HANDLE;
        // 整体重置会一并清空 next 游标与 free_list。
        combined_texture_descriptors_ = {};
        storage_buffer_descriptors_ = {};
        storage_image_descriptors_ = {};
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
            const uint32_t slot = storage_buffer_descriptors_.allocate();
            if (slot == DescriptorArray::invalid_slot)
            {
                throw std::runtime_error("Storage buffer descriptor array is full.");
            }

            buffer.bindless_index = static_cast<std::int32_t>(slot);
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

    void ResourceManager::flush_buffer(const BufferWrap& buffer, uint64_t byte_offset, uint64_t byte_size)
    {
        if (!buffer.valid() || buffer.buffer_alloc == VK_NULL_HANDLE || byte_size == 0)
            return;

        if (byte_offset > std::numeric_limits<VkDeviceSize>::max() ||
            byte_size > std::numeric_limits<VkDeviceSize>::max() - byte_offset)
        {
            throw std::overflow_error("HardwareBuffer flush range exceeds VkDeviceSize.");
        }

        std::lock_guard lock(mutex_);
        if (allocator_ == VK_NULL_HANDLE)
            return;

        throw_if_failed(vmaFlushAllocation(allocator_,
                                           buffer.buffer_alloc,
                                           static_cast<VkDeviceSize>(byte_offset),
                                           static_cast<VkDeviceSize>(byte_size)),
                        "vmaFlushAllocation");
    }

    void ResourceManager::invalidate_buffer(const BufferWrap& buffer, uint64_t byte_offset, uint64_t byte_size)
    {
        if (!buffer.valid() || buffer.buffer_alloc == VK_NULL_HANDLE || byte_size == 0)
            return;

        if (byte_offset > std::numeric_limits<VkDeviceSize>::max() ||
            byte_size > std::numeric_limits<VkDeviceSize>::max() - byte_offset)
        {
            throw std::overflow_error("HardwareBuffer invalidate range exceeds VkDeviceSize.");
        }

        std::lock_guard lock(mutex_);
        if (allocator_ == VK_NULL_HANDLE)
            return;

        throw_if_failed(vmaInvalidateAllocation(allocator_,
                                                buffer.buffer_alloc,
                                                static_cast<VkDeviceSize>(byte_offset),
                                                static_cast<VkDeviceSize>(byte_size)),
                        "vmaInvalidateAllocation");
    }

    void ResourceManager::flush_image(const ImageWrap& image, uint64_t byte_offset, uint64_t byte_size)
    {
        if (!image.valid() || image.image_alloc == VK_NULL_HANDLE || byte_size == 0)
            return;

        if (byte_offset > std::numeric_limits<VkDeviceSize>::max() ||
            byte_size > std::numeric_limits<VkDeviceSize>::max() - byte_offset)
        {
            throw std::overflow_error("HardwareImage flush range exceeds VkDeviceSize.");
        }

        std::lock_guard lock(mutex_);
        if (allocator_ == VK_NULL_HANDLE)
            return;

        throw_if_failed(vmaFlushAllocation(allocator_,
                                           image.image_alloc,
                                           static_cast<VkDeviceSize>(byte_offset),
                                           static_cast<VkDeviceSize>(byte_size)),
                        "vmaFlushAllocation");
    }

    void ResourceManager::invalidate_image(const ImageWrap& image, uint64_t byte_offset, uint64_t byte_size)
    {
        if (!image.valid() || image.image_alloc == VK_NULL_HANDLE || byte_size == 0)
            return;

        if (byte_offset > std::numeric_limits<VkDeviceSize>::max() ||
            byte_size > std::numeric_limits<VkDeviceSize>::max() - byte_offset)
        {
            throw std::overflow_error("HardwareImage invalidate range exceeds VkDeviceSize.");
        }

        std::lock_guard lock(mutex_);
        if (allocator_ == VK_NULL_HANDLE)
            return;

        throw_if_failed(vmaInvalidateAllocation(allocator_,
                                                image.image_alloc,
                                                static_cast<VkDeviceSize>(byte_offset),
                                                static_cast<VkDeviceSize>(byte_size)),
                        "vmaInvalidateAllocation");
    }

    ImageWrap ResourceManager::create_image(const HardwareImageDesc& desc)
    {
        std::lock_guard lock(mutex_);

        if (device_manager_ == nullptr || allocator_ == VK_NULL_HANDLE)
        {
            throw std::runtime_error("ResourceManager::create_image called before initialize().");
        }

        if (desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.depth == 0 ||
            desc.array_layers == 0 || desc.mip_levels == 0 || to_vk_format(desc.format) == VK_FORMAT_UNDEFINED)
        {
            return {};
        }

        ImageWrap image;
        image.desc = desc;
        image.range = ImageSubresourceRange::whole();
        image.image_usage = to_vk_image_usage(desc.usage);
        image.image_format = to_vk_format(desc.format);
        image.aspect_mask = aspect_mask(desc.format);
        image.device_manager = device_manager_;
        image.resource_manager = this;

        VkImageCreateInfo create_info = image_info(desc);

        VkExternalMemoryImageCreateInfo external_image {};
        if (desc.exportable)
        {
            const VkExternalMemoryHandleTypeFlags handle_type = external_memory_handle_type();
            if (handle_type == 0)
            {
                throw std::runtime_error("Exportable HardwareImage is not supported on this platform.");
            }

            external_image.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
            external_image.handleTypes = handle_type;
            create_info.pNext = &external_image;
            require_external_image_feature(device_manager_->physical_device(),
                                           create_info,
                                           static_cast<VkExternalMemoryHandleTypeFlagBits>(handle_type),
                                           VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT,
                                           "Exportable HardwareImage");
        }

        require_image_format_support(device_manager_->physical_device(), create_info, desc, "HardwareImage");

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

            result = vmaCreateDedicatedImage(allocator_,
                                             &create_info,
                                             &alloc_info,
                                             memory_next,
                                             &image.image_handle,
                                             &image.image_alloc,
                                             &image.image_alloc_info);
        }
        else
        {
            result = vmaCreateImage(allocator_,
                                    &create_info,
                                    &alloc_info,
                                    &image.image_handle,
                                    &image.image_alloc,
                                    &image.image_alloc_info);
        }

        throw_if_failed(result, "vmaCreateImage");

        try
        {
            image.allocation_size = image.image_alloc_info.size != 0 ? static_cast<uint64_t>(image.image_alloc_info.size) : 0;
            image.imported = false;
            image.owns_native_image = true;
            image.image_view = create_image_view(image, ImageSubresourceRange::whole());
            name_image(device_manager_->logical_device(), allocator_, image);
        }
        catch (...)
        {
            if (image.image_view != VK_NULL_HANDLE)
            {
                vkDestroyImageView(device_manager_->logical_device(), image.image_view, nullptr);
            }
            if (image.image_handle != VK_NULL_HANDLE && image.image_alloc != VK_NULL_HANDLE)
            {
                vmaDestroyImage(allocator_, image.image_handle, image.image_alloc);
            }
            image.clear_handles();
            throw;
        }

        return image;
    }

    ImageWrap ResourceManager::import_image(const ExternalMemoryHandle& handle, const HardwareImageDesc& desc, uint64_t allocation_size)
    {
        std::lock_guard lock(mutex_);

        if (device_manager_ == nullptr || allocator_ == VK_NULL_HANDLE)
        {
            throw std::runtime_error("ResourceManager::import_image called before initialize().");
        }

        if (!handle)
        {
            throw std::invalid_argument("ResourceManager::import_image requires a valid external memory handle.");
        }

        if (desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.depth == 0 ||
            desc.array_layers == 0 || desc.mip_levels == 0 || to_vk_format(desc.format) == VK_FORMAT_UNDEFINED)
        {
            return {};
        }

        const VkExternalMemoryHandleTypeFlagBits handle_type = to_vk_external_memory_handle_type(handle.type);
        if (handle_type == 0)
        {
            throw std::runtime_error("External image memory handle type is not supported on this platform.");
        }

        const uint64_t imported_allocation_size = allocation_size != 0
            ? allocation_size
            : handle.allocation_size;
        const BufferRange memory_range = handle.memory_range.resolve(imported_allocation_size);
        if (memory_range.byte_offset != 0)
        {
            throw std::invalid_argument("HardwareImage external import only supports zero-offset dedicated memory.");
        }

        ImageWrap image;
        image.desc = desc;
        image.range = ImageSubresourceRange::whole();
        image.image_usage = to_vk_image_usage(desc.usage);
        image.image_format = to_vk_format(desc.format);
        image.aspect_mask = aspect_mask(desc.format);
        image.device_manager = device_manager_;
        image.resource_manager = this;

        VkImageCreateInfo create_info = image_info(desc);

        VkExternalMemoryImageCreateInfo external_image {};
        external_image.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        external_image.handleTypes = handle_type;
        create_info.pNext = &external_image;

        require_external_image_feature(device_manager_->physical_device(),
                                       create_info,
                                       handle_type,
                                       VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT,
                                       "Imported HardwareImage");
        require_image_format_support(device_manager_->physical_device(), create_info, desc, "Imported HardwareImage");

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
            throw std::runtime_error("External image memory import is not supported on this platform.");
        }

        const VkResult result = vmaCreateDedicatedImage(allocator_,
                                                        &create_info,
                                                        &alloc_info,
                                                        import_next,
                                                        &image.image_handle,
                                                        &image.image_alloc,
                                                        &image.image_alloc_info);
        throw_if_failed(result, "vmaCreateDedicatedImage(import)");

        try
        {
            image.allocation_size = imported_allocation_size != 0
                ? imported_allocation_size
                : static_cast<uint64_t>(image.image_alloc_info.size);
            image.imported = true;
            image.owns_native_image = true;
            image.image_view = create_image_view(image, ImageSubresourceRange::whole());
            name_image(device_manager_->logical_device(), allocator_, image);
        }
        catch (...)
        {
            if (image.image_view != VK_NULL_HANDLE)
            {
                vkDestroyImageView(device_manager_->logical_device(), image.image_view, nullptr);
            }
            if (image.image_handle != VK_NULL_HANDLE && image.image_alloc != VK_NULL_HANDLE)
            {
                vmaDestroyImage(allocator_, image.image_handle, image.image_alloc);
            }
            image.clear_handles();
            throw;
        }

        return image;
    }

    ImageWrap ResourceManager::wrap_swapchain_image(VkImage native_image,
                                                    VkFormat format,
                                                    VkExtent2D extent,
                                                    VkImageUsageFlags usage,
                                                    std::string debug_name)
    {
        std::lock_guard lock(mutex_);

        if (device_manager_ == nullptr || device_manager_->logical_device() == VK_NULL_HANDLE)
        {
            throw std::runtime_error("ResourceManager::wrap_swapchain_image called before initialize().");
        }

        if (native_image == VK_NULL_HANDLE || format == VK_FORMAT_UNDEFINED || extent.width == 0 || extent.height == 0)
        {
            return {};
        }

        ImageWrap image;
        image.desc = HardwareImageDesc::texture_2d(extent.width,
                                                   extent.height,
                                                   from_vk_format(format),
                                                   ImageUsageFlags::ColorAttachment | ImageUsageFlags::TransferDst,
                                                   std::move(debug_name));
        image.range = ImageSubresourceRange::whole();
        image.image_handle = native_image;
        image.image_usage = usage;
        image.image_format = format;
        image.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
        image.device_manager = device_manager_;
        image.resource_manager = this;
        image.owns_native_image = false;
        image.imported = false;

        try
        {
            image.image_view = create_image_view(image, ImageSubresourceRange::whole());
            name_image(device_manager_->logical_device(), allocator_, image);
        }
        catch (...)
        {
            if (image.image_view != VK_NULL_HANDLE)
            {
                vkDestroyImageView(device_manager_->logical_device(), image.image_view, nullptr);
            }
            image.clear_handles();
            throw;
        }

        return image;
    }

    ExternalMemoryHandle ResourceManager::export_image(ImageWrap& image)
    {
        std::lock_guard lock(mutex_);

        if (device_manager_ == nullptr || allocator_ == VK_NULL_HANDLE)
        {
            throw std::runtime_error("ResourceManager::export_image called before initialize().");
        }

        if (!image.valid() || image.image_alloc == VK_NULL_HANDLE)
        {
            throw std::invalid_argument("ResourceManager::export_image requires a valid HardwareImage allocation.");
        }

        if (!image.desc.exportable)
        {
            throw std::logic_error("HardwareImage was not created with exportable=true.");
        }

        const uint64_t allocation_size = image.allocation_size != 0
            ? image.allocation_size
            : static_cast<uint64_t>(image.image_alloc_info.size);
        const BufferRange memory_range { 0, allocation_size };

#if defined(_WIN32) || defined(_WIN64)
        if (image.exported_win32_handle == nullptr)
        {
            VmaAllocationInfo allocation_info {};
            vmaGetAllocationInfo(allocator_, image.image_alloc, &allocation_info);

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
            image.exported_win32_handle = exported_handle;
        }

        HANDLE duplicate = duplicate_win32_handle(static_cast<HANDLE>(image.exported_win32_handle));
        return ExternalMemoryHandle::win32(duplicate, allocation_size, memory_range);
#elif defined(__linux__) && VMA_EXTERNAL_MEMORY
        VmaAllocationInfo allocation_info {};
        vmaGetAllocationInfo(allocator_, image.image_alloc, &allocation_info);

        VkMemoryGetFdInfoKHR fd_info {};
        fd_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
        fd_info.memory = allocation_info.deviceMemory;
        fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

        int fd = -1;
        const VkResult result = vkGetMemoryFdKHR(device_manager_->logical_device(), &fd_info, &fd);
        throw_if_failed(result, "vkGetMemoryFdKHR");
        return ExternalMemoryHandle::opaque_fd(fd, allocation_size, memory_range);
#else
        throw std::runtime_error("External image memory export is not supported on this platform.");
#endif
    }

    uint32_t ResourceManager::store_descriptor(ImageWrap& image)
    {
        const bool supports_sampled = has_flag(image.desc.usage, ImageUsageFlags::Sampled);
        const bool supports_storage = has_flag(image.desc.usage, ImageUsageFlags::Storage);

        if (supports_sampled && !supports_storage)
            return store_sampled_descriptor(image);
        if (supports_storage && !supports_sampled)
            return store_storage_descriptor(image);

        std::lock_guard lock(mutex_);

        if (device_manager_ == nullptr || allocator_ == VK_NULL_HANDLE)
        {
            throw std::runtime_error("ResourceManager::store_descriptor called before initialize().");
        }

        if (!image.valid() || image.image_view == VK_NULL_HANDLE)
        {
            throw std::invalid_argument("ResourceManager::store_descriptor requires a valid HardwareImage.");
        }

        if (!supports_sampled && !supports_storage)
        {
            throw std::invalid_argument("HardwareImage bindless descriptor requires ImageUsageFlags::Sampled or ImageUsageFlags::Storage.");
        }

        create_combined_texture_descriptors();
        create_storage_image_descriptors();

        // dual-usage 图像需在两个数组占用同一 index，故不走各自的 free_list（否则两边
        // 弹出的槽位可能不同），而用两个游标的最大值 bump 分配：该 index 必 >= 两数组
        // 的 next，故不与任一数组已回收的低位槽位冲突。
        const bool has_shared_index = image.sampled_bindless_index >= 0 &&
                                      image.sampled_bindless_index == image.storage_bindless_index;
        uint32_t descriptor_index = has_shared_index
                                        ? static_cast<uint32_t>(image.sampled_bindless_index)
                                        : std::max(combined_texture_descriptors_.next, storage_image_descriptors_.next);

        if (descriptor_index >= combined_texture_descriptors_.capacity)
        {
            throw std::runtime_error("HardwareImage descriptor index exceeds the combined texture descriptor array.");
        }
        if (descriptor_index >= storage_image_descriptors_.capacity)
        {
            throw std::runtime_error("HardwareImage descriptor index exceeds the storage image descriptor array.");
        }

        image.sampled_bindless_index = static_cast<std::int32_t>(descriptor_index);
        image.storage_bindless_index = static_cast<std::int32_t>(descriptor_index);
        combined_texture_descriptors_.next = std::max(combined_texture_descriptors_.next, descriptor_index + 1);
        storage_image_descriptors_.next = std::max(storage_image_descriptors_.next, descriptor_index + 1);

        VkDescriptorImageInfo sampled_info {};
        sampled_info.sampler = default_sampler_;
        sampled_info.imageView = image.image_view;
        sampled_info.imageLayout = sampled_descriptor_layout(image);

        VkWriteDescriptorSet sampled_write {};
        sampled_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        sampled_write.dstSet = combined_texture_descriptors_.set;
        sampled_write.dstBinding = 0;
        sampled_write.dstArrayElement = descriptor_index;
        sampled_write.descriptorCount = 1;
        sampled_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sampled_write.pImageInfo = &sampled_info;

        VkDescriptorImageInfo storage_info {};
        storage_info.imageView = image.image_view;
        storage_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet storage_write {};
        storage_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        storage_write.dstSet = storage_image_descriptors_.set;
        storage_write.dstBinding = 0;
        storage_write.dstArrayElement = descriptor_index;
        storage_write.descriptorCount = 1;
        storage_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        storage_write.pImageInfo = &storage_info;

        const VkWriteDescriptorSet writes[] = { sampled_write, storage_write };
        vkUpdateDescriptorSets(device_manager_->logical_device(), 2, writes, 0, nullptr);
        return descriptor_index;
    }

    uint32_t ResourceManager::store_sampled_descriptor(ImageWrap& image)
    {
        std::lock_guard lock(mutex_);

        if (device_manager_ == nullptr || allocator_ == VK_NULL_HANDLE)
        {
            throw std::runtime_error("ResourceManager::store_descriptor called before initialize().");
        }

        if (!image.valid() || image.image_view == VK_NULL_HANDLE)
        {
            throw std::invalid_argument("ResourceManager::store_descriptor requires a valid HardwareImage.");
        }

        if (!has_flag(image.desc.usage, ImageUsageFlags::Sampled))
        {
            throw std::invalid_argument("HardwareImage sampled bindless descriptor requires ImageUsageFlags::Sampled.");
        }

        create_combined_texture_descriptors();

        if (image.sampled_bindless_index < 0)
        {
            const uint32_t slot = combined_texture_descriptors_.allocate();
            if (slot == DescriptorArray::invalid_slot)
            {
                throw std::runtime_error("Combined texture descriptor array is full.");
            }

            image.sampled_bindless_index = static_cast<std::int32_t>(slot);
        }

        const uint32_t descriptor_index = static_cast<uint32_t>(image.sampled_bindless_index);
        if (descriptor_index >= combined_texture_descriptors_.capacity)
        {
            throw std::runtime_error("HardwareImage descriptor index exceeds the combined texture descriptor array.");
        }

        VkDescriptorImageInfo image_info {};
        image_info.sampler = default_sampler_;
        image_info.imageView = image.image_view;
        image_info.imageLayout = sampled_descriptor_layout(image);

        VkWriteDescriptorSet write {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = combined_texture_descriptors_.set;
        write.dstBinding = 0;
        write.dstArrayElement = descriptor_index;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image_info;

        vkUpdateDescriptorSets(device_manager_->logical_device(), 1, &write, 0, nullptr);
        return descriptor_index;
    }

    uint32_t ResourceManager::store_storage_descriptor(ImageWrap& image)
    {
        std::lock_guard lock(mutex_);

        if (device_manager_ == nullptr || allocator_ == VK_NULL_HANDLE)
        {
            throw std::runtime_error("ResourceManager::store_descriptor called before initialize().");
        }

        if (!image.valid() || image.image_view == VK_NULL_HANDLE)
        {
            throw std::invalid_argument("ResourceManager::store_descriptor requires a valid HardwareImage.");
        }

        if (!has_flag(image.desc.usage, ImageUsageFlags::Storage))
        {
            throw std::invalid_argument("HardwareImage storage bindless descriptor requires ImageUsageFlags::Storage.");
        }

        create_storage_image_descriptors();

        if (image.storage_bindless_index < 0)
        {
            const uint32_t slot = storage_image_descriptors_.allocate();
            if (slot == DescriptorArray::invalid_slot)
            {
                throw std::runtime_error("Storage image descriptor array is full.");
            }

            image.storage_bindless_index = static_cast<std::int32_t>(slot);
        }

        const uint32_t descriptor_index = static_cast<uint32_t>(image.storage_bindless_index);
        if (descriptor_index >= storage_image_descriptors_.capacity)
        {
            throw std::runtime_error("HardwareImage descriptor index exceeds the storage image descriptor array.");
        }

        VkDescriptorImageInfo image_info {};
        image_info.imageView = image.image_view;
        image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet write {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = storage_image_descriptors_.set;
        write.dstBinding = 0;
        write.dstArrayElement = descriptor_index;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.pImageInfo = &image_info;

        vkUpdateDescriptorSets(device_manager_->logical_device(), 1, &write, 0, nullptr);
        return descriptor_index;
    }

    std::array<VkDescriptorSetLayout, ResourceManager::bindless_descriptor_set_count> ResourceManager::bindless_descriptor_set_layouts()
    {
        std::lock_guard lock(mutex_);
        ensure_bindless_descriptors_unlocked();

        std::array<VkDescriptorSetLayout, bindless_descriptor_set_count> layouts {};
        layouts[bindless_texture_set] = combined_texture_descriptors_.layout;
        layouts[bindless_storage_buffer_set] = storage_buffer_descriptors_.layout;
        layouts[bindless_storage_image_set] = storage_image_descriptors_.layout;
        return layouts;
    }

    std::array<VkDescriptorSet, ResourceManager::bindless_descriptor_set_count> ResourceManager::bindless_descriptor_sets()
    {
        std::lock_guard lock(mutex_);
        ensure_bindless_descriptors_unlocked();

        std::array<VkDescriptorSet, bindless_descriptor_set_count> sets {};
        sets[bindless_texture_set] = combined_texture_descriptors_.set;
        sets[bindless_storage_buffer_set] = storage_buffer_descriptors_.set;
        sets[bindless_storage_image_set] = storage_image_descriptors_.set;
        return sets;
    }

    ImageSubresourceLayout ResourceManager::image_subresource_layout(const ImageWrap& image, uint32_t layer, uint32_t mip) const
    {
        std::lock_guard lock(mutex_);

        ImageSubresourceLayout result {};
        if (device_manager_ == nullptr || device_manager_->logical_device() == VK_NULL_HANDLE || !image.valid())
        {
            return result;
        }

        VkImageAspectFlagBits aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        if ((image.aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT) != 0)
        {
            aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        }
        else if ((image.aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT) != 0)
        {
            aspect = VK_IMAGE_ASPECT_STENCIL_BIT;
        }

        VkImageSubresource subresource {};
        subresource.aspectMask = aspect;
        subresource.mipLevel = mip;
        subresource.arrayLayer = layer;

        VkSubresourceLayout layout {};
        vkGetImageSubresourceLayout(device_manager_->logical_device(), image.image_handle, &subresource, &layout);

        result.byte_offset = layout.offset;
        result.byte_size = layout.size;
        result.row_pitch = layout.rowPitch;
        result.slice_pitch = layout.depthPitch != 0 ? layout.depthPitch : layout.arrayPitch;
        result.extent = {
            std::max(1u, image.desc.extent.width >> mip),
            std::max(1u, image.desc.extent.height >> mip),
            std::max(1u, image.desc.extent.depth >> mip),
        };
        return result;
    }

    void ResourceManager::destroy_buffer(BufferWrap& buffer) noexcept
    {
        std::lock_guard lock(mutex_);

        // 归还 bindless 槽位供后续复用（P0：避免 create/destroy 单调耗尽数组）。
        // 必须在 clear_handles() 之前，后者会把 bindless_index 重置为 -1。
        if (buffer.bindless_index >= 0)
        {
            storage_buffer_descriptors_.release(static_cast<uint32_t>(buffer.bindless_index));
        }

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

    void ResourceManager::destroy_image(ImageWrap& image) noexcept
    {
        std::lock_guard lock(mutex_);

        // 归还 bindless 槽位供后续复用（P0）。dual-usage 图像 sampled/storage index 相同，
        // 但分属两个独立数组，各归各的 free_list（同一数组内不会重复归还）。
        // 必须在 clear_handles() 之前，后者会把索引重置为 -1。
        if (image.sampled_bindless_index >= 0)
        {
            combined_texture_descriptors_.release(static_cast<uint32_t>(image.sampled_bindless_index));
        }
        if (image.storage_bindless_index >= 0)
        {
            storage_image_descriptors_.release(static_cast<uint32_t>(image.storage_bindless_index));
        }

#if defined(_WIN32) || defined(_WIN64)
        if (image.desc.exportable)
        {
            close_win32_handle(image.exported_win32_handle);
        }
#endif

        if (device_manager_ != nullptr && device_manager_->logical_device() != VK_NULL_HANDLE)
        {
            if (image.image_view != VK_NULL_HANDLE)
            {
                vkDestroyImageView(device_manager_->logical_device(), image.image_view, nullptr);
            }
        }

        if (image.owns_native_image && image.image_handle != VK_NULL_HANDLE)
        {
            if (allocator_ != VK_NULL_HANDLE && image.image_alloc != VK_NULL_HANDLE)
            {
                vmaDestroyImage(allocator_, image.image_handle, image.image_alloc);
            }
            else if (device_manager_ != nullptr && device_manager_->logical_device() != VK_NULL_HANDLE)
            {
                vkDestroyImage(device_manager_->logical_device(), image.image_handle, nullptr);
            }
        }

        image.clear_handles();
    }
}
