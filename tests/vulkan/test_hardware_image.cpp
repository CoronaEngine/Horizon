#include "test_registry.h"

#include "hardware_wrapper_vulkan/hardware/command.h"
#include "hardware_wrapper_vulkan/hardware_context.h"
#include "horizon_refac.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

#include <volk.h>

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

    void close_external_handle(Corona::Horizon::ExternalMemoryHandle handle) noexcept
    {
#if defined(_WIN32) || defined(_WIN64)
        if (handle.type == Corona::Horizon::ExternalMemoryHandleType::OpaqueWin32)
        {
            HANDLE native = static_cast<HANDLE>(handle.handle);
            if (native != nullptr && native != INVALID_HANDLE_VALUE)
            {
                CloseHandle(native);
            }
        }
#elif defined(__linux__)
        if (handle.type == Corona::Horizon::ExternalMemoryHandleType::OpaqueFd && handle.fd >= 0)
        {
            close(handle.fd);
        }
#else
        (void)handle;
#endif
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

    [[nodiscard]] Corona::Horizon::HardwareImageOptions host_read_write_image_options() noexcept
    {
        Corona::Horizon::HardwareImageOptions options;
        options.cpu_access = Corona::Horizon::CpuAccessMode::ReadWrite;
        return options;
    }

    [[nodiscard]] Corona::Horizon::HardwareBufferOptions host_read_write_buffer_options() noexcept
    {
        Corona::Horizon::HardwareBufferOptions options;
        options.cpu_access = Corona::Horizon::CpuAccessMode::ReadWrite;
        return options;
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_create_lifetime_and_subresources()
    {
        const auto environment = require_vulkan_environment();
        if (environment.status == Corona::Horizon::Tests::TestStatus::Skipped)
        {
            return environment;
        }

        Corona::Horizon::HardwareImageDesc desc =
            Corona::Horizon::HardwareImageDesc::texture_2d_array(16, 8, 2, Corona::Horizon::Format::RGBA8_UNORM);
        desc.mip_levels = 3;

        std::weak_ptr<const Corona::Horizon::IResourceRef> weak_token;
        std::uintptr_t id = 0;
        Corona::Horizon::HardwareImage survivor;

        try
        {
            Corona::Horizon::HardwareImage image(desc);
            expect(static_cast<bool>(image), "HardwareImage constructor should create a valid image.");
            expect(image.extent().width == 16 && image.extent().height == 8 && image.extent().depth == 1, "HardwareImage should preserve base extent.");
            expect(image.subresource_count() == 6, "HardwareImage should report array layer times mip subresources.");
            expect(image.mip_extent(2).width == 4 && image.mip_extent(2).height == 2, "HardwareImage mip extent should clamp by mip level.");

            id = image.get_image_id();
            expect(id != 0, "HardwareImage resource id should be non-zero.");
            weak_token = Corona::Horizon::ResourceBridge::keep_alive(image);

            Corona::Horizon::HardwareImage layer = image.layer(1);
            expect(static_cast<bool>(layer), "HardwareImage::layer should return a valid view wrapper.");
            expect(layer.get_image_id() == id, "HardwareImage layer views should share the same resource token.");
            expect(layer.subresource_count() == 3, "HardwareImage layer view should keep all mips for one layer.");
            expect(layer.subresource_index(0, 2) == 5, "HardwareImage layer view should calculate absolute subresource indices.");

            Corona::Horizon::HardwareImage mip = image.mip(1);
            expect(static_cast<bool>(mip), "HardwareImage::mip should return a valid view wrapper.");
            expect(mip.subresource_count() == 2, "HardwareImage mip view should keep all layers for one mip.");
            expect(mip.extent().width == 8 && mip.extent().height == 4, "HardwareImage mip view should expose selected mip extent.");

            Corona::Horizon::HardwareImage single = image[1][2];
            expect(static_cast<bool>(single), "HardwareImage layer selector should return a valid single subresource.");
            expect(single.subresource_count() == 1, "HardwareImage subresource view should contain one subresource.");
            expect(single.subresource_index(0, 0) == 5, "HardwareImage subresource view should preserve absolute layer/mip selection.");

            survivor = image;
            image = {};
            expect(static_cast<bool>(survivor), "HardwareImage copy should keep the resource alive after the original resets.");
            expect(!weak_token.expired(), "HardwareImage token should survive while a copy exists.");
        }
        catch (const std::exception& error)
        {
            return Corona::Horizon::Tests::TestResult::skip(std::string("HardwareImage creation is unavailable on this Vulkan device: ") + error.what());
        }

        survivor = {};
        expect(weak_token.expired(), "HardwareImage token should release after the last wrapper resets.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_linear_image_host_io()
    {
        const auto environment = require_vulkan_environment();
        if (environment.status == Corona::Horizon::Tests::TestStatus::Skipped)
        {
            return environment;
        }

        Corona::Horizon::HardwareImageDesc desc =
            Corona::Horizon::HardwareImageDesc::texture_2d(4,
                                                           4,
                                                           Corona::Horizon::Format::RGBA8_UNORM,
                                                           Corona::Horizon::ImageUsageFlags::TransferSrc | Corona::Horizon::ImageUsageFlags::TransferDst,
                                                           "hardware_image.linear",
                                                           host_read_write_image_options());

        try
        {
            Corona::Horizon::HardwareImage image(desc);
            expect(static_cast<bool>(image), "Host-visible HardwareImage should be valid when the device supports linear images.");

            std::array<uint32_t, 16> pixels {};
            for (uint32_t i = 0; i < pixels.size(); ++i)
            {
                pixels[i] = 0xff000000u | i;
            }

            constexpr uint64_t row_pitch = 4 * sizeof(uint32_t);
            constexpr uint64_t slice_pitch = row_pitch * 4;
            expect(image.write<uint32_t>(pixels, row_pitch, slice_pitch), "Host-visible HardwareImage should accept row-pitched writes.");

            std::array<uint32_t, 16> readback {};
            expect(image.read<uint32_t>(readback, row_pitch, slice_pitch), "Host-visible HardwareImage should accept row-pitched reads.");
            expect(std::equal(readback.begin(), readback.end(), pixels.begin()), "HardwareImage host readback should match written pixels.");

            Corona::Horizon::HardwareImage tight_write_image(desc);
            expect(tight_write_image.write<uint32_t>(pixels), "HardwareImage default write pitch should treat input as tightly packed pixels.");
            std::array<uint32_t, 16> tight_write_readback {};
            expect(tight_write_image.read<uint32_t>(tight_write_readback, row_pitch, slice_pitch),
                   "HardwareImage row-pitched read should observe a default tight write.");
            expect(std::equal(tight_write_readback.begin(), tight_write_readback.end(), pixels.begin()),
                   "HardwareImage default tight write should respect Vulkan row pitch padding.");

            Corona::Horizon::HardwareImage tight_read_image(desc);
            expect(tight_read_image.write<uint32_t>(pixels, row_pitch, slice_pitch),
                   "HardwareImage row-pitched write should prepare default tight readback.");
            std::array<uint32_t, 16> tight_readback {};
            expect(tight_read_image.read<uint32_t>(tight_readback), "HardwareImage default read pitch should return tightly packed pixels.");
            expect(std::equal(tight_readback.begin(), tight_readback.end(), pixels.begin()),
                   "HardwareImage default tight read should respect Vulkan row pitch padding.");
        }
        catch (const std::exception& error)
        {
            return Corona::Horizon::Tests::TestResult::skip(std::string("Linear host-visible HardwareImage is unavailable on this Vulkan device: ") + error.what());
        }

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_copy_command_facades()
    {
        const auto environment = require_vulkan_environment();
        if (environment.status == Corona::Horizon::Tests::TestStatus::Skipped)
        {
            return environment;
        }

        Corona::Horizon::HardwareImageDesc desc =
            Corona::Horizon::HardwareImageDesc::texture_2d_array(8, 8, 2, Corona::Horizon::Format::RGBA8_UNORM);
        desc.mip_levels = 2;

        Corona::Horizon::HardwareImage src(desc);
        Corona::Horizon::HardwareImage dst(desc);
        const std::array<uint32_t, 16> staging_data {};
        Corona::Horizon::HardwareBuffer buffer =
            Corona::Horizon::HardwareBuffer::storage<uint32_t>(std::span<const uint32_t>(staging_data),
                                                               "hardware_image.copy.buffer",
                                                               host_read_write_buffer_options());

        Corona::Horizon::CopyImageCommand image_copy = src.copy_to(dst, 1, 0, 1, 0);
        expect(image_copy.copy_region().src_layer == 1, "CopyImageCommand should preserve source layer.");
        expect(image_copy.copy_region().dst_layer == 0, "CopyImageCommand should preserve destination layer.");
        expect(image_copy.copy_region().src_mip == 1, "CopyImageCommand should preserve source mip.");
        expect(image_copy.copy_region().dst_mip == 0, "CopyImageCommand should preserve destination mip.");

        Corona::Horizon::CommandRecorder image_copy_recorder;
        image_copy.record(image_copy_recorder);
        Corona::Horizon::RecordedTask image_copy_task = image_copy_recorder.close();
        expect(image_copy_task.commands.size() == 1, "CopyImageCommand should record one IR command.");
        expect(image_copy_task.commands[0].op == Corona::Horizon::CommandOp::CopyImage, "CopyImageCommand should record CopyImage IR.");

        Corona::Horizon::CopyImageToBufferCommand image_to_buffer = src.copy_to(buffer, 1, 1, sizeof(uint32_t));
        expect(image_to_buffer.copy_region().image_layer == 1, "CopyImageToBufferCommand should preserve image layer.");
        expect(image_to_buffer.copy_region().image_mip == 1, "CopyImageToBufferCommand should preserve image mip.");
        expect(image_to_buffer.copy_region().buffer_offset == sizeof(uint32_t), "CopyImageToBufferCommand should preserve buffer offset.");

        Corona::Horizon::CommandRecorder image_to_buffer_recorder;
        image_to_buffer.record(image_to_buffer_recorder);
        Corona::Horizon::RecordedTask image_to_buffer_task = image_to_buffer_recorder.close();
        expect(image_to_buffer_task.commands.size() == 1, "CopyImageToBufferCommand should record one IR command.");
        expect(image_to_buffer_task.commands[0].op == Corona::Horizon::CommandOp::CopyImageToBuffer, "CopyImageToBufferCommand should record CopyImageToBuffer IR.");

        Corona::Horizon::CopyBufferToImageCommand buffer_to_image = dst.copy_from(buffer, sizeof(uint32_t), 1, 1);
        expect(buffer_to_image.copy_region().image_layer == 1, "CopyBufferToImageCommand from HardwareImage should preserve image layer.");
        expect(buffer_to_image.copy_region().image_mip == 1, "CopyBufferToImageCommand from HardwareImage should preserve image mip.");
        expect(buffer_to_image.copy_region().buffer_offset == sizeof(uint32_t), "CopyBufferToImageCommand from HardwareImage should preserve buffer offset.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_descriptor_and_external_round_trip()
    {
        const auto environment = require_vulkan_environment();
        if (environment.status == Corona::Horizon::Tests::TestStatus::Skipped)
        {
            return environment;
        }

        Corona::Horizon::HardwareImageDesc desc =
            Corona::Horizon::HardwareImageDesc::texture_2d(8, 8, Corona::Horizon::Format::RGBA8_UNORM);
        Corona::Horizon::HardwareImage image(desc);

        try
        {
            const uint32_t first = image.store_descriptor();
            const uint32_t second = image.store_descriptor();
            expect(first == second, "HardwareImage::store_descriptor should return a stable sampled-image descriptor index.");
        }
        catch (const std::runtime_error& error)
        {
            return Corona::Horizon::Tests::TestResult::skip(std::string("Sampled image descriptors are unavailable on this Vulkan device: ") + error.what());
        }

        Corona::Horizon::HardwareImageOptions options;
        options.dedicated = true;
        options.exportable = true;
        Corona::Horizon::HardwareImageDesc external_desc =
            Corona::Horizon::HardwareImageDesc::texture_2d(4,
                                                           4,
                                                           Corona::Horizon::Format::RGBA8_UNORM,
                                                           Corona::Horizon::ImageUsageFlags::Sampled | Corona::Horizon::ImageUsageFlags::TransferDst,
                                                           "hardware_image.external",
                                                           options);

        Corona::Horizon::ExternalMemoryHandle handle {};
        Corona::Horizon::ExternalMemoryHandle second_handle {};
        Corona::Horizon::HardwareImage imported;
        bool import_succeeded = false;

        try
        {
            Corona::Horizon::HardwareImage external_image(external_desc);
            handle = external_image.export_external();
            expect(static_cast<bool>(handle), "HardwareImage::export_external should return a valid handle.");
            second_handle = external_image.export_external();
            expect(static_cast<bool>(second_handle), "Repeated HardwareImage::export_external should return a valid handle.");
            imported = Corona::Horizon::HardwareImage::import_external(handle, external_desc);
            import_succeeded = true;
        }
        catch (const std::exception& error)
        {
#if defined(__linux__)
            if (!import_succeeded)
            {
                close_external_handle(handle);
            }
            close_external_handle(second_handle);
#else
            close_external_handle(handle);
            close_external_handle(second_handle);
#endif
            return Corona::Horizon::Tests::TestResult::skip(std::string("External HardwareImage import/export is unavailable on this Vulkan device: ") + error.what());
        }

#if defined(_WIN32) || defined(_WIN64)
        close_external_handle(handle);
        close_external_handle(second_handle);
#elif defined(__linux__)
        close_external_handle(second_handle);
#endif

        expect(static_cast<bool>(imported), "HardwareImage::import_external should create a valid image wrapper.");
        expect(imported.extent().width == external_desc.extent.width && imported.extent().height == external_desc.extent.height,
               "Imported HardwareImage should preserve logical extent.");

        return Corona::Horizon::Tests::TestResult::pass();
    }
}

namespace Corona::Horizon::Tests
{
    std::vector<TestCase> hardware_image_tests()
    {
        return {
            {
                "hardware_image.create_lifetime_subresources",
                "HardwareImage creation, copy lifetime, and layer/mip views share one resource token.",
                test_create_lifetime_and_subresources,
            },
            {
                "hardware_image.linear_host_io",
                "Host-visible linear HardwareImage supports row-pitched subresource read/write when the device allows it.",
                test_linear_image_host_io,
            },
            {
                "hardware_image.copy_command_facades",
                "HardwareImage copy facades return value commands that preserve image metadata and record typed IR.",
                test_copy_command_facades,
            },
            {
                "hardware_image.descriptor_external",
                "HardwareImage sampled descriptors are stable and external memory round-trips when supported.",
                test_descriptor_and_external_round_trip,
            },
        };
    }
}
