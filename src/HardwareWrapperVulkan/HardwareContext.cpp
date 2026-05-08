#include "HardwareContext.h"

#include <algorithm>

#include "HardwareUtilsVulkan.h"

#define VOLK_IMPLEMENTATION
#include <volk.h>

HardwareContext globalHardwareContext;

HardwareContext::HardwareContext()
{
    if (volkInitialize() != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to initialize Volk!");
    }

    prepareFeaturesChain();
    createVkInstance(hardwareCreateInfos);

    volkLoadInstance(vkInstance);

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(vkInstance, &deviceCount, nullptr);

    if (deviceCount == 0)
    {
        throw std::runtime_error("Failed to find GPUs! Please ensure you have a Vulkan-capable GPU.");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(vkInstance, &deviceCount, devices.data());

    hardwareUtils.reserve(devices.size());
    for (const auto &physicalDevice : devices)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physicalDevice, &properties);

        if (properties.apiVersion < VK_API_VERSION_1_4)
        {
            CFW_LOG_WARNING("Skipping device '{}' due to Vulkan API {}.{}.{} (< 1.4)",
                            properties.deviceName,
                            VK_VERSION_MAJOR(properties.apiVersion),
                            VK_VERSION_MINOR(properties.apiVersion),
                            VK_VERSION_PATCH(properties.apiVersion));
            continue;
        }

        auto utils = std::make_shared<HardwareUtils>();
        utils->deviceManager.initDeviceManager(hardwareCreateInfos, vkInstance, physicalDevice);
        utils->resourceManager.initResourceManager(utils->deviceManager);
        hardwareUtils.push_back(std::move(utils));
    }

    if (hardwareUtils.empty())
    {
        throw std::runtime_error("No Vulkan 1.4-capable GPU found.");
    }

    //setupCrossDeviceSemaphores();

    chooseMainDevice();

    CFW_LOG_DEBUG("Hardware Context initialized with {} device(s)", hardwareUtils.size());
}

HardwareContext::~HardwareContext()
{
    for (auto &utils : hardwareUtils)
    {
        if (utils)
        {
            utils->resourceManager.cleanUpResourceManager();
            utils->deviceManager.cleanUpDeviceManager();
        }
    }

    hardwareUtils.clear();
    mainDevice.reset();

    cleanupDebugMessenger();

    if (vkInstance != VK_NULL_HANDLE)
    {
        // vkDestroyInstance(vkInstance, nullptr);
        vkInstance = VK_NULL_HANDLE;
    }
}

void HardwareContext::prepareFeaturesChain()
{
    // 配置所需实例扩展
    hardwareCreateInfos.requiredInstanceExtensions = [](const VkInstance &, const VkPhysicalDevice &) {
        std::set<const char *> extensions{
            "VK_KHR_surface",
            // VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME,      // 移除: 未使用且部分设备不支持
            // VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME, // 移除: surface_maintenance1 的依赖
            VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
            VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
            VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME};

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

    // 配置所需设备扩展
    hardwareCreateInfos.requiredDeviceExtensions = [](const VkInstance &, const VkPhysicalDevice &) {
        return std::set<const char *>{
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

    hardwareCreateInfos.requiredDeviceFeatures = [](const VkInstance &, const VkPhysicalDevice &) {
        VkPhysicalDeviceFeatures features{};
        features.samplerAnisotropy = VK_TRUE;
        features.shaderInt16 = VK_TRUE;
        features.wideLines = VK_TRUE;
        features.fragmentStoresAndAtomics = VK_TRUE;

        VkPhysicalDeviceVulkan11Features features11{};
        features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        features11.multiview = VK_TRUE;

        VkPhysicalDeviceVulkan12Features features12{};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        features12.bufferDeviceAddress = VK_TRUE;
        features12.shaderFloat16 = VK_TRUE;
        features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        features12.shaderUniformBufferArrayNonUniformIndexing = VK_TRUE;
        features12.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
        features12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
        features12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
        features12.descriptorBindingPartiallyBound = VK_TRUE;
        features12.runtimeDescriptorArray = VK_TRUE;
        features12.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
        features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
        features12.descriptorIndexing = VK_TRUE;
        features12.timelineSemaphore = VK_TRUE;

        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features13.synchronization2 = VK_TRUE;

        // 移除: swapchainMaintenance1 特性 (部分 AMD 核显不支持)
        // VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT features_swapchain_maintenance1{};
        // features_swapchain_maintenance1.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
        // features_swapchain_maintenance1.swapchainMaintenance1 = VK_TRUE;

        return (DeviceFeaturesChain() | features | features11 | features12 | features13);
    };
}

void HardwareContext::createVkInstance(const CreateCallback &initInfo)
{
    const auto inputExtensions = initInfo.requiredInstanceExtensions(vkInstance, nullptr);
    std::vector<const char *> requiredExtensions(inputExtensions.begin(), inputExtensions.end());
    std::vector<const char *> requiredLayers;

#ifdef CABBAGE_ENGINE_DEBUG
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    const auto validationLayerAvailable = std::any_of(availableLayers.begin(), availableLayers.end(),
                                                      [](const VkLayerProperties &props) {
                                                          return strcmp("VK_LAYER_KHRONOS_validation", props.layerName) == 0;
                                                      });

    if (validationLayerAvailable)
    {
        requiredExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        requiredExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
        requiredLayers.push_back("VK_LAYER_KHRONOS_validation");
    }
#endif

    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());

    requiredExtensions.erase(std::remove_if(requiredExtensions.begin(), requiredExtensions.end(),
                                            [&availableExtensions](const char *ext) {
                                                const bool supported = std::any_of(availableExtensions.begin(), availableExtensions.end(),
                                                                                   [ext](const VkExtensionProperties &props) {
                                                                                       return strcmp(ext, props.extensionName) == 0;
                                                                                   });

                                                if (!supported)
                                                {
                                                    CFW_LOG_WARNING("Warning: Instance extension not supported: {}", ext);
                                                }
                                                return !supported;
                                            }),

                             requiredExtensions.end());

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.apiVersion = VK_API_VERSION_1_4;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size());
    createInfo.ppEnabledExtensionNames = requiredExtensions.data();
    createInfo.enabledLayerCount = static_cast<uint32_t>(requiredLayers.size());
    createInfo.ppEnabledLayerNames = requiredLayers.data();
    createInfo.pNext = nullptr;

#ifdef CABBAGE_ENGINE_DEBUG
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (!requiredLayers.empty())
    {
        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = debugCallback;
        createInfo.pNext = &debugCreateInfo;
    }
#endif

    coronaHardwareCheck(vkCreateInstance(&createInfo, nullptr, &vkInstance));

#ifdef CABBAGE_ENGINE_DEBUG
    if (!requiredLayers.empty())
    {
        setupDebugMessenger();
    }
#endif
}

void HardwareContext::setupDebugMessenger()
{
#ifdef CABBAGE_ENGINE_DEBUG
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;

    if (createDebugUtilsMessengerEXT(vkInstance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to set up debug messenger!");
    }
#endif
}

void HardwareContext::cleanupDebugMessenger()
{
#ifdef CABBAGE_ENGINE_DEBUG
    if (debugMessenger != VK_NULL_HANDLE && vkInstance != VK_NULL_HANDLE)
    {
        destroyDebugUtilsMessengerEXT(vkInstance, debugMessenger, nullptr);
        debugMessenger = VK_NULL_HANDLE;
    }
#endif
}

void HardwareContext::setupCrossDeviceSemaphores()
{
    if (hardwareUtils.size() <= 1)
    {
        return; // 单设备无需跨设备导入
    }

    for (size_t i = 0; i < hardwareUtils.size(); ++i)
    {
        std::vector<DeviceManager *> otherDevices;
        otherDevices.reserve(hardwareUtils.size() - 1);

        for (size_t j = 0; j < hardwareUtils.size(); ++j)
        {
            if (i != j)
            {
                otherDevices.push_back(&hardwareUtils[j]->deviceManager);
            }
        }

        hardwareUtils[i]->deviceManager.importForeignSemaphores(otherDevices);
    }

    CFW_LOG_DEBUG("Cross-device timeline semaphore import completed for {} devices", hardwareUtils.size());
}

void HardwareContext::chooseMainDevice()
{
    if (hardwareUtils.empty())
    {
        throw std::runtime_error("No hardware devices available!");
    }

    mainDevice = *std::min_element(hardwareUtils.begin(), hardwareUtils.end(),
                                   [](const std::shared_ptr<HardwareUtils> &a, const std::shared_ptr<HardwareUtils> &b) {
                                       const auto typeA = a->deviceManager.getFeaturesUtils().supportedProperties.properties.deviceType;
                                       const auto typeB = b->deviceManager.getFeaturesUtils().supportedProperties.properties.deviceType;
                                       return getDeviceTypePriority(typeA) < getDeviceTypePriority(typeB);
                                   });

    CFW_LOG_DEBUG("Selected main device: {}",
                  mainDevice->deviceManager.getFeaturesUtils().supportedProperties.properties.deviceName);
}
