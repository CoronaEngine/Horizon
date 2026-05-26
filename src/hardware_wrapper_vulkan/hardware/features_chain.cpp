#include "features_chain.h"

#include <array>
#include <cstddef>

namespace Corona::Horizon
{
    enum class FeatureBoolOp
    {
        AndOp,
        OrOp
    };

    constexpr VkBool32 apply_feature_bool_op(VkBool32 lhs, VkBool32 rhs, FeatureBoolOp op) noexcept
    {
        return op == FeatureBoolOp::AndOp ? static_cast<VkBool32>(lhs && rhs) : static_cast<VkBool32>(lhs || rhs);
    }

    template <typename feature_type, std::size_t field_count>
    void apply_feature_fields(feature_type& dst, const feature_type& src, const std::array<VkBool32 feature_type::*, field_count>& fields, FeatureBoolOp op) noexcept
    {
        for (const auto field : fields)
        {
            dst.*field = apply_feature_bool_op(dst.*field, src.*field, op);
        }
    }

    constexpr std::array physical_device_feature_fields
    {
        &VkPhysicalDeviceFeatures::robustBufferAccess,
        &VkPhysicalDeviceFeatures::fullDrawIndexUint32,
        &VkPhysicalDeviceFeatures::imageCubeArray,
        &VkPhysicalDeviceFeatures::independentBlend,
        &VkPhysicalDeviceFeatures::geometryShader,
        &VkPhysicalDeviceFeatures::tessellationShader,
        &VkPhysicalDeviceFeatures::sampleRateShading,
        &VkPhysicalDeviceFeatures::dualSrcBlend,
        &VkPhysicalDeviceFeatures::logicOp,
        &VkPhysicalDeviceFeatures::multiDrawIndirect,
        &VkPhysicalDeviceFeatures::drawIndirectFirstInstance,
        &VkPhysicalDeviceFeatures::depthClamp,
        &VkPhysicalDeviceFeatures::depthBiasClamp,
        &VkPhysicalDeviceFeatures::fillModeNonSolid,
        &VkPhysicalDeviceFeatures::depthBounds,
        &VkPhysicalDeviceFeatures::wideLines,
        &VkPhysicalDeviceFeatures::largePoints,
        &VkPhysicalDeviceFeatures::alphaToOne,
        &VkPhysicalDeviceFeatures::multiViewport,
        &VkPhysicalDeviceFeatures::samplerAnisotropy,
        &VkPhysicalDeviceFeatures::textureCompressionETC2,
        &VkPhysicalDeviceFeatures::textureCompressionASTC_LDR,
        &VkPhysicalDeviceFeatures::textureCompressionBC,
        &VkPhysicalDeviceFeatures::occlusionQueryPrecise,
        &VkPhysicalDeviceFeatures::pipelineStatisticsQuery,
        &VkPhysicalDeviceFeatures::vertexPipelineStoresAndAtomics,
        &VkPhysicalDeviceFeatures::fragmentStoresAndAtomics,
        &VkPhysicalDeviceFeatures::shaderTessellationAndGeometryPointSize,
        &VkPhysicalDeviceFeatures::shaderImageGatherExtended,
        &VkPhysicalDeviceFeatures::shaderStorageImageExtendedFormats,
        &VkPhysicalDeviceFeatures::shaderStorageImageMultisample,
        &VkPhysicalDeviceFeatures::shaderStorageImageReadWithoutFormat,
        &VkPhysicalDeviceFeatures::shaderStorageImageWriteWithoutFormat,
        &VkPhysicalDeviceFeatures::shaderUniformBufferArrayDynamicIndexing,
        &VkPhysicalDeviceFeatures::shaderSampledImageArrayDynamicIndexing,
        &VkPhysicalDeviceFeatures::shaderStorageBufferArrayDynamicIndexing,
        &VkPhysicalDeviceFeatures::shaderStorageImageArrayDynamicIndexing,
        &VkPhysicalDeviceFeatures::shaderClipDistance,
        &VkPhysicalDeviceFeatures::shaderCullDistance,
        &VkPhysicalDeviceFeatures::shaderFloat64,
        &VkPhysicalDeviceFeatures::shaderInt64,
        &VkPhysicalDeviceFeatures::shaderInt16,
        &VkPhysicalDeviceFeatures::shaderResourceResidency,
        &VkPhysicalDeviceFeatures::shaderResourceMinLod,
        &VkPhysicalDeviceFeatures::sparseBinding,
        &VkPhysicalDeviceFeatures::sparseResidencyBuffer,
        &VkPhysicalDeviceFeatures::sparseResidencyImage2D,
        &VkPhysicalDeviceFeatures::sparseResidencyImage3D,
        &VkPhysicalDeviceFeatures::sparseResidency2Samples,
        &VkPhysicalDeviceFeatures::sparseResidency4Samples,
        &VkPhysicalDeviceFeatures::sparseResidency8Samples,
        &VkPhysicalDeviceFeatures::sparseResidency16Samples,
        &VkPhysicalDeviceFeatures::sparseResidencyAliased,
        &VkPhysicalDeviceFeatures::variableMultisampleRate,
        &VkPhysicalDeviceFeatures::inheritedQueries,
    };

    constexpr std::array vulkan_11_feature_fields 
    {
        &VkPhysicalDeviceVulkan11Features::storageBuffer16BitAccess,
        &VkPhysicalDeviceVulkan11Features::uniformAndStorageBuffer16BitAccess,
        &VkPhysicalDeviceVulkan11Features::storagePushConstant16,
        &VkPhysicalDeviceVulkan11Features::storageInputOutput16,
        &VkPhysicalDeviceVulkan11Features::multiview,
        &VkPhysicalDeviceVulkan11Features::multiviewGeometryShader,
        &VkPhysicalDeviceVulkan11Features::multiviewTessellationShader,
        &VkPhysicalDeviceVulkan11Features::variablePointersStorageBuffer,
        &VkPhysicalDeviceVulkan11Features::variablePointers,
        &VkPhysicalDeviceVulkan11Features::protectedMemory,
        &VkPhysicalDeviceVulkan11Features::samplerYcbcrConversion,
        &VkPhysicalDeviceVulkan11Features::shaderDrawParameters,
    };


}