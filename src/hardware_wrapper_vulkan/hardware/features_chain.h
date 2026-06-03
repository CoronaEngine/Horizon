#pragma once

#include <functional>
#include <set>

#include <volk.h>

namespace Corona::Horizon
{
    struct DeviceFeaturesChain
    {
    public:
        DeviceFeaturesChain();
        DeviceFeaturesChain(const DeviceFeaturesChain& other);
        DeviceFeaturesChain(DeviceFeaturesChain&& other) noexcept;
        DeviceFeaturesChain& operator=(const DeviceFeaturesChain& other);
        DeviceFeaturesChain& operator=(DeviceFeaturesChain&& other) noexcept;

        VkPhysicalDeviceFeatures2* chain_head();
        const VkPhysicalDeviceFeatures2* chain_head() const;

        DeviceFeaturesChain operator&(const DeviceFeaturesChain& features) const;
        DeviceFeaturesChain operator&(const VkPhysicalDeviceFeatures& features) const;
        DeviceFeaturesChain operator&(const VkPhysicalDeviceFeatures2& features) const;
        DeviceFeaturesChain operator&(const VkPhysicalDeviceVulkan11Features& features) const;
        DeviceFeaturesChain operator&(const VkPhysicalDeviceVulkan12Features& features) const;
        DeviceFeaturesChain operator&(const VkPhysicalDeviceVulkan13Features& features) const;
        DeviceFeaturesChain operator&(const VkPhysicalDeviceVulkan14Features& features) const;
        DeviceFeaturesChain operator&(const VkPhysicalDeviceAccelerationStructureFeaturesKHR& features) const;
        DeviceFeaturesChain operator&(const VkPhysicalDeviceRayTracingPipelineFeaturesKHR& features) const;
        DeviceFeaturesChain operator&(const VkPhysicalDeviceRayQueryFeaturesKHR& features) const;
        //DeviceFeaturesChain operator&(const VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT& features) const;

        DeviceFeaturesChain operator|(const DeviceFeaturesChain& features) const;
        DeviceFeaturesChain operator|(const VkPhysicalDeviceFeatures& features) const;
        DeviceFeaturesChain operator|(const VkPhysicalDeviceFeatures2& features) const;
        DeviceFeaturesChain operator|(const VkPhysicalDeviceVulkan11Features& features) const;
        DeviceFeaturesChain operator|(const VkPhysicalDeviceVulkan12Features& features) const;
        DeviceFeaturesChain operator|(const VkPhysicalDeviceVulkan13Features& features) const;
        DeviceFeaturesChain operator|(const VkPhysicalDeviceVulkan14Features& features) const;
        DeviceFeaturesChain operator|(const VkPhysicalDeviceAccelerationStructureFeaturesKHR& features) const;
        DeviceFeaturesChain operator|(const VkPhysicalDeviceRayTracingPipelineFeaturesKHR& features) const;
        DeviceFeaturesChain operator|(const VkPhysicalDeviceRayQueryFeaturesKHR& features) const;
        //DeviceFeaturesChain operator|(const VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT& features) const;

    private:
        void copy_from(const DeviceFeaturesChain& other) noexcept;
        void initialize_chain() noexcept;

        VkPhysicalDeviceFeatures2 device_features_2 {};
        VkPhysicalDeviceVulkan11Features device_features_11 {};
        VkPhysicalDeviceVulkan12Features device_features_12 {};
        VkPhysicalDeviceVulkan13Features device_features_13 {};
        VkPhysicalDeviceVulkan14Features device_features_14 {};

        VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT swapchain_maintenance_1_features {};
        VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure_features {};
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_pipeline_features {};
        VkPhysicalDeviceRayQueryFeaturesKHR ray_query_features {};
    };

    struct HardwareCreateConfig
    {
        using instance_extensions_func = std::function<std::set<const char*>(VkInstance instance, VkPhysicalDevice physical_device)>;
        using device_extensions_func = std::function<std::set<const char*>(VkInstance instance, VkPhysicalDevice physical_device)>;
        using device_features_func = std::function<DeviceFeaturesChain(VkInstance instance, VkPhysicalDevice physical_device)>;

        instance_extensions_func get_instance_extensions =
            [](VkInstance, VkPhysicalDevice)
            {
                return std::set<const char*> {};
            };

        device_extensions_func get_device_extensions =
            [](VkInstance, VkPhysicalDevice)
            {
                return std::set<const char*> {};
            };

        device_features_func get_device_features =
            [](VkInstance, VkPhysicalDevice) 
            {
                return DeviceFeaturesChain {};
            };
    };
}
