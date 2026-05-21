#pragma once

namespace Corona::Horizon::Vulkan
{
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

        [[nodiscard]] VkInstance instance() const { return instance_; }
        [[nodiscard]] const std::vector<std::shared_ptr<DeviceContext>>& devices() const { return devices_; }
        [[nodiscard]] std::shared_ptr<DeviceContext> main_device() const { return main_device_; }

    private:
        void prepare_features();
        void create_instance();
        void create_devices();
        void choose_main_device();
        void setup_cross_device_semaphores();
        void setup_debug_messenger();
        void cleanup_debug_messenger();

        VkInstance instance_{VK_NULL_HANDLE};
        VkDebugUtilsMessengerEXT debug_messenger_{VK_NULL_HANDLE};
        CreateCallback create_info_{};

        std::vector<std::shared_ptr<DeviceContext>> devices_;
        std::shared_ptr<DeviceContext> main_device_;
    };

    HardwareContext& hardware_context();
    HardwareContext::DeviceContext& main_device_context();
    ResourceManager& resource_manager();
    DeviceManager& device_manager();
    VkInstance vulkan_instance();
    const std::vector<std::shared_ptr<HardwareContext::DeviceContext>>& all_devices();
}
