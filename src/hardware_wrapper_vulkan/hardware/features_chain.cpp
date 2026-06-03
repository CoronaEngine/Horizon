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

    constexpr std::array vulkan_12_feature_fields 
    {
        &VkPhysicalDeviceVulkan12Features::samplerMirrorClampToEdge,
        &VkPhysicalDeviceVulkan12Features::drawIndirectCount,
        &VkPhysicalDeviceVulkan12Features::storageBuffer8BitAccess,
        &VkPhysicalDeviceVulkan12Features::uniformAndStorageBuffer8BitAccess,
        &VkPhysicalDeviceVulkan12Features::storagePushConstant8,
        &VkPhysicalDeviceVulkan12Features::shaderBufferInt64Atomics,
        &VkPhysicalDeviceVulkan12Features::shaderSharedInt64Atomics,
        &VkPhysicalDeviceVulkan12Features::shaderFloat16,
        &VkPhysicalDeviceVulkan12Features::shaderInt8,
        &VkPhysicalDeviceVulkan12Features::descriptorIndexing,
        &VkPhysicalDeviceVulkan12Features::shaderInputAttachmentArrayDynamicIndexing,
        &VkPhysicalDeviceVulkan12Features::shaderUniformTexelBufferArrayDynamicIndexing,
        &VkPhysicalDeviceVulkan12Features::shaderStorageTexelBufferArrayDynamicIndexing,
        &VkPhysicalDeviceVulkan12Features::shaderUniformBufferArrayNonUniformIndexing,
        &VkPhysicalDeviceVulkan12Features::shaderSampledImageArrayNonUniformIndexing,
        &VkPhysicalDeviceVulkan12Features::shaderStorageBufferArrayNonUniformIndexing,
        &VkPhysicalDeviceVulkan12Features::shaderStorageImageArrayNonUniformIndexing,
        &VkPhysicalDeviceVulkan12Features::shaderInputAttachmentArrayNonUniformIndexing,
        &VkPhysicalDeviceVulkan12Features::shaderUniformTexelBufferArrayNonUniformIndexing,
        &VkPhysicalDeviceVulkan12Features::shaderStorageTexelBufferArrayNonUniformIndexing,
        &VkPhysicalDeviceVulkan12Features::descriptorBindingUniformBufferUpdateAfterBind,
        &VkPhysicalDeviceVulkan12Features::descriptorBindingSampledImageUpdateAfterBind,
        &VkPhysicalDeviceVulkan12Features::descriptorBindingStorageImageUpdateAfterBind,
        &VkPhysicalDeviceVulkan12Features::descriptorBindingStorageBufferUpdateAfterBind,
        &VkPhysicalDeviceVulkan12Features::descriptorBindingUniformTexelBufferUpdateAfterBind,
        &VkPhysicalDeviceVulkan12Features::descriptorBindingStorageTexelBufferUpdateAfterBind,
        &VkPhysicalDeviceVulkan12Features::descriptorBindingUpdateUnusedWhilePending,
        &VkPhysicalDeviceVulkan12Features::descriptorBindingPartiallyBound,
        &VkPhysicalDeviceVulkan12Features::descriptorBindingVariableDescriptorCount,
        &VkPhysicalDeviceVulkan12Features::runtimeDescriptorArray,
        &VkPhysicalDeviceVulkan12Features::samplerFilterMinmax,
        &VkPhysicalDeviceVulkan12Features::scalarBlockLayout,
        &VkPhysicalDeviceVulkan12Features::imagelessFramebuffer,
        &VkPhysicalDeviceVulkan12Features::uniformBufferStandardLayout,
        &VkPhysicalDeviceVulkan12Features::shaderSubgroupExtendedTypes,
        &VkPhysicalDeviceVulkan12Features::separateDepthStencilLayouts,
        &VkPhysicalDeviceVulkan12Features::hostQueryReset,
        &VkPhysicalDeviceVulkan12Features::timelineSemaphore,
        &VkPhysicalDeviceVulkan12Features::bufferDeviceAddress,
        &VkPhysicalDeviceVulkan12Features::bufferDeviceAddressCaptureReplay,
        &VkPhysicalDeviceVulkan12Features::bufferDeviceAddressMultiDevice,
        &VkPhysicalDeviceVulkan12Features::vulkanMemoryModel,
        &VkPhysicalDeviceVulkan12Features::vulkanMemoryModelDeviceScope,
        &VkPhysicalDeviceVulkan12Features::vulkanMemoryModelAvailabilityVisibilityChains,
        &VkPhysicalDeviceVulkan12Features::shaderOutputViewportIndex,
        &VkPhysicalDeviceVulkan12Features::shaderOutputLayer,
        &VkPhysicalDeviceVulkan12Features::subgroupBroadcastDynamicId,
    };

    constexpr std::array vulkan_13_feature_fields 
    {
        &VkPhysicalDeviceVulkan13Features::robustImageAccess,
        &VkPhysicalDeviceVulkan13Features::inlineUniformBlock,
        &VkPhysicalDeviceVulkan13Features::descriptorBindingInlineUniformBlockUpdateAfterBind,
        &VkPhysicalDeviceVulkan13Features::pipelineCreationCacheControl,
        &VkPhysicalDeviceVulkan13Features::privateData,
        &VkPhysicalDeviceVulkan13Features::shaderDemoteToHelperInvocation,
        &VkPhysicalDeviceVulkan13Features::shaderTerminateInvocation,
        &VkPhysicalDeviceVulkan13Features::subgroupSizeControl,
        &VkPhysicalDeviceVulkan13Features::computeFullSubgroups,
        &VkPhysicalDeviceVulkan13Features::synchronization2,
        &VkPhysicalDeviceVulkan13Features::textureCompressionASTC_HDR,
        &VkPhysicalDeviceVulkan13Features::shaderZeroInitializeWorkgroupMemory,
        &VkPhysicalDeviceVulkan13Features::dynamicRendering,
        &VkPhysicalDeviceVulkan13Features::shaderIntegerDotProduct,
        &VkPhysicalDeviceVulkan13Features::maintenance4,
    };

    constexpr std::array vulkan_14_feature_fields 
    {
        &VkPhysicalDeviceVulkan14Features::globalPriorityQuery,
        &VkPhysicalDeviceVulkan14Features::shaderSubgroupRotate,
        &VkPhysicalDeviceVulkan14Features::shaderSubgroupRotateClustered,
        &VkPhysicalDeviceVulkan14Features::shaderFloatControls2,
        &VkPhysicalDeviceVulkan14Features::shaderExpectAssume,
        &VkPhysicalDeviceVulkan14Features::rectangularLines,
        &VkPhysicalDeviceVulkan14Features::bresenhamLines,
        &VkPhysicalDeviceVulkan14Features::smoothLines,
        &VkPhysicalDeviceVulkan14Features::stippledRectangularLines,
        &VkPhysicalDeviceVulkan14Features::stippledBresenhamLines,
        &VkPhysicalDeviceVulkan14Features::stippledSmoothLines,
        &VkPhysicalDeviceVulkan14Features::vertexAttributeInstanceRateDivisor,
        &VkPhysicalDeviceVulkan14Features::vertexAttributeInstanceRateZeroDivisor,
        &VkPhysicalDeviceVulkan14Features::indexTypeUint8,
        &VkPhysicalDeviceVulkan14Features::dynamicRenderingLocalRead,
        &VkPhysicalDeviceVulkan14Features::maintenance5,
        &VkPhysicalDeviceVulkan14Features::maintenance6,
        &VkPhysicalDeviceVulkan14Features::pipelineProtectedAccess,
        &VkPhysicalDeviceVulkan14Features::pipelineRobustness,
        &VkPhysicalDeviceVulkan14Features::hostImageCopy,
        &VkPhysicalDeviceVulkan14Features::pushDescriptor,
    };

    constexpr std::array acceleration_structure_feature_fields 
    {
        &VkPhysicalDeviceAccelerationStructureFeaturesKHR::accelerationStructure,
        &VkPhysicalDeviceAccelerationStructureFeaturesKHR::accelerationStructureCaptureReplay,
        &VkPhysicalDeviceAccelerationStructureFeaturesKHR::accelerationStructureIndirectBuild,
        &VkPhysicalDeviceAccelerationStructureFeaturesKHR::accelerationStructureHostCommands,
        &VkPhysicalDeviceAccelerationStructureFeaturesKHR::descriptorBindingAccelerationStructureUpdateAfterBind,
    };

    constexpr std::array ray_tracing_pipeline_feature_fields 
    {
        &VkPhysicalDeviceRayTracingPipelineFeaturesKHR::rayTracingPipeline,
        &VkPhysicalDeviceRayTracingPipelineFeaturesKHR::rayTracingPipelineShaderGroupHandleCaptureReplay,
        &VkPhysicalDeviceRayTracingPipelineFeaturesKHR::rayTracingPipelineShaderGroupHandleCaptureReplayMixed,
        &VkPhysicalDeviceRayTracingPipelineFeaturesKHR::rayTracingPipelineTraceRaysIndirect,
        &VkPhysicalDeviceRayTracingPipelineFeaturesKHR::rayTraversalPrimitiveCulling,
    };

    constexpr std::array ray_query_feature_fields 
    {
        &VkPhysicalDeviceRayQueryFeaturesKHR::rayQuery,
    };

    template <typename feature_type, std::size_t field_count> 
    DeviceFeaturesChain apply_to_chain(const DeviceFeaturesChain& chain,
                                       const feature_type& features,
                                       feature_type DeviceFeaturesChain::* destination,
                                       const std::array<VkBool32 feature_type::*, field_count>& fields,
                                       FeatureBoolOp op)
    {
        DeviceFeaturesChain result(chain);
        apply_feature_fields(result.*destination, features, fields, op);
        return result;
    }

    DeviceFeaturesChain::DeviceFeaturesChain()
    {
        initialize_chain();
    }

    // 防止任何一次拷贝把 pNext 指针复制错。

    DeviceFeaturesChain::DeviceFeaturesChain(const DeviceFeaturesChain& other)
    {
        copy_from(other);
    }

    DeviceFeaturesChain::DeviceFeaturesChain(DeviceFeaturesChain&& other) noexcept
    {
        copy_from(other);
    }

    DeviceFeaturesChain& DeviceFeaturesChain::operator=(const DeviceFeaturesChain& other)
    {
        if (this != &other)
        {
            copy_from(other);
        }

        return *this;
    }

    DeviceFeaturesChain& DeviceFeaturesChain::operator=(DeviceFeaturesChain&& other) noexcept
    {
        if (this != &other)
        {
            copy_from(other);
        }

        return *this;
    }

    void DeviceFeaturesChain::copy_from(const DeviceFeaturesChain& other) noexcept
    {
        device_features_2 = other.device_features_2;
        device_features_11 = other.device_features_11;
        device_features_12 = other.device_features_12;
        device_features_13 = other.device_features_13;
        device_features_14 = other.device_features_14;
        swapchain_maintenance_1_features = other.swapchain_maintenance_1_features;
        acceleration_structure_features = other.acceleration_structure_features;
        ray_tracing_pipeline_features = other.ray_tracing_pipeline_features;
        ray_query_features = other.ray_query_features;

        initialize_chain();
    }

    void DeviceFeaturesChain::initialize_chain() noexcept
    {
        //acceleration_structure_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        //acceleration_structure_features.pNext = nullptr;

        //ray_tracing_pipeline_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
        //ray_tracing_pipeline_features.pNext = nullptr;

        //ray_query_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
        //ray_query_features.pNext = nullptr;

        // 移除: swapchainMaintenance1Features (部分 AMD 核显不支持 VK_EXT_swapchain_maintenance1)
        //swapchain_maintenance_1_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
        //swapchain_maintenance_1_features.pNext = nullptr;

        // 不再链接 swapchainMaintenance1Features
        device_features_14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
        device_features_14.pNext = nullptr;

        device_features_13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        device_features_13.pNext = &device_features_14;

        device_features_12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        device_features_12.pNext = &device_features_13;

        device_features_11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        device_features_11.pNext = &device_features_12;

        device_features_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        device_features_2.pNext = &device_features_11;
    }

    VkPhysicalDeviceFeatures2* DeviceFeaturesChain::chain_head()
    {
        return &device_features_2;
    }

    const VkPhysicalDeviceFeatures2* DeviceFeaturesChain::chain_head() const
    {
        return &device_features_2;
    }

    DeviceFeaturesChain DeviceFeaturesChain::operator&(const DeviceFeaturesChain& features) const
    {
        return (*this & features.device_features_2.features & features.device_features_11 & features.device_features_12 & features.device_features_13 & features.device_features_14 & features.acceleration_structure_features & features.ray_tracing_pipeline_features & features.ray_query_features);
    }

    DeviceFeaturesChain DeviceFeaturesChain::operator|(const DeviceFeaturesChain& features) const
    {
        return (*this | features.device_features_2.features | features.device_features_11 | features.device_features_12 | features.device_features_13 | features.device_features_14 | features.acceleration_structure_features | features.ray_tracing_pipeline_features | features.ray_query_features);
    }

    DeviceFeaturesChain DeviceFeaturesChain::operator&(const VkPhysicalDeviceFeatures2& features) const
    {
        return *this & features.features;
    }

    DeviceFeaturesChain DeviceFeaturesChain::operator|(const VkPhysicalDeviceFeatures2& features) const
    {
        return *this | features.features;
    }

    DeviceFeaturesChain DeviceFeaturesChain::operator&(const VkPhysicalDeviceFeatures& features) const
    {
        DeviceFeaturesChain result(*this);
        apply_feature_fields(result.device_features_2.features,
                             features,
                             physical_device_feature_fields,
                             FeatureBoolOp::AndOp);
        return result;
    }

    DeviceFeaturesChain DeviceFeaturesChain::operator|(const VkPhysicalDeviceFeatures& features) const
    {
        DeviceFeaturesChain result(*this);
        apply_feature_fields(result.device_features_2.features,
                             features,
                             physical_device_feature_fields,
                             FeatureBoolOp::OrOp);
        return result;
    }

    DeviceFeaturesChain DeviceFeaturesChain::operator&(const VkPhysicalDeviceVulkan11Features& features) const
    {
        return apply_to_chain(*this, features, &DeviceFeaturesChain::device_features_11, vulkan_11_feature_fields, FeatureBoolOp::AndOp);
    }

    DeviceFeaturesChain DeviceFeaturesChain::operator|(const VkPhysicalDeviceVulkan11Features& features) const
    {
        return apply_to_chain(*this, features, &DeviceFeaturesChain::device_features_11, vulkan_11_feature_fields, FeatureBoolOp::OrOp);
    }

    DeviceFeaturesChain DeviceFeaturesChain::operator&(const VkPhysicalDeviceVulkan12Features& features) const
    {
        return apply_to_chain(*this, features, &DeviceFeaturesChain::device_features_12, vulkan_12_feature_fields, FeatureBoolOp::AndOp);
    }

    DeviceFeaturesChain DeviceFeaturesChain::operator|(const VkPhysicalDeviceVulkan12Features& features) const
    {
        return apply_to_chain(*this, features, &DeviceFeaturesChain::device_features_12, vulkan_12_feature_fields, FeatureBoolOp::OrOp);
    }

    DeviceFeaturesChain DeviceFeaturesChain::operator&(const VkPhysicalDeviceVulkan13Features& features) const
    {
        return apply_to_chain(*this, features, &DeviceFeaturesChain::device_features_13, vulkan_13_feature_fields, FeatureBoolOp::AndOp);
    }

    DeviceFeaturesChain DeviceFeaturesChain::operator|(const VkPhysicalDeviceVulkan13Features& features) const
    {
        return apply_to_chain(*this, features, &DeviceFeaturesChain::device_features_13, vulkan_13_feature_fields, FeatureBoolOp::OrOp);
    }

    DeviceFeaturesChain DeviceFeaturesChain::operator&(const VkPhysicalDeviceVulkan14Features& features) const
    {
        return apply_to_chain(*this, features, &DeviceFeaturesChain::device_features_14, vulkan_14_feature_fields, FeatureBoolOp::AndOp);
    }

    DeviceFeaturesChain DeviceFeaturesChain::operator|(const VkPhysicalDeviceVulkan14Features& features) const
    {
        return apply_to_chain(*this, features, &DeviceFeaturesChain::device_features_14, vulkan_14_feature_fields, FeatureBoolOp::OrOp);
    }

    DeviceFeaturesChain DeviceFeaturesChain::operator&(const VkPhysicalDeviceAccelerationStructureFeaturesKHR& features) const
    {
        return apply_to_chain(*this, features, &DeviceFeaturesChain::acceleration_structure_features, acceleration_structure_feature_fields, FeatureBoolOp::AndOp);
    }

    DeviceFeaturesChain DeviceFeaturesChain::operator|(const VkPhysicalDeviceAccelerationStructureFeaturesKHR& features) const
    {
        return apply_to_chain(*this, features, &DeviceFeaturesChain::acceleration_structure_features, acceleration_structure_feature_fields, FeatureBoolOp::OrOp);
    }

    DeviceFeaturesChain DeviceFeaturesChain::operator&(const VkPhysicalDeviceRayTracingPipelineFeaturesKHR& features) const
    {
        return apply_to_chain(*this, features, &DeviceFeaturesChain::ray_tracing_pipeline_features, ray_tracing_pipeline_feature_fields, FeatureBoolOp::AndOp);
    }

    DeviceFeaturesChain DeviceFeaturesChain::operator|(const VkPhysicalDeviceRayTracingPipelineFeaturesKHR& features) const
    {
        return apply_to_chain(*this, features, &DeviceFeaturesChain::ray_tracing_pipeline_features, ray_tracing_pipeline_feature_fields, FeatureBoolOp::OrOp);
    }

    DeviceFeaturesChain DeviceFeaturesChain::operator&(const VkPhysicalDeviceRayQueryFeaturesKHR& features) const
    {
        return apply_to_chain(*this, features, &DeviceFeaturesChain::ray_query_features, ray_query_feature_fields, FeatureBoolOp::AndOp);
    }

    DeviceFeaturesChain DeviceFeaturesChain::operator|(const VkPhysicalDeviceRayQueryFeaturesKHR& features) const
    {
        return apply_to_chain(*this, features, &DeviceFeaturesChain::ray_query_features, ray_query_feature_fields, FeatureBoolOp::OrOp);
    }
}
