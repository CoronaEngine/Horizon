//
// Created by Zero on 06/06/2022.
//

#pragma once
#include "core/util/logging.h"
#include <vulkan/vulkan.h>
#include "core/image/image_base.h"
#include "rhi/graphics_descriptions.h"

static std::string errorString(VkResult errorCode) {
    switch (errorCode) {
#define STR(r) \
    case VK_##r: return #r
        STR(NOT_READY);
        STR(TIMEOUT);
        STR(EVENT_SET);
        STR(EVENT_RESET);
        STR(INCOMPLETE);
        STR(ERROR_OUT_OF_HOST_MEMORY);
        STR(ERROR_OUT_OF_DEVICE_MEMORY);
        STR(ERROR_INITIALIZATION_FAILED);
        STR(ERROR_DEVICE_LOST);
        STR(ERROR_MEMORY_MAP_FAILED);
        STR(ERROR_LAYER_NOT_PRESENT);
        STR(ERROR_EXTENSION_NOT_PRESENT);
        STR(ERROR_FEATURE_NOT_PRESENT);
        STR(ERROR_INCOMPATIBLE_DRIVER);
        STR(ERROR_TOO_MANY_OBJECTS);
        STR(ERROR_FORMAT_NOT_SUPPORTED);
        STR(ERROR_SURFACE_LOST_KHR);
        STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
        STR(SUBOPTIMAL_KHR);
        STR(ERROR_OUT_OF_DATE_KHR);
        STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
        STR(ERROR_VALIDATION_FAILED_EXT);
        STR(ERROR_INVALID_SHADER_NV);
        STR(ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);
#undef STR
        default:
            return "UNKNOWN_ERROR";
    }
}

#define VK_CHECK_RESULT(f)                                                                                                                    \
    {                                                                                                                                         \
        VkResult res = (f);                                                                                                                   \
        if (res != VK_SUCCESS) {                                                                                                              \
            OC_ERROR_FORMAT("Vulkan rhi error: {} at {}:{}", errorString(res), __FILE__, __LINE__);                                    \
            assert(res == VK_SUCCESS);                                                                                                        \
        }                                                                                                                                     \
    }


namespace ocarina {
#define IS_VK_NULL_HANDLE(h) (h == VK_NULL_HANDLE)

static VkFormat get_vulkan_format(PixelStorage format, bool srgb) {
    switch (format) {
        case ocarina::PixelStorage::BYTE1:
            return srgb ? VK_FORMAT_R8_SRGB : VK_FORMAT_A8_UNORM_KHR;
        case ocarina::PixelStorage::BYTE2:
            return srgb ? VK_FORMAT_R8G8_SRGB : VK_FORMAT_R8G8_UNORM;
            break;
        case ocarina::PixelStorage::BYTE4:
            return srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
            break;
        case ocarina::PixelStorage::UINT1:
            return VK_FORMAT_R32_UINT;
            break;
        case ocarina::PixelStorage::UINT2:
            return VK_FORMAT_R32G32_UINT;
            break;
        case ocarina::PixelStorage::UINT4:
            return VK_FORMAT_R32G32B32A32_UINT;
            break;
        case ocarina::PixelStorage::FLOAT1:
            return VK_FORMAT_R32_SFLOAT;
            break;
        case ocarina::PixelStorage::FLOAT2:
            return VK_FORMAT_R32G32_SFLOAT;
            break;
        case ocarina::PixelStorage::FLOAT4:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
            break;
        case ocarina::PixelStorage::UNKNOWN:
            break;
        default:
            break;
            return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

static VkColorSpaceKHR colorspace_vulkan(const ColorSpace colorSpace) {
    switch (colorSpace) {
        case ColorSpace::SRGB:
            //return VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        case ColorSpace::LINEAR:
            return VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        default:
            return VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    }
}

static VkPrimitiveTopology get_vulkan_topology(PrimitiveType primitive_type) {
    switch (primitive_type) {
        case PrimitiveType::TRIANGLES:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case PrimitiveType::TRIANGLE_STRIP:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case PrimitiveType::POINTS:
            return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case PrimitiveType::LINES:
            return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case PrimitiveType::LINE_STRIP:
            return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        default:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

static VkShaderStageFlagBits get_vulkan_shader_stage(ShaderType shader_type) {
    switch (shader_type) {
        case ocarina::ShaderType::VertexShader:
            return VK_SHADER_STAGE_VERTEX_BIT;
        case ocarina::ShaderType::PixelShader:
            return VK_SHADER_STAGE_FRAGMENT_BIT;
        case ocarina::ShaderType::GeometryShader:
            return VK_SHADER_STAGE_GEOMETRY_BIT;
        case ocarina::ShaderType::ComputeShader:
            return VK_SHADER_STAGE_COMPUTE_BIT;
        case ocarina::ShaderType::MeshShader:
            return VK_SHADER_STAGE_MESH_BIT_EXT;
        case ocarina::ShaderType::NumShaderType:
        default:
            return VK_SHADER_STAGE_VERTEX_BIT;
    }
}

static VkCullModeFlags get_vulkan_cull_mode(CullingMode cull_mode) {
    switch (cull_mode) {
        case ocarina::CullingMode::NONE:
            return VK_CULL_MODE_NONE;
        case ocarina::CullingMode::FRONT:
            return VK_CULL_MODE_FRONT_BIT;
        case ocarina::CullingMode::BACK:
            return VK_CULL_MODE_BACK_BIT;
        case ocarina::CullingMode::FRONT_AND_BACK:
            return VK_CULL_MODE_FRONT_AND_BACK;
        default:
            return VK_CULL_MODE_NONE;
    }
}
static VkFrontFace get_vulkan_front_face(bool front) {
    return front ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

static VkBool32 get_supported_depth_format(VkPhysicalDevice physicalDevice, VkFormat *depthFormat) {
    // Since all depth formats may be optional, we need to find a suitable depth format to use
    // Start with the highest precision packed format
    std::vector<VkFormat> formatList = {
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM};

    for (auto &format : formatList) {
        VkFormatProperties formatProps;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProps);
        if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            *depthFormat = format;
            return true;
        }
    }

    return false;
}

static VkBufferUsageFlagBits get_buffer_usage_flag(BufferType buffer_type) {
    switch (buffer_type) {
        case BufferType::VertexBuffer:
            return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        case BufferType::IndexBuffer:
            return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        case BufferType::ConstantBuffer:
            return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
}

static VkMemoryPropertyFlags get_memory_property_flags(DeviceMemoryUsage usage) {
    switch (usage) {
        case DeviceMemoryUsage::MEMORY_USAGE_GPU_ONLY:
            return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        case DeviceMemoryUsage::MEMORY_USAGE_CPU_ONLY:
            return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        case DeviceMemoryUsage::MEMORY_USAGE_CPU_TO_GPU:
            return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        case DeviceMemoryUsage::MEMORY_USAGE_GPU_TO_CPU:
            return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        default:
            return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }
}

static VkSampleCountFlagBits get_vulkan_sample_count(uint sample_count) {
    switch (sample_count) {
        case 1:
            return VK_SAMPLE_COUNT_1_BIT;
        case 2:
            return VK_SAMPLE_COUNT_2_BIT;
        case 4:
            return VK_SAMPLE_COUNT_4_BIT;
        case 8:
            return VK_SAMPLE_COUNT_8_BIT;
        case 16:
            return VK_SAMPLE_COUNT_16_BIT;
        default:
            return VK_SAMPLE_COUNT_1_BIT;
    }
}

static VkImageUsageFlagBits get_vulkan_image_usage_flag(uint32_t image_usage) {
    uint32_t usage = 0;
    if (image_usage & static_cast<uint32_t>(TextureUsageFlags::ShaderReadOnly)) {
        usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }

    if (image_usage & static_cast<uint32_t>(TextureUsageFlags::ShaderReadWrite)) {
        usage |= (VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    }

    if (image_usage & static_cast<uint32_t>(TextureUsageFlags::RenderTarget)) {
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }

    if (image_usage & static_cast<uint32_t>(TextureUsageFlags::DepthStencil)) {
        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }

    if (image_usage & static_cast<uint32_t>(TextureUsageFlags::CopySrc)) {
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    if (image_usage & static_cast<uint32_t>(TextureUsageFlags::CopyDst)) {
        usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    if (image_usage & static_cast<uint32_t>(TextureUsageFlags::SwapChain)) {
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }

    return static_cast<VkImageUsageFlagBits>(usage);
}

static VkSampleCountFlagBits get_vulkan_sample_count_flag_bit(MultiSampleCount sample_count) {
    switch (sample_count) {
        case MultiSampleCount::SAMPLE_COUNT_1:
            return VK_SAMPLE_COUNT_1_BIT;
        case MultiSampleCount::SAMPLE_COUNT_2:
            return VK_SAMPLE_COUNT_2_BIT;
        case MultiSampleCount::SAMPLE_COUNT_4:
            return VK_SAMPLE_COUNT_4_BIT;
        case MultiSampleCount::SAMPLE_COUNT_8:
            return VK_SAMPLE_COUNT_8_BIT;
        case MultiSampleCount::SAMPLE_COUNT_16:
            return VK_SAMPLE_COUNT_16_BIT;
        case MultiSampleCount::SAMPLE_COUNT_32:
            return VK_SAMPLE_COUNT_32_BIT;
        case MultiSampleCount::SAMPLE_COUNT_64:
            return VK_SAMPLE_COUNT_64_BIT;
        default:
            return VK_SAMPLE_COUNT_1_BIT;
    }
}

static VkColorComponentFlagBits get_vulkan_color_component_flag_bits(ColorMask color_mask)
{
    uint32_t flags = 0;
    uint8_t mask = (uint8_t)color_mask;
    if (mask & (uint8_t)ColorMask::ColorMaskR) {
        flags |= VK_COLOR_COMPONENT_R_BIT;
    }
    if (mask & (uint8_t)ColorMask::ColorMaskG) {
        flags |= VK_COLOR_COMPONENT_G_BIT;
    }
    if (mask & (uint8_t)ColorMask::ColorMaskB) {
        flags |= VK_COLOR_COMPONENT_B_BIT;
    }
    if (mask & (uint8_t)ColorMask::ColorMaskA) {
        flags |= VK_COLOR_COMPONENT_A_BIT;
    }
    return (VkColorComponentFlagBits)flags;
}

static VkBlendOp get_vulkan_blend_op(BlendOperator op)
{
    switch (op) {
        case BlendOperator::ADD:
            return VK_BLEND_OP_ADD;
        case BlendOperator::SUBTRACT:
            return VK_BLEND_OP_SUBTRACT;
        case BlendOperator::REVERSE_SUBTRACT:
            return VK_BLEND_OP_REVERSE_SUBTRACT;
        case BlendOperator::MIN:
            return VK_BLEND_OP_MIN;
        case BlendOperator::MAX:
            return VK_BLEND_OP_MAX;
        default:
            return VK_BLEND_OP_ADD;
    }
}

static VkBlendFactor get_vulkan_blend_factor(BlendFunction blend_function) {
    switch (blend_function) {
        case BlendFunction::ZERO:
            return VK_BLEND_FACTOR_ZERO;
        case BlendFunction::ONE:
            return VK_BLEND_FACTOR_ONE;
        case BlendFunction::SRC_COLOR:
            return VK_BLEND_FACTOR_SRC_COLOR;
        case BlendFunction::ONE_MINUS_SRC_COLOR:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case BlendFunction::DST_COLOR:
            return VK_BLEND_FACTOR_DST_COLOR;
        default:
            return VK_BLEND_FACTOR_ZERO;
    }
}

static VkCompareOp get_vulkan_compare_op(SamplerCompareFunc func)
{
    switch (func) {
        case SamplerCompareFunc::LE:
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        case SamplerCompareFunc::GE:
            return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case SamplerCompareFunc::L:
            return VK_COMPARE_OP_LESS;
        case SamplerCompareFunc::G:
            return VK_COMPARE_OP_GREATER;
        case SamplerCompareFunc::E:
            return VK_COMPARE_OP_EQUAL;
        case SamplerCompareFunc::NE:
            return VK_COMPARE_OP_NOT_EQUAL;
        case SamplerCompareFunc::A:
            return VK_COMPARE_OP_ALWAYS;
        default:
            return VK_COMPARE_OP_NEVER;
    }
}

static VkFilter get_vulkan_filter(FilterMode filter) {
    switch (filter) {
        case FilterMode::POINT:
            return VK_FILTER_NEAREST;
        case FilterMode::BILINEAR:
        case FilterMode::TRILINEAR:
            return VK_FILTER_LINEAR;
        //case FilterMode::ANISOTROPIC:
        //return VK_FILTER_CUBIC_EXT;
        default:
            return VK_FILTER_LINEAR;
    }
}

static VkSamplerAddressMode get_vulkan_sampler_address(AddressMode address_mode) {
    switch (address_mode) {
        case AddressMode::WRAP:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case AddressMode::MIRROR:
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case AddressMode::CLAMP:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case AddressMode::BORDER:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        case AddressMode::MIRROR_ONCE:
            return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
        default:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }

}

static VkSamplerMipmapMode get_vulkan_sampler_mipmap_mode(FilterMode filter) {
    switch (filter) {
        case FilterMode::POINT:
            return VK_SAMPLER_MIPMAP_MODE_NEAREST;
        case FilterMode::BILINEAR:
        case FilterMode::TRILINEAR:
        case FilterMode::ANISOTROPIC:
            return VK_SAMPLER_MIPMAP_MODE_LINEAR;
        default:
            return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
}

}// namespace ocarina 
