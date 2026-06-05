#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include "hardware_context.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "corona/kernel/core/i_logger.h"
#include "resource_pool.h"

#define VOLK_IMPLEMENTATION
#include <volk.h>

namespace Corona::Horizon
{
#ifndef HORIZON_ENABLE_VALIDATION
#define HORIZON_ENABLE_VALIDATION 1
#endif

    HardwareContext g_hardware_context;

    constexpr uint32_t required_api_version = VK_API_VERSION_1_4;

#if HORIZON_ENABLE_VALIDATION
    constexpr const char* validation_layer_name = "VK_LAYER_KHRONOS_validation";

    constexpr std::array<VkValidationFeatureEnableEXT, 5> enabled_validation_features {
        VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
        VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT,
        VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
        VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT,
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

    bool supports_required_api(VkPhysicalDevice physical_device)
    {
        VkPhysicalDeviceProperties properties {};
        vkGetPhysicalDeviceProperties(physical_device, &properties);

        if (properties.apiVersion >= required_api_version)
        {
            return true;
        }

        CFW_LOG_WARNING("Skipping device '{}' due to Vulkan API {}.{}.{} (< 1.4)",
                        properties.deviceName,
                        VK_VERSION_MAJOR(properties.apiVersion),
                        VK_VERSION_MINOR(properties.apiVersion),
                        VK_VERSION_PATCH(properties.apiVersion));

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

#if HORIZON_ENABLE_VALIDATION
    bool contains_name(const std::vector<const char*>& names, const char* target)
    {
        return std::any_of(names.begin(), names.end(), [target](const char* name) {
            return std::strcmp(name, target) == 0;
        });
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

    VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                                         VkDebugUtilsMessageTypeFlagsEXT message_type,
                                                         const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
                                                         void* user_data)
    {
        (void)message_type;
        (void)user_data;

        const char* message = callback_data != nullptr && callback_data->pMessage != nullptr ? callback_data->pMessage : "<no message>";
        const char* message_id = callback_data != nullptr && callback_data->pMessageIdName != nullptr ? callback_data->pMessageIdName : "<no id>";
        const int32_t message_id_number = callback_data != nullptr ? callback_data->messageIdNumber : 0;

        if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
        {
            CFW_LOG_ERROR("[Vulkan validation] {} ({}): {}", message_id, message_id_number, message);
        }
        else if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
        {
            CFW_LOG_WARNING("[Vulkan validation] {} ({}): {}", message_id, message_id_number, message);
        }
        else if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) != 0)
        {
            CFW_LOG_INFO("[Vulkan validation] {} ({}): {}", message_id, message_id_number, message);
        }
        else
        {
            CFW_LOG_DEBUG("[Vulkan validation] {} ({}): {}", message_id, message_id_number, message);
        }

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

    VulkanValidationReport make_validation_report()
    {
        VulkanValidationReport report;
        report.compiled = true;

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
#if HORIZON_ENABLE_VALIDATION
        return make_validation_report();
#else
        VulkanValidationReport report;
        report.compiled = false;
        report.missing_requirements.emplace_back("HORIZON_ENABLE_VALIDATION is disabled at compile time.");
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
            if (volkInitialize() != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to initialize Volk!");
            }
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

#if HORIZON_ENABLE_VALIDATION
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

        const auto enabled_extensions = supported_instance_extensions(std::move(requested_extensions), requested_layers);
#if HORIZON_ENABLE_VALIDATION
        const bool debug_utils_enabled = contains_name(enabled_extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        const bool validation_features_enabled = contains_name(enabled_extensions, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
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

#if HORIZON_ENABLE_VALIDATION
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
            throw std::runtime_error("Failed to create Vulkan instance.");
        }

        instance_ = instance;

#if HORIZON_ENABLE_VALIDATION
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
            throw std::runtime_error("Failed to enumerate Vulkan physical devices.");
        }

        if (device_count == 0)
        {
            throw std::runtime_error("Failed to find GPUs! Please ensure you have a Vulkan-capable GPU.");
        }

        std::vector<VkPhysicalDevice> physical_devices(device_count);
        result = vkEnumeratePhysicalDevices(instance_, &device_count, physical_devices.data());
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to enumerate Vulkan physical devices.");
        }

        std::vector<std::shared_ptr<DeviceContext>> devices;
        devices.reserve(physical_devices.size());
        for (VkPhysicalDevice physical_device : physical_devices)
        {
            if (!supports_required_api(physical_device))
            {
                continue;
            }

            auto device = std::make_shared<DeviceContext>();
            device->device_manager.initialize(create_config_, instance_, physical_device);
            device->resource_manager.initialize(device->device_manager);
            devices.push_back(std::move(device));
        }

        if (devices.empty())
        {
            throw std::runtime_error("No Vulkan 1.4-capable GPU found.");
        }

        devices_ = std::move(devices);
    }

    void HardwareContext::setup_debug_messenger()
    {
#if HORIZON_ENABLE_VALIDATION
        VkDebugUtilsMessengerCreateInfoEXT create_info = debug_messenger_create_info();
        if (create_debug_utils_messenger_ext(instance_, &create_info, nullptr, &debug_messenger_) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to set up debug messenger!");
        }
#endif
    }

    void HardwareContext::cleanup_debug_messenger()
    {
#if HORIZON_ENABLE_VALIDATION
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

        CFW_LOG_DEBUG("Selected main device: {}", main_device_->device_manager.properties().properties.deviceName);
    }
}
