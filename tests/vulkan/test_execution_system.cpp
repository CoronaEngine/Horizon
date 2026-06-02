#include "test_registry.h"

#include "hardware_wrapper_vulkan/display/display_manager.h"
#include "hardware_wrapper_vulkan/hardware/device_manager.h"
#include "hardware_wrapper_vulkan/hardware/execution.h"
#include "hardware_wrapper_vulkan/hardware_context.h"
#include "hardware_wrapper_vulkan/pipeline/vulkan_rasterizer_pipeline.h"
#include "hardware_wrapper_vulkan/resource_pool.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    void expect(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    class TestResourceRef final : public Corona::Horizon::IResourceRef
    {
    public:
        explicit TestResourceRef(std::uintptr_t id)
            : id_(id)
        {
        }

        [[nodiscard]] std::uintptr_t id() const noexcept override { return id_; }
        [[nodiscard]] bool valid() const noexcept override { return true; }

    private:
        std::uintptr_t id_ { 0 };
    };

    struct TestRenderTargetProxy
    {
        void* boundResource_ { nullptr };
    };

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

    [[nodiscard]] Corona::Horizon::HardwareBufferOptions host_read_write_buffer_options() noexcept
    {
        Corona::Horizon::HardwareBufferOptions options;
        options.cpu_access = Corona::Horizon::CpuAccessMode::ReadWrite;
        return options;
    }

    [[nodiscard]] Corona::Horizon::ResourceHandle test_resource(std::uintptr_t id)
    {
        Corona::Horizon::ResourceHandle handle;
        Corona::Horizon::ResourceBridge::set(handle, std::make_shared<TestResourceRef>(id));
        return handle;
    }

    [[nodiscard]] Corona::Horizon::HardwareBuffer test_buffer(uint64_t element_count,
                                                              uint32_t element_size,
                                                              Corona::Horizon::BufferUsageFlags usage)
    {
        Corona::Horizon::HardwareBuffer buffer;
        auto resource = Corona::Horizon::resource_pool().buffers.create([=] {
            Corona::Horizon::BufferWrap wrap;
            wrap.desc.element_count = element_count;
            wrap.desc.element_size = element_size;
            wrap.desc.usage = usage;
            return wrap;
        });

        Corona::Horizon::ResourceBridge::set(
            buffer,
            Corona::Horizon::make_token<Corona::Horizon::ResourceStore<Corona::Horizon::BufferWrap, Corona::Horizon::BufferReleaser>>(
                std::move(resource)));
        return buffer;
    }

    [[nodiscard]] Corona::Horizon::HardwareImage test_color_image(uint32_t width, uint32_t height)
    {
        Corona::Horizon::HardwareImage image;
        auto resource = Corona::Horizon::resource_pool().images.create([=] {
            Corona::Horizon::ImageWrap wrap;
            wrap.desc = Corona::Horizon::HardwareImageDesc::color_attachment(width, height, Corona::Horizon::Format::RGBA8_UNORM);
            return wrap;
        });

        Corona::Horizon::ResourceBridge::set(
            image,
            Corona::Horizon::make_token<Corona::Horizon::ResourceStore<Corona::Horizon::ImageWrap, Corona::Horizon::ImageReleaser>>(
                std::move(resource)));
        return image;
    }

    [[nodiscard]] Corona::Horizon::RasterizerPipelineDesc test_rasterizer_desc()
    {
        EmbeddedShader::ShaderCodeModule::ShaderResources vertex_resources;
        vertex_resources.pushConstantSize = sizeof(uint32_t);

        EmbeddedShader::ShaderCodeModule::ShaderResources fragment_resources;
        fragment_resources.pushConstantSize = sizeof(uint32_t);

        Corona::Horizon::RasterizerPipelineDesc desc(
            {
                Corona::Horizon::PipelineShaderStage::Vertex,
                EmbeddedShader::ShaderCodeModule(std::vector<uint32_t> { 0x07230203u }, std::move(vertex_resources)),
            },
            {
                Corona::Horizon::PipelineShaderStage::Fragment,
                EmbeddedShader::ShaderCodeModule(std::vector<uint32_t> { 0x07230203u }, std::move(fragment_resources)),
            });
        desc.depth_stencil.depth_test_enabled = false;
        desc.depth_stencil.depth_write_enabled = false;
        return desc;
    }

    [[nodiscard]] Corona::Horizon::RasterizerPipelineDesc real_rasterizer_desc()
    {
        EmbeddedShader::CompilerOption compiler_option;
        compiler_option.compileGLSL = false;
        compiler_option.compileHLSL = false;
        compiler_option.compileDXIL = false;
        compiler_option.compileDXBC = false;
        compiler_option.compileSpirV = true;
        compiler_option.enableBindless = false;

        Corona::Horizon::RasterizerPipelineDesc desc = Corona::Horizon::RasterizerPipelineDesc::from_source(
            R"glsl(
#version 450

vec2 fullscreen_positions[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

void main()
{
    gl_Position = vec4(fullscreen_positions[gl_VertexIndex], 0.0, 1.0);
}
)glsl",
            R"glsl(
#version 450

layout(location = 0) out vec4 out_color;

void main()
{
    out_color = vec4(1.0, 0.0, 0.0, 1.0);
}
)glsl",
            EmbeddedShader::ShaderLanguage::GLSL,
            EmbeddedShader::ShaderLanguage::GLSL,
            compiler_option);

        desc.rasterizer.cull_mode = Corona::Horizon::CullMode::None;
        desc.depth_stencil.depth_test_enabled = false;
        desc.depth_stencil.depth_write_enabled = false;
        return desc;
    }

    void wait_for_token(Corona::Horizon::Queue& queue, Corona::Horizon::SubmissionToken token)
    {
        if (token.timeline == VK_NULL_HANDLE)
        {
            queue.mark_completed_for_tests(token.value);
            queue.retire_completed();
            return;
        }

        VkSemaphoreWaitInfo wait_info {};
        wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        wait_info.semaphoreCount = 1;
        wait_info.pSemaphores = &token.timeline;
        wait_info.pValues = &token.value;

        const VkResult result = vkWaitSemaphores(queue.device(), &wait_info, 5'000'000'000ull);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("vkWaitSemaphores failed while waiting for RasterizerPipeline smoke test. VkResult=" +
                                     std::to_string(static_cast<int>(result)));
        }

        queue.retire_completed();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_keep_alive_retires_after_timeline_completion()
    {
        Corona::Horizon::Queue queue({ 0 }, Corona::Horizon::QueueCapability::Transfer);

        std::weak_ptr<int> weak_marker;
        {
            auto marker = std::make_shared<int>(7);
            weak_marker = marker;

            Corona::Horizon::QueueSubmission submission;
            submission.command_buffer = queue.acquire();
            submission.keep_alive.add_object(marker);
            marker.reset();

            const Corona::Horizon::SubmissionToken token = queue.submit(submission, {}, {});
            expect(token.value == 1, "First fake submit should signal timeline value 1.");
            expect(queue.in_flight_count() == 1, "Submitted command buffer should be in flight.");
            expect(!weak_marker.expired(), "Keep-alive object must survive while the submission is in flight.");

            queue.retire_completed();
            expect(!weak_marker.expired(), "Retire before timeline completion must keep resources alive.");

            queue.mark_completed_for_tests(token.value);
            queue.retire_completed();
            expect(queue.in_flight_count() == 0, "Completed submission should leave the in-flight list.");
            expect(queue.pooled_count() == 1, "Completed command buffer should return to the pool.");
        }

        expect(weak_marker.expired(), "Keep-alive object should release when the tracked command buffer retires.");
        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_partial_timeline_retirement_keeps_newer_work()
    {
        Corona::Horizon::Queue queue({ 0 }, Corona::Horizon::QueueCapability::Transfer);
        std::weak_ptr<int> first_weak;
        std::weak_ptr<int> second_weak;

        Corona::Horizon::SubmissionToken first_token;
        Corona::Horizon::SubmissionToken second_token;
        {
            auto first_marker = std::make_shared<int>(1);
            first_weak = first_marker;

            Corona::Horizon::QueueSubmission submission;
            submission.command_buffer = queue.acquire();
            submission.keep_alive.add_object(first_marker);
            first_marker.reset();
            first_token = queue.submit(submission, {}, {});
        }

        {
            auto second_marker = std::make_shared<int>(2);
            second_weak = second_marker;

            Corona::Horizon::QueueSubmission submission;
            submission.command_buffer = queue.acquire();
            submission.keep_alive.add_object(second_marker);
            second_marker.reset();
            second_token = queue.submit(submission, {}, {});
        }

        expect(first_token.value == 1, "First submission should signal timeline value 1.");
        expect(second_token.value == 2, "Second submission should signal timeline value 2.");
        expect(queue.in_flight_count() == 2, "Both submissions should start in flight.");

        queue.mark_completed_for_tests(first_token.value);
        queue.retire_completed();
        expect(queue.in_flight_count() == 1, "Partial timeline completion should keep newer work in flight.");
        expect(queue.pooled_count() == 1, "Only the completed command buffer should return to the pool.");
        expect(first_weak.expired(), "The completed submission keep-alive should be released.");
        expect(!second_weak.expired(), "The newer submission keep-alive should stay alive.");

        queue.mark_completed_for_tests(second_token.value);
        queue.retire_completed();
        expect(queue.in_flight_count() == 0, "All work should retire after the second completion value.");
        expect(queue.pooled_count() == 2, "Both retired command buffers should be pooled.");
        expect(second_weak.expired(), "The second keep-alive should release after retirement.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_retired_command_buffer_is_reused()
    {
        Corona::Horizon::Queue queue({ 0 }, Corona::Horizon::QueueCapability::Transfer);

        Corona::Horizon::QueueSubmission submission;
        submission.command_buffer = queue.acquire();
        Corona::Horizon::TrackedCommandBuffer* retired_command_buffer = submission.command_buffer.get();
        const uint64_t first_recording_id = submission.command_buffer->recording_id();

        const Corona::Horizon::SubmissionToken token = queue.submit(submission, {}, {});
        queue.mark_completed_for_tests(token.value);
        queue.retire_completed();
        expect(queue.pooled_count() == 1, "Completed command buffer should be available for reuse.");

        std::shared_ptr<Corona::Horizon::TrackedCommandBuffer> reused = queue.acquire();
        expect(reused.get() == retired_command_buffer, "Queue should reuse retired command buffers before allocating new ones.");
        expect(reused->recording_id() == first_recording_id + 1, "Reused command buffer should receive a new recording id.");
        expect(reused->submission_id() == 0, "Reused command buffer should be reset out of submitted state.");
        expect(queue.pooled_count() == 0, "Acquiring the reused command buffer should remove it from the pool.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_submit_creates_missing_command_buffer()
    {
        Corona::Horizon::Queue queue({ 0 }, Corona::Horizon::QueueCapability::Transfer);

        Corona::Horizon::QueueSubmission submission;
        const Corona::Horizon::SubmissionToken token = queue.submit(submission, {}, {});

        expect(token.value == 1, "Submit without a caller command buffer should still signal timeline value 1.");
        expect(submission.command_buffer == nullptr, "Successful submit should move the auto-created command buffer into the queue.");
        expect(queue.in_flight_count() == 1, "Auto-created command buffer should be tracked in flight.");

        queue.mark_completed_for_tests(token.value);
        queue.retire_completed();
        expect(queue.in_flight_count() == 0, "Auto-created command buffer should retire normally.");
        expect(queue.pooled_count() == 1, "Auto-created command buffer should return to the pool.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_submit_failure_keeps_submission_resources()
    {
        Corona::Horizon::Queue queue({ 0 }, Corona::Horizon::QueueCapability::Transfer);
        std::weak_ptr<int> weak_marker;

        Corona::Horizon::QueueSubmission submission;
        submission.command_buffer = queue.acquire();
        {
            auto marker = std::make_shared<int>(11);
            weak_marker = marker;
            submission.keep_alive.add_object(marker);
        }

        queue.fail_next_submit_for_tests();

        bool threw = false;
        try
        {
            (void)queue.submit(submission, {}, {});
        }
        catch (const std::runtime_error&)
        {
            threw = true;
        }

        expect(threw, "Injected submit failure should throw.");
        expect(!weak_marker.expired(), "Failed submit must leave keep-alive ownership with the caller submission.");
        expect(submission.command_buffer != nullptr, "Failed submit must leave the command buffer with the caller submission.");
        expect(queue.in_flight_count() == 0, "Failed submit must not enqueue in-flight work.");

        submission.keep_alive.clear();
        expect(weak_marker.expired(), "Caller should still control failed-submit keep-alive lifetime.");
        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_recorder_and_compiler_collect_requirements()
    {
        Corona::Horizon::ResourceHandle buffer_a = test_resource(101);
        Corona::Horizon::ResourceHandle buffer_b = test_resource(102);

        Corona::Horizon::CommandRecorder recorder;
        recorder.copy({ buffer_a }, { buffer_b }, { 0, 16, 64 });
        recorder.copy({ buffer_b }, { buffer_a }, { 0, 0, 64 });
        recorder.require_feature(Corona::Horizon::FeatureRequirement::DeferredHostOperations);

        Corona::Horizon::RecordedTask task = recorder.close();
        expect(task.commands.size() == 2, "Recorder should preserve abstract copy commands.");
        expect(task.requirements.transfer, "Copy commands should require a transfer-capable queue.");
        expect(task.requirements.timeline_semaphore, "Executor requirements should default to timeline semaphore support.");
        expect(task.requirements.deferred_host_operations, "Explicit deferred host operation requirement should be recorded.");

        Corona::Horizon::ExecutionCompiler compiler;
        Corona::Horizon::ExecutionPlan plan = compiler.compile(task);
        expect(plan.submissions.size() == 1, "Two transfer commands should compile into one transfer submission.");
        expect(plan.submissions[0].commands.size() == 2, "Compiled submission should retain command order.");
        expect(plan.submissions[0].keep_alive.resource_count() == 2, "Compiler should keep each referenced resource alive once.");
        expect(!plan.submissions[0].barriers.empty(), "Read/write reuse of the same resources should produce a barrier record.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_rasterizer_pipeline_records_graphics_ir()
    {
        Corona::Horizon::VulkanRasterizerPipeline pipeline(test_rasterizer_desc());
        Corona::Horizon::HardwareBuffer index =
            test_buffer(6, sizeof(uint16_t), Corona::Horizon::BufferUsageFlags::Index);
        Corona::Horizon::HardwareBuffer vertex =
            test_buffer(3, sizeof(float) * 5, Corona::Horizon::BufferUsageFlags::Vertex);
        Corona::Horizon::HardwareImage color = test_color_image(128, 64);

        const uint32_t push_constant = 42;
        pipeline.set_extent(128, 64);
        pipeline.set_resource_direct(
            0,
            0,
            color,
            static_cast<int32_t>(EmbeddedShader::ShaderCodeModule::ShaderResources::stageOutputs),
            0);
        pipeline.set_push_constant_direct(
            0,
            &push_constant,
            sizeof(push_constant),
            static_cast<int32_t>(EmbeddedShader::ShaderCodeModule::ShaderResources::pushConstantMembers));

        Corona::Horizon::DrawIndexedParams params;
        params.index_count = 3;
        params.first_index = 1;
        params.vertex_offset = -2;
        Corona::Horizon::ResourceHandle pipeline_handle = test_resource(999);
        pipeline.record(pipeline_handle, index, vertex, params);

        Corona::Horizon::VulkanRasterizerPipeline::Snapshot snapshot = pipeline.snapshot();
        expect(snapshot.width == 128 && snapshot.height == 64, "RasterizerPipeline should retain the render extent.");
        expect(snapshot.images.size() == 1, "RasterizerPipeline should retain the bound render target.");
        expect(snapshot.draws.size() == 1, "RasterizerPipeline should retain one draw record.");
        expect(snapshot.draws[0].params.index_count == 3, "RasterizerPipeline should preserve draw index_count.");
        expect(snapshot.draws[0].push_constant_data.size() >= sizeof(push_constant), "Draw records should snapshot push constants.");
        expect(std::memcmp(snapshot.draws[0].push_constant_data.data(), &push_constant, sizeof(push_constant)) == 0,
               "Draw records should snapshot push constant bytes.");

        Corona::Horizon::CommandBatch batch = pipeline.command_batch();
        Corona::Horizon::CommandRecorder recorder;
        for (const Corona::Horizon::StreamCommand& command : batch.commands())
        {
            command.record(recorder);
        }

        Corona::Horizon::RecordedTask task = recorder.close();
        expect(task.commands.size() == 3, "RasterizerPipeline command batch should record begin, draw, and end commands.");
        expect(task.commands[0].op == Corona::Horizon::CommandOp::BeginRendering, "RasterizerPipeline should begin rendering before drawing.");
        expect(task.commands[1].op == Corona::Horizon::CommandOp::DrawIndexed, "RasterizerPipeline should record DrawIndexed IR.");
        expect(task.commands[2].op == Corona::Horizon::CommandOp::EndRendering, "RasterizerPipeline should end rendering after drawing.");
        expect(task.commands[0].payload.rendering.width == 128, "Rendering IR should preserve width.");
        expect(task.commands[1].payload.draw_indexed.first_index == 1, "DrawIndexed IR should preserve first_index.");
        expect(task.commands[1].payload.draw_indexed.vertex_offset == -2, "DrawIndexed IR should preserve vertex_offset.");
        std::shared_ptr<Corona::Horizon::IResourceRef> draw_pipeline_token =
            Corona::Horizon::ResourceBridge::token(task.commands[1].payload.draw_indexed.pipeline);
        expect(draw_pipeline_token && draw_pipeline_token->id() == 999,
               "DrawIndexed IR should carry the rasterizer pipeline handle.");
        expect(task.commands[1].payload.draw_indexed.push_constant_data.size() >= sizeof(push_constant),
               "DrawIndexed IR should carry the push constant snapshot.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_rasterizer_pipeline_uses_resource_handle()
    {
        Corona::Horizon::RasterizerPipeline pipeline(test_rasterizer_desc());
        expect(static_cast<bool>(pipeline), "RasterizerPipeline should expose ResourceHandle validity.");

        const std::uintptr_t pipeline_id = pipeline.get_rasterizer_pipeline_id();
        expect(pipeline_id != 0, "RasterizerPipeline should expose a non-empty resource id.");

        std::shared_ptr<const Corona::Horizon::IResourceRef> keep_alive =
            Corona::Horizon::ResourceBridge::keep_alive(pipeline);
        expect(keep_alive && keep_alive->id() == pipeline_id,
               "RasterizerPipeline should provide its token through ResourceBridge.");

        Corona::Horizon::RasterizerPipeline copy = pipeline;
        expect(copy.get_rasterizer_pipeline_id() == pipeline_id,
               "RasterizerPipeline copies should share the underlying resource handle.");

        Corona::Horizon::RasterizerPipeline moved = std::move(copy);
        expect(moved.get_rasterizer_pipeline_id() == pipeline_id,
               "RasterizerPipeline moves should transfer the underlying resource handle.");
        expect(copy.get_rasterizer_pipeline_id() == 0,
               "Moved-from RasterizerPipeline should no longer own a resource handle.");

        Corona::Horizon::HardwareImage color = test_color_image(64, 32);
        TestRenderTargetProxy target;
        target.boundResource_ = &color;
        moved.bind_render_target(2, target);
        moved(64, 32);

        using RasterizerPipelineStore =
            Corona::Horizon::ResourceStore<Corona::Horizon::RasterizerPipelineWrap, Corona::Horizon::NoopReleaser>;
        std::shared_ptr<Corona::Horizon::VulkanRasterizerPipeline> impl;
        Corona::Horizon::VulkanRasterizerPipeline::Snapshot snapshot = [&] {
            auto pipeline_resource =
                Corona::Horizon::read<RasterizerPipelineStore>(Corona::Horizon::ResourceBridge::token(moved));
            expect(pipeline_resource && pipeline_resource->impl,
                   "RasterizerPipeline ResourceHandle should point at a backend implementation.");

            impl = std::static_pointer_cast<Corona::Horizon::VulkanRasterizerPipeline>(pipeline_resource->impl);
            return impl->snapshot();
        }();
        expect(snapshot.desc.auto_bind_entries.size() == 1,
               "RasterizerPipeline bind_render_target should update RasterizerPipelineDesc auto bindings.");
        expect(snapshot.images.size() == 1,
               "RasterizerPipeline operator() should bind render targets from descriptor auto bindings.");
        expect(snapshot.images[0].location == 2,
               "RasterizerPipeline should preserve the auto-bound render target location.");

        Corona::Horizon::HardwareBuffer index =
            test_buffer(3, sizeof(uint32_t), Corona::Horizon::BufferUsageFlags::Index);
        Corona::Horizon::HardwareBuffer vertex =
            test_buffer(4, sizeof(float) * 6, Corona::Horizon::BufferUsageFlags::Vertex);
        Corona::Horizon::DrawIndexedParams params;
        params.index_count = 3;
        params.index_type = Corona::Horizon::IndexType::UInt32;
        moved.record(index, vertex, params);

        snapshot = impl->snapshot();
        expect(snapshot.draws.size() == 1,
               "RasterizerPipeline should expose recorded draws before clear_records.");

        moved.clear_records();
        snapshot = impl->snapshot();
        expect(snapshot.draws.empty(),
               "RasterizerPipeline clear_records should remove recorded draws for dynamic mesh re-recording.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_rasterizer_pipeline_real_vulkan_render()
    {
        const auto environment = require_vulkan_environment();
        if (environment.status == Corona::Horizon::Tests::TestStatus::Skipped)
        {
            return environment;
        }

        Corona::Horizon::DeviceManager& manager = Corona::Horizon::device_manager();
        if (manager.queue_for(Corona::Horizon::QueueCapability::Graphics) == nullptr)
        {
            return Corona::Horizon::Tests::TestResult::skip("No graphics-capable Vulkan queue was found.");
        }
        if (manager.queue_for(Corona::Horizon::QueueCapability::Transfer) == nullptr)
        {
            return Corona::Horizon::Tests::TestResult::skip("No transfer-capable Vulkan queue was found.");
        }

        constexpr uint32_t width = 4;
        constexpr uint32_t height = 4;
        constexpr uint32_t pixel_count = width * height;

        Corona::Horizon::HardwareImage color(
            Corona::Horizon::HardwareImageDesc::color_attachment(width,
                                                                 height,
                                                                 Corona::Horizon::Format::RGBA8_UNORM,
                                                                 "execution.rasterizer.real.color"));
        color.set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);

        const std::array<uint16_t, 3> indices { 0, 1, 2 };
        Corona::Horizon::HardwareBuffer index =
            Corona::Horizon::HardwareBuffer::index<uint16_t>(std::span<const uint16_t>(indices),
                                                             "execution.rasterizer.real.index",
                                                             host_read_write_buffer_options());

        const std::array<uint32_t, 1> dummy_vertex { 0 };
        Corona::Horizon::HardwareBuffer vertex =
            Corona::Horizon::HardwareBuffer::vertex<uint32_t>(std::span<const uint32_t>(dummy_vertex),
                                                              "execution.rasterizer.real.vertex",
                                                              host_read_write_buffer_options());

        const std::vector<uint32_t> zeros(pixel_count, 0);
        Corona::Horizon::HardwareBuffer readback =
            Corona::Horizon::HardwareBuffer::storage<uint32_t>(std::span<const uint32_t>(zeros),
                                                               "execution.rasterizer.real.readback",
                                                               host_read_write_buffer_options());

        Corona::Horizon::RasterizerPipeline pipeline(real_rasterizer_desc());
        TestRenderTargetProxy target;
        target.boundResource_ = &color;
        pipeline.bind_render_target(0, target);
        pipeline(static_cast<uint16_t>(width), static_cast<uint16_t>(height));

        Corona::Horizon::DrawIndexedParams params;
        params.index_count = static_cast<uint32_t>(indices.size());
        params.index_type = Corona::Horizon::IndexType::UInt16;
        pipeline.record(index, vertex, params);

        using RasterizerPipelineStore =
            Corona::Horizon::ResourceStore<Corona::Horizon::RasterizerPipelineWrap, Corona::Horizon::NoopReleaser>;
        auto pipeline_resource =
            Corona::Horizon::read<RasterizerPipelineStore>(Corona::Horizon::ResourceBridge::token(pipeline));
        expect(pipeline_resource && pipeline_resource->impl,
               "Real RasterizerPipeline smoke test should resolve the Vulkan implementation.");

        auto impl =
            std::static_pointer_cast<Corona::Horizon::VulkanRasterizerPipeline>(pipeline_resource->impl);
        Corona::Horizon::CommandBatch batch = impl->command_batch();
        batch << color.copy_to(readback);

        Corona::Horizon::HardwareExecutor executor([&manager](Corona::Horizon::DeviceId, Corona::Horizon::QueueCapability capability) -> Corona::Horizon::Queue& {
            Corona::Horizon::Queue* queue = manager.queue_for(capability);
            if (queue == nullptr)
            {
                throw std::runtime_error("RasterizerPipeline smoke test could not resolve a Vulkan queue.");
            }
            return *queue;
        });

        Corona::Horizon::SubmitReceipt receipt =
            executor.stream()
            << batch
            << Corona::Horizon::commit();

        expect(!receipt.tokens.empty(), "Real RasterizerPipeline smoke test should submit Vulkan queue work.");
        for (Corona::Horizon::SubmissionToken token : receipt.tokens)
        {
            Corona::Horizon::Queue* queue = manager.queue_for(token.queue.capability);
            expect(queue != nullptr, "Submitted RasterizerPipeline token should resolve to a queue for retirement.");
            wait_for_token(*queue, token);
        }

        std::vector<uint32_t> pixels(pixel_count, 0);
        expect(readback.read<uint32_t>(pixels), "RasterizerPipeline smoke test should read back the copied color attachment.");

        const uint32_t center = pixels[(height / 2) * width + (width / 2)];
        expect(center == 0xff0000ffu, "RasterizerPipeline should render the red fullscreen triangle into the color attachment.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_hardware_executor_uses_injected_queue()
    {
        Corona::Horizon::Queue transfer_queue({ 0 }, Corona::Horizon::QueueCapability::Transfer);
        Corona::Horizon::HardwareExecutor executor([&transfer_queue](Corona::Horizon::DeviceId, Corona::Horizon::QueueCapability capability) -> Corona::Horizon::Queue& {
            expect(capability == Corona::Horizon::QueueCapability::Transfer, "Recorded copy should resolve the transfer queue.");
            return transfer_queue;
        });

        Corona::Horizon::ResourceHandle src = test_resource(201);
        Corona::Horizon::ResourceHandle dst = test_resource(202);
        Corona::Horizon::RecordedTask task = executor.record([&](Corona::Horizon::CommandRecorder& recorder) {
            recorder.copy({ src }, { dst }, { 0, 0, 4 });
        });

        Corona::Horizon::ExecutionPlan plan = executor.compile(task);
        std::vector<Corona::Horizon::SubmissionToken> tokens = executor.submit(plan);
        expect(tokens.size() == 1, "Executor should submit one queue batch.");
        expect(tokens[0].value == 1, "Submitted batch should expose the queue timeline token.");
        expect(transfer_queue.in_flight_count() == 1, "Injected queue should own the submitted work.");

        transfer_queue.mark_completed_for_tests(tokens[0].value);
        transfer_queue.retire_completed();
        expect(transfer_queue.in_flight_count() == 0, "Submitted executor work should retire through the queue.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_stream_facade_records_and_commits()
    {
        Corona::Horizon::Queue transfer_queue({ 0 }, Corona::Horizon::QueueCapability::Transfer);
        Corona::Horizon::Queue compute_queue({ 0 }, Corona::Horizon::QueueCapability::Compute);

        Corona::Horizon::HardwareExecutor executor(
            [&](Corona::Horizon::DeviceId, Corona::Horizon::QueueCapability capability) -> Corona::Horizon::Queue& {
                if (capability == Corona::Horizon::QueueCapability::Compute)
                {
                    return compute_queue;
                }
                return transfer_queue;
            });

        Corona::Horizon::ResourceHandle src = test_resource(301);
        Corona::Horizon::ResourceHandle dst = test_resource(302);
        Corona::Horizon::ResourceHandle shader = test_resource(303);

        Corona::Horizon::SubmitReceipt receipt =
            executor.stream()
            << Corona::Horizon::copy({ src }, { dst }, { 0, 0, 16 })
            << Corona::Horizon::dispatch({ shader }, { 2, 1, 1 })
            << Corona::Horizon::commit();

        expect(receipt.serial == 1, "Stream commit should assign a submit serial.");
        expect(receipt.tokens.size() == 2, "Copy and dispatch should submit to two queue batches.");
        expect(transfer_queue.in_flight_count() == 1, "Transfer queue should receive the copy batch.");
        expect(compute_queue.in_flight_count() == 1, "Compute queue should receive the dispatch batch.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_stream_batch_and_close_for_tests_preserve_order()
    {
        Corona::Horizon::HardwareExecutor executor;
        Corona::Horizon::ResourceHandle src = test_resource(311);
        Corona::Horizon::ResourceHandle dst = test_resource(312);
        Corona::Horizon::ResourceHandle shader = test_resource(313);

        Corona::Horizon::CommandBatch batch;
        batch << Corona::Horizon::copy({ src }, { dst }, { 0, 4, 8 })
              << Corona::Horizon::dispatch({ shader }, { 1, 2, 3 });

        auto stream = executor.stream();
        stream << batch;
        Corona::Horizon::RecordedTask task = stream.close_for_tests();

        expect(task.commands.size() == 2, "CommandBatch should append both commands.");
        expect(task.commands[0].op == Corona::Horizon::CommandOp::CopyBuffer, "First stream command should be copy.");
        expect(task.commands[1].op == Corona::Horizon::CommandOp::Dispatch, "Second stream command should be dispatch.");
        expect(task.commands[0].sequence == 0 && task.commands[1].sequence == 1, "Recorder should preserve command sequence numbers.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_ocarina_style_value_commands_feed_stream()
    {
        Corona::Horizon::HardwareExecutor executor;
        Corona::Horizon::ResourceHandle src = test_resource(315);
        Corona::Horizon::ResourceHandle dst = test_resource(316);
        Corona::Horizon::ResourceHandle shader = test_resource(317);

        Corona::Horizon::CopyBufferCommand copy_command =
            Corona::Horizon::copy({ src }, { dst }, { 4, 8, 32 });
        expect(copy_command.copy_region().size == 32, "Typed copy command should expose its payload.");

        auto retained = std::make_shared<int>(9);

        Corona::Horizon::CommandBatch batch;
        batch << copy_command
              << Corona::Horizon::dispatch({ shader }, { 3, 2, 1 })
              << Corona::Horizon::keep_alive(retained)
              << Corona::Horizon::keep_alive(std::string { "host payload" });

        auto stream = executor.stream();
        stream << batch;
        Corona::Horizon::RecordedTask task = stream.close_for_tests();

        expect(task.commands.size() == 4, "Typed value commands should erase into stream commands.");
        expect(task.commands[0].op == Corona::Horizon::CommandOp::CopyBuffer, "First typed command should record copy IR.");
        expect(task.commands[1].op == Corona::Horizon::CommandOp::Dispatch, "Second typed command should record dispatch IR.");
        expect(task.commands[2].op == Corona::Horizon::CommandOp::KeepAlive, "Shared pointer keep_alive should record keep-alive IR.");
        expect(task.commands[3].op == Corona::Horizon::CommandOp::KeepAlive, "Copyable host values should record keep-alive IR.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_compiler_keeps_non_contiguous_queue_batches_ordered()
    {
        Corona::Horizon::ResourceHandle src = test_resource(321);
        Corona::Horizon::ResourceHandle dst = test_resource(322);
        Corona::Horizon::ResourceHandle shader = test_resource(323);

        Corona::Horizon::CommandRecorder recorder;
        recorder.copy({ src }, { dst }, { 0, 0, 16 });
        recorder.dispatch({ shader }, { 1, 1, 1 });
        recorder.copy({ dst }, { src }, { 0, 0, 16 });

        Corona::Horizon::ExecutionCompiler compiler;
        Corona::Horizon::ExecutionPlan plan = compiler.compile(recorder.close());

        expect(plan.submissions.size() == 3, "Non-contiguous transfer work should not be merged across compute work.");
        expect(plan.submissions[0].queue == Corona::Horizon::QueueCapability::Transfer, "First batch should be transfer.");
        expect(plan.submissions[1].queue == Corona::Horizon::QueueCapability::Compute, "Second batch should be compute.");
        expect(plan.submissions[2].queue == Corona::Horizon::QueueCapability::Transfer, "Third batch should return to transfer.");
        expect(!plan.dependencies.empty(), "Reusing written resources across non-contiguous batches should create a DAG dependency.");

        const auto waits_for_first_transfer = std::ranges::find_if(plan.dependencies, [](const Corona::Horizon::SubmissionDependency& dependency) {
            return dependency.producer == 0 && dependency.consumer == 2;
        });
        expect(waits_for_first_transfer != plan.dependencies.end(),
               "The second transfer batch should wait for the first transfer batch.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_host_callback_runs_when_submission_retires()
    {
        Corona::Horizon::Queue transfer_queue({ 0 }, Corona::Horizon::QueueCapability::Transfer);
        Corona::Horizon::HardwareExecutor executor([&](Corona::Horizon::DeviceId, Corona::Horizon::QueueCapability) -> Corona::Horizon::Queue& {
            return transfer_queue;
        });

        bool callback_ran = false;
        Corona::Horizon::SubmitReceipt receipt =
            executor.stream()
            << Corona::Horizon::host_callback([&callback_ran] {
                   callback_ran = true;
               })
            << Corona::Horizon::commit();

        expect(receipt.tokens.size() == 1, "Host callback should submit as a tracked timeline batch.");
        expect(!callback_ran, "Host callback should wait for queue retirement.");

        transfer_queue.mark_completed_for_tests(receipt.tokens[0].value);
        transfer_queue.retire_completed();

        expect(callback_ran, "Host callback should run when tracked keep-alives retire.");
        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_present_node_reports_receipt_status()
    {
        Corona::Horizon::Queue present_queue({ 0 }, Corona::Horizon::QueueCapability::Present);
        Corona::Horizon::HardwareExecutor executor([&](Corona::Horizon::DeviceId, Corona::Horizon::QueueCapability capability) -> Corona::Horizon::Queue& {
            expect(capability == Corona::Horizon::QueueCapability::Present, "Present command should resolve the present queue.");
            return present_queue;
        });

        Corona::Horizon::ResourceHandle image = test_resource(401);
        Corona::Horizon::SubmitReceipt receipt =
            executor.stream()
            << Corona::Horizon::present({ 77 }, { image }, { 0 })
            << Corona::Horizon::commit();

        expect(receipt.tokens.size() == 1, "Present node should still produce a queue submission token.");
        expect(receipt.presents.size() == 1, "SubmitReceipt should include present status.");
        expect(receipt.presents[0].status == Corona::Horizon::PresentStatus::Skipped,
               "No swapchain-bound DisplayManager should report a skipped present.");

        std::shared_ptr<Corona::Horizon::DisplayManager> manager =
            Corona::Horizon::make_fake_display_manager({ 78 });
        manager->set_fake_present_status_for_tests(Corona::Horizon::PresentStatus::Presented, "fake presented");
        Corona::Horizon::register_display_manager(manager);

        Corona::Horizon::SubmitReceipt fake_receipt =
            executor.stream()
            << Corona::Horizon::present({ 78 }, { image }, { 0 })
            << Corona::Horizon::commit();

        expect(fake_receipt.tokens.size() == 1, "Registered fake DisplayManager should still submit a present batch.");
        expect(fake_receipt.presents.size() == 1, "Registered fake DisplayManager should report one present result.");
        expect(fake_receipt.presents[0].status == Corona::Horizon::PresentStatus::Presented,
               "Registered fake DisplayManager status should flow through SubmitReceipt.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_cross_device_present_fallback_and_strict_dependency()
    {
        Corona::Horizon::ResourceHandle src = test_resource(501);
        Corona::Horizon::ResourceHandle dst = test_resource(502);

        {
            Corona::Horizon::CommandRecorder recorder;
            recorder.copy({ src }, { dst }, { 0, 0, 16 });
            recorder.present({ 91 }, { dst }, { 1 }, true);

            Corona::Horizon::ExecutionCompiler compiler;
            Corona::Horizon::ExecutionPlan plan = compiler.compile(recorder.close());
            expect(plan.cross_device_dependencies.size() == 1,
                   "Cross-device present should record a dependency bridge.");
            expect(plan.cross_device_dependencies[0].present_cpu_bridge_fallback,
                   "Present may use the CPU bridge fallback when imported timeline sync is unavailable.");
        }

        {
            Corona::Horizon::CommandRecorder recorder;
            recorder.copy({ src }, { dst }, { 0, 0, 16 });
            recorder.copy({ dst }, { src }, { 0, 0, 16 }, { 2 });

            bool threw = false;
            try
            {
                Corona::Horizon::ExecutionCompiler compiler;
                (void)compiler.compile(recorder.close());
            }
            catch (const std::logic_error&)
            {
                threw = true;
            }

            expect(threw, "Non-present cross-device resource dependency should fail without explicit sync.");
        }

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_parallel_recorders_and_serialized_queue_submit()
    {
        constexpr int thread_count = 8;
        constexpr int submits_per_thread = 16;

        std::atomic<int> recorded { 0 };
        std::vector<std::thread> recorder_threads;
        recorder_threads.reserve(thread_count);

        for (int thread = 0; thread < thread_count; ++thread)
        {
            recorder_threads.emplace_back([&recorded] {
                Corona::Horizon::CommandRecorder recorder;
                recorder.dispatch({}, { 1, 1, 1 });
                Corona::Horizon::RecordedTask task = recorder.close();
                if (task.commands.size() == 1 && task.requirements.compute)
                {
                    recorded.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        for (std::thread& thread : recorder_threads)
        {
            thread.join();
        }

        expect(recorded.load(std::memory_order_relaxed) == thread_count,
               "Independent command recorders should close correctly on multiple threads.");

        Corona::Horizon::Queue queue({ 0 }, Corona::Horizon::QueueCapability::Transfer);
        std::vector<std::thread> submit_threads;
        submit_threads.reserve(thread_count);

        for (int thread = 0; thread < thread_count; ++thread)
        {
            submit_threads.emplace_back([&queue] {
                for (int index = 0; index < submits_per_thread; ++index)
                {
                    Corona::Horizon::QueueSubmission submission;
                    submission.command_buffer = queue.acquire();
                    (void)queue.submit(submission, {}, {});
                }
            });
        }

        for (std::thread& thread : submit_threads)
        {
            thread.join();
        }

        const uint64_t expected_submissions = static_cast<uint64_t>(thread_count * submits_per_thread);
        expect(queue.last_submitted_value() == expected_submissions, "Queue submit should serialize timeline increments.");
        expect(queue.in_flight_count() == expected_submissions, "Every fake submit should be tracked in flight.");

        queue.mark_completed_for_tests(expected_submissions);
        queue.retire_completed();
        expect(queue.in_flight_count() == 0, "Completed parallel submissions should all retire.");
        expect(queue.pooled_count() == expected_submissions, "Retired command buffers should return to the pool.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    struct MeshRenderDisplayThreadState
    {
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<uint32_t> mesh_frames;
        std::shared_ptr<Corona::Horizon::DisplayManager> display_manager;
        std::exception_ptr failure;
        Corona::Horizon::DisplayerRef displayer { 601 };
        uint32_t mesh_produced { 0 };
        uint32_t render_consumed { 0 };
        uint32_t present_results { 0 };
        uint64_t submitted_timeline { 0 };
        size_t in_flight_after_retire { 0 };
        bool display_ready { false };
        bool mesh_finished { false };
        bool render_finished { false };
        bool stop_requested { false };
    };

    void publish_thread_failure(MeshRenderDisplayThreadState& state)
    {
        {
            std::lock_guard lock(state.mutex);
            if (state.failure == nullptr)
            {
                state.failure = std::current_exception();
            }
            state.stop_requested = true;
        }
        state.cv.notify_all();
    }

    [[nodiscard]] bool push_mesh_frame(MeshRenderDisplayThreadState& state, uint32_t frame)
    {
        std::unique_lock lock(state.mutex);
        state.cv.wait(lock, [&] {
            return state.stop_requested || state.mesh_frames.size() < 2;
        });

        if (state.stop_requested)
        {
            return false;
        }

        state.mesh_frames.push_back(frame);
        ++state.mesh_produced;
        lock.unlock();
        state.cv.notify_all();
        return true;
    }

    [[nodiscard]] std::optional<uint32_t> pop_mesh_frame(MeshRenderDisplayThreadState& state)
    {
        std::unique_lock lock(state.mutex);
        state.cv.wait(lock, [&] {
            return state.stop_requested || !state.mesh_frames.empty() || state.mesh_finished;
        });

        if (state.stop_requested || state.mesh_frames.empty())
        {
            return std::nullopt;
        }

        uint32_t frame = state.mesh_frames.front();
        state.mesh_frames.pop_front();
        lock.unlock();
        state.cv.notify_all();
        return frame;
    }

    [[nodiscard]] Corona::Horizon::DisplayerRef wait_for_fake_display(MeshRenderDisplayThreadState& state)
    {
        std::unique_lock lock(state.mutex);
        state.cv.wait(lock, [&] {
            return state.display_ready || state.stop_requested || state.failure != nullptr;
        });

        std::exception_ptr failure = state.failure;
        if (failure != nullptr)
        {
            lock.unlock();
            std::rethrow_exception(failure);
        }

        expect(state.display_ready, "Display thread should publish a displayer before render starts.");
        return state.displayer;
    }

    void mesh_thread_for_tests(MeshRenderDisplayThreadState& state, uint32_t frame_count) noexcept
    {
        try
        {
            for (uint32_t frame = 0; frame < frame_count; ++frame)
            {
                if (!push_mesh_frame(state, frame))
                {
                    break;
                }
            }
        }
        catch (...)
        {
            publish_thread_failure(state);
        }

        {
            std::lock_guard lock(state.mutex);
            state.mesh_finished = true;
        }
        state.cv.notify_all();
    }

    void display_thread_for_tests(MeshRenderDisplayThreadState& state) noexcept
    {
        try
        {
            std::shared_ptr<Corona::Horizon::DisplayManager> manager =
                Corona::Horizon::make_fake_display_manager(state.displayer);
            manager->set_fake_present_status_for_tests(Corona::Horizon::PresentStatus::Presented, "mesh/render/display fake present");
            Corona::Horizon::register_display_manager(manager);

            {
                std::lock_guard lock(state.mutex);
                state.display_manager = std::move(manager);
                state.display_ready = true;
            }
            state.cv.notify_all();

            std::unique_lock lock(state.mutex);
            state.cv.wait(lock, [&] {
                return state.render_finished || state.stop_requested || state.failure != nullptr;
            });
        }
        catch (...)
        {
            publish_thread_failure(state);
        }
    }

    void render_thread_for_tests(MeshRenderDisplayThreadState& state) noexcept
    {
        try
        {
            Corona::Horizon::DisplayerRef displayer = wait_for_fake_display(state);
            Corona::Horizon::Queue present_queue({ 0 }, Corona::Horizon::QueueCapability::Present);
            Corona::Horizon::HardwareExecutor executor(
                [&](Corona::Horizon::DeviceId, Corona::Horizon::QueueCapability capability) -> Corona::Horizon::Queue& {
                    expect(capability == Corona::Horizon::QueueCapability::Present,
                           "Threaded render path should submit only the present queue in this fake test.");
                    return present_queue;
                });

            Corona::Horizon::ResourceHandle image = test_resource(602);
            uint32_t consumed = 0;
            uint32_t presented = 0;
            for (std::optional<uint32_t> frame = pop_mesh_frame(state);
                 frame;
                 frame = pop_mesh_frame(state))
            {
                (void)*frame;
                Corona::Horizon::SubmitReceipt receipt =
                    executor.stream()
                    << Corona::Horizon::present(displayer, { image }, { 0 })
                    << Corona::Horizon::commit();

                expect(receipt.tokens.size() == 1, "Each threaded fake frame should submit one present batch.");
                expect(receipt.presents.size() == 1, "Each threaded fake frame should report one present result.");
                expect(receipt.presents[0].status == Corona::Horizon::PresentStatus::Presented,
                       "Fake display manager should surface a Presented receipt.");
                ++consumed;
                ++presented;
            }

            const uint64_t submitted = present_queue.last_submitted_value();
            present_queue.mark_completed_for_tests(submitted);
            present_queue.retire_completed();

            {
                std::lock_guard lock(state.mutex);
                state.render_consumed = consumed;
                state.present_results = presented;
                state.submitted_timeline = submitted;
                state.in_flight_after_retire = present_queue.in_flight_count();
                state.render_finished = true;
                state.stop_requested = true;
            }
            state.cv.notify_all();
        }
        catch (...)
        {
            publish_thread_failure(state);
            {
                std::lock_guard lock(state.mutex);
                state.render_finished = true;
                state.stop_requested = true;
            }
            state.cv.notify_all();
        }
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_mesh_render_display_thread_pipeline()
    {
        constexpr uint32_t frame_count = 4;
        MeshRenderDisplayThreadState state;

        std::thread display_thread(display_thread_for_tests, std::ref(state));
        std::thread mesh_thread(mesh_thread_for_tests, std::ref(state), frame_count);
        std::thread render_thread(render_thread_for_tests, std::ref(state));

        mesh_thread.join();
        render_thread.join();
        display_thread.join();

        std::exception_ptr failure;
        uint32_t mesh_produced = 0;
        uint32_t render_consumed = 0;
        uint32_t present_results = 0;
        uint64_t submitted_timeline = 0;
        size_t in_flight_after_retire = 0;
        {
            std::lock_guard lock(state.mutex);
            failure = state.failure;
            mesh_produced = state.mesh_produced;
            render_consumed = state.render_consumed;
            present_results = state.present_results;
            submitted_timeline = state.submitted_timeline;
            in_flight_after_retire = state.in_flight_after_retire;
        }

        if (failure != nullptr)
        {
            std::rethrow_exception(failure);
        }

        expect(mesh_produced == frame_count, "Mesh thread should produce every requested immutable frame snapshot.");
        expect(render_consumed == frame_count, "Render thread should consume every mesh frame snapshot.");
        expect(present_results == frame_count, "Render thread should receive one present result per frame.");
        expect(submitted_timeline == frame_count, "Present queue should serialize one submission per consumed frame.");
        expect(in_flight_after_retire == 0, "Threaded fake present submissions should retire cleanly.");

        return Corona::Horizon::Tests::TestResult::pass();
    }
}

namespace Corona::Horizon::Tests
{
    std::vector<TestCase> execution_system_tests()
    {
        return {
            {
                "execution.keep_alive_retirement",
                "Fake queue keeps submission resources alive until its timeline completion value retires the command buffer.",
                test_keep_alive_retires_after_timeline_completion,
            },
            {
                "execution.partial_timeline_retirement",
                "Fake queue retires only submissions whose timeline values have completed.",
                test_partial_timeline_retirement_keeps_newer_work,
            },
            {
                "execution.command_buffer_pool_reuse",
                "Queue reuses retired command buffers before allocating fresh tracked buffers.",
                test_retired_command_buffer_is_reused,
            },
            {
                "execution.submit_auto_command_buffer",
                "Queue submit creates and tracks a command buffer when the caller did not acquire one first.",
                test_submit_creates_missing_command_buffer,
            },
            {
                "execution.submit_failure_keeps_resources",
                "A failed submit leaves command buffer and keep-alive ownership with the caller.",
                test_submit_failure_keeps_submission_resources,
            },
            {
                "execution.recorder_compiler_ir",
                "CommandRecorder records abstract IR and ExecutionCompiler collects requirements, keep-alives, and hazards.",
                test_recorder_and_compiler_collect_requirements,
            },
            {
                "execution.rasterizer_pipeline_ir",
                "RasterizerPipeline keeps render state and erases recorded draws into graphics IR.",
                test_rasterizer_pipeline_records_graphics_ir,
            },
            {
                "execution.rasterizer_pipeline_handle",
                "RasterizerPipeline exposes the same ResourceHandle lifetime semantics as other public resources.",
                test_rasterizer_pipeline_uses_resource_handle,
            },
            {
                "execution.rasterizer_pipeline_real_vulkan_render",
                "RasterizerPipeline creates a real Vulkan graphics pipeline, renders, copies the color target, and verifies readback.",
                test_rasterizer_pipeline_real_vulkan_render,
            },
            {
                "execution.hardware_executor_injected_queue",
                "HardwareExecutor compiles recorded work and submits it through an injected queue resolver.",
                test_hardware_executor_uses_injected_queue,
            },
            {
                "execution.stream_facade_commit",
                "HardwareStream records ocarina-style commands and commits them through executor queues.",
                test_stream_facade_records_and_commits,
            },
            {
                "execution.stream_batch_order",
                "CommandBatch and HardwareStream preserve typed IR order before compile.",
                test_stream_batch_and_close_for_tests_preserve_order,
            },
            {
                "execution.ocarina_value_commands",
                "Ocarina-style value command objects erase into CommandBatch and HardwareStream.",
                test_ocarina_style_value_commands_feed_stream,
            },
            {
                "execution.compiler_dag_order",
                "ExecutionCompiler keeps non-contiguous queue batches ordered with explicit DAG dependencies.",
                test_compiler_keeps_non_contiguous_queue_batches_ordered,
            },
            {
                "execution.host_callback_retire",
                "Host callbacks are retained by the command buffer and run when the timeline retires.",
                test_host_callback_runs_when_submission_retires,
            },
            {
                "execution.present_receipt",
                "Present nodes submit through the present queue and report status through SubmitReceipt.",
                test_present_node_reports_receipt_status,
            },
            {
                "execution.cross_device_present",
                "Present cross-device dependencies allow a fallback while ordinary cross-device hazards fail.",
                test_cross_device_present_fallback_and_strict_dependency,
            },
            {
                "execution.parallel_record_and_submit",
                "Independent recorders close concurrently and Queue serializes parallel fake submissions.",
                test_parallel_recorders_and_serialized_queue_submit,
            },
            {
                "execution.mesh_render_display_threads",
                "Mesh, render, and display threads exchange frame snapshots and keep present as an execution node.",
                test_mesh_render_display_thread_pipeline,
            },
        };
    }
}
