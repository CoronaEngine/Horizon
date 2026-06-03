#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include <volk.h>

#include "hardware_wrapper_vulkan/hardware/device_manager.h"
#include "hardware_wrapper_vulkan/hardware/resource_manager.h"

namespace Corona::Horizon
{
    struct HardwareContextTestAccess;

    struct HardwareContext
    {
    public:
        struct DeviceContext
        {
            DeviceManager device_manager;
            ResourceManager resource_manager;
        };

        HardwareContext();
        ~HardwareContext();

        HardwareContext(const HardwareContext&) = delete;
        HardwareContext& operator=(const HardwareContext&) = delete;
        HardwareContext(HardwareContext&&) = delete;
        HardwareContext& operator=(HardwareContext&&) = delete;

        [[nodiscard]] VkInstance instance();
        [[nodiscard]] std::vector<std::shared_ptr<DeviceContext>> devices();
        [[nodiscard]] std::shared_ptr<DeviceContext> main_device();

    private:
        friend struct HardwareContextTestAccess;

        void prepare_features();
        void create_instance();
        void create_devices();
        void choose_main_device();
        void setup_cross_device_semaphores();
        void setup_debug_messenger();
        void cleanup_debug_messenger();

        void ensure_volk();
        void ensure_instance();
        void ensure_devices();

        std::once_flag instance_once_;
        std::once_flag devices_once_;

        VkInstance instance_ { VK_NULL_HANDLE };
        VkDebugUtilsMessengerEXT debug_messenger_ { VK_NULL_HANDLE };
        HardwareCreateConfig create_config_ {};

        std::vector<std::shared_ptr<DeviceContext>> devices_;
        std::shared_ptr<DeviceContext> main_device_;
    };

    HardwareContext& hardware_context();
    HardwareContext::DeviceContext& main_device_context();
    ResourceManager& resource_manager();
    DeviceManager& device_manager();
    VkInstance vulkan_instance();
    std::vector<std::shared_ptr<HardwareContext::DeviceContext>> all_devices();
    extern HardwareContext g_hardware_context;
}
