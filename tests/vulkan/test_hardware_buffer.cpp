#include "test_registry.h"

#include "hardware_wrapper_vulkan/hardware/command.h"
#include "hardware_wrapper_vulkan/hardware_context.h"
#include "hardware_wrapper_vulkan/resource_pool.h"
#include "horizon_refac.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
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

    template <typename T, size_t Size>
    void expect_array_eq(const std::array<T, Size>& actual, const std::array<T, Size>& expected, const char* message)
    {
        if (!std::equal(actual.begin(), actual.end(), expected.begin()))
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

    [[nodiscard]] Corona::Horizon::HardwareBufferOptions host_read_write_options() noexcept
    {
        Corona::Horizon::HardwareBufferOptions options;
        options.cpu_access = Corona::Horizon::CpuAccessMode::ReadWrite;
        return options;
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_create_upload_read_write()
    {
        const auto environment = require_vulkan_environment();
        if (environment.status == Corona::Horizon::Tests::TestStatus::Skipped)
        {
            return environment;
        }

        const std::array<uint32_t, 4> initial { 1, 2, 3, 4 };
        Corona::Horizon::HardwareBuffer buffer =
            Corona::Horizon::HardwareBuffer::storage<uint32_t>(std::span<const uint32_t>(initial), "hardware_buffer.read_write", host_read_write_options());

        expect(static_cast<bool>(buffer), "HardwareBuffer::storage should create a valid buffer.");
        expect(buffer.get_element_size() == sizeof(uint32_t), "HardwareBuffer should preserve element size.");
        expect(buffer.get_element_count() == initial.size(), "HardwareBuffer should preserve element count.");
        expect(buffer.get_byte_size() == initial.size() * sizeof(uint32_t), "HardwareBuffer should report byte size.");
        expect(buffer.get_mapped_data() != nullptr, "ReadWrite HardwareBuffer should expose mapped host memory.");

        std::array<uint32_t, 4> readback {};
        expect(buffer.read<uint32_t>(readback), "HardwareBuffer should read initial upload data.");
        expect_array_eq(readback, initial, "Initial upload data should round-trip through mapped memory.");

        const std::array<uint32_t, 2> patch { 9, 10 };
        expect(buffer.write_elements<uint32_t>(patch, 1), "HardwareBuffer should write a typed element range.");

        std::array<uint32_t, 4> patched {};
        expect(buffer.read<uint32_t>(patched), "HardwareBuffer should read after a typed write.");
        expect_array_eq(patched, std::array<uint32_t, 4> { 1, 9, 10, 4 }, "Typed write should update only the requested element range.");

        const uint32_t value = 77;
        expect(buffer.write_element(value, 0), "HardwareBuffer should write one typed element.");
        expect(buffer.read<uint32_t>(patched), "HardwareBuffer should read after a single element write.");
        expect_array_eq(patched, std::array<uint32_t, 4> { 77, 9, 10, 4 }, "Single element write should update the first element only.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_copy_move_keep_resource_alive()
    {
        const auto environment = require_vulkan_environment();
        if (environment.status == Corona::Horizon::Tests::TestStatus::Skipped)
        {
            return environment;
        }

        std::weak_ptr<const Corona::Horizon::IResourceRef> weak_token;
        const std::array<uint32_t, 3> initial { 5, 6, 7 };
        Corona::Horizon::HardwareBuffer survivor;
        std::uintptr_t shared_id = 0;

        {
            Corona::Horizon::HardwareBuffer original =
                Corona::Horizon::HardwareBuffer::storage<uint32_t>(std::span<const uint32_t>(initial), "hardware_buffer.copy_lifetime", host_read_write_options());

            expect(static_cast<bool>(original), "Original HardwareBuffer should be valid.");
            weak_token = Corona::Horizon::ResourceBridge::keep_alive(original);
            shared_id = original.get_buffer_id();
            expect(shared_id != 0, "HardwareBuffer resource id should be non-zero.");

            Corona::Horizon::HardwareBuffer copy = original;
            expect(static_cast<bool>(copy), "Copy-constructed HardwareBuffer should be valid.");
            expect(copy.get_buffer_id() == shared_id, "HardwareBuffer copies should share the same resource id.");

            original = {};
            expect(!original, "Reset original HardwareBuffer should become invalid.");
            expect(static_cast<bool>(copy), "Copied HardwareBuffer should keep the underlying buffer alive.");
            expect(!weak_token.expired(), "Resource token should stay alive while a copy exists.");

            Corona::Horizon::HardwareBuffer moved = std::move(copy);
            expect(!copy, "Moved-from HardwareBuffer should become invalid.");
            expect(static_cast<bool>(moved), "Move-constructed HardwareBuffer should keep the resource alive.");
            expect(moved.get_buffer_id() == shared_id, "Move-constructed HardwareBuffer should preserve the resource id.");

            survivor = moved;
            expect(static_cast<bool>(survivor), "Copy-assigned survivor should be valid.");
            expect(survivor.get_buffer_id() == shared_id, "Copy-assigned survivor should share the resource id.");

            moved = {};
            expect(!moved, "Reset moved HardwareBuffer should become invalid.");
            expect(static_cast<bool>(survivor), "Survivor copy should keep the resource alive after all earlier handles reset.");
        }

        expect(!weak_token.expired(), "Resource token should survive after the original scope while survivor owns it.");

        std::array<uint32_t, 3> readback {};
        expect(survivor.read<uint32_t>(readback), "Survivor copy should still read the original data.");
        expect_array_eq(readback, initial, "Survivor copy should see the same mapped data.");

        survivor = {};
        expect(weak_token.expired(), "Resource token should release after the last HardwareBuffer handle resets.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_range_validation_and_unmapped_buffer()
    {
        const auto environment = require_vulkan_environment();
        if (environment.status == Corona::Horizon::Tests::TestStatus::Skipped)
        {
            return environment;
        }

        const std::array<uint32_t, 2> initial { 11, 12 };
        Corona::Horizon::HardwareBuffer buffer =
            Corona::Horizon::HardwareBuffer::storage<uint32_t>(std::span<const uint32_t>(initial), "hardware_buffer.bounds", host_read_write_options());

        bool threw = false;
        try
        {
            const uint32_t value = 13;
            (void)buffer.write_element(value, 2);
        }
        catch (const std::invalid_argument&)
        {
            threw = true;
        }
        expect(threw, "Out-of-range write should throw when validation mode is Throw.");

        {
            ValidationConfigGuard guard;
            Corona::Horizon::set_hardware_validation_config({ Corona::Horizon::HardwareValidationMode::Disabled });

            const uint32_t value = 14;
            expect(!buffer.write_element(value, 2), "Out-of-range write should return false when optional validation is disabled.");

            std::array<uint32_t, 1> readback {};
            expect(!buffer.read_elements<uint32_t>(readback, 2), "Out-of-range read should return false when optional validation is disabled.");
        }

        Corona::Horizon::HardwareBufferDesc device_local_desc =
            Corona::Horizon::HardwareBufferDesc::storage<uint32_t>(2, "hardware_buffer.device_local", {});
        device_local_desc.cpu_access = Corona::Horizon::CpuAccessMode::None;

        Corona::Horizon::HardwareBuffer device_local(device_local_desc);
        expect(static_cast<bool>(device_local), "Device-local HardwareBuffer should still create a valid Vulkan buffer.");
        expect(device_local.get_mapped_data() == nullptr, "Device-local HardwareBuffer should not expose mapped host memory.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_concurrent_disjoint_read_write()
    {
        const auto environment = require_vulkan_environment();
        if (environment.status == Corona::Horizon::Tests::TestStatus::Skipped)
        {
            return environment;
        }

        constexpr size_t thread_count = 8;
        constexpr size_t values_per_thread = 32;
        constexpr size_t total_values = thread_count * values_per_thread;

        std::vector<uint32_t> zeros(total_values, 0);
        std::vector<uint32_t> expected(total_values, 0);
        for (size_t thread = 0; thread < thread_count; ++thread)
        {
            for (size_t index = 0; index < values_per_thread; ++index)
            {
                expected[thread * values_per_thread + index] = static_cast<uint32_t>(thread * 1000 + index + 1);
            }
        }

        Corona::Horizon::HardwareBuffer shared =
            Corona::Horizon::HardwareBuffer::storage<uint32_t>(std::span<const uint32_t>(zeros), "hardware_buffer.concurrent", host_read_write_options());
        const std::uintptr_t shared_id = shared.get_buffer_id();

        std::array<std::thread, thread_count> threads;
        std::array<std::exception_ptr, thread_count> failures {};
        std::atomic<int> completed { 0 };

        for (size_t thread = 0; thread < thread_count; ++thread)
        {
            threads[thread] = std::thread([&, thread] {
                try
                {
                    Corona::Horizon::HardwareBuffer local = shared;
                    expect(static_cast<bool>(local), "Thread-local HardwareBuffer copy should be valid.");
                    expect(local.get_buffer_id() == shared_id, "Thread-local HardwareBuffer copy should share the resource id.");

                    const size_t first_element = thread * values_per_thread;
                    std::span<const uint32_t> write_range(expected.data() + first_element, values_per_thread);
                    expect(local.write_elements<uint32_t>(write_range, first_element), "Thread-local HardwareBuffer copy should write its disjoint range.");

                    std::array<uint32_t, values_per_thread> readback {};
                    expect(local.read_elements<uint32_t>(readback, first_element), "Thread-local HardwareBuffer copy should read its disjoint range.");

                    for (size_t index = 0; index < values_per_thread; ++index)
                    {
                        if (readback[index] != expected[first_element + index])
                        {
                            throw std::runtime_error("Thread-local HardwareBuffer readback did not match its write range.");
                        }
                    }

                    completed.fetch_add(1, std::memory_order_relaxed);
                }
                catch (...)
                {
                    failures[thread] = std::current_exception();
                }
            });
        }

        for (std::thread& thread : threads)
        {
            thread.join();
        }

        for (const std::exception_ptr& failure : failures)
        {
            if (failure)
            {
                std::rethrow_exception(failure);
            }
        }

        expect(completed.load(std::memory_order_relaxed) == static_cast<int>(thread_count),
               "Every HardwareBuffer worker should complete.");

        std::vector<uint32_t> readback(total_values, 0);
        expect(shared.read<uint32_t>(readback), "Shared HardwareBuffer should read back the full concurrent write result.");
        expect(readback == expected, "Concurrent disjoint HardwareBuffer writes should produce the expected final data.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_copy_to_buffer_command_facade()
    {
        const auto environment = require_vulkan_environment();
        if (environment.status == Corona::Horizon::Tests::TestStatus::Skipped)
        {
            return environment;
        }

        const std::array<uint32_t, 4> src_data { 1, 2, 3, 4 };
        const std::array<uint32_t, 4> dst_data { 0, 0, 0, 0 };
        Corona::Horizon::HardwareBuffer src =
            Corona::Horizon::HardwareBuffer::storage<uint32_t>(std::span<const uint32_t>(src_data), "hardware_buffer.copy_cmd.src", host_read_write_options());
        Corona::Horizon::HardwareBuffer dst =
            Corona::Horizon::HardwareBuffer::storage<uint32_t>(std::span<const uint32_t>(dst_data), "hardware_buffer.copy_cmd.dst", host_read_write_options());

        const Corona::Horizon::BufferRange source_range { sizeof(uint32_t), sizeof(uint32_t) * 2 };
        Corona::Horizon::CopyBufferCommand command = src.copy_to(dst, source_range, sizeof(uint32_t));

        expect(command.copy_region().src_offset == sizeof(uint32_t), "HardwareBuffer::copy_to should preserve source offset.");
        expect(command.copy_region().dst_offset == sizeof(uint32_t), "HardwareBuffer::copy_to should preserve destination offset.");
        expect(command.copy_region().size == sizeof(uint32_t) * 2, "HardwareBuffer::copy_to should preserve copy byte size.");

        auto src_token = Corona::Horizon::ResourceBridge::keep_alive(command.source().handle);
        auto dst_token = Corona::Horizon::ResourceBridge::keep_alive(command.destination().handle);
        expect(src_token && src_token->id() == src.get_buffer_id(), "CopyBufferCommand should retain the source HardwareBuffer handle.");
        expect(dst_token && dst_token->id() == dst.get_buffer_id(), "CopyBufferCommand should retain the destination HardwareBuffer handle.");

        Corona::Horizon::CommandRecorder recorder;
        command.record(recorder);
        Corona::Horizon::RecordedTask task = recorder.close();
        expect(task.commands.size() == 1, "HardwareBuffer::copy_to command should record one IR command.");
        expect(task.commands[0].op == Corona::Horizon::CommandOp::CopyBuffer, "HardwareBuffer::copy_to should record CopyBuffer IR.");
        expect(task.commands[0].payload.copy.size == sizeof(uint32_t) * 2, "Recorded CopyBuffer IR should preserve byte size.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_copy_to_image_command_facade()
    {
        const auto environment = require_vulkan_environment();
        if (environment.status == Corona::Horizon::Tests::TestStatus::Skipped)
        {
            return environment;
        }

        const std::array<uint32_t, 4> src_data { 1, 2, 3, 4 };
        Corona::Horizon::HardwareBuffer src =
            Corona::Horizon::HardwareBuffer::storage<uint32_t>(std::span<const uint32_t>(src_data), "hardware_buffer.copy_image.src", host_read_write_options());

        Corona::Horizon::HardwareImage image;
        auto image_resource = Corona::Horizon::resource_pool().images.create([] {
            return Corona::Horizon::ImageWrap {};
        });
        Corona::Horizon::ResourceBridge::set(
            image,
            Corona::Horizon::make_token<Corona::Horizon::ResourceStore<Corona::Horizon::ImageWrap, Corona::Horizon::ImageReleaser>>(
                std::move(image_resource)));

        Corona::Horizon::CopyBufferToImageCommand command = src.copy_to(image, sizeof(uint32_t), 2, 3);

        expect(command.copy_region().buffer_offset == sizeof(uint32_t), "HardwareBuffer::copy_to(image) should preserve the buffer offset.");
        expect(command.copy_region().image_layer == 2, "HardwareBuffer::copy_to(image) should preserve the image layer.");
        expect(command.copy_region().image_mip == 3, "HardwareBuffer::copy_to(image) should preserve the image mip.");

        auto src_token = Corona::Horizon::ResourceBridge::keep_alive(command.source().handle);
        auto image_token = Corona::Horizon::ResourceBridge::keep_alive(command.destination().handle);
        expect(src_token && src_token->id() == src.get_buffer_id(), "CopyBufferToImageCommand should retain the source HardwareBuffer handle.");
        expect(image_token && image_token->id() == image.get_image_id(), "CopyBufferToImageCommand should retain the destination HardwareImage handle.");

        Corona::Horizon::CommandRecorder recorder;
        command.record(recorder);
        Corona::Horizon::RecordedTask task = recorder.close();
        expect(task.commands.size() == 1, "HardwareBuffer::copy_to(image) command should record one IR command.");
        expect(task.commands[0].op == Corona::Horizon::CommandOp::CopyBufferToImage, "HardwareBuffer::copy_to(image) should record CopyBufferToImage IR.");
        expect(task.commands[0].payload.buffer_image_copy.buffer_offset == sizeof(uint32_t), "Recorded CopyBufferToImage IR should preserve buffer offset.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_store_descriptor_is_stable()
    {
        const auto environment = require_vulkan_environment();
        if (environment.status == Corona::Horizon::Tests::TestStatus::Skipped)
        {
            return environment;
        }

        const std::array<uint32_t, 2> initial { 42, 43 };
        Corona::Horizon::HardwareBuffer buffer =
            Corona::Horizon::HardwareBuffer::storage<uint32_t>(std::span<const uint32_t>(initial), "hardware_buffer.descriptor", host_read_write_options());

        try
        {
            const uint32_t first = buffer.store_descriptor();
            const uint32_t second = buffer.store_descriptor();
            expect(first == second, "HardwareBuffer::store_descriptor should return a stable descriptor index for the same buffer.");
        }
        catch (const std::runtime_error& error)
        {
            return Corona::Horizon::Tests::TestResult::skip(std::string("Storage buffer descriptors are unavailable on this Vulkan device: ") + error.what());
        }

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_external_export_import_buffer()
    {
        const auto environment = require_vulkan_environment();
        if (environment.status == Corona::Horizon::Tests::TestStatus::Skipped)
        {
            return environment;
        }

        Corona::Horizon::HardwareBufferOptions options = host_read_write_options();
        options.dedicated = true;
        options.exportable = true;

        const std::array<uint32_t, 4> initial { 7, 8, 9, 10 };
        const Corona::Horizon::HardwareBufferDesc desc =
            Corona::Horizon::HardwareBufferDesc::storage<uint32_t>(initial.size(), "hardware_buffer.external", options);

        Corona::Horizon::HardwareBuffer imported;
        Corona::Horizon::ExternalMemoryHandle handle {};
        Corona::Horizon::ExternalMemoryHandle second_handle {};
        bool import_succeeded = false;

        try
        {
            Corona::Horizon::HardwareBuffer source(desc, std::as_bytes(std::span<const uint32_t>(initial)));
            handle = source.export_external();
            expect(static_cast<bool>(handle), "HardwareBuffer::export_external should return a valid external memory handle.");
            second_handle = source.export_external();
            expect(static_cast<bool>(second_handle), "Repeated HardwareBuffer::export_external should return a valid external memory handle.");

            imported = Corona::Horizon::HardwareBuffer::import_external(handle, desc);
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
            return Corona::Horizon::Tests::TestResult::skip(std::string("External HardwareBuffer import/export is unavailable on this Vulkan device: ") + error.what());
        }

#if defined(_WIN32) || defined(_WIN64)
        close_external_handle(handle);
        close_external_handle(second_handle);
#elif defined(__linux__)
        close_external_handle(second_handle);
#endif

        expect(static_cast<bool>(imported), "HardwareBuffer::import_external should create a valid buffer wrapper.");
        expect(imported.get_byte_size() == desc.byte_size(), "Imported HardwareBuffer should preserve the logical byte size.");

        return Corona::Horizon::Tests::TestResult::pass();
    }
}

namespace Corona::Horizon::Tests
{
    std::vector<TestCase> hardware_buffer_tests()
    {
        return {
            {
                "hardware_buffer.create_upload_read_write",
                "Host-visible HardwareBuffer creation preserves metadata, uploads initial data, and supports typed offset read/write.",
                test_create_upload_read_write,
            },
            {
                "hardware_buffer.copy_move_lifetime",
                "HardwareBuffer copy/move handles share one resource token and release it only after the last wrapper resets.",
                test_copy_move_keep_resource_alive,
            },
            {
                "hardware_buffer.range_and_mapping",
                "HardwareBuffer host I/O handles validation ranges and keeps device-local buffers unmapped.",
                test_range_validation_and_unmapped_buffer,
            },
            {
                "hardware_buffer.concurrent_disjoint_io",
                "Multiple threads can copy one HardwareBuffer handle and read/write disjoint host-mapped ranges safely.",
                test_concurrent_disjoint_read_write,
            },
            {
                "hardware_buffer.copy_to_buffer_command",
                "HardwareBuffer::copy_to returns a value command that preserves ranges and records CopyBuffer IR.",
                test_copy_to_buffer_command_facade,
            },
            {
                "hardware_buffer.copy_to_image_command",
                "HardwareBuffer::copy_to(image) returns a value command that preserves image copy metadata and records CopyBufferToImage IR.",
                test_copy_to_image_command_facade,
            },
            {
                "hardware_buffer.store_descriptor_stable",
                "HardwareBuffer::store_descriptor writes a storage-buffer descriptor and returns a stable index.",
                test_store_descriptor_is_stable,
            },
            {
                "hardware_buffer.external_export_import",
                "Exportable HardwareBuffer memory can round-trip through the ResourceManager external import/export facade when supported.",
                test_external_export_import_buffer,
            },
        };
    }
}
