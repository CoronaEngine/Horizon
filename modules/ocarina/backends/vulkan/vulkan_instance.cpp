//
// Created by Zero on 06/06/2022.
//
#include "rhi/device.h"
#include "vulkan_instance.h"
#include "util.h"
#if defined(_WIN32)
#include <vulkan/vulkan_win32.h>
#endif
#include "vulkan_debug.h"

namespace ocarina {

VulkanInstance::VulkanInstance(const InstanceCreation& instanceCreation) {
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = instanceCreation.applicationName;
    appInfo.pEngineName = instanceCreation.applicationName;
    appInfo.apiVersion = VK_API_VERSION_1_1;
    validation_ = instanceCreation.validation;

    std::vector<const char *> instanceExtensions = {VK_KHR_SURFACE_EXTENSION_NAME};

    // Enable surface extensions depending on os
#if defined(_WIN32)
    instanceExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
    instanceExtensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#elif defined(_DIRECT2DISPLAY)
    instanceExtensions.push_back(VK_KHR_DISPLAY_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_DIRECTFB_EXT)
    instanceExtensions.push_back(VK_EXT_DIRECTFB_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
    instanceExtensions.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
    instanceExtensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_IOS_MVK)
    instanceExtensions.push_back(VK_MVK_IOS_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_MACOS_MVK)
    instanceExtensions.push_back(VK_MVK_MACOS_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_HEADLESS_EXT)
    instanceExtensions.push_back(VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_SCREEN_QNX)
    instanceExtensions.push_back(VK_QNX_SCREEN_SURFACE_EXTENSION_NAME);
#endif
    

    // Get extensions supported by the instance and store for later use
    uint32_t extCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extCount);
    if (extCount > 0) {
        if (vkEnumerateInstanceExtensionProperties(nullptr, &extCount, &extensions.front()) == VK_SUCCESS) {
            for (VkExtensionProperties &extension : extensions) {
                m_supportedInstanceExtensions.push_back(extension.extensionName);
            }
        }
    }

    if (validation_ && std::find(m_supportedInstanceExtensions.begin(), m_supportedInstanceExtensions.end(), VK_EXT_DEBUG_UTILS_EXTENSION_NAME)
        != m_supportedInstanceExtensions.end())
    {
        instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkInstanceCreateInfo instanceCreateInfo = {};
    instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCreateInfo.pNext = NULL;
    instanceCreateInfo.pApplicationInfo = &appInfo;

    VkDebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCI{};
    if (validation_) {
        debugUtilsMessengerCI.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugUtilsMessengerCI.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugUtilsMessengerCI.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        debugUtilsMessengerCI.pfnUserCallback = debug_message_callback;
        debugUtilsMessengerCI.pNext = instanceCreateInfo.pNext;
        instanceCreateInfo.pNext = &debugUtilsMessengerCI;
    }

    // The VK_LAYER_KHRONOS_validation contains all current validation functionality.
    // Note that on Android this layer requires at least NDK r20
    const char *validationLayerName = "VK_LAYER_KHRONOS_validation";
    if (validation_) {
        // Check if this layer is available at instance level
        uint32_t instanceLayerCount;
        vkEnumerateInstanceLayerProperties(&instanceLayerCount, nullptr);
        std::vector<VkLayerProperties> instanceLayerProperties(instanceLayerCount);
        vkEnumerateInstanceLayerProperties(&instanceLayerCount, instanceLayerProperties.data());
        bool validationLayerPresent = false;
        for (VkLayerProperties &layer : instanceLayerProperties) {
            if (strcmp(layer.layerName, validationLayerName) == 0) {
                validationLayerPresent = true;
                break;
            }
        }
        if (validationLayerPresent) {
            instanceCreateInfo.ppEnabledLayerNames = &validationLayerName;
            instanceCreateInfo.enabledLayerCount = 1;
        } else {
            std::cerr << "Validation layer VK_LAYER_KHRONOS_validation not present, validation is disabled";
        }
    }
    if (instanceExtensions.size() > 0) {
        instanceCreateInfo.enabledExtensionCount = (uint32_t)instanceExtensions.size();
        instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();
    }

    VkResult result = vkCreateInstance(&instanceCreateInfo, nullptr, &instance_);
    VK_CHECK_RESULT(result);

    if (validation_)
    {
        setup_debugging(instance_);
    }
}

VulkanInstance::~VulkanInstance()
{
    if (validation_)
    {
        free_debug_callback(instance_);
    }
    vkDestroyInstance(instance_, nullptr);
}

}// namespace ocarina


