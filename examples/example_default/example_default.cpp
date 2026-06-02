#include "example_default.h"

#if defined(_WIN32) || defined(_WIN64)
#define GLFW_EXPOSE_NATIVE_WIN32
#endif

#include <GLFW/glfw3.h>
#if defined(_WIN32) || defined(_WIN64)
#include <GLFW/glfw3native.h>
#endif

#include "Horizon.h"
#include "hardware_wrapper_vulkan/hardware/device_manager.h"
#include "hardware_wrapper_vulkan/hardware_context.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace
{
    constexpr uint32_t example_width = 800;
    constexpr uint32_t example_height = 480;

    struct MeshFrameSnapshot
    {
        uint32_t frame_index { 0 };
        std::array<uint16_t, 3> indices { 0, 1, 2 };
        std::array<uint32_t, 1> vertex_stub { 0 };
    };

    struct ExampleRenderResources
    {
        Corona::Horizon::HardwareImage color;
        Corona::Horizon::RasterizerPipeline pipeline;
    };

    struct ThreeThreadState
    {
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<MeshFrameSnapshot> mesh_frames;
        std::optional<Corona::Horizon::HardwareDisplayer> displayer;
        std::exception_ptr failure;
        bool display_ready { false };
        bool mesh_finished { false };
        bool render_finished { false };
        bool stop_requested { false };
    };

    [[nodiscard]] Corona::Horizon::RasterizerPipelineDesc fullscreen_triangle_desc()
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
    out_color = vec4(0.95, 0.12, 0.05, 1.0);
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

    [[nodiscard]] MeshFrameSnapshot make_mesh_frame(uint32_t frame_index) noexcept
    {
        MeshFrameSnapshot frame;
        frame.frame_index = frame_index;
        return frame;
    }

    [[nodiscard]] ExampleRenderResources create_render_resources(uint32_t width,
                                                                 uint32_t height,
                                                                 const MeshFrameSnapshot& mesh)
    {
        ExampleRenderResources resources {
            Corona::Horizon::HardwareImage(
                Corona::Horizon::HardwareImageDesc::color_attachment(width,
                                                                     height,
                                                                     Corona::Horizon::Format::BGRA8_UNORM,
                                                                     "example.default.color")),
            Corona::Horizon::RasterizerPipeline(fullscreen_triangle_desc()),
        };
        resources.color.set_clear_color(0.02f, 0.03f, 0.04f, 1.0f);

        resources.pipeline.bind_render_target(0, resources.color);
        resources.pipeline(static_cast<uint16_t>(width), static_cast<uint16_t>(height));

        Corona::Horizon::HardwareBuffer index_buffer =
            Corona::Horizon::HardwareBuffer::index(mesh.indices, "example.default.index");
        Corona::Horizon::HardwareBuffer vertex_buffer =
            Corona::Horizon::HardwareBuffer::vertex(mesh.vertex_stub, "example.default.vertex");

        Corona::Horizon::DrawIndexedParams draw;
        draw.index_count = static_cast<uint32_t>(mesh.indices.size());
        draw.index_type = Corona::Horizon::IndexType::UInt16;
        resources.pipeline.record(index_buffer, vertex_buffer, draw);

        return resources;
    }

    void wait_for_receipt(const Corona::Horizon::SubmitReceipt& receipt)
    {
        for (const Corona::Horizon::SubmissionToken& token : receipt.tokens)
        {
            Corona::Horizon::Queue* queue =
                Corona::Horizon::main_device_context().device_manager.queue_for(token.queue.capability);
            if (queue == nullptr)
            {
                continue;
            }

            if (token.timeline == VK_NULL_HANDLE)
            {
                queue->mark_completed_for_tests(token.value);
                queue->retire_completed();
                continue;
            }

            VkSemaphoreWaitInfo wait_info {};
            wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
            wait_info.semaphoreCount = 1;
            wait_info.pSemaphores = &token.timeline;
            wait_info.pValues = &token.value;

            const VkResult result = vkWaitSemaphores(queue->device(), &wait_info, 5'000'000'000ull);
            if (result != VK_SUCCESS)
            {
                throw std::runtime_error("vkWaitSemaphores failed while waiting for HorizonExamples frame. VkResult=" +
                                         std::to_string(static_cast<int>(result)));
            }

            queue->retire_completed();
        }
    }

    void request_stop(ThreeThreadState& state)
    {
        {
            std::lock_guard lock(state.mutex);
            state.stop_requested = true;
        }
        state.cv.notify_all();
    }

    void publish_failure(ThreeThreadState& state)
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

    void mark_mesh_finished(ThreeThreadState& state)
    {
        {
            std::lock_guard lock(state.mutex);
            state.mesh_finished = true;
        }
        state.cv.notify_all();
    }

    void mark_render_finished(ThreeThreadState& state)
    {
        {
            std::lock_guard lock(state.mutex);
            state.render_finished = true;
            state.stop_requested = true;
        }
        state.cv.notify_all();
    }

    [[nodiscard]] bool push_mesh_frame(ThreeThreadState& state, MeshFrameSnapshot frame)
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
        lock.unlock();
        state.cv.notify_all();
        return true;
    }

    [[nodiscard]] std::optional<MeshFrameSnapshot> pop_mesh_frame(ThreeThreadState& state)
    {
        std::unique_lock lock(state.mutex);
        state.cv.wait(lock, [&] {
            return state.stop_requested || !state.mesh_frames.empty() || state.mesh_finished;
        });

        if (state.stop_requested || state.mesh_frames.empty())
        {
            return std::nullopt;
        }

        MeshFrameSnapshot frame = state.mesh_frames.front();
        state.mesh_frames.pop_front();
        lock.unlock();
        state.cv.notify_all();
        return frame;
    }

    [[nodiscard]] Corona::Horizon::HardwareDisplayer wait_for_displayer(ThreeThreadState& state)
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

        if (!state.display_ready || !state.displayer)
        {
            throw std::runtime_error("Display thread stopped before creating the Horizon displayer.");
        }

        return *state.displayer;
    }

    [[nodiscard]] bool should_stop(ThreeThreadState& state)
    {
        std::lock_guard lock(state.mutex);
        return state.stop_requested;
    }

    void mesh_thread_main(ThreeThreadState& state, uint32_t frame_count) noexcept
    {
        try
        {
            for (uint32_t frame = 0; frame < frame_count; ++frame)
            {
                if (!push_mesh_frame(state, make_mesh_frame(frame)))
                {
                    break;
                }
            }
        }
        catch (...)
        {
            publish_failure(state);
        }

        mark_mesh_finished(state);
    }

    void display_thread_main(ThreeThreadState& state) noexcept
    {
        try
        {
#if !defined(_WIN32) && !defined(_WIN64)
            throw std::runtime_error("Horizon threaded default example currently supports Win32 GLFW windows only.");
#else
            if (glfwInit() != GLFW_TRUE)
            {
                throw std::runtime_error("glfwInit failed.");
            }

            struct GlfwGuard
            {
                ~GlfwGuard() { glfwTerminate(); }
            } glfw_guard;

            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            GLFWwindow* window = glfwCreateWindow(example_width, example_height, "Horizon default [mesh/render/display]", nullptr, nullptr);
            if (window == nullptr)
            {
                throw std::runtime_error("glfwCreateWindow failed.");
            }

            struct WindowGuard
            {
                GLFWwindow* window {};
                ~WindowGuard()
                {
                    if (window != nullptr)
                    {
                        glfwDestroyWindow(window);
                    }
                }
            } window_guard { window };

            {
                std::lock_guard lock(state.mutex);
                state.displayer = Corona::Horizon::HardwareDisplayer(glfwGetWin32Window(window));
                state.display_ready = true;
            }
            state.cv.notify_all();

            for (;;)
            {
                glfwPollEvents();
                if (glfwWindowShouldClose(window))
                {
                    request_stop(state);
                }

                {
                    std::lock_guard lock(state.mutex);
                    if (state.render_finished)
                    {
                        break;
                    }
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
#endif
        }
        catch (...)
        {
            publish_failure(state);
        }

        request_stop(state);
    }

    void render_thread_main(ThreeThreadState& state, uint32_t frame_count) noexcept
    {
        try
        {
            Corona::Horizon::HardwareDisplayer displayer = wait_for_displayer(state);
            std::optional<MeshFrameSnapshot> first_frame = pop_mesh_frame(state);
            if (!first_frame)
            {
                mark_render_finished(state);
                return;
            }

            ExampleRenderResources resources = create_render_resources(example_width, example_height, *first_frame);
            Corona::Horizon::HardwareExecutor executor;

            uint32_t rendered_frames = 0;
            std::optional<MeshFrameSnapshot> frame = std::move(first_frame);
            while (frame && rendered_frames < frame_count)
            {
                if (should_stop(state))
                {
                    break;
                }

                Corona::Horizon::SubmitReceipt receipt =
                    executor.stream()
                    << resources.pipeline.command_batch()
                    << Corona::Horizon::present(displayer, resources.color)
                    << Corona::Horizon::commit();

                wait_for_receipt(receipt);
                ++rendered_frames;
                if (rendered_frames < frame_count)
                {
                    frame = pop_mesh_frame(state);
                }
            }
        }
        catch (...)
        {
            publish_failure(state);
        }

        mark_render_finished(state);
    }

    void join_if_needed(std::thread& thread)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

    void rethrow_recorded_failure(ThreeThreadState& state)
    {
        std::exception_ptr failure;
        {
            std::lock_guard lock(state.mutex);
            failure = state.failure;
        }

        if (failure != nullptr)
        {
            std::rethrow_exception(failure);
        }
    }
}

namespace
{
    void run_example_default_single_threaded(uint32_t frame_count)
    {
#if !defined(_WIN32) && !defined(_WIN64)
        (void)frame_count;
        throw std::runtime_error("Horizon default example currently supports Win32 GLFW windows only.");
#else
        if (glfwInit() != GLFW_TRUE)
        {
            throw std::runtime_error("glfwInit failed.");
        }

        struct GlfwGuard
        {
            ~GlfwGuard() { glfwTerminate(); }
        } glfw_guard;

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        GLFWwindow* window = glfwCreateWindow(example_width, example_height, "Horizon default", nullptr, nullptr);
        if (window == nullptr)
        {
            throw std::runtime_error("glfwCreateWindow failed.");
        }

        struct WindowGuard
        {
            GLFWwindow* window {};
            ~WindowGuard()
            {
                if (window != nullptr)
                {
                    glfwDestroyWindow(window);
                }
            }
        } window_guard { window };

        ExampleRenderResources resources = create_render_resources(example_width, example_height, make_mesh_frame(0));
        Corona::Horizon::HardwareExecutor executor;
        Corona::Horizon::HardwareDisplayer displayer(glfwGetWin32Window(window));

        for (uint32_t frame = 0; frame < frame_count && !glfwWindowShouldClose(window); ++frame)
        {
            Corona::Horizon::SubmitReceipt receipt =
                executor.stream()
                << resources.pipeline.command_batch()
                << Corona::Horizon::present(displayer, resources.color)
                << Corona::Horizon::commit();

            wait_for_receipt(receipt);
            glfwPollEvents();
        }
#endif
    }

    void run_example_default_mesh_render_display(uint32_t frame_count)
    {
        ThreeThreadState state;
        std::thread display_thread;
        std::thread mesh_thread;
        std::thread render_thread;

        try
        {
            display_thread = std::thread(display_thread_main, std::ref(state));
            mesh_thread = std::thread(mesh_thread_main, std::ref(state), frame_count);
            render_thread = std::thread(render_thread_main, std::ref(state), frame_count);
        }
        catch (...)
        {
            mark_render_finished(state);
            join_if_needed(display_thread);
            join_if_needed(mesh_thread);
            join_if_needed(render_thread);
            throw;
        }

        join_if_needed(mesh_thread);
        join_if_needed(render_thread);
        join_if_needed(display_thread);
        rethrow_recorded_failure(state);
    }
}

void run_example_default(uint32_t frame_count, ExampleDefaultThreadMode thread_mode)
{
    switch (thread_mode)
    {
        case ExampleDefaultThreadMode::SingleThreaded:
            run_example_default_single_threaded(frame_count);
            break;
        case ExampleDefaultThreadMode::MeshRenderDisplay:
            run_example_default_mesh_render_display(frame_count);
            break;
        default:
            throw std::invalid_argument("Unknown Horizon default example thread mode.");
    }
}
