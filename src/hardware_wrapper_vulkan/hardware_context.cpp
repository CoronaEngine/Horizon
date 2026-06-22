#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "hardware_context.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "corona/kernel/core/i_logger.h"
#include "hardware_wrapper/diagnostics.h"
#include "resource_pool.h"

#define VOLK_IMPLEMENTATION
#include <volk.h>

namespace Corona::Horizon
{
#ifndef HORIZON_ENABLE_VULKAN_VALIDATION
#if defined(NDEBUG)
#define HORIZON_ENABLE_VULKAN_VALIDATION 0
#else
#define HORIZON_ENABLE_VULKAN_VALIDATION 1
#endif
#endif

    HardwareContext g_hardware_context;

    constexpr uint32_t required_api_version = VK_API_VERSION_1_4;

    namespace
    {
        constexpr std::array<const char*, 6> blocked_overlay_layers = {{
            "VK_LAYER_OBS_HOOK",
            "VK_LAYER_RTSS",
            "VK_LAYER_EOS_Overlay",
            "VK_LAYER_VALVE_steam_fossilize",
            "VK_LAYER_VALVE_steam_overlay",
            "VK_LAYER_TENCENT_wegame_cross_overlay",
        }};

        bool environment_flag_enabled(const char* name)
        {
            const char* value = std::getenv(name);
            return value != nullptr && std::strcmp(value, "1") == 0;
        }

        void set_process_environment(const char* name, const std::string& value)
        {
#if defined(_WIN32)
            if (SetEnvironmentVariableA(name, value.c_str()) == 0)
            {
                throw std::runtime_error(std::string("Failed to set process environment variable: ") + name);
            }
#else
            if (setenv(name, value.c_str(), 1) != 0)
            {
                throw std::runtime_error(std::string("Failed to set process environment variable: ") + name);
            }
#endif
        }

        std::vector<std::string> parse_layer_filters(const char* value)
        {
            std::vector<std::string> filters;
            if (value == nullptr)
            {
                return filters;
            }

            std::string input(value);
            size_t begin = 0;
            while (begin <= input.size())
            {
                const size_t end = input.find(',', begin);
                const size_t token_end = end == std::string::npos ? input.size() : end;
                const size_t first = input.find_first_not_of(" \t", begin);
                if (first != std::string::npos && first < token_end)
                {
                    const size_t last = input.find_last_not_of(" \t", token_end - 1);
                    filters.emplace_back(input.substr(first, last - first + 1));
                }

                if (end == std::string::npos)
                {
                    break;
                }
                begin = end + 1;
            }
            return filters;
        }

        std::string join_layer_filters(const std::vector<std::string>& filters)
        {
            std::ostringstream stream;
            for (size_t index = 0; index < filters.size(); ++index)
            {
                if (index != 0)
                {
                    stream << ',';
                }
                stream << filters[index];
            }
            return stream.str();
        }

        void configure_vulkan_layer_isolation()
        {
            static std::once_flag isolation_once;

            std::call_once(isolation_once, [] {
                if (environment_flag_enabled("HORIZON_ALLOW_VULKAN_OVERLAYS"))
                {
                    Diagnostics::write(Diagnostics::Level::Info,
                                       "VULKAN PROFILE",
                                       "Vulkan overlay isolation: disabled by HORIZON_ALLOW_VULKAN_OVERLAYS");
                    return;
                }

                std::vector<std::string> disabled_layers =
                    parse_layer_filters(std::getenv("VK_LOADER_LAYERS_DISABLE"));
                for (const char* layer_name : blocked_overlay_layers)
                {
                    if (std::find(disabled_layers.begin(), disabled_layers.end(), layer_name) == disabled_layers.end())
                    {
                        disabled_layers.emplace_back(layer_name);
                    }
                }

                const std::string merged_disabled_layers = join_layer_filters(disabled_layers);
                set_process_environment("VK_LOADER_LAYERS_DISABLE", merged_disabled_layers);

                std::ostringstream report;
                report << "Vulkan overlay isolation: enabled\n"
                       << "  scope: current process\n"
                       << "  VK_LOADER_LAYERS_DISABLE: " << merged_disabled_layers;
                Diagnostics::write(Diagnostics::Level::Info, "VULKAN PROFILE", report.str());
            });
        }
    }

#if HORIZON_ENABLE_VULKAN_VALIDATION
    constexpr const char* validation_layer_name = "VK_LAYER_KHRONOS_validation";

    constexpr std::array<VkValidationFeatureEnableEXT, 2> enabled_validation_features {
        VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
        //VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT,
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
    };

    const char* validation_feature_name(VkValidationFeatureEnableEXT feature) noexcept
    {
        switch (feature)
        {
        case VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT:
            return "VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT";
        case VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT:
            return "VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT";
        case VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT:
            return "VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT";
        case VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT:
            return "VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT";
        case VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT:
            return "VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT";
        default:
            return "UNKNOWN_VALIDATION_FEATURE";
        }
    }

    std::vector<std::string> validation_feature_names()
    {
        std::vector<std::string> names;
        names.reserve(enabled_validation_features.size());
        for (VkValidationFeatureEnableEXT feature : enabled_validation_features)
        {
            names.emplace_back(validation_feature_name(feature));
        }
        return names;
    }

    std::string validation_feature_list()
    {
        std::ostringstream stream;
        for (size_t index = 0; index < enabled_validation_features.size(); ++index)
        {
            if (index != 0)
            {
                stream << ", ";
            }
            stream << validation_feature_name(enabled_validation_features[index]);
        }
        return stream.str();
    }
#endif

    void collect_instance_extensions(std::vector<VkExtensionProperties>& extensions, const char* layer_name)
    {
        uint32_t extension_count = 0;
        if (vkEnumerateInstanceExtensionProperties(layer_name, &extension_count, nullptr) != VK_SUCCESS)
        {
            return;
        }

        const size_t offset = extensions.size();
        extensions.resize(offset + extension_count);
        if (vkEnumerateInstanceExtensionProperties(layer_name, &extension_count, extensions.data() + offset) != VK_SUCCESS)
        {
            extensions.resize(offset);
        }
    }

    std::vector<const char*> supported_instance_extensions(std::set<const char*> requested_extensions,
                                                           const std::vector<const char*>& requested_layers)
    {
        std::vector<VkExtensionProperties> available_extensions;
        collect_instance_extensions(available_extensions, nullptr);
        for (const char* layer_name : requested_layers)
        {
            collect_instance_extensions(available_extensions, layer_name);
        }

        const auto contains_extension = [&available_extensions](const char* name) {
            return std::any_of(available_extensions.begin(), available_extensions.end(), [name](const VkExtensionProperties& extension) {
                return std::strcmp(name, extension.extensionName) == 0;
            });
        };

        std::vector<const char*> enabled_extensions;
        enabled_extensions.reserve(requested_extensions.size());

        for (const char* extension : requested_extensions)
        {
            if (contains_extension(extension))
            {
                enabled_extensions.push_back(extension);
                continue;
            }

            CFW_LOG_WARNING("Warning: Instance extension not supported: {}", extension);
        }

        return enabled_extensions;
    }

    [[nodiscard]] std::string api_version_string(uint32_t version)
    {
        std::ostringstream stream;
        stream << VK_VERSION_MAJOR(version) << '.'
               << VK_VERSION_MINOR(version) << '.'
               << VK_VERSION_PATCH(version);
        return stream.str();
    }

    bool supports_required_api(VkPhysicalDevice physical_device)
    {
        VkPhysicalDeviceProperties properties {};
        vkGetPhysicalDeviceProperties(physical_device, &properties);

        if (properties.apiVersion >= required_api_version)
        {
            return true;
        }

        CFW_LOG_WARNING("Skipping device '{}' due to Vulkan API {} (< {})",
                        properties.deviceName,
                        api_version_string(properties.apiVersion),
                        api_version_string(required_api_version));

        return false;
    }

    int device_type_priority(VkPhysicalDeviceType type) noexcept
    {
        switch (type)
        {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            return 0;
        case VK_PHYSICAL_DEVICE_TYPE_OTHER:
            return 1;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            return 2;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            return 3;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            return 4;
        default:
            return 5;
        }
    }

    [[nodiscard]] std::string hex_u32(uint32_t value)
    {
        std::ostringstream stream;
        stream << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << value;
        return stream.str();
    }

    [[nodiscard]] std::string byte_size_string(uint64_t bytes)
    {
        constexpr double kib = 1024.0;
        constexpr double mib = kib * 1024.0;
        constexpr double gib = mib * 1024.0;

        std::ostringstream stream;
        stream << std::fixed << std::setprecision(2);
        if (bytes >= static_cast<uint64_t>(gib))
        {
            stream << static_cast<double>(bytes) / gib << " GiB";
        }
        else if (bytes >= static_cast<uint64_t>(mib))
        {
            stream << static_cast<double>(bytes) / mib << " MiB";
        }
        else if (bytes >= static_cast<uint64_t>(kib))
        {
            stream << static_cast<double>(bytes) / kib << " KiB";
        }
        else
        {
            stream << bytes << " B";
        }
        return stream.str();
    }

    [[nodiscard]] std::string memory_heap_flags_string(VkMemoryHeapFlags flags)
    {
        std::string text;
        const auto append = [&text](const char* name) {
            if (!text.empty())
            {
                text += '|';
            }
            text += name;
        };

        if ((flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
        {
            append("device_local");
        }
        if ((flags & VK_MEMORY_HEAP_MULTI_INSTANCE_BIT) != 0)
        {
            append("multi_instance");
        }

        return text.empty() ? "none" : text;
    }

    [[nodiscard]] std::string memory_property_flags_string(VkMemoryPropertyFlags flags)
    {
        std::string text;
        const auto append = [&text](const char* name) {
            if (!text.empty())
            {
                text += '|';
            }
            text += name;
        };

        if ((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
        {
            append("device_local");
        }
        if ((flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0)
        {
            append("host_visible");
        }
        if ((flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0)
        {
            append("host_coherent");
        }
        if ((flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) != 0)
        {
            append("host_cached");
        }
        if ((flags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) != 0)
        {
            append("lazily_allocated");
        }
        if ((flags & VK_MEMORY_PROPERTY_PROTECTED_BIT) != 0)
        {
            append("protected");
        }
#ifdef VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD
        if ((flags & VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD) != 0)
        {
            append("device_coherent_amd");
        }
#endif
#ifdef VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD
        if ((flags & VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD) != 0)
        {
            append("device_uncached_amd");
        }
#endif
#ifdef VK_MEMORY_PROPERTY_RDMA_CAPABLE_BIT_NV
        if ((flags & VK_MEMORY_PROPERTY_RDMA_CAPABLE_BIT_NV) != 0)
        {
            append("rdma_capable_nv");
        }
#endif

        return text.empty() ? "none" : text;
    }

    struct DeviceMemorySummary
    {
        uint64_t device_local_bytes { 0 };
        uint64_t host_visible_bytes { 0 };
    };

    [[nodiscard]] DeviceMemorySummary summarize_device_memory(const VkPhysicalDeviceMemoryProperties& memory_properties)
    {
        std::array<bool, VK_MAX_MEMORY_HEAPS> host_visible_heaps {};
        for (uint32_t type_index = 0; type_index < memory_properties.memoryTypeCount; ++type_index)
        {
            const VkMemoryType& type = memory_properties.memoryTypes[type_index];
            if ((type.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0 && type.heapIndex < host_visible_heaps.size())
            {
                host_visible_heaps[type.heapIndex] = true;
            }
        }

        DeviceMemorySummary summary;
        for (uint32_t heap_index = 0; heap_index < memory_properties.memoryHeapCount; ++heap_index)
        {
            const VkMemoryHeap& heap = memory_properties.memoryHeaps[heap_index];
            if ((heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
            {
                summary.device_local_bytes += heap.size;
            }
            if (heap_index < host_visible_heaps.size() && host_visible_heaps[heap_index])
            {
                summary.host_visible_bytes += heap.size;
            }
        }
        return summary;
    }

    [[nodiscard]] VkPhysicalDeviceMemoryProperties query_device_memory_properties(VkPhysicalDevice physical_device)
    {
        VkPhysicalDeviceMemoryProperties memory_properties {};
        vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
        return memory_properties;
    }

    [[nodiscard]] VkPhysicalDeviceDriverProperties query_device_driver_properties(VkPhysicalDevice physical_device)
    {
        VkPhysicalDeviceDriverProperties driver_properties {};
        driver_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;

        VkPhysicalDeviceProperties2 properties {};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties.pNext = &driver_properties;
        vkGetPhysicalDeviceProperties2(physical_device, &properties);
        return driver_properties;
    }

    struct SystemMemorySummary
    {
        bool available { false };
        uint64_t total_physical_bytes { 0 };
        uint64_t available_physical_bytes { 0 };
    };

    [[nodiscard]] SystemMemorySummary query_system_memory() noexcept
    {
        SystemMemorySummary summary;
#if defined(_WIN32)
        MEMORYSTATUSEX status {};
        status.dwLength = sizeof(status);
        if (GlobalMemoryStatusEx(&status) != 0)
        {
            summary.available = true;
            summary.total_physical_bytes = static_cast<uint64_t>(status.ullTotalPhys);
            summary.available_physical_bytes = static_cast<uint64_t>(status.ullAvailPhys);
        }
#endif
        return summary;
    }

    [[nodiscard]] const char* device_type_name(VkPhysicalDeviceType type) noexcept
    {
        switch (type)
        {
        case VK_PHYSICAL_DEVICE_TYPE_OTHER:
            return "other";
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            return "integrated";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            return "discrete";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            return "virtual";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            return "cpu";
        default:
            return "unknown";
        }
    }

    [[nodiscard]] std::string queue_flags_string(VkQueueFlags flags)
    {
        std::string text;
        const auto append = [&text](const char* name) {
            if (!text.empty())
            {
                text += '|';
            }
            text += name;
        };

        if ((flags & VK_QUEUE_GRAPHICS_BIT) != 0)
        {
            append("graphics");
        }
        if ((flags & VK_QUEUE_COMPUTE_BIT) != 0)
        {
            append("compute");
        }
        if ((flags & VK_QUEUE_TRANSFER_BIT) != 0)
        {
            append("transfer");
        }
        if ((flags & VK_QUEUE_SPARSE_BINDING_BIT) != 0)
        {
            append("sparse");
        }

        return text.empty() ? "none" : text;
    }

    [[nodiscard]] std::vector<VkExtensionProperties> enumerate_device_extensions(VkPhysicalDevice physical_device, std::string& error)
    {
        uint32_t extension_count = 0;
        VkResult result = vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, nullptr);
        if (result != VK_SUCCESS)
        {
            error = "vkEnumerateDeviceExtensionProperties(count) failed. VkResult=" + std::to_string(static_cast<int>(result));
            return {};
        }

        std::vector<VkExtensionProperties> available(extension_count);
        result = vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, available.data());
        if (result != VK_SUCCESS)
        {
            error = "vkEnumerateDeviceExtensionProperties(data) failed. VkResult=" + std::to_string(static_cast<int>(result));
            return {};
        }

        return available;
    }

    [[nodiscard]] bool contains_extension(const std::vector<VkExtensionProperties>& extensions, const char* name)
    {
        return std::any_of(extensions.begin(), extensions.end(), [name](const VkExtensionProperties& extension) {
            return std::strcmp(extension.extensionName, name) == 0;
        });
    }

    struct FeatureStatus
    {
        const char* group { "" };
        const char* name { "" };
        bool supported { false };
    };

    struct DeviceCompatibilityReport
    {
        VkPhysicalDeviceProperties properties {};
        VkPhysicalDeviceDriverProperties driver_properties {};
        VkPhysicalDeviceMemoryProperties memory_properties {};
        DeviceMemorySummary memory_summary {};
        std::vector<QueueFamilyInfo> queue_families;
        std::vector<std::string> requested_extensions;
        std::vector<std::string> missing_extensions;
        std::vector<FeatureStatus> required_features;
        std::vector<std::string> missing_queues;
        bool api_supported { false };
        bool extension_query_failed { false };
        std::string extension_query_error;

        [[nodiscard]] bool compatible() const noexcept
        {
            return api_supported && missing_queues.empty() &&
                   std::none_of(required_features.begin(), required_features.end(), [](const FeatureStatus& feature) {
                       return !feature.supported;
                   });
        }
    };

    void add_required_feature(std::vector<FeatureStatus>& features, const char* group, const char* name, VkBool32 value)
    {
        features.push_back({ group, name, value == VK_TRUE });
    }

    [[nodiscard]] std::vector<FeatureStatus> required_feature_status(VkPhysicalDevice physical_device)
    {
        VkPhysicalDeviceVulkan11Features features11 {};
        features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;

        VkPhysicalDeviceVulkan12Features features12 {};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

        VkPhysicalDeviceVulkan13Features features13 {};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

        VkPhysicalDeviceVulkan14Features features14 {};
        features14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;

        VkPhysicalDeviceFeatures2 features {};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &features11;
        features11.pNext = &features12;
        features12.pNext = &features13;
        features13.pNext = &features14;

        vkGetPhysicalDeviceFeatures2(physical_device, &features);

        std::vector<FeatureStatus> status;
        add_required_feature(status, "Vulkan 1.0", "samplerAnisotropy", features.features.samplerAnisotropy);
        add_required_feature(status, "Vulkan 1.0", "geometryShader", features.features.geometryShader);
        add_required_feature(status, "Vulkan 1.0", "shaderInt16", features.features.shaderInt16);
        add_required_feature(status, "Vulkan 1.0", "wideLines", features.features.wideLines);
        add_required_feature(status, "Vulkan 1.0", "fragmentStoresAndAtomics", features.features.fragmentStoresAndAtomics);
        add_required_feature(status, "Vulkan 1.0", "shaderStorageImageExtendedFormats", features.features.shaderStorageImageExtendedFormats);
        add_required_feature(status, "Vulkan 1.0", "shaderStorageImageReadWithoutFormat", features.features.shaderStorageImageReadWithoutFormat);
        add_required_feature(status, "Vulkan 1.0", "shaderStorageImageWriteWithoutFormat", features.features.shaderStorageImageWriteWithoutFormat);
        add_required_feature(status, "Vulkan 1.1", "multiview", features11.multiview);
        add_required_feature(status, "Vulkan 1.2", "bufferDeviceAddress", features12.bufferDeviceAddress);
        add_required_feature(status, "Vulkan 1.2", "shaderFloat16", features12.shaderFloat16);
        add_required_feature(status, "Vulkan 1.2", "shaderSampledImageArrayNonUniformIndexing", features12.shaderSampledImageArrayNonUniformIndexing);
        add_required_feature(status, "Vulkan 1.2", "descriptorBindingSampledImageUpdateAfterBind", features12.descriptorBindingSampledImageUpdateAfterBind);
        add_required_feature(status, "Vulkan 1.2", "shaderUniformBufferArrayNonUniformIndexing", features12.shaderUniformBufferArrayNonUniformIndexing);
        add_required_feature(status, "Vulkan 1.2", "descriptorBindingUniformBufferUpdateAfterBind", features12.descriptorBindingUniformBufferUpdateAfterBind);
        add_required_feature(status, "Vulkan 1.2", "shaderStorageBufferArrayNonUniformIndexing", features12.shaderStorageBufferArrayNonUniformIndexing);
        add_required_feature(status, "Vulkan 1.2", "descriptorBindingStorageBufferUpdateAfterBind", features12.descriptorBindingStorageBufferUpdateAfterBind);
        add_required_feature(status, "Vulkan 1.2", "shaderStorageImageArrayNonUniformIndexing", features12.shaderStorageImageArrayNonUniformIndexing);
        add_required_feature(status, "Vulkan 1.2", "descriptorBindingPartiallyBound", features12.descriptorBindingPartiallyBound);
        add_required_feature(status, "Vulkan 1.2", "runtimeDescriptorArray", features12.runtimeDescriptorArray);
        add_required_feature(status, "Vulkan 1.2", "descriptorBindingStorageImageUpdateAfterBind", features12.descriptorBindingStorageImageUpdateAfterBind);
        add_required_feature(status, "Vulkan 1.2", "descriptorBindingVariableDescriptorCount", features12.descriptorBindingVariableDescriptorCount);
        add_required_feature(status, "Vulkan 1.2", "descriptorIndexing", features12.descriptorIndexing);
        add_required_feature(status, "Vulkan 1.2", "timelineSemaphore", features12.timelineSemaphore);
        add_required_feature(status, "Vulkan 1.3", "synchronization2", features13.synchronization2);
        add_required_feature(status, "Vulkan 1.3", "dynamicRendering", features13.dynamicRendering);

        (void)features14;
        return status;
    }

    [[nodiscard]] DeviceCompatibilityReport analyze_device_compatibility(VkPhysicalDevice physical_device,
                                                                         const HardwareCreateConfig& config,
                                                                         VkInstance instance)
    {
        DeviceCompatibilityReport report;
        report.driver_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
        VkPhysicalDeviceProperties2 properties {};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties.pNext = &report.driver_properties;
        vkGetPhysicalDeviceProperties2(physical_device, &properties);
        report.properties = properties.properties;
        report.api_supported = report.properties.apiVersion >= required_api_version;
        report.memory_properties = query_device_memory_properties(physical_device);
        report.memory_summary = summarize_device_memory(report.memory_properties);

        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);
        std::vector<VkQueueFamilyProperties> queue_properties(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_properties.data());

        bool has_graphics = false;
        bool has_compute = false;
        bool has_transfer = false;
        report.queue_families.reserve(queue_properties.size());
        for (uint32_t index = 0; index < queue_properties.size(); ++index)
        {
            if (queue_properties[index].queueCount == 0)
            {
                continue;
            }

            const VkQueueFlags flags = queue_properties[index].queueFlags;
            has_graphics = has_graphics || (flags & VK_QUEUE_GRAPHICS_BIT) != 0;
            has_compute = has_compute || (flags & VK_QUEUE_COMPUTE_BIT) != 0;
            has_transfer = has_transfer || (flags & VK_QUEUE_TRANSFER_BIT) != 0;
            report.queue_families.push_back({ index, queue_properties[index].queueCount, flags });
        }

        if (!has_graphics)
        {
            report.missing_queues.emplace_back("graphics");
        }
        if (!has_compute)
        {
            report.missing_queues.emplace_back("compute");
        }
        if (!has_transfer)
        {
            report.missing_queues.emplace_back("transfer");
        }

        std::string extension_error;
        const std::vector<VkExtensionProperties> available_extensions = enumerate_device_extensions(physical_device, extension_error);
        if (!extension_error.empty())
        {
            report.extension_query_failed = true;
            report.extension_query_error = extension_error;
        }

        for (const char* extension : config.get_device_extensions(instance, physical_device))
        {
            report.requested_extensions.emplace_back(extension);
            if (!contains_extension(available_extensions, extension))
            {
                report.missing_extensions.emplace_back(extension);
            }
        }

        report.required_features = required_feature_status(physical_device);
        return report;
    }

    void write_device_compatibility_report(const DeviceCompatibilityReport& report)
    {
        std::ostringstream stream;
        stream << "Physical device: " << report.properties.deviceName << '\n'
               << "  type: " << device_type_name(report.properties.deviceType) << '\n'
               << "  vendor_id: " << hex_u32(report.properties.vendorID) << '\n'
               << "  device_id: " << hex_u32(report.properties.deviceID) << '\n'
               << "  api_version: " << api_version_string(report.properties.apiVersion) << '\n'
               << "  driver_version_raw: " << report.properties.driverVersion << '\n'
               << "  driver_name: " << (report.driver_properties.driverName[0] == '\0' ? "unknown" : report.driver_properties.driverName) << '\n'
               << "  driver_info: " << (report.driver_properties.driverInfo[0] == '\0' ? "unknown" : report.driver_properties.driverInfo) << '\n'
               << "  driver_id: " << static_cast<uint32_t>(report.driver_properties.driverID) << '\n'
               << "  conformance_version: "
               << static_cast<uint32_t>(report.driver_properties.conformanceVersion.major) << '.'
               << static_cast<uint32_t>(report.driver_properties.conformanceVersion.minor) << '.'
               << static_cast<uint32_t>(report.driver_properties.conformanceVersion.subminor) << '.'
               << static_cast<uint32_t>(report.driver_properties.conformanceVersion.patch) << '\n'
               << "  memory:\n"
               << "    device_local: " << byte_size_string(report.memory_summary.device_local_bytes) << '\n'
               << "    host_visible: " << byte_size_string(report.memory_summary.host_visible_bytes) << '\n'
               << "    heaps:\n";

        for (uint32_t heap_index = 0; heap_index < report.memory_properties.memoryHeapCount; ++heap_index)
        {
            const VkMemoryHeap& heap = report.memory_properties.memoryHeaps[heap_index];
            stream << "      heap " << heap_index
                   << ": size=" << byte_size_string(heap.size)
                   << ", flags=" << memory_heap_flags_string(heap.flags) << '\n';
        }

        stream << "    types:\n";
        for (uint32_t type_index = 0; type_index < report.memory_properties.memoryTypeCount; ++type_index)
        {
            const VkMemoryType& type = report.memory_properties.memoryTypes[type_index];
            stream << "      type " << type_index
                   << ": heap=" << type.heapIndex
                   << ", flags=" << memory_property_flags_string(type.propertyFlags) << '\n';
        }

        if (!report.api_supported)
        {
            stream << "  COMPATIBILITY FAIL: Vulkan API " << api_version_string(required_api_version) << " is required.\n";
        }

        stream << "  queue_families:\n";
        for (const QueueFamilyInfo& family : report.queue_families)
        {
            stream << "    family " << family.family_index
                   << ": count=" << family.queue_count
                   << ", flags=" << queue_flags_string(family.flags) << '\n';
        }
        for (const std::string& missing_queue : report.missing_queues)
        {
            stream << "  COMPATIBILITY FAIL: missing " << missing_queue << " queue capability.\n";
        }

        stream << "  requested_device_extensions:\n";
        for (const std::string& extension : report.requested_extensions)
        {
            const bool missing = std::find(report.missing_extensions.begin(), report.missing_extensions.end(), extension) != report.missing_extensions.end();
            stream << "    " << extension << ": " << (missing ? "missing" : "available") << '\n';
        }
        if (report.extension_query_failed)
        {
            stream << "  extension_query_error: " << report.extension_query_error << '\n';
        }

        stream << "  required_features:\n";
        for (const FeatureStatus& feature : report.required_features)
        {
            stream << "    " << feature.group << "::" << feature.name << ": " << (feature.supported ? "supported" : "missing") << '\n';
            if (!feature.supported)
            {
                stream << "  COMPATIBILITY FAIL: missing required feature " << feature.name << ".\n";
            }
        }

        stream << "  compatibility: " << (report.compatible() ? "PASS (candidate)" : "FAIL (skipped)") << '\n';

        Diagnostics::write(report.compatible() ? Diagnostics::Level::Info : Diagnostics::Level::Error,
                           "VULKAN PROFILE",
                           stream.str());
    }

#if HORIZON_ENABLE_VULKAN_VALIDATION
    bool contains_name(const std::vector<const char*>& names, const char* target)
    {
        return std::any_of(names.begin(), names.end(), [target](const char* name) {
            return std::strcmp(name, target) == 0;
        });
    }

    [[nodiscard]] const char* severity_name(VkDebugUtilsMessageSeverityFlagBitsEXT severity) noexcept
    {
        if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
        {
            return "error";
        }
        if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
        {
            return "warning";
        }
        if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) != 0)
        {
            return "info";
        }
        return "verbose";
    }

    [[nodiscard]] std::string message_type_string(VkDebugUtilsMessageTypeFlagsEXT message_type)
    {
        std::string text;
        const auto append = [&text](const char* name) {
            if (!text.empty())
            {
                text += '|';
            }
            text += name;
        };

        if ((message_type & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) != 0)
        {
            append("general");
        }
        if ((message_type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) != 0)
        {
            append("validation");
        }
        if ((message_type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) != 0)
        {
            append("performance");
        }

        return text.empty() ? "unknown" : text;
    }

    [[nodiscard]] const char* object_type_name(VkObjectType type) noexcept
    {
        switch (type)
        {
        case VK_OBJECT_TYPE_INSTANCE:
            return "VkInstance";
        case VK_OBJECT_TYPE_PHYSICAL_DEVICE:
            return "VkPhysicalDevice";
        case VK_OBJECT_TYPE_DEVICE:
            return "VkDevice";
        case VK_OBJECT_TYPE_QUEUE:
            return "VkQueue";
        case VK_OBJECT_TYPE_SEMAPHORE:
            return "VkSemaphore";
        case VK_OBJECT_TYPE_COMMAND_BUFFER:
            return "VkCommandBuffer";
        case VK_OBJECT_TYPE_BUFFER:
            return "VkBuffer";
        case VK_OBJECT_TYPE_IMAGE:
            return "VkImage";
        case VK_OBJECT_TYPE_IMAGE_VIEW:
            return "VkImageView";
        case VK_OBJECT_TYPE_SHADER_MODULE:
            return "VkShaderModule";
        case VK_OBJECT_TYPE_PIPELINE_LAYOUT:
            return "VkPipelineLayout";
        case VK_OBJECT_TYPE_PIPELINE:
            return "VkPipeline";
        case VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT:
            return "VkDescriptorSetLayout";
        case VK_OBJECT_TYPE_DESCRIPTOR_POOL:
            return "VkDescriptorPool";
        case VK_OBJECT_TYPE_DESCRIPTOR_SET:
            return "VkDescriptorSet";
        case VK_OBJECT_TYPE_SWAPCHAIN_KHR:
            return "VkSwapchainKHR";
        default:
            return "VkObject";
        }
    }

    bool validation_layer_available()
    {
        uint32_t layer_count = 0;
        if (vkEnumerateInstanceLayerProperties(&layer_count, nullptr) != VK_SUCCESS)
        {
            return false;
        }

        std::vector<VkLayerProperties> available_layers(layer_count);
        if (vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data()) != VK_SUCCESS)
        {
            return false;
        }

        return std::any_of(available_layers.begin(), available_layers.end(), [](const VkLayerProperties& layer) {
            return std::strcmp(validation_layer_name, layer.layerName) == 0;
        });
    }

    bool is_low_severity_loader_message(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, const char* message_id) noexcept
    {
        const bool warning_or_error = (message_severity & (VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)) != 0;
        return !warning_or_error && std::strcmp(message_id, "Loader Message") == 0;
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                                         VkDebugUtilsMessageTypeFlagsEXT message_type,
                                                         const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
                                                         void* user_data)
    {
        (void)user_data;

        const char* message = callback_data != nullptr && callback_data->pMessage != nullptr ? callback_data->pMessage : "<no message>";
        const char* message_id = callback_data != nullptr && callback_data->pMessageIdName != nullptr ? callback_data->pMessageIdName : "<no id>";
        const int32_t message_id_number = callback_data != nullptr ? callback_data->messageIdNumber : 0;

        if (is_low_severity_loader_message(message_severity, message_id))
        {
            return VK_FALSE;
        }

        std::ostringstream diagnostic;
        diagnostic << "severity=" << severity_name(message_severity)
                   << ", type=" << message_type_string(message_type)
                   << ", id=" << message_id
                   << ", id_number=" << message_id_number
                   << '\n'
                   << message;

        if (callback_data != nullptr && callback_data->objectCount != 0 && callback_data->pObjects != nullptr)
        {
            diagnostic << "\nObjects:";
            for (uint32_t index = 0; index < callback_data->objectCount; ++index)
            {
                const VkDebugUtilsObjectNameInfoEXT& object = callback_data->pObjects[index];
                diagnostic << "\n  [" << index << "] "
                           << object_type_name(object.objectType)
                           << " handle=0x" << std::hex << object.objectHandle << std::dec;
                if (object.pObjectName != nullptr)
                {
                    diagnostic << " name=" << object.pObjectName;
                }
            }
        }

        Diagnostics::Level diagnostic_level = Diagnostics::Level::Info;
        if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
        {
            diagnostic_level = Diagnostics::Level::Error;
            CFW_LOG_ERROR("[Vulkan validation] {} ({}): {}", message_id, message_id_number, message);
        }
        else if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
        {
            diagnostic_level = Diagnostics::Level::Warning;
            CFW_LOG_WARNING("[Vulkan validation] {} ({}): {}", message_id, message_id_number, message);
        }
        else if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) != 0)
        {
            // CFW_LOG_INFO("[Vulkan validation] {} ({}): {}", message_id, message_id_number, message);
        }
        else
        {
            CFW_LOG_DEBUG("[Vulkan validation] {} ({}): {}", message_id, message_id_number, message);
        }

        Diagnostics::write(diagnostic_level, "VULKAN VALIDATION", diagnostic.str());
        return VK_FALSE;
    }

    VkResult create_debug_utils_messenger_ext(VkInstance instance,
                                              const VkDebugUtilsMessengerCreateInfoEXT* create_info,
                                              const VkAllocationCallbacks* allocator,
                                              VkDebugUtilsMessengerEXT* debug_messenger)
    {
        auto function = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        if (function == nullptr)
        {
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }

        return function(instance, create_info, allocator, debug_messenger);
    }

    void destroy_debug_utils_messenger_ext(VkInstance instance, VkDebugUtilsMessengerEXT debug_messenger, const VkAllocationCallbacks* allocator)
    {
        auto function = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (function != nullptr)
        {
            function(instance, debug_messenger, allocator);
        }
    }

    VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info()
    {
        VkDebugUtilsMessengerCreateInfoEXT create_info {};
        create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        create_info.pfnUserCallback = vulkan_debug_callback;
        return create_info;
    }

    VkValidationFeaturesEXT validation_features_create_info()
    {
        VkValidationFeaturesEXT create_info {};
        create_info.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        create_info.enabledValidationFeatureCount = static_cast<uint32_t>(enabled_validation_features.size());
        create_info.pEnabledValidationFeatures = enabled_validation_features.data();
        return create_info;
    }

    void write_instance_extension_report(const std::set<const char*>& requested_extensions,
                                         const std::vector<const char*>& enabled_extensions,
                                         const std::vector<const char*>& requested_layers,
                                         bool validation_layer_enabled,
                                         bool debug_utils_enabled,
                                         bool validation_features_enabled)
    {
        std::ostringstream stream;
        stream << "Instance profile\n"
               << "  required_api_version: " << api_version_string(required_api_version) << '\n'
               << "  validation_layer_requested: " << (validation_layer_enabled ? "yes" : "no") << '\n'
               << "  requested_layers:\n";
        if (requested_layers.empty())
        {
            stream << "    <none>\n";
        }
        else
        {
            for (const char* layer : requested_layers)
            {
                stream << "    " << layer << '\n';
            }
        }

        stream << "  requested_instance_extensions:\n";
        for (const char* extension : requested_extensions)
        {
            stream << "    " << extension << ": " << (contains_name(enabled_extensions, extension) ? "enabled" : "missing") << '\n';
        }

        stream << "  debug_utils_enabled: " << (debug_utils_enabled ? "yes" : "no") << '\n'
               << "  validation_features_enabled: " << (validation_features_enabled ? "yes" : "no") << '\n';

        if (!validation_layer_enabled)
        {
            stream << "  COMPATIBILITY FAIL: VK_LAYER_KHRONOS_validation is not available for the debug profile.\n";
        }
        if (!debug_utils_enabled)
        {
            stream << "  COMPATIBILITY FAIL: VK_EXT_debug_utils is missing, validation messages cannot be captured.\n";
        }
        if (!validation_features_enabled)
        {
            stream << "  COMPATIBILITY FAIL: VK_EXT_validation_features is missing, extended validation is disabled.\n";
        }

        Diagnostics::write(debug_utils_enabled && validation_features_enabled ? Diagnostics::Level::Info : Diagnostics::Level::Warning,
                           "VULKAN PROFILE",
                           stream.str());
    }

    VulkanValidationReport make_validation_report()
    {
        VulkanValidationReport report;
        report.compiled = true;

        configure_vulkan_layer_isolation();
        if (volkInitialize() != VK_SUCCESS)
        {
            report.missing_requirements.emplace_back("Vulkan loader is not available.");
            return report;
        }

        report.layer_available = validation_layer_available();
        if (!report.layer_available)
        {
            report.missing_requirements.emplace_back("VK_LAYER_KHRONOS_validation is not available.");
            return report;
        }

        std::set<const char*> requested_extensions {
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
            VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME,
        };
        std::vector<const char*> requested_layers { validation_layer_name };
        const auto enabled_extensions = supported_instance_extensions(std::move(requested_extensions), requested_layers);
        report.debug_utils_enabled = contains_name(enabled_extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        report.validation_features_enabled = contains_name(enabled_extensions, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
        report.enabled_features = validation_feature_names();

        if (!report.debug_utils_enabled)
        {
            report.missing_requirements.emplace_back("VK_EXT_debug_utils is not available from global or validation-layer extensions.");
        }
        if (!report.validation_features_enabled)
        {
            report.missing_requirements.emplace_back("VK_EXT_validation_features is not available from global or validation-layer extensions.");
        }

        return report;
    }
#endif

    VulkanValidationReport vulkan_validation_report()
    {
#if HORIZON_ENABLE_VULKAN_VALIDATION
        return make_validation_report();
#else
        VulkanValidationReport report;
        report.compiled = false;
        report.missing_requirements.emplace_back("HORIZON_ENABLE_VULKAN_VALIDATION is disabled at compile time.");
        return report;
#endif
    }

    HardwareContext& hardware_context()
    {
        return g_hardware_context;
    }

    HardwareContext::DeviceContext& main_device_context()
    {
        auto device = hardware_context().main_device();
        if (!device)
        {
            throw std::runtime_error("Hardware context has no main device.");
        }

        return *device;
    }

    ResourceManager& resource_manager()
    {
        return main_device_context().resource_manager;
    }

    DeviceManager& device_manager()
    {
        return main_device_context().device_manager;
    }

    VkInstance vulkan_instance()
    {
        return hardware_context().instance();
    }

    std::vector<std::shared_ptr<HardwareContext::DeviceContext>> all_devices()
    {
        return hardware_context().devices();
    }

    HardwareContext::HardwareContext()
    {
        prepare_features();
    }

    HardwareContext::~HardwareContext()
    {
        for (auto& device : devices_)
        {
            if (!device)
            {
                continue;
            }

            if (device->device_manager.logical_device() != VK_NULL_HANDLE)
            {
                (void)vkDeviceWaitIdle(device->device_manager.logical_device());
            }
        }

        resource_pool().release_all();

        for (auto& device : devices_)
        {
            if (!device)
            {
                continue;
            }

            device->resource_manager.shutdown();
            device->device_manager.shutdown();
        }

        devices_.clear();
        main_device_.reset();

        cleanup_debug_messenger();

        if (instance_ != VK_NULL_HANDLE)
        {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }
    }

    VkInstance HardwareContext::instance()
    {
        ensure_instance();
        return instance_;
    }

    std::vector<std::shared_ptr<HardwareContext::DeviceContext>> HardwareContext::devices()
    {
        ensure_devices();
        return devices_;
    }

    std::shared_ptr<HardwareContext::DeviceContext> HardwareContext::main_device()
    {
        ensure_devices();
        return main_device_;
    }

    void HardwareContext::prepare_features()
    {
        create_config_.get_instance_extensions = [](const VkInstance&, const VkPhysicalDevice&) {
            std::set<const char*> extensions {
                "VK_KHR_surface",
                // VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME,       // 移除: 未使用且部分设备不支持
                // VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME,  // 移除: surface_maintenance1 的依赖
                VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
                VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
                VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME
            };

#if _WIN32 || _WIN64
            extensions.insert(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif __APPLE__
            extensions.insert(VK_MVK_MOLTENVK_EXTENSION_NAME);
            extensions.insert(VK_MVK_MACOS_SURFACE_EXTENSION_NAME);
#elif __linux__
            extensions.insert(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
#endif
            return extensions;
        };

        create_config_.get_device_extensions = [](const VkInstance&, const VkPhysicalDevice&) {
            return std::set<const char*> {
                VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                // VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,  // 移除: 部分 AMD 核显不支持
                VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
                VK_KHR_16BIT_STORAGE_EXTENSION_NAME,
                VK_KHR_MULTIVIEW_EXTENSION_NAME,
                //VK_AMD_GPU_SHADER_HALF_FLOAT_EXTENSION_NAME,
                VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
                VK_EXT_SHADER_SUBGROUP_BALLOT_EXTENSION_NAME,
                VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
                VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
                VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
                VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
                VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME,
#if _WIN32 || _WIN64
                VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
                VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME
#elif __linux__
                VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
                VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME
#endif
            };
        };

        create_config_.get_device_features = [](const VkInstance&, const VkPhysicalDevice&) {
            VkPhysicalDeviceFeatures features {};
            features.samplerAnisotropy = VK_TRUE;
            features.geometryShader = VK_TRUE;
            features.shaderInt16 = VK_TRUE;
            features.wideLines = VK_TRUE;
            features.fragmentStoresAndAtomics = VK_TRUE;
            features.shaderStorageImageExtendedFormats = VK_TRUE;
            features.shaderStorageImageReadWithoutFormat = VK_TRUE;
            features.shaderStorageImageWriteWithoutFormat = VK_TRUE;

            VkPhysicalDeviceVulkan11Features features11 {};
            features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
            features11.multiview = VK_TRUE;

            VkPhysicalDeviceVulkan12Features features12 {};
            features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
            features12.bufferDeviceAddress = VK_TRUE;
            features12.shaderFloat16 = VK_TRUE;
            features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
            features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
            features12.shaderUniformBufferArrayNonUniformIndexing = VK_TRUE;
            features12.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
            features12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
            features12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
            features12.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
            features12.descriptorBindingPartiallyBound = VK_TRUE;
            features12.runtimeDescriptorArray = VK_TRUE;
            features12.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
            features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
            features12.descriptorIndexing = VK_TRUE;
            features12.timelineSemaphore = VK_TRUE;

            VkPhysicalDeviceVulkan13Features features13 {};
            features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
            features13.synchronization2 = VK_TRUE;
            features13.dynamicRendering = VK_TRUE;

            // 移除: swapchainMaintenance1 特性 (部分 AMD 核显不支持)
            // VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT features_swapchain_maintenance1{};
            // features_swapchain_maintenance1.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
            // features_swapchain_maintenance1.swapchainMaintenance1 = VK_TRUE;

            return (DeviceFeaturesChain() | features | features11 | features12 | features13);
        };
    }

    void HardwareContext::ensure_volk()
    {
        static std::once_flag volk_once;

        std::call_once(volk_once, [] {
            configure_vulkan_layer_isolation();
            if (volkInitialize() != VK_SUCCESS)
            {
                Diagnostics::write(Diagnostics::Level::Error, "VULKAN PROFILE", "COMPATIBILITY FAIL: failed to initialize Volk/Vulkan loader.");
                throw std::runtime_error("Failed to initialize Volk!");
            }

            uint32_t loader_version = VK_API_VERSION_1_0;
            if (vkEnumerateInstanceVersion != nullptr)
            {
                const VkResult result = vkEnumerateInstanceVersion(&loader_version);
                if (result != VK_SUCCESS)
                {
                    Diagnostics::write(Diagnostics::Level::Warning,
                                       "VULKAN PROFILE",
                                       "vkEnumerateInstanceVersion failed. VkResult=" + std::to_string(static_cast<int>(result)));
                    return;
                }
            }

            std::ostringstream stream;
            stream << "Vulkan loader profile\n"
                   << "  loader_api_version: " << api_version_string(loader_version) << '\n'
                   << "  required_api_version: " << api_version_string(required_api_version) << '\n';
            if (loader_version < required_api_version)
            {
                stream << "  COMPATIBILITY FAIL: loader is below the Vulkan API requirement.\n";
            }

            Diagnostics::write(loader_version >= required_api_version ? Diagnostics::Level::Info : Diagnostics::Level::Error,
                               "VULKAN PROFILE",
                               stream.str());

            CFW_LOG_INFO("Vulkan loader API: {} (required {})",
                         api_version_string(loader_version),
                         api_version_string(required_api_version));
        });
    }

    void HardwareContext::ensure_instance()
    {
        ensure_volk();
        std::call_once(instance_once_, [this] {
            create_instance();
            volkLoadInstance(instance_);
        });
    }

    void HardwareContext::ensure_devices()
    {
        std::call_once(devices_once_, [this] {
            ensure_instance();
            create_devices();
            //setup_cross_device_semaphores();
            choose_main_device();

            CFW_LOG_DEBUG("Hardware Context initialized with {} device(s)", devices_.size());
        });
    }

    void HardwareContext::create_instance()
    {
        auto requested_extensions = create_config_.get_instance_extensions(instance_, nullptr);
        std::vector<const char*> requested_layers;

#if HORIZON_ENABLE_VULKAN_VALIDATION
        const bool enable_validation = validation_layer_available();
        if (enable_validation)
        {
            requested_extensions.insert(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            requested_extensions.insert(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
            requested_extensions.insert(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
            requested_layers.push_back(validation_layer_name);
        }
        else
        {
            CFW_LOG_WARNING("Vulkan validation layer '{}' is not available.", validation_layer_name);
        }
#endif

        const std::set<const char*> requested_extensions_report = requested_extensions;
        const auto enabled_extensions = supported_instance_extensions(std::move(requested_extensions), requested_layers);
#if HORIZON_ENABLE_VULKAN_VALIDATION
        const bool debug_utils_enabled = contains_name(enabled_extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        const bool validation_features_enabled = contains_name(enabled_extensions, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
        write_instance_extension_report(requested_extensions_report,
                                        enabled_extensions,
                                        requested_layers,
                                        enable_validation,
                                        debug_utils_enabled,
                                        validation_features_enabled);
#endif

        VkApplicationInfo app_info {};
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.apiVersion = required_api_version;

        VkInstanceCreateInfo instance_info {};
        instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instance_info.pApplicationInfo = &app_info;
        instance_info.enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size());
        instance_info.ppEnabledExtensionNames = enabled_extensions.data();
        instance_info.enabledLayerCount = static_cast<uint32_t>(requested_layers.size());
        instance_info.ppEnabledLayerNames = requested_layers.data();

#if HORIZON_ENABLE_VULKAN_VALIDATION
        VkDebugUtilsMessengerCreateInfoEXT debug_info {};
        VkValidationFeaturesEXT validation_features {};
        if (!requested_layers.empty())
        {
            void* p_next = nullptr;

            if (debug_utils_enabled)
            {
                debug_info = debug_messenger_create_info();
                debug_info.pNext = p_next;
                p_next = &debug_info;
            }
            else
            {
                CFW_LOG_WARNING("Vulkan validation debug callback disabled because '{}' is not supported.", VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            }

            if (validation_features_enabled)
            {
                validation_features = validation_features_create_info();
                validation_features.pNext = p_next;
                p_next = &validation_features;
            }
            else
            {
                CFW_LOG_WARNING("Extended Vulkan validation features disabled because '{}' is not supported.", VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
            }

            instance_info.pNext = p_next;
        }
#endif

        VkInstance instance = VK_NULL_HANDLE;
        const VkResult result = vkCreateInstance(&instance_info, nullptr, &instance);
        if (result != VK_SUCCESS)
        {
            Diagnostics::write(Diagnostics::Level::Error,
                               "VK_ERROR",
                               "vkCreateInstance failed. VkResult=" + std::to_string(static_cast<int>(result)));
            throw std::runtime_error("Failed to create Vulkan instance.");
        }

        instance_ = instance;

#if HORIZON_ENABLE_VULKAN_VALIDATION
        if (!requested_layers.empty() && debug_utils_enabled)
        {
            try
            {
                setup_debug_messenger();
                CFW_LOG_INFO("Khronos Validation Layer Active. Current Enables: {}", validation_feature_list());
            }
            catch (...)
            {
                vkDestroyInstance(instance_, nullptr);
                instance_ = VK_NULL_HANDLE;
                throw;
            }
        }
#endif
    }

    void HardwareContext::create_devices()
    {
        uint32_t device_count = 0;
        VkResult result = vkEnumeratePhysicalDevices(instance_, &device_count, nullptr);
        if (result != VK_SUCCESS)
        {
            Diagnostics::write(Diagnostics::Level::Error,
                               "VK_ERROR",
                               "vkEnumeratePhysicalDevices(count) failed. VkResult=" + std::to_string(static_cast<int>(result)));
            throw std::runtime_error("Failed to enumerate Vulkan physical devices.");
        }

        if (device_count == 0)
        {
            Diagnostics::write(Diagnostics::Level::Error, "VULKAN PROFILE", "COMPATIBILITY FAIL: no Vulkan physical devices were found.");
            throw std::runtime_error("Failed to find GPUs! Please ensure you have a Vulkan-capable GPU.");
        }

        std::vector<VkPhysicalDevice> physical_devices(device_count);
        result = vkEnumeratePhysicalDevices(instance_, &device_count, physical_devices.data());
        if (result != VK_SUCCESS)
        {
            Diagnostics::write(Diagnostics::Level::Error,
                               "VK_ERROR",
                               "vkEnumeratePhysicalDevices(data) failed. VkResult=" + std::to_string(static_cast<int>(result)));
            throw std::runtime_error("Failed to enumerate Vulkan physical devices.");
        }

        std::vector<std::shared_ptr<DeviceContext>> devices;
        devices.reserve(physical_devices.size());
        for (VkPhysicalDevice physical_device : physical_devices)
        {
            const DeviceCompatibilityReport report = analyze_device_compatibility(physical_device, create_config_, instance_);
            write_device_compatibility_report(report);
            if (!report.compatible())
            {
                continue;
            }

            auto device = std::make_shared<DeviceContext>();
            try
            {
                device->device_manager.initialize(create_config_, instance_, physical_device);
                device->resource_manager.initialize(device->device_manager);
            }
            catch (const std::exception& error)
            {
                Diagnostics::write(Diagnostics::Level::Error,
                                   "VK_ERROR",
                                   std::string("Device initialization failed for '") + report.properties.deviceName + "': " + error.what());
                throw;
            }
            devices.push_back(std::move(device));
        }

        if (devices.empty())
        {
            const std::string required_version = api_version_string(required_api_version);
            Diagnostics::write(Diagnostics::Level::Error,
                               "VULKAN PROFILE",
                               "COMPATIBILITY FAIL: no Vulkan " + required_version +
                                   "-capable GPU satisfied Horizon's required queue and feature profile.");
            throw std::runtime_error("No compatible Vulkan " + required_version + "-capable GPU found. See horizon-vulkan-diagnostics.txt.");
        }

        devices_ = std::move(devices);
    }

    void HardwareContext::setup_debug_messenger()
    {
#if HORIZON_ENABLE_VULKAN_VALIDATION
        VkDebugUtilsMessengerCreateInfoEXT create_info = debug_messenger_create_info();
        const VkResult result = create_debug_utils_messenger_ext(instance_, &create_info, nullptr, &debug_messenger_);
        if (result != VK_SUCCESS)
        {
            Diagnostics::write(Diagnostics::Level::Error,
                               "VK_ERROR",
                               "vkCreateDebugUtilsMessengerEXT failed. VkResult=" + std::to_string(static_cast<int>(result)));
            throw std::runtime_error("Failed to set up debug messenger!");
        }
#endif
    }

    void HardwareContext::cleanup_debug_messenger()
    {
#if HORIZON_ENABLE_VULKAN_VALIDATION
        if (debug_messenger_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE)
        {
            destroy_debug_utils_messenger_ext(instance_, debug_messenger_, nullptr);
            debug_messenger_ = VK_NULL_HANDLE;
        }
#endif
    }

    void HardwareContext::setup_cross_device_semaphores()
    {
        if (devices_.size() <= 1)
        {
            return;
        }

        for (size_t device_index = 0; device_index < devices_.size(); ++device_index)
        {
            std::vector<DeviceManager*> peer_devices;
            peer_devices.reserve(devices_.size() - 1);

            for (size_t peer_index = 0; peer_index < devices_.size(); ++peer_index)
            {
                if (device_index != peer_index)
                {
                    peer_devices.push_back(&devices_[peer_index]->device_manager);
                }
            }

            //devices_[device_index]->device_manager.importForeignSemaphores(peer_devices);
        }

        CFW_LOG_DEBUG("Cross-device timeline semaphore import completed for {} devices", devices_.size());
    }

    void HardwareContext::choose_main_device()
    {
        if (devices_.empty())
        {
            throw std::runtime_error("No hardware devices available.");
        }

        main_device_ = *std::min_element(devices_.begin(), devices_.end(), [](const auto& left, const auto& right) {
            const auto left_type = left->device_manager.properties().properties.deviceType;
            const auto right_type = right->device_manager.properties().properties.deviceType;
            return device_type_priority(left_type) < device_type_priority(right_type);
        });

        const DeviceManager& device_manager = main_device_->device_manager;
        const VkPhysicalDevice physical_device = device_manager.physical_device();
        const VkPhysicalDeviceProperties& properties = device_manager.properties().properties;
        const VkPhysicalDeviceMemoryProperties memory_properties = query_device_memory_properties(physical_device);
        const DeviceMemorySummary memory_summary = summarize_device_memory(memory_properties);
        const VkPhysicalDeviceDriverProperties driver_properties = query_device_driver_properties(physical_device);
        const SystemMemorySummary system_memory = query_system_memory();
        const char* driver_name = driver_properties.driverName[0] == '\0' ? "unknown" : driver_properties.driverName;
        const char* driver_info = driver_properties.driverInfo[0] == '\0' ? "unknown" : driver_properties.driverInfo;

        CFW_LOG_INFO("Vulkan GPU: {} ({}, API {}, driver: {} - {})",
                     properties.deviceName,
                     device_type_name(properties.deviceType),
                     api_version_string(properties.apiVersion),
                     driver_name,
                     driver_info);
        CFW_LOG_INFO("Vulkan device memory: VRAM={}, host-visible={}",
                     byte_size_string(memory_summary.device_local_bytes),
                     byte_size_string(memory_summary.host_visible_bytes));
        if (system_memory.available)
        {
            CFW_LOG_INFO("System memory: total={}, available={}",
                         byte_size_string(system_memory.total_physical_bytes),
                         byte_size_string(system_memory.available_physical_bytes));
        }
        CFW_LOG_INFO("Vulkan queue families: {}", device_manager.queue_families().size());

        std::ostringstream stream;
        stream << "Selected main device: " << properties.deviceName << '\n'
               << "  type: " << device_type_name(properties.deviceType) << '\n'
               << "  api_version: " << api_version_string(properties.apiVersion) << '\n'
               << "  driver_name: " << driver_name << '\n'
               << "  driver_info: " << driver_info << '\n'
               << "  device_local_memory: " << byte_size_string(memory_summary.device_local_bytes) << '\n'
               << "  host_visible_memory: " << byte_size_string(memory_summary.host_visible_bytes) << '\n';
        if (system_memory.available)
        {
            stream << "  system_memory_total: " << byte_size_string(system_memory.total_physical_bytes) << '\n'
                   << "  system_memory_available: " << byte_size_string(system_memory.available_physical_bytes) << '\n';
        }
        stream << "  queue_families: " << device_manager.queue_families().size() << '\n';

        Diagnostics::write(Diagnostics::Level::Info, "VULKAN PROFILE", stream.str());
    }
}
