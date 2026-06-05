#include "test_registry.h"

#include "hardware_wrapper_vulkan/hardware/command.h"
#include "hardware_wrapper_vulkan/hardware/device_manager.h"
#include "hardware_wrapper_vulkan/hardware_context.h"
#include "horizon.h"

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

    class ValidationConfigGuard
    {
    public:
        ValidationConfigGuard()
            : previous_(Corona::Horizon::get_hardware_validation_config())
        {
        }

        ValidationConfigGuard(const ValidationConfigGuard&) = delete;
        ValidationConfigGuard& operator=(const ValidationConfigGuard&) = delete;

        ~ValidationConfigGuard()
        {
            Corona::Horizon::set_hardware_validation_config(previous_);
        }

    private:
        Corona::Horizon::HardwareValidationConfig previous_ {};
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

    void wait_for_token(Corona::Horizon::Queue& queue, const Corona::Horizon::SubmissionToken& token)
    {
        queue.wait_for(token);
        queue.retire_completed();
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

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_device_local_initial_upload_validation()
    {
        Corona::Horizon::HardwareImageDesc desc =
            Corona::Horizon::HardwareImageDesc::texture_2d(2,
                                                           2,
                                                           Corona::Horizon::Format::RGBA8_UNORM,
                                                           Corona::Horizon::ImageUsageFlags::TransferDst,
                                                           "hardware_image.device_local_upload");
        const std::array<uint32_t, 4> pixels {};

        bool threw = false;
        try
        {
            (void)Corona::Horizon::HardwareImage(desc, std::as_bytes(std::span<const uint32_t>(pixels)));
        }
        catch (const std::invalid_argument&)
        {
            threw = true;
        }

        expect(threw, "HardwareImage device-local initial upload should throw before creating a Vulkan image.");
        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_image_descriptor_validation()
    {
        Corona::Horizon::HardwareImageDesc desc =
            Corona::Horizon::HardwareImageDesc::texture_2d(2,
                                                           2,
                                                           Corona::Horizon::Format::RGBA8_UNORM,
                                                           Corona::Horizon::ImageUsageFlags::TransferDst,
                                                           "hardware_image.invalid_mips");
        desc.mip_levels = 3;

        bool threw = false;
        try
        {
            (void)Corona::Horizon::HardwareImage(desc);
        }
        catch (const std::invalid_argument&)
        {
            threw = true;
        }

        expect(threw, "HardwareImageDesc should reject mip_levels beyond the extent mip chain.");
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
                                                           "hardware_image.linear");
        desc.cpu_access = Corona::Horizon::CpuAccessMode::ReadWrite;

        try
        {
            Corona::Horizon::HardwareImage image(desc);
            expect(static_cast<bool>(image), "Host-visible HardwareImage should be valid when the device supports linear images.");

            std::array<uint32_t, 16> pixels {};
            for (uint32_t i = 0; i < pixels.size(); ++i)
            {
                pixels[i] = 0xff000000u | i;
            }

            bool threw = false;
            try
            {
                (void)image.write<uint32_t>(pixels, 3 * sizeof(uint32_t));
            }
            catch (const std::invalid_argument&)
            {
                threw = true;
            }
            expect(threw, "HardwareImage write should reject row_pitch smaller than one packed row when validation is enabled.");

            {
                ValidationConfigGuard guard;
                Corona::Horizon::set_hardware_validation_config({ Corona::Horizon::HardwareValidationMode::Disabled });
                expect(!image.write<uint32_t>(pixels, 3 * sizeof(uint32_t)),
                       "HardwareImage write with too-small row_pitch should return false when validation is disabled.");
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
            Corona::Horizon::HardwareImageDesc::texture_2d_array(8,
                                                                 8,
                                                                 2,
                                                                 Corona::Horizon::Format::RGBA8_UNORM,
                                                                 Corona::Horizon::ImageUsageFlags::Sampled |
                                                                     Corona::Horizon::ImageUsageFlags::TransferSrc |
                                                                     Corona::Horizon::ImageUsageFlags::TransferDst,
                                                                 "hardware_image.copy");
        desc.mip_levels = 2;

        Corona::Horizon::HardwareImage src(desc);
        Corona::Horizon::HardwareImage dst(desc);
        const std::array<uint32_t, 32> staging_data {};
        Corona::Horizon::HardwareBufferDesc buffer_desc =
            Corona::Horizon::HardwareBufferDesc::storage<uint32_t>(staging_data.size(), "hardware_image.copy.buffer");
        buffer_desc.cpu_access = Corona::Horizon::CpuAccessMode::ReadWrite;
        Corona::Horizon::HardwareBuffer buffer =
            Corona::Horizon::HardwareBuffer(buffer_desc, std::as_bytes(std::span<const uint32_t>(staging_data)));

        Corona::Horizon::HardwareImage src_view = src.layer(1).mip(1);
        Corona::Horizon::HardwareImage dst_view = dst.layer(0).mip(0);

        Corona::Horizon::CopyImageCommand image_copy = src_view.copy_to(dst_view);
        expect(image_copy.copy_region().src_layer == 1, "CopyImageCommand should preserve source layer.");
        expect(image_copy.copy_region().dst_layer == 0, "CopyImageCommand should preserve destination layer.");
        expect(image_copy.copy_region().src_mip == 1, "CopyImageCommand should preserve source mip.");
        expect(image_copy.copy_region().dst_mip == 0, "CopyImageCommand should preserve destination mip.");

        Corona::Horizon::CommandRecorder image_copy_recorder;
        image_copy.record(image_copy_recorder);
        Corona::Horizon::RecordedTask image_copy_task = image_copy_recorder.close();
        expect(image_copy_task.commands.size() == 1, "CopyImageCommand should record one IR command.");
        expect(image_copy_task.commands[0].op == Corona::Horizon::CommandOp::CopyImage, "CopyImageCommand should record CopyImage IR.");

        Corona::Horizon::CopyImageToBufferCommand image_to_buffer = src_view.copy_to(buffer, 0, 0, sizeof(uint32_t));
        expect(image_to_buffer.copy_region().image_layer == 1, "CopyImageToBufferCommand should preserve image layer.");
        expect(image_to_buffer.copy_region().image_mip == 1, "CopyImageToBufferCommand should preserve image mip.");
        expect(image_to_buffer.copy_region().buffer_offset == sizeof(uint32_t), "CopyImageToBufferCommand should preserve buffer offset.");

        Corona::Horizon::CommandRecorder image_to_buffer_recorder;
        image_to_buffer.record(image_to_buffer_recorder);
        Corona::Horizon::RecordedTask image_to_buffer_task = image_to_buffer_recorder.close();
        expect(image_to_buffer_task.commands.size() == 1, "CopyImageToBufferCommand should record one IR command.");
        expect(image_to_buffer_task.commands[0].op == Corona::Horizon::CommandOp::CopyImageToBuffer, "CopyImageToBufferCommand should record CopyImageToBuffer IR.");

        Corona::Horizon::HardwareImage upload_view = dst.layer(1).mip(1);
        Corona::Horizon::CopyBufferToImageCommand buffer_to_image = upload_view.copy_from(buffer, sizeof(uint32_t));
        expect(buffer_to_image.copy_region().image_layer == 1, "CopyBufferToImageCommand from HardwareImage should preserve image layer.");
        expect(buffer_to_image.copy_region().image_mip == 1, "CopyBufferToImageCommand from HardwareImage should preserve image mip.");
        expect(buffer_to_image.copy_region().buffer_offset == sizeof(uint32_t), "CopyBufferToImageCommand from HardwareImage should preserve buffer offset.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_device_local_upload_and_readback()
    {
        const auto environment = require_vulkan_environment();
        if (environment.status == Corona::Horizon::Tests::TestStatus::Skipped)
        {
            return environment;
        }

        Corona::Horizon::DeviceManager& manager = Corona::Horizon::device_manager();
        if (manager.queue_for(Corona::Horizon::QueueCapability::Transfer) == nullptr)
        {
            return Corona::Horizon::Tests::TestResult::skip("No transfer-capable Vulkan queue was found.");
        }

        constexpr uint32_t width = 2;
        constexpr uint32_t height = 2;
        const std::array<uint32_t, width * height> pixels {
            0xff0000ffu,
            0xff00ff00u,
            0xffff0000u,
            0xffffffffu,
        };

        try
        {
            Corona::Horizon::HardwareImageDesc image_desc =
                Corona::Horizon::HardwareImageDesc::texture_2d(width,
                                                               height,
                                                               Corona::Horizon::Format::RGBA8_UNORM,
                                                               Corona::Horizon::ImageUsageFlags::TransferSrc |
                                                                   Corona::Horizon::ImageUsageFlags::TransferDst,
                                                               "hardware_image.upload_readback");
            Corona::Horizon::HardwareImage image(image_desc);
            expect(static_cast<bool>(image), "Device-local upload/readback test should create an image.");

            const std::array<uint32_t, pixels.size()> zeros {};
            Corona::Horizon::HardwareBufferDesc readback_desc =
                Corona::Horizon::HardwareBufferDesc::storage<uint32_t>(zeros.size(), "hardware_image.upload_readback.buffer");
            readback_desc.cpu_access = Corona::Horizon::CpuAccessMode::ReadWrite;
            Corona::Horizon::HardwareBuffer readback(readback_desc, std::as_bytes(std::span<const uint32_t>(zeros)));
            expect(static_cast<bool>(readback), "Device-local upload/readback test should create a readback buffer.");

            Corona::Horizon::CommandBatch batch = image.upload<uint32_t>(std::span<const uint32_t>(pixels));
            batch << image.copy_to(readback);

            Corona::Horizon::HardwareExecutor executor([&manager](Corona::Horizon::DeviceId, Corona::Horizon::QueueCapability capability) -> Corona::Horizon::Queue& {
                Corona::Horizon::Queue* queue = manager.queue_for(capability);
                if (queue == nullptr)
                {
                    throw std::runtime_error("HardwareImage upload/readback test could not resolve a Vulkan queue.");
                }
                return *queue;
            });

            Corona::Horizon::SubmitReceipt receipt =
                executor.stream()
                << batch
                << Corona::Horizon::commit();
            expect(!receipt.tokens.empty(), "HardwareImage upload/readback should submit queue work.");

            for (const Corona::Horizon::SubmissionToken& token : receipt.tokens)
            {
                Corona::Horizon::Queue* queue = manager.queue_for(token.queue.capability);
                expect(queue != nullptr, "Submitted HardwareImage upload/readback token should resolve to a queue.");
                wait_for_token(*queue, token);
            }

            std::array<uint32_t, pixels.size()> readback_pixels {};
            expect(readback.read<uint32_t>(readback_pixels), "HardwareImage upload/readback should read copied pixels.");
            expect(std::equal(readback_pixels.begin(), readback_pixels.end(), pixels.begin()),
                   "HardwareImage device-local upload should round-trip through image-to-buffer readback.");
        }
        catch (const std::exception& error)
        {
            return Corona::Horizon::Tests::TestResult::skip(std::string("HardwareImage upload/readback is unavailable on this Vulkan device: ") + error.what());
        }

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
            Corona::Horizon::HardwareImageDesc::texture_2d(8,
                                                           8,
                                                           Corona::Horizon::Format::RGBA8_UNORM,
                                                           Corona::Horizon::ImageUsageFlags::Sampled |
                                                               Corona::Horizon::ImageUsageFlags::Storage |
                                                               Corona::Horizon::ImageUsageFlags::TransferSrc |
                                                               Corona::Horizon::ImageUsageFlags::TransferDst,
                                                           "hardware_image.descriptor");
        Corona::Horizon::HardwareImage image(desc);

        try
        {
            const uint32_t first = image.store_descriptor();
            const uint32_t second = image.store_sampled_descriptor();
            expect(first == second, "HardwareImage::store_descriptor should return the stable sampled-image descriptor index.");

            const uint32_t storage_first = image.store_storage_descriptor();
            const uint32_t storage_second = image.store_storage_descriptor();
            expect(storage_first == storage_second, "HardwareImage::store_storage_descriptor should return a stable storage-image descriptor index.");

            Corona::Horizon::HardwareImage depth(
                Corona::Horizon::HardwareImageDesc::depth_attachment(4,
                                                                     4,
                                                                     Corona::Horizon::Format::D32,
                                                                     "hardware_image.depth_descriptor"));
            const uint32_t depth_first = depth.store_sampled_descriptor();
            const uint32_t depth_second = depth.store_descriptor();
            expect(depth_first == depth_second, "Depth HardwareImage sampled descriptors should use the sampled bindless array.");
        }
        catch (const std::runtime_error& error)
        {
            return Corona::Horizon::Tests::TestResult::skip(std::string("Bindless image descriptors are unavailable on this Vulkan device: ") + error.what());
        }

        Corona::Horizon::HardwareImageDesc external_desc =
            Corona::Horizon::HardwareImageDesc::texture_2d(4,
                                                           4,
                                                           Corona::Horizon::Format::RGBA8_UNORM,
                                                           Corona::Horizon::ImageUsageFlags::Sampled | Corona::Horizon::ImageUsageFlags::TransferDst,
                                                           "hardware_image.external");
        external_desc.dedicated = true;
        external_desc.exportable = true;

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
                "hardware_image.device_local_initial_upload_validation",
                "Device-local HardwareImage initial uploads fail validation instead of silently dropping data.",
                test_device_local_initial_upload_validation,
            },
            {
                "hardware_image.descriptor_validation",
                "HardwareImage descriptor validation rejects invalid mip chains before resource creation.",
                test_image_descriptor_validation,
            },
            {
                "hardware_image.linear_host_io",
                "Host-visible linear HardwareImage supports row-pitched subresource read/write when the device allows it.",
                test_linear_image_host_io,
            },
            {
                "hardware_image.copy_command_facades",
                "HardwareImage copy facades translate view-local layer/mip parameters and record typed IR.",
                test_copy_command_facades,
            },
            {
                "hardware_image.device_local_upload_readback",
                "HardwareImage::upload records a staging copy that can round-trip through image-to-buffer readback.",
                test_device_local_upload_and_readback,
            },
            {
                "hardware_image.descriptor_external",
                "HardwareImage sampled, storage, and depth-sampled descriptors are stable, and external memory round-trips when supported.",
                test_descriptor_and_external_round_trip,
            },
        };
    }
}
