#include "FeaturesChain.h"

#define CORONA_APPLY_BOOL_OP(dst, src, op)       \
    do                                           \
    {                                            \
        dst = static_cast<VkBool32>(dst op src); \
    } while (0)

#define CORONA_AND_FEATURES(result, field) \
    CORONA_APPLY_BOOL_OP(result.field, features.field, &)

#define CORONA_OR_FEATURES(result, field) \
    CORONA_APPLY_BOOL_OP(result.field, features.field, |)

DeviceFeaturesChain::DeviceFeaturesChain()
{
    initializeChain();
}

void DeviceFeaturesChain::initializeChain()
{
    // rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    // rayQueryFeatures.pNext = nullptr;

    // rayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    // rayTracingPipelineFeatures.pNext = &rayQueryFeatures;

    // accelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    // accelerationStructureFeatures.pNext = &rayTracingPipelineFeatures;

    // 移除: swapchainMaintenance1Features (部分 AMD 核显不支持 VK_EXT_swapchain_maintenance1)
    // swapchainMaintenance1Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
    // swapchainMaintenance1Features.pNext = nullptr;

    deviceFeatures14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
    deviceFeatures14.pNext = nullptr;  // 不再链接 swapchainMaintenance1Features

    deviceFeatures13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    deviceFeatures13.pNext = &deviceFeatures14;

    deviceFeatures12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    deviceFeatures12.pNext = &deviceFeatures13;

    deviceFeatures11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    deviceFeatures11.pNext = &deviceFeatures12;

    deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    deviceFeatures2.pNext = &deviceFeatures11;
}

VkPhysicalDeviceFeatures2 *DeviceFeaturesChain::getChainHead()
{
    return &deviceFeatures2;
}

// DeviceFeaturesChain DeviceFeaturesChain::operator&(const DeviceFeaturesChain& features) const {
//     return (*this & features.deviceFeatures2.features & features.deviceFeatures11 & features.deviceFeatures12 & features.deviceFeatures13 & features.deviceFeatures14);
// }

DeviceFeaturesChain DeviceFeaturesChain::operator&(const DeviceFeaturesChain &features) const
{
    return (*this & features.deviceFeatures2.features & features.deviceFeatures11 & features.deviceFeatures12 & features.deviceFeatures13 & features.deviceFeatures14 & features.swapchainMaintenance1Features);
}

DeviceFeaturesChain DeviceFeaturesChain::operator&(const VkPhysicalDeviceFeatures2 &features) const
{
    return (*this & features.features);
}

DeviceFeaturesChain DeviceFeaturesChain::operator&(const VkPhysicalDeviceFeatures &features) const
{
    DeviceFeaturesChain result = *this;
    auto &dst = result.deviceFeatures2.features;

    CORONA_AND_FEATURES(dst, robustBufferAccess);
    CORONA_AND_FEATURES(dst, fullDrawIndexUint32);
    CORONA_AND_FEATURES(dst, imageCubeArray);
    CORONA_AND_FEATURES(dst, independentBlend);
    CORONA_AND_FEATURES(dst, geometryShader);
    CORONA_AND_FEATURES(dst, tessellationShader);
    CORONA_AND_FEATURES(dst, sampleRateShading);
    CORONA_AND_FEATURES(dst, dualSrcBlend);
    CORONA_AND_FEATURES(dst, logicOp);
    CORONA_AND_FEATURES(dst, multiDrawIndirect);
    CORONA_AND_FEATURES(dst, drawIndirectFirstInstance);
    CORONA_AND_FEATURES(dst, depthClamp);
    CORONA_AND_FEATURES(dst, depthBiasClamp);
    CORONA_AND_FEATURES(dst, fillModeNonSolid);
    CORONA_AND_FEATURES(dst, depthBounds);
    CORONA_AND_FEATURES(dst, wideLines);
    CORONA_AND_FEATURES(dst, largePoints);
    CORONA_AND_FEATURES(dst, alphaToOne);
    CORONA_AND_FEATURES(dst, multiViewport);
    CORONA_AND_FEATURES(dst, samplerAnisotropy);
    CORONA_AND_FEATURES(dst, textureCompressionETC2);
    CORONA_AND_FEATURES(dst, textureCompressionASTC_LDR);
    CORONA_AND_FEATURES(dst, textureCompressionBC);
    CORONA_AND_FEATURES(dst, occlusionQueryPrecise);
    CORONA_AND_FEATURES(dst, pipelineStatisticsQuery);
    CORONA_AND_FEATURES(dst, vertexPipelineStoresAndAtomics);
    CORONA_AND_FEATURES(dst, fragmentStoresAndAtomics);
    CORONA_AND_FEATURES(dst, shaderTessellationAndGeometryPointSize);
    CORONA_AND_FEATURES(dst, shaderImageGatherExtended);
    CORONA_AND_FEATURES(dst, shaderStorageImageExtendedFormats);
    CORONA_AND_FEATURES(dst, shaderStorageImageMultisample);
    CORONA_AND_FEATURES(dst, shaderStorageImageReadWithoutFormat);
    CORONA_AND_FEATURES(dst, shaderStorageImageWriteWithoutFormat);
    CORONA_AND_FEATURES(dst, shaderUniformBufferArrayDynamicIndexing);
    CORONA_AND_FEATURES(dst, shaderSampledImageArrayDynamicIndexing);
    CORONA_AND_FEATURES(dst, shaderStorageBufferArrayDynamicIndexing);
    CORONA_AND_FEATURES(dst, shaderStorageImageArrayDynamicIndexing);
    CORONA_AND_FEATURES(dst, shaderClipDistance);
    CORONA_AND_FEATURES(dst, shaderCullDistance);
    CORONA_AND_FEATURES(dst, shaderFloat64);
    CORONA_AND_FEATURES(dst, shaderInt64);
    CORONA_AND_FEATURES(dst, shaderInt16);
    CORONA_AND_FEATURES(dst, shaderResourceResidency);
    CORONA_AND_FEATURES(dst, shaderResourceMinLod);
    CORONA_AND_FEATURES(dst, sparseBinding);
    CORONA_AND_FEATURES(dst, sparseResidencyBuffer);
    CORONA_AND_FEATURES(dst, sparseResidencyImage2D);
    CORONA_AND_FEATURES(dst, sparseResidencyImage3D);
    CORONA_AND_FEATURES(dst, sparseResidency2Samples);
    CORONA_AND_FEATURES(dst, sparseResidency4Samples);
    CORONA_AND_FEATURES(dst, sparseResidency8Samples);
    CORONA_AND_FEATURES(dst, sparseResidency16Samples);
    CORONA_AND_FEATURES(dst, sparseResidencyAliased);
    CORONA_AND_FEATURES(dst, variableMultisampleRate);
    CORONA_AND_FEATURES(dst, inheritedQueries);

    return result;
}

DeviceFeaturesChain DeviceFeaturesChain::operator&(const VkPhysicalDeviceVulkan11Features &features) const
{
    DeviceFeaturesChain result = *this;
    auto &dst = result.deviceFeatures11;

    CORONA_AND_FEATURES(dst, storageBuffer16BitAccess);
    CORONA_AND_FEATURES(dst, uniformAndStorageBuffer16BitAccess);
    CORONA_AND_FEATURES(dst, storagePushConstant16);
    CORONA_AND_FEATURES(dst, storageInputOutput16);
    CORONA_AND_FEATURES(dst, multiview);
    CORONA_AND_FEATURES(dst, multiviewGeometryShader);
    CORONA_AND_FEATURES(dst, multiviewTessellationShader);
    CORONA_AND_FEATURES(dst, variablePointersStorageBuffer);
    CORONA_AND_FEATURES(dst, variablePointers);
    CORONA_AND_FEATURES(dst, protectedMemory);
    CORONA_AND_FEATURES(dst, samplerYcbcrConversion);
    CORONA_AND_FEATURES(dst, shaderDrawParameters);

    return result;
}

DeviceFeaturesChain DeviceFeaturesChain::operator&(const VkPhysicalDeviceVulkan12Features &features) const
{
    DeviceFeaturesChain result = *this;
    auto &dst = result.deviceFeatures12;

    CORONA_AND_FEATURES(dst, samplerMirrorClampToEdge);
    CORONA_AND_FEATURES(dst, drawIndirectCount);
    CORONA_AND_FEATURES(dst, storageBuffer8BitAccess);
    CORONA_AND_FEATURES(dst, uniformAndStorageBuffer8BitAccess);
    CORONA_AND_FEATURES(dst, storagePushConstant8);
    CORONA_AND_FEATURES(dst, shaderBufferInt64Atomics);
    CORONA_AND_FEATURES(dst, shaderSharedInt64Atomics);
    CORONA_AND_FEATURES(dst, shaderFloat16);
    CORONA_AND_FEATURES(dst, shaderInt8);
    CORONA_AND_FEATURES(dst, descriptorIndexing);
    CORONA_AND_FEATURES(dst, shaderInputAttachmentArrayDynamicIndexing);
    CORONA_AND_FEATURES(dst, shaderUniformTexelBufferArrayDynamicIndexing);
    CORONA_AND_FEATURES(dst, shaderStorageTexelBufferArrayDynamicIndexing);
    CORONA_AND_FEATURES(dst, shaderUniformBufferArrayNonUniformIndexing);
    CORONA_AND_FEATURES(dst, shaderSampledImageArrayNonUniformIndexing);
    CORONA_AND_FEATURES(dst, shaderStorageBufferArrayNonUniformIndexing);
    CORONA_AND_FEATURES(dst, shaderStorageImageArrayNonUniformIndexing);
    CORONA_AND_FEATURES(dst, shaderInputAttachmentArrayNonUniformIndexing);
    CORONA_AND_FEATURES(dst, shaderUniformTexelBufferArrayNonUniformIndexing);
    CORONA_AND_FEATURES(dst, shaderStorageTexelBufferArrayNonUniformIndexing);
    CORONA_AND_FEATURES(dst, descriptorBindingUniformBufferUpdateAfterBind);
    CORONA_AND_FEATURES(dst, descriptorBindingSampledImageUpdateAfterBind);
    CORONA_AND_FEATURES(dst, descriptorBindingStorageImageUpdateAfterBind);
    CORONA_AND_FEATURES(dst, descriptorBindingStorageBufferUpdateAfterBind);
    CORONA_AND_FEATURES(dst, descriptorBindingUniformTexelBufferUpdateAfterBind);
    CORONA_AND_FEATURES(dst, descriptorBindingStorageTexelBufferUpdateAfterBind);
    CORONA_AND_FEATURES(dst, descriptorBindingUpdateUnusedWhilePending);
    CORONA_AND_FEATURES(dst, descriptorBindingPartiallyBound);
    CORONA_AND_FEATURES(dst, descriptorBindingVariableDescriptorCount);
    CORONA_AND_FEATURES(dst, runtimeDescriptorArray);
    CORONA_AND_FEATURES(dst, samplerFilterMinmax);
    CORONA_AND_FEATURES(dst, scalarBlockLayout);
    CORONA_AND_FEATURES(dst, imagelessFramebuffer);
    CORONA_AND_FEATURES(dst, uniformBufferStandardLayout);
    CORONA_AND_FEATURES(dst, shaderSubgroupExtendedTypes);
    CORONA_AND_FEATURES(dst, separateDepthStencilLayouts);
    CORONA_AND_FEATURES(dst, hostQueryReset);
    CORONA_AND_FEATURES(dst, timelineSemaphore);
    CORONA_AND_FEATURES(dst, bufferDeviceAddress);
    CORONA_AND_FEATURES(dst, bufferDeviceAddressCaptureReplay);
    CORONA_AND_FEATURES(dst, bufferDeviceAddressMultiDevice);
    CORONA_AND_FEATURES(dst, vulkanMemoryModel);
    CORONA_AND_FEATURES(dst, vulkanMemoryModelDeviceScope);
    CORONA_AND_FEATURES(dst, vulkanMemoryModelAvailabilityVisibilityChains);
    CORONA_AND_FEATURES(dst, shaderOutputViewportIndex);
    CORONA_AND_FEATURES(dst, shaderOutputLayer);
    CORONA_AND_FEATURES(dst, subgroupBroadcastDynamicId);

    return result;
}

DeviceFeaturesChain DeviceFeaturesChain::operator&(const VkPhysicalDeviceVulkan13Features &features) const
{
    DeviceFeaturesChain result = *this;
    auto &dst = result.deviceFeatures13;

    CORONA_AND_FEATURES(dst, robustImageAccess);
    CORONA_AND_FEATURES(dst, inlineUniformBlock);
    CORONA_AND_FEATURES(dst, descriptorBindingInlineUniformBlockUpdateAfterBind);
    CORONA_AND_FEATURES(dst, pipelineCreationCacheControl);
    CORONA_AND_FEATURES(dst, privateData);
    CORONA_AND_FEATURES(dst, shaderDemoteToHelperInvocation);
    CORONA_AND_FEATURES(dst, shaderTerminateInvocation);
    CORONA_AND_FEATURES(dst, subgroupSizeControl);
    CORONA_AND_FEATURES(dst, computeFullSubgroups);
    CORONA_AND_FEATURES(dst, synchronization2);
    CORONA_AND_FEATURES(dst, textureCompressionASTC_HDR);
    CORONA_AND_FEATURES(dst, shaderZeroInitializeWorkgroupMemory);
    CORONA_AND_FEATURES(dst, dynamicRendering);
    CORONA_AND_FEATURES(dst, shaderIntegerDotProduct);
    CORONA_AND_FEATURES(dst, maintenance4);

    return result;
}

DeviceFeaturesChain DeviceFeaturesChain::operator&(const VkPhysicalDeviceVulkan14Features &features) const
{
    DeviceFeaturesChain result = *this;
    auto &dst = result.deviceFeatures14;

    CORONA_AND_FEATURES(dst, globalPriorityQuery);
    CORONA_AND_FEATURES(dst, shaderSubgroupRotate);
    CORONA_AND_FEATURES(dst, shaderSubgroupRotateClustered);
    CORONA_AND_FEATURES(dst, shaderFloatControls2);
    CORONA_AND_FEATURES(dst, shaderExpectAssume);
    CORONA_AND_FEATURES(dst, rectangularLines);
    CORONA_AND_FEATURES(dst, bresenhamLines);
    CORONA_AND_FEATURES(dst, smoothLines);
    CORONA_AND_FEATURES(dst, stippledRectangularLines);
    CORONA_AND_FEATURES(dst, stippledBresenhamLines);
    CORONA_AND_FEATURES(dst, stippledSmoothLines);
    CORONA_AND_FEATURES(dst, vertexAttributeInstanceRateDivisor);
    CORONA_AND_FEATURES(dst, vertexAttributeInstanceRateZeroDivisor);
    CORONA_AND_FEATURES(dst, indexTypeUint8);
    CORONA_AND_FEATURES(dst, dynamicRenderingLocalRead);
    CORONA_AND_FEATURES(dst, maintenance5);
    CORONA_AND_FEATURES(dst, maintenance6);
    CORONA_AND_FEATURES(dst, pipelineProtectedAccess);
    CORONA_AND_FEATURES(dst, pipelineRobustness);
    CORONA_AND_FEATURES(dst, hostImageCopy);
    CORONA_AND_FEATURES(dst, pushDescriptor);

    return result;
}

DeviceFeaturesChain DeviceFeaturesChain::operator&(const VkPhysicalDeviceAccelerationStructureFeaturesKHR &features) const
{
    DeviceFeaturesChain result = *this;
    auto &dst = result.accelerationStructureFeatures;

    CORONA_AND_FEATURES(dst, accelerationStructure);
    CORONA_AND_FEATURES(dst, accelerationStructureCaptureReplay);
    CORONA_AND_FEATURES(dst, accelerationStructureIndirectBuild);
    CORONA_AND_FEATURES(dst, accelerationStructureHostCommands);
    CORONA_AND_FEATURES(dst, descriptorBindingAccelerationStructureUpdateAfterBind);

    return result;
}

DeviceFeaturesChain DeviceFeaturesChain::operator&(const VkPhysicalDeviceRayTracingPipelineFeaturesKHR &features) const
{
    DeviceFeaturesChain result = *this;
    auto &dst = result.rayTracingPipelineFeatures;

    CORONA_AND_FEATURES(dst, rayTracingPipeline);
    CORONA_AND_FEATURES(dst, rayTracingPipelineShaderGroupHandleCaptureReplay);
    CORONA_AND_FEATURES(dst, rayTracingPipelineShaderGroupHandleCaptureReplayMixed);
    CORONA_AND_FEATURES(dst, rayTracingPipelineTraceRaysIndirect);
    CORONA_AND_FEATURES(dst, rayTraversalPrimitiveCulling);

    return result;
}

DeviceFeaturesChain DeviceFeaturesChain::operator&(const VkPhysicalDeviceRayQueryFeaturesKHR &features) const
{
    DeviceFeaturesChain result = *this;
    CORONA_AND_FEATURES(result.rayQueryFeatures, rayQuery);
    return result;
}

DeviceFeaturesChain DeviceFeaturesChain::operator&(const VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT &features) const
{
    DeviceFeaturesChain result = *this;
    auto &dst = result.swapchainMaintenance1Features;
    CORONA_AND_FEATURES(dst, swapchainMaintenance1);
    return result;
}

DeviceFeaturesChain DeviceFeaturesChain::operator|(const DeviceFeaturesChain &features) const
{
    return (*this | features.deviceFeatures2.features | features.deviceFeatures11 | features.deviceFeatures12 | features.deviceFeatures13 | features.deviceFeatures14 | features.swapchainMaintenance1Features);
}

// DeviceFeaturesChain DeviceFeaturesChain::operator|(const DeviceFeaturesChain& features) const {
//     return (*this | features.deviceFeatures2.features | features.deviceFeatures11 | features.deviceFeatures12 | features.deviceFeatures13 | features.deviceFeatures14);
// }

DeviceFeaturesChain DeviceFeaturesChain::operator|(const VkPhysicalDeviceFeatures2 &features) const
{
    return (*this | features.features);
}

DeviceFeaturesChain DeviceFeaturesChain::operator|(const VkPhysicalDeviceFeatures &features) const
{
    DeviceFeaturesChain result = *this;
    auto &dst = result.deviceFeatures2.features;

    CORONA_OR_FEATURES(dst, robustBufferAccess);
    CORONA_OR_FEATURES(dst, fullDrawIndexUint32);
    CORONA_OR_FEATURES(dst, imageCubeArray);
    CORONA_OR_FEATURES(dst, independentBlend);
    CORONA_OR_FEATURES(dst, geometryShader);
    CORONA_OR_FEATURES(dst, tessellationShader);
    CORONA_OR_FEATURES(dst, sampleRateShading);
    CORONA_OR_FEATURES(dst, dualSrcBlend);
    CORONA_OR_FEATURES(dst, logicOp);
    CORONA_OR_FEATURES(dst, multiDrawIndirect);
    CORONA_OR_FEATURES(dst, drawIndirectFirstInstance);
    CORONA_OR_FEATURES(dst, depthClamp);
    CORONA_OR_FEATURES(dst, depthBiasClamp);
    CORONA_OR_FEATURES(dst, fillModeNonSolid);
    CORONA_OR_FEATURES(dst, depthBounds);
    CORONA_OR_FEATURES(dst, wideLines);
    CORONA_OR_FEATURES(dst, largePoints);
    CORONA_OR_FEATURES(dst, alphaToOne);
    CORONA_OR_FEATURES(dst, multiViewport);
    CORONA_OR_FEATURES(dst, samplerAnisotropy);
    CORONA_OR_FEATURES(dst, textureCompressionETC2);
    CORONA_OR_FEATURES(dst, textureCompressionASTC_LDR);
    CORONA_OR_FEATURES(dst, textureCompressionBC);
    CORONA_OR_FEATURES(dst, occlusionQueryPrecise);
    CORONA_OR_FEATURES(dst, pipelineStatisticsQuery);
    CORONA_OR_FEATURES(dst, vertexPipelineStoresAndAtomics);
    CORONA_OR_FEATURES(dst, fragmentStoresAndAtomics);
    CORONA_OR_FEATURES(dst, shaderTessellationAndGeometryPointSize);
    CORONA_OR_FEATURES(dst, shaderImageGatherExtended);
    CORONA_OR_FEATURES(dst, shaderStorageImageExtendedFormats);
    CORONA_OR_FEATURES(dst, shaderStorageImageMultisample);
    CORONA_OR_FEATURES(dst, shaderStorageImageReadWithoutFormat);
    CORONA_OR_FEATURES(dst, shaderStorageImageWriteWithoutFormat);
    CORONA_OR_FEATURES(dst, shaderUniformBufferArrayDynamicIndexing);
    CORONA_OR_FEATURES(dst, shaderSampledImageArrayDynamicIndexing);
    CORONA_OR_FEATURES(dst, shaderStorageBufferArrayDynamicIndexing);
    CORONA_OR_FEATURES(dst, shaderStorageImageArrayDynamicIndexing);
    CORONA_OR_FEATURES(dst, shaderClipDistance);
    CORONA_OR_FEATURES(dst, shaderCullDistance);
    CORONA_OR_FEATURES(dst, shaderFloat64);
    CORONA_OR_FEATURES(dst, shaderInt64);
    CORONA_OR_FEATURES(dst, shaderInt16);
    CORONA_OR_FEATURES(dst, shaderResourceResidency);
    CORONA_OR_FEATURES(dst, shaderResourceMinLod);
    CORONA_OR_FEATURES(dst, sparseBinding);
    CORONA_OR_FEATURES(dst, sparseResidencyBuffer);
    CORONA_OR_FEATURES(dst, sparseResidencyImage2D);
    CORONA_OR_FEATURES(dst, sparseResidencyImage3D);
    CORONA_OR_FEATURES(dst, sparseResidency2Samples);
    CORONA_OR_FEATURES(dst, sparseResidency4Samples);
    CORONA_OR_FEATURES(dst, sparseResidency8Samples);
    CORONA_OR_FEATURES(dst, sparseResidency16Samples);
    CORONA_OR_FEATURES(dst, sparseResidencyAliased);
    CORONA_OR_FEATURES(dst, variableMultisampleRate);
    CORONA_OR_FEATURES(dst, inheritedQueries);

    return result;
}

DeviceFeaturesChain DeviceFeaturesChain::operator|(const VkPhysicalDeviceVulkan11Features &features) const
{
    DeviceFeaturesChain result = *this;
    auto &dst = result.deviceFeatures11;

    CORONA_OR_FEATURES(dst, storageBuffer16BitAccess);
    CORONA_OR_FEATURES(dst, uniformAndStorageBuffer16BitAccess);
    CORONA_OR_FEATURES(dst, storagePushConstant16);
    CORONA_OR_FEATURES(dst, storageInputOutput16);
    CORONA_OR_FEATURES(dst, multiview);
    CORONA_OR_FEATURES(dst, multiviewGeometryShader);
    CORONA_OR_FEATURES(dst, multiviewTessellationShader);
    CORONA_OR_FEATURES(dst, variablePointersStorageBuffer);
    CORONA_OR_FEATURES(dst, variablePointers);
    CORONA_OR_FEATURES(dst, protectedMemory);
    CORONA_OR_FEATURES(dst, samplerYcbcrConversion);
    CORONA_OR_FEATURES(dst, shaderDrawParameters);

    return result;
}

DeviceFeaturesChain DeviceFeaturesChain::operator|(const VkPhysicalDeviceVulkan12Features &features) const
{
    DeviceFeaturesChain result = *this;
    auto &dst = result.deviceFeatures12;

    CORONA_OR_FEATURES(dst, samplerMirrorClampToEdge);
    CORONA_OR_FEATURES(dst, drawIndirectCount);
    CORONA_OR_FEATURES(dst, storageBuffer8BitAccess);
    CORONA_OR_FEATURES(dst, uniformAndStorageBuffer8BitAccess);
    CORONA_OR_FEATURES(dst, storagePushConstant8);
    CORONA_OR_FEATURES(dst, shaderBufferInt64Atomics);
    CORONA_OR_FEATURES(dst, shaderSharedInt64Atomics);
    CORONA_OR_FEATURES(dst, shaderFloat16);
    CORONA_OR_FEATURES(dst, shaderInt8);
    CORONA_OR_FEATURES(dst, descriptorIndexing);
    CORONA_OR_FEATURES(dst, shaderInputAttachmentArrayDynamicIndexing);
    CORONA_OR_FEATURES(dst, shaderUniformTexelBufferArrayDynamicIndexing);
    CORONA_OR_FEATURES(dst, shaderStorageTexelBufferArrayDynamicIndexing);
    CORONA_OR_FEATURES(dst, shaderUniformBufferArrayNonUniformIndexing);
    CORONA_OR_FEATURES(dst, shaderSampledImageArrayNonUniformIndexing);
    CORONA_OR_FEATURES(dst, shaderStorageBufferArrayNonUniformIndexing);
    CORONA_OR_FEATURES(dst, shaderStorageImageArrayNonUniformIndexing);
    CORONA_OR_FEATURES(dst, shaderInputAttachmentArrayNonUniformIndexing);
    CORONA_OR_FEATURES(dst, shaderUniformTexelBufferArrayNonUniformIndexing);
    CORONA_OR_FEATURES(dst, shaderStorageTexelBufferArrayNonUniformIndexing);
    CORONA_OR_FEATURES(dst, descriptorBindingUniformBufferUpdateAfterBind);
    CORONA_OR_FEATURES(dst, descriptorBindingSampledImageUpdateAfterBind);
    CORONA_OR_FEATURES(dst, descriptorBindingStorageImageUpdateAfterBind);
    CORONA_OR_FEATURES(dst, descriptorBindingStorageBufferUpdateAfterBind);
    CORONA_OR_FEATURES(dst, descriptorBindingUniformTexelBufferUpdateAfterBind);
    CORONA_OR_FEATURES(dst, descriptorBindingStorageTexelBufferUpdateAfterBind);
    CORONA_OR_FEATURES(dst, descriptorBindingUpdateUnusedWhilePending);
    CORONA_OR_FEATURES(dst, descriptorBindingPartiallyBound);
    CORONA_OR_FEATURES(dst, descriptorBindingVariableDescriptorCount);
    CORONA_OR_FEATURES(dst, runtimeDescriptorArray);
    CORONA_OR_FEATURES(dst, samplerFilterMinmax);
    CORONA_OR_FEATURES(dst, scalarBlockLayout);
    CORONA_OR_FEATURES(dst, imagelessFramebuffer);
    CORONA_OR_FEATURES(dst, uniformBufferStandardLayout);
    CORONA_OR_FEATURES(dst, shaderSubgroupExtendedTypes);
    CORONA_OR_FEATURES(dst, separateDepthStencilLayouts);
    CORONA_OR_FEATURES(dst, hostQueryReset);
    CORONA_OR_FEATURES(dst, timelineSemaphore);
    CORONA_OR_FEATURES(dst, bufferDeviceAddress);
    CORONA_OR_FEATURES(dst, bufferDeviceAddressCaptureReplay);
    CORONA_OR_FEATURES(dst, bufferDeviceAddressMultiDevice);
    CORONA_OR_FEATURES(dst, vulkanMemoryModel);
    CORONA_OR_FEATURES(dst, vulkanMemoryModelDeviceScope);
    CORONA_OR_FEATURES(dst, vulkanMemoryModelAvailabilityVisibilityChains);
    CORONA_OR_FEATURES(dst, shaderOutputViewportIndex);
    CORONA_OR_FEATURES(dst, shaderOutputLayer);
    CORONA_OR_FEATURES(dst, subgroupBroadcastDynamicId);

    return result;
}

DeviceFeaturesChain DeviceFeaturesChain::operator|(const VkPhysicalDeviceVulkan13Features &features) const
{
    DeviceFeaturesChain result = *this;
    auto &dst = result.deviceFeatures13;

    CORONA_OR_FEATURES(dst, robustImageAccess);
    CORONA_OR_FEATURES(dst, inlineUniformBlock);
    CORONA_OR_FEATURES(dst, descriptorBindingInlineUniformBlockUpdateAfterBind);
    CORONA_OR_FEATURES(dst, pipelineCreationCacheControl);
    CORONA_OR_FEATURES(dst, privateData);
    CORONA_OR_FEATURES(dst, shaderDemoteToHelperInvocation);
    CORONA_OR_FEATURES(dst, shaderTerminateInvocation);
    CORONA_OR_FEATURES(dst, subgroupSizeControl);
    CORONA_OR_FEATURES(dst, computeFullSubgroups);
    CORONA_OR_FEATURES(dst, synchronization2);
    CORONA_OR_FEATURES(dst, textureCompressionASTC_HDR);
    CORONA_OR_FEATURES(dst, shaderZeroInitializeWorkgroupMemory);
    CORONA_OR_FEATURES(dst, dynamicRendering);
    CORONA_OR_FEATURES(dst, shaderIntegerDotProduct);
    CORONA_OR_FEATURES(dst, maintenance4);

    return result;
}

DeviceFeaturesChain DeviceFeaturesChain::operator|(const VkPhysicalDeviceVulkan14Features &features) const
{
    DeviceFeaturesChain result = *this;
    auto &dst = result.deviceFeatures14;

    CORONA_OR_FEATURES(dst, globalPriorityQuery);
    CORONA_OR_FEATURES(dst, shaderSubgroupRotate);
    CORONA_OR_FEATURES(dst, shaderSubgroupRotateClustered);
    CORONA_OR_FEATURES(dst, shaderFloatControls2);
    CORONA_OR_FEATURES(dst, shaderExpectAssume);
    CORONA_OR_FEATURES(dst, rectangularLines);
    CORONA_OR_FEATURES(dst, bresenhamLines);
    CORONA_OR_FEATURES(dst, smoothLines);
    CORONA_OR_FEATURES(dst, stippledRectangularLines);
    CORONA_OR_FEATURES(dst, stippledBresenhamLines);
    CORONA_OR_FEATURES(dst, stippledSmoothLines);
    CORONA_OR_FEATURES(dst, vertexAttributeInstanceRateDivisor);
    CORONA_OR_FEATURES(dst, vertexAttributeInstanceRateZeroDivisor);
    CORONA_OR_FEATURES(dst, indexTypeUint8);
    CORONA_OR_FEATURES(dst, dynamicRenderingLocalRead);
    CORONA_OR_FEATURES(dst, maintenance5);
    CORONA_OR_FEATURES(dst, maintenance6);
    CORONA_OR_FEATURES(dst, pipelineProtectedAccess);
    CORONA_OR_FEATURES(dst, pipelineRobustness);
    CORONA_OR_FEATURES(dst, hostImageCopy);
    CORONA_OR_FEATURES(dst, pushDescriptor);

    return result;
}

DeviceFeaturesChain DeviceFeaturesChain::operator|(const VkPhysicalDeviceAccelerationStructureFeaturesKHR &features) const
{
    DeviceFeaturesChain result = *this;
    auto &dst = result.accelerationStructureFeatures;

    CORONA_OR_FEATURES(dst, accelerationStructure);
    CORONA_OR_FEATURES(dst, accelerationStructureCaptureReplay);
    CORONA_OR_FEATURES(dst, accelerationStructureIndirectBuild);
    CORONA_OR_FEATURES(dst, accelerationStructureHostCommands);
    CORONA_OR_FEATURES(dst, descriptorBindingAccelerationStructureUpdateAfterBind);

    return result;
}

DeviceFeaturesChain DeviceFeaturesChain::operator|(const VkPhysicalDeviceRayTracingPipelineFeaturesKHR &features) const
{
    DeviceFeaturesChain result = *this;
    auto &dst = result.rayTracingPipelineFeatures;

    CORONA_OR_FEATURES(dst, rayTracingPipeline);
    CORONA_OR_FEATURES(dst, rayTracingPipelineShaderGroupHandleCaptureReplay);
    CORONA_OR_FEATURES(dst, rayTracingPipelineShaderGroupHandleCaptureReplayMixed);
    CORONA_OR_FEATURES(dst, rayTracingPipelineTraceRaysIndirect);
    CORONA_OR_FEATURES(dst, rayTraversalPrimitiveCulling);

    return result;
}

DeviceFeaturesChain DeviceFeaturesChain::operator|(const VkPhysicalDeviceRayQueryFeaturesKHR &features) const
{
    DeviceFeaturesChain result = *this;
    CORONA_OR_FEATURES(result.rayQueryFeatures, rayQuery);
    return result;
}

DeviceFeaturesChain DeviceFeaturesChain::operator|(const VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT &features) const
{
    DeviceFeaturesChain result = *this;
    auto &dst = result.swapchainMaintenance1Features;
    CORONA_OR_FEATURES(dst, swapchainMaintenance1);
    return result;
}

#undef CORONA_APPLY_BOOL_OP
#undef CORONA_AND_FEATURES
#undef CORONA_OR_FEATURES