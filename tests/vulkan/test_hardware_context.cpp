#include "test_registry.h"

#include "hardware_wrapper_vulkan/hardware_context.h"

#include <stdexcept>
#include <string>
#include <vector>

#include <volk.h>

namespace Corona::Horizon
{
    struct HardwareContextTestAccess
    {
        [[nodiscard]] static bool is_instance_loaded(const HardwareContext& context) noexcept
        {
            return context.instance_ != VK_NULL_HANDLE;
        }

        [[nodiscard]] static bool are_devices_loaded(const HardwareContext& context) noexcept
        {
            return !context.devices_.empty();
        }
    };
}

namespace
{
    constexpr uint32_t required_api_version = VK_API_VERSION_1_4;

    struct PrecheckResult
    {
        bool available { false };
        std::string reason;
    };

    [[nodiscard]] std::string vk_result_name(VkResult result)
    {
        return std::to_string(static_cast<int>(result));
    }

    [[nodiscard]] std::string api_version_string(uint32_t version)
    {
        return std::to_string(VK_VERSION_MAJOR(version)) + "." +
               std::to_string(VK_VERSION_MINOR(version)) + "." +
               std::to_string(VK_VERSION_PATCH(version));
    }

    void expect(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    [[nodiscard]] PrecheckResult check_vulkan_environment()
    {
        if (volkInitialize() != VK_SUCCESS)
        {
            return { false, "Vulkan loader is not available." };
        }

        uint32_t loader_version = VK_API_VERSION_1_0;
        if (vkEnumerateInstanceVersion != nullptr)
        {
            const VkResult result = vkEnumerateInstanceVersion(&loader_version);
            if (result != VK_SUCCESS)
            {
                return { false, "vkEnumerateInstanceVersion failed with VkResult " + vk_result_name(result) + "." };
            }
        }

        if (loader_version < required_api_version)
        {
            return { false,
                     "Vulkan loader reports " + api_version_string(loader_version) +
                         ", but " + api_version_string(required_api_version) + " is required." };
        }

        VkApplicationInfo app_info {};
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.apiVersion = required_api_version;

        VkInstanceCreateInfo instance_info {};
        instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instance_info.pApplicationInfo = &app_info;

        VkInstance instance = VK_NULL_HANDLE;
        VkResult result = vkCreateInstance(&instance_info, nullptr, &instance);
        if (result != VK_SUCCESS)
        {
            return { false, "vkCreateInstance failed with VkResult " + vk_result_name(result) + "." };
        }

        volkLoadInstance(instance);

        uint32_t device_count = 0;
        result = vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
        if (result != VK_SUCCESS)
        {
            vkDestroyInstance(instance, nullptr);
            return { false, "vkEnumeratePhysicalDevices failed with VkResult " + vk_result_name(result) + "." };
        }

        if (device_count == 0)
        {
            vkDestroyInstance(instance, nullptr);
            return { false, "No Vulkan physical devices were found." };
        }

        std::vector<VkPhysicalDevice> physical_devices(device_count);
        result = vkEnumeratePhysicalDevices(instance, &device_count, physical_devices.data());
        if (result != VK_SUCCESS)
        {
            vkDestroyInstance(instance, nullptr);
            return { false, "vkEnumeratePhysicalDevices failed with VkResult " + vk_result_name(result) + "." };
        }

        for (VkPhysicalDevice physical_device : physical_devices)
        {
            VkPhysicalDeviceProperties properties {};
            vkGetPhysicalDeviceProperties(physical_device, &properties);
            if (properties.apiVersion >= required_api_version)
            {
                vkDestroyInstance(instance, nullptr);
                return { true, {} };
            }
        }

        vkDestroyInstance(instance, nullptr);
        return { false, "No Vulkan 1.4-capable physical device was found." };
    }

    [[nodiscard]] const PrecheckResult& vulkan_precheck()
    {
        static const PrecheckResult result = check_vulkan_environment();
        return result;
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult require_vulkan_environment()
    {
        const PrecheckResult& precheck = vulkan_precheck();
        if (!precheck.available)
        {
            return Corona::Horizon::Tests::TestResult::skip(precheck.reason);
        }

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_stack_construction_is_lazy()
    {
        Corona::Horizon::HardwareContext context;
        expect(!Corona::Horizon::HardwareContextTestAccess::is_instance_loaded(context),
               "Stack construction should not create a Vulkan instance.");
        expect(!Corona::Horizon::HardwareContextTestAccess::are_devices_loaded(context),
               "Stack construction should not enumerate devices.");
        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_local_context_lifecycle()
    {
        const auto environment = require_vulkan_environment();
        if (environment.status == Corona::Horizon::Tests::TestStatus::Skipped)
        {
            return environment;
        }

        Corona::Horizon::HardwareContext context;

        expect(!Corona::Horizon::HardwareContextTestAccess::is_instance_loaded(context),
               "New context should start without a Vulkan instance.");
        expect(!Corona::Horizon::HardwareContextTestAccess::are_devices_loaded(context),
               "New context should start without loaded devices.");

        const VkInstance instance = context.instance();
        expect(instance != VK_NULL_HANDLE, "instance() should create a Vulkan instance.");
        expect(Corona::Horizon::HardwareContextTestAccess::is_instance_loaded(context),
               "instance() should mark the Vulkan instance as loaded.");
        expect(!Corona::Horizon::HardwareContextTestAccess::are_devices_loaded(context),
               "instance() should not enumerate devices.");

        const auto& devices = context.devices();
        expect(Corona::Horizon::HardwareContextTestAccess::are_devices_loaded(context),
               "devices() should mark devices as loaded.");
        expect(!devices.empty(), "devices() should expose at least one device.");
        expect(context.main_device() != nullptr, "devices() should select a main device.");

        for (const std::shared_ptr<Corona::Horizon::HardwareContext::DeviceContext>& device : devices)
        {
            expect(device != nullptr, "devices() should not expose null device contexts.");
            const Corona::Horizon::DeviceManager& device_manager = device->device_manager;
            expect(device_manager.physical_device() != VK_NULL_HANDLE,
                   "DeviceManager should keep the selected physical device.");
            expect(device_manager.logical_device() != VK_NULL_HANDLE,
                   "DeviceManager should create a logical Vulkan device.");
            expect(!device_manager.queue_families().empty(),
                   "DeviceManager should capture at least one queue family.");
            expect(device_manager.queue_for(Corona::Horizon::QueueCapability::Transfer) != nullptr,
                   "DeviceManager should expose a queue usable for transfer work.");
        }

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_global_context_entrypoints()
    {
        const auto environment = require_vulkan_environment();
        if (environment.status == Corona::Horizon::Tests::TestStatus::Skipped)
        {
            return environment;
        }

        auto& context = Corona::Horizon::hardware_context();
        expect(&context == &Corona::Horizon::hardware_context(), "hardware_context() should return a stable singleton.");

        const VkInstance instance = Corona::Horizon::vulkan_instance();
        expect(instance != VK_NULL_HANDLE, "vulkan_instance() should create a Vulkan instance.");
        expect(Corona::Horizon::HardwareContextTestAccess::is_instance_loaded(context),
               "vulkan_instance() should mark the singleton instance as loaded.");

        const auto& devices = Corona::Horizon::all_devices();
        expect(Corona::Horizon::HardwareContextTestAccess::are_devices_loaded(context),
               "all_devices() should load singleton devices.");
        expect(!devices.empty(), "all_devices() should expose at least one device.");
        expect(context.main_device() != nullptr, "all_devices() should select a singleton main device.");

        expect(&Corona::Horizon::main_device_context() == context.main_device().get(),
               "main_device_context() should return the selected main device.");
        expect(&Corona::Horizon::resource_manager() == &context.main_device()->resource_manager,
               "resource_manager() should come from the selected main device.");
        expect(&Corona::Horizon::device_manager() == &context.main_device()->device_manager,
               "device_manager() should come from the selected main device.");

        return Corona::Horizon::Tests::TestResult::pass();
    }
}

namespace Corona::Horizon::Tests
{
    std::vector<TestCase> hardware_context_tests()
    {
        return {
            {
                "hardware_context.lazy_construction",
                "Constructing HardwareContext must not create a Vulkan instance or enumerate devices.",
                test_stack_construction_is_lazy,
            },
            {
                "hardware_context.local_lifecycle",
                "A local HardwareContext creates VkInstance on instance(), then loads devices and main_device() on demand.",
                test_local_context_lifecycle,
            },
            {
                "hardware_context.global_entrypoints",
                "Global helpers hardware_context(), vulkan_instance(), all_devices(), resource_manager(), and device_manager() use the same lazy singleton.",
                test_global_context_entrypoints,
            },
        };
    }
}
