#pragma once

#include <functional>
#include <set>
#include <stdexcept>

#include "HardwareWrapperVulkan/HardwareUtilsVulkan.h"

struct DeviceFeaturesChain
{
  public:
    DeviceFeaturesChain();

    VkPhysicalDeviceFeatures2 *getChainHead();

    DeviceFeaturesChain operator&(const DeviceFeaturesChain &features) const;
    DeviceFeaturesChain operator&(const VkPhysicalDeviceFeatures &features) const;
    DeviceFeaturesChain operator&(const VkPhysicalDeviceFeatures2 &features) const;
    DeviceFeaturesChain operator&(const VkPhysicalDeviceVulkan11Features &features) const;
    DeviceFeaturesChain operator&(const VkPhysicalDeviceVulkan12Features &features) const;
    DeviceFeaturesChain operator&(const VkPhysicalDeviceVulkan13Features &features) const;
    DeviceFeaturesChain operator&(const VkPhysicalDeviceVulkan14Features &features) const;
    DeviceFeaturesChain operator&(const VkPhysicalDeviceAccelerationStructureFeaturesKHR &features) const;
    DeviceFeaturesChain operator&(const VkPhysicalDeviceRayTracingPipelineFeaturesKHR &features) const;
    DeviceFeaturesChain operator&(const VkPhysicalDeviceRayQueryFeaturesKHR &features) const;
    DeviceFeaturesChain operator&(const VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT &features) const;

    DeviceFeaturesChain operator|(const DeviceFeaturesChain &features) const;
    DeviceFeaturesChain operator|(const VkPhysicalDeviceFeatures &features) const;
    DeviceFeaturesChain operator|(const VkPhysicalDeviceFeatures2 &features) const;
    DeviceFeaturesChain operator|(const VkPhysicalDeviceVulkan11Features &features) const;
    DeviceFeaturesChain operator|(const VkPhysicalDeviceVulkan12Features &features) const;
    DeviceFeaturesChain operator|(const VkPhysicalDeviceVulkan13Features &features) const;
    DeviceFeaturesChain operator|(const VkPhysicalDeviceVulkan14Features &features) const;
    DeviceFeaturesChain operator|(const VkPhysicalDeviceAccelerationStructureFeaturesKHR &features) const;
    DeviceFeaturesChain operator|(const VkPhysicalDeviceRayTracingPipelineFeaturesKHR &features) const;
    DeviceFeaturesChain operator|(const VkPhysicalDeviceRayQueryFeaturesKHR &features) const;
    DeviceFeaturesChain operator|(const VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT &features) const;

  private:
    void initializeChain();

    VkPhysicalDeviceFeatures2 deviceFeatures2{};
    VkPhysicalDeviceVulkan11Features deviceFeatures11{};
    VkPhysicalDeviceVulkan12Features deviceFeatures12{};
    VkPhysicalDeviceVulkan13Features deviceFeatures13{};
    VkPhysicalDeviceVulkan14Features deviceFeatures14{};

    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT swapchainMaintenance1Features{};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures{};
    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{};

    friend DeviceFeaturesChain;
};

struct CreateCallback
{
    using InstanceExtensionsFunc = std::function<std::set<const char *>(const VkInstance &, const VkPhysicalDevice &)>;
    using DeviceExtensionsFunc = std::function<std::set<const char *>(const VkInstance &, const VkPhysicalDevice &)>;
    using DeviceFeaturesFunc = std::function<DeviceFeaturesChain(const VkInstance &, const VkPhysicalDevice &)>;

    InstanceExtensionsFunc requiredInstanceExtensions = [](const VkInstance &, const VkPhysicalDevice &) { return std::set<const char *>(); };
    DeviceExtensionsFunc requiredDeviceExtensions = [](const VkInstance &, const VkPhysicalDevice &) { return std::set<const char *>(); };
    DeviceFeaturesFunc requiredDeviceFeatures = [](const VkInstance &, const VkPhysicalDevice &) { return DeviceFeaturesChain(); };
};