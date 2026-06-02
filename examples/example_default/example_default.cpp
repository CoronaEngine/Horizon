#include "example_default.h"

#if defined(_WIN32) || defined(_WIN64)
#define GLFW_EXPOSE_NATIVE_WIN32
#endif

#include <GLFW/glfw3.h>
#if defined(_WIN32) || defined(_WIN64)
#include <GLFW/glfw3native.h>
#endif

#include "Horizon.h"
#include "Codegen/BuiltinVariate.h"
#include "Codegen/TypeAlias.h"
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
#include <vector>

namespace HorizonExampleDefaultEdsl
{
    struct ExampleVertex
    {
        std::array<float, 2> position {};
        std::array<float, 3> color {};
    };

    struct ExampleVertexProxy
    {
        EmbeddedShader::Float2 position;
        EmbeddedShader::Float3 color;
    };
}

namespace
{
    using HorizonExampleDefaultEdsl::ExampleVertex;
    using HorizonExampleDefaultEdsl::ExampleVertexProxy;

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

    [[nodiscard]] std::string color_fragment_source(const std::array<float, 4>& color)
    {
        return std::string(R"glsl(
#version 450

layout(location = 0) out vec4 out_color;

void main()
{
    out_color = vec4()glsl") +
               std::to_string(color[0]) + ", " +
               std::to_string(color[1]) + ", " +
               std::to_string(color[2]) + ", " +
               std::to_string(color[3]) + R"glsl();
}
)glsl";
    }

    [[nodiscard]] Corona::Horizon::RasterizerPipelineDesc fullscreen_triangle_desc(std::array<float, 4> color = { 0.95f, 0.12f, 0.05f, 1.0f })
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
            color_fragment_source(color),
            EmbeddedShader::ShaderLanguage::GLSL,
            EmbeddedShader::ShaderLanguage::GLSL,
            compiler_option);

        desc.rasterizer.cull_mode = Corona::Horizon::CullMode::None;
        desc.depth_stencil.depth_test_enabled = false;
        desc.depth_stencil.depth_write_enabled = false;
        return desc;
    }

    [[nodiscard]] Corona::Horizon::RasterizerPipelineDesc edsl_triangle_desc()
    {
        using namespace EmbeddedShader;

        auto vertex_shader = [](Aggregate<ExampleVertexProxy> vertex) -> Float4 {
            position() = Float4(vertex->position, 0.0f, 1.0f);
            return Float4(vertex->color, 1.0f);
        };

        auto fragment_shader = [](Float4 input) -> Float4 {
            return input;
        };

        EmbeddedShader::CompilerOption compiler_option;
        compiler_option.compileGLSL = false;
        compiler_option.compileHLSL = false;
        compiler_option.compileDXIL = false;
        compiler_option.compileDXBC = false;
        compiler_option.compileSpirV = true;
        compiler_option.enableBindless = false;

        Corona::Horizon::RasterizerPipelineDesc desc =
            Corona::Horizon::RasterizerPipelineDesc::from_edsl(vertex_shader,
                                                               fragment_shader,
                                                               { compiler_option, true });
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

    [[nodiscard]] ExampleRenderResources create_fullscreen_resources(uint32_t width,
                                                                     uint32_t height,
                                                                     const MeshFrameSnapshot& mesh,
                                                                     Corona::Horizon::RasterizerPipelineDesc desc = fullscreen_triangle_desc(),
                                                                     std::string name = "example.default")
    {
        ExampleRenderResources resources {
            Corona::Horizon::HardwareImage(
                Corona::Horizon::HardwareImageDesc::color_attachment(width,
                                                                     height,
                                                                     Corona::Horizon::Format::BGRA8_UNORM,
                                                                     name + ".color")),
            Corona::Horizon::RasterizerPipeline(std::move(desc)),
        };
        resources.color.set_clear_color(0.02f, 0.03f, 0.04f, 1.0f);

        resources.pipeline.bind_render_target(0, resources.color);
        resources.pipeline(static_cast<uint16_t>(width), static_cast<uint16_t>(height));

        Corona::Horizon::HardwareBuffer index_buffer =
            Corona::Horizon::HardwareBuffer::index(mesh.indices, name + ".index");
        Corona::Horizon::HardwareBuffer vertex_buffer =
            Corona::Horizon::HardwareBuffer::vertex(mesh.vertex_stub, name + ".vertex");

        Corona::Horizon::DrawIndexedParams draw;
        draw.index_count = static_cast<uint32_t>(mesh.indices.size());
        draw.index_type = Corona::Horizon::IndexType::UInt16;
        resources.pipeline.record(index_buffer, vertex_buffer, draw);

        return resources;
    }

    [[nodiscard]] ExampleRenderResources create_edsl_resources(uint32_t width, uint32_t height)
    {
        ExampleRenderResources resources {
            Corona::Horizon::HardwareImage(
                Corona::Horizon::HardwareImageDesc::color_attachment(width,
                                                                     height,
                                                                     Corona::Horizon::Format::BGRA8_UNORM,
                                                                     "example.edsl.color")),
            Corona::Horizon::RasterizerPipeline(edsl_triangle_desc()),
        };
        resources.color.set_clear_color(0.02f, 0.03f, 0.04f, 1.0f);

        resources.pipeline.bind_render_target(0, resources.color);
        resources.pipeline(static_cast<uint16_t>(width), static_cast<uint16_t>(height));

        const std::array<ExampleVertex, 3> vertices {
            ExampleVertex { { -0.72f, -0.62f }, { 0.95f, 0.20f, 0.16f } },
            ExampleVertex { { 0.72f, -0.58f }, { 0.08f, 0.72f, 0.35f } },
            ExampleVertex { { 0.02f, 0.72f }, { 0.10f, 0.45f, 0.95f } },
        };
        const std::array<uint16_t, 3> indices { 0, 1, 2 };
        Corona::Horizon::HardwareBuffer vertex_buffer =
            Corona::Horizon::HardwareBuffer::vertex(vertices, "example.edsl.vertex");
        Corona::Horizon::HardwareBuffer index_buffer =
            Corona::Horizon::HardwareBuffer::index(indices, "example.edsl.index");

        Corona::Horizon::DrawIndexedParams draw;
        draw.index_count = static_cast<uint32_t>(indices.size());
        draw.index_type = Corona::Horizon::IndexType::UInt16;
        resources.pipeline.record(index_buffer, vertex_buffer, draw);

        return resources;
    }

    [[nodiscard]] std::vector<uint32_t> make_checker_pixels(uint32_t width, uint32_t height)
    {
        std::vector<uint32_t> pixels(static_cast<size_t>(width) * height);
        for (uint32_t y = 0; y < height; ++y)
        {
            for (uint32_t x = 0; x < width; ++x)
            {
                const bool bright = ((x / 32u) ^ (y / 32u)) != 0;
                const uint8_t r = bright ? 0xf0u : 0x18u;
                const uint8_t g = bright ? 0xdau : 0x28u;
                const uint8_t b = bright ? 0x30u : 0x80u;
                const uint8_t a = 0xffu;
                pixels[static_cast<size_t>(y) * width + x] =
                    static_cast<uint32_t>(r) |
                    (static_cast<uint32_t>(g) << 8u) |
                    (static_cast<uint32_t>(b) << 16u) |
                    (static_cast<uint32_t>(a) << 24u);
            }
        }
        return pixels;
    }

    [[nodiscard]] uint32_t div_ceil(uint32_t value, uint32_t divisor) noexcept
    {
        return divisor == 0 ? 0 : (value + divisor - 1u) / divisor;
    }

    [[nodiscard]] Corona::Horizon::ComputePipelineDesc compute_checker_desc()
    {
        EmbeddedShader::CompilerOption compiler_option;
        compiler_option.compileGLSL = false;
        compiler_option.compileHLSL = false;
        compiler_option.compileDXIL = false;
        compiler_option.compileDXBC = false;
        compiler_option.compileSpirV = true;
        compiler_option.enableBindless = false;

        return Corona::Horizon::ComputePipelineDesc::from_source(
            R"glsl(
#version 450

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0, rgba8) uniform writeonly image2D out_image;

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 extent = imageSize(out_image);
    if (pixel.x >= extent.x || pixel.y >= extent.y)
    {
        return;
    }

    vec2 uv = (vec2(pixel) + vec2(0.5)) / vec2(extent);
    bool tile = ((pixel.x / 24) ^ (pixel.y / 24)) != 0;
    vec3 a = vec3(0.08, 0.25, 0.70);
    vec3 b = vec3(0.95, 0.78, 0.16);
    vec3 color = mix(a, b, tile ? 1.0 : 0.0) * (0.65 + 0.35 * uv.y);
    imageStore(out_image, pixel, vec4(color, 1.0));
}
)glsl",
            EmbeddedShader::ShaderLanguage::GLSL,
            compiler_option);
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

            ExampleRenderResources resources = create_fullscreen_resources(example_width, example_height, *first_frame);
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

    using FrameRenderer = std::function<Corona::Horizon::SubmitReceipt(Corona::Horizon::HardwareExecutor&,
                                                                        const Corona::Horizon::HardwareDisplayer&,
                                                                        uint32_t)>;

    void run_single_window_loop(uint32_t frame_count, const char* title, FrameRenderer render_frame)
    {
#if !defined(_WIN32) && !defined(_WIN64)
        (void)frame_count;
        (void)title;
        (void)render_frame;
        throw std::runtime_error("Horizon examples currently support Win32 GLFW windows only.");
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
        GLFWwindow* window = glfwCreateWindow(example_width, example_height, title, nullptr, nullptr);
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

        Corona::Horizon::HardwareExecutor executor;
        Corona::Horizon::HardwareDisplayer displayer(glfwGetWin32Window(window));

        for (uint32_t frame = 0; frame < frame_count && !glfwWindowShouldClose(window); ++frame)
        {
            Corona::Horizon::SubmitReceipt receipt = render_frame(executor, displayer, frame);
            wait_for_receipt(receipt);
            glfwPollEvents();
        }
#endif
    }

    [[nodiscard]] Corona::Horizon::SubmitReceipt submit_render_present(Corona::Horizon::HardwareExecutor& executor,
                                                                       const Corona::Horizon::HardwareDisplayer& displayer,
                                                                       ExampleRenderResources& resources)
    {
        return executor.stream()
               << resources.pipeline.command_batch()
               << Corona::Horizon::present(displayer, resources.color)
               << Corona::Horizon::commit();
    }
}

namespace
{
    void run_example_default_single_threaded(uint32_t frame_count)
    {
        ExampleRenderResources resources =
            create_fullscreen_resources(example_width, example_height, make_mesh_frame(0));
        run_single_window_loop(frame_count,
                               "Horizon default",
                               [&resources](Corona::Horizon::HardwareExecutor& executor,
                                            const Corona::Horizon::HardwareDisplayer& displayer,
                                            uint32_t) {
                                   return submit_render_present(executor, displayer, resources);
                               });
    }

    void run_example_glsl(uint32_t frame_count)
    {
        ExampleRenderResources resources =
            create_fullscreen_resources(example_width,
                                        example_height,
                                        make_mesh_frame(0),
                                        fullscreen_triangle_desc({ 0.12f, 0.62f, 0.95f, 1.0f }),
                                        "example.glsl");
        run_single_window_loop(frame_count,
                               "Horizon GLSL",
                               [&resources](Corona::Horizon::HardwareExecutor& executor,
                                            const Corona::Horizon::HardwareDisplayer& displayer,
                                            uint32_t) {
                                   return submit_render_present(executor, displayer, resources);
                               });
    }

    void run_example_edsl(uint32_t frame_count)
    {
        ExampleRenderResources resources = create_edsl_resources(example_width, example_height);
        run_single_window_loop(frame_count,
                               "Horizon EDSL",
                               [&resources](Corona::Horizon::HardwareExecutor& executor,
                                            const Corona::Horizon::HardwareDisplayer& displayer,
                                            uint32_t) {
                                   return submit_render_present(executor, displayer, resources);
                               });
    }

    void run_example_texture(uint32_t frame_count)
    {
        std::vector<uint32_t> pixels = make_checker_pixels(example_width, example_height);
        Corona::Horizon::HardwareBuffer upload = Corona::Horizon::HardwareBuffer::storage(pixels, "example.texture.upload");
        Corona::Horizon::HardwareImage texture(
            Corona::Horizon::HardwareImageDesc::texture_2d(example_width,
                                                           example_height,
                                                           Corona::Horizon::Format::RGBA8_UNORM,
                                                           Corona::Horizon::ImageUsageFlags::Sampled |
                                                               Corona::Horizon::ImageUsageFlags::TransferDst |
                                                               Corona::Horizon::ImageUsageFlags::TransferSrc,
                                                           "example.texture.image"));

        bool uploaded = false;
        run_single_window_loop(frame_count,
                               "Horizon texture",
                               [&upload, &texture, &uploaded](Corona::Horizon::HardwareExecutor& executor,
                                                              const Corona::Horizon::HardwareDisplayer& displayer,
                                                              uint32_t) {
                                   if (!uploaded)
                                   {
                                       uploaded = true;
                                       return executor.stream()
                                              << upload.copy_to(texture)
                                              << Corona::Horizon::present(displayer, texture)
                                              << Corona::Horizon::commit();
                                   }

                                   return executor.stream()
                                          << Corona::Horizon::present(displayer, texture)
                                          << Corona::Horizon::commit();
                               });
    }

    void run_example_compute(uint32_t frame_count)
    {
        Corona::Horizon::HardwareImage output(
            Corona::Horizon::HardwareImageDesc::texture_2d(example_width,
                                                           example_height,
                                                           Corona::Horizon::Format::RGBA8_UNORM,
                                                           Corona::Horizon::ImageUsageFlags::Storage |
                                                               Corona::Horizon::ImageUsageFlags::TransferSrc |
                                                               Corona::Horizon::ImageUsageFlags::TransferDst,
                                                           "example.compute.output"));

        Corona::Horizon::ComputePipeline compute(compute_checker_desc());
        compute.bind_storage_image(0, output);
        compute(static_cast<uint16_t>(div_ceil(example_width, 16u)),
                static_cast<uint16_t>(div_ceil(example_height, 16u)),
                1);

        run_single_window_loop(frame_count,
                               "Horizon compute",
                               [&compute, &output](Corona::Horizon::HardwareExecutor& executor,
                                                   const Corona::Horizon::HardwareDisplayer& displayer,
                                                   uint32_t) {
                                   return executor.stream()
                                          << compute.command_batch()
                                          << Corona::Horizon::present(displayer, output)
                                          << Corona::Horizon::commit();
                               });
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

    void run_example_multi_window(uint32_t frame_count)
    {
#if !defined(_WIN32) && !defined(_WIN64)
        (void)frame_count;
        throw std::runtime_error("Horizon multi-window example currently supports Win32 GLFW windows only.");
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
        GLFWwindow* left_window = glfwCreateWindow(example_width, example_height, "Horizon multi-window A", nullptr, nullptr);
        GLFWwindow* right_window = glfwCreateWindow(example_width, example_height, "Horizon multi-window B", nullptr, nullptr);
        if (left_window == nullptr || right_window == nullptr)
        {
            if (left_window != nullptr)
                glfwDestroyWindow(left_window);
            if (right_window != nullptr)
                glfwDestroyWindow(right_window);
            throw std::runtime_error("glfwCreateWindow failed.");
        }

        struct WindowPairGuard
        {
            GLFWwindow* left {};
            GLFWwindow* right {};
            ~WindowPairGuard()
            {
                if (right != nullptr)
                    glfwDestroyWindow(right);
                if (left != nullptr)
                    glfwDestroyWindow(left);
            }
        } windows { left_window, right_window };

        ExampleRenderResources left =
            create_fullscreen_resources(example_width,
                                        example_height,
                                        make_mesh_frame(0),
                                        fullscreen_triangle_desc({ 0.92f, 0.18f, 0.20f, 1.0f }),
                                        "example.multi_window.left");
        ExampleRenderResources right =
            create_fullscreen_resources(example_width,
                                        example_height,
                                        make_mesh_frame(0),
                                        fullscreen_triangle_desc({ 0.10f, 0.70f, 0.32f, 1.0f }),
                                        "example.multi_window.right");

        Corona::Horizon::HardwareExecutor executor;
        Corona::Horizon::HardwareDisplayer left_displayer(glfwGetWin32Window(left_window));
        Corona::Horizon::HardwareDisplayer right_displayer(glfwGetWin32Window(right_window));

        for (uint32_t frame = 0;
             frame < frame_count && !glfwWindowShouldClose(left_window) && !glfwWindowShouldClose(right_window);
             ++frame)
        {
            Corona::Horizon::SubmitReceipt receipt =
                executor.stream()
                << left.pipeline.command_batch()
                << Corona::Horizon::present(left_displayer, left.color)
                << right.pipeline.command_batch()
                << Corona::Horizon::present(right_displayer, right.color)
                << Corona::Horizon::commit();

            wait_for_receipt(receipt);
            glfwPollEvents();
        }
#endif
    }
}

void run_example_default(uint32_t frame_count, ExampleDefaultThreadMode thread_mode, ExampleDefaultMode mode)
{
    switch (mode)
    {
        case ExampleDefaultMode::Default:
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
            break;
        case ExampleDefaultMode::Glsl:
            run_example_glsl(frame_count);
            break;
        case ExampleDefaultMode::Edsl:
            run_example_edsl(frame_count);
            break;
        case ExampleDefaultMode::Texture:
            run_example_texture(frame_count);
            break;
        case ExampleDefaultMode::Compute:
            run_example_compute(frame_count);
            break;
        case ExampleDefaultMode::MultiWindow:
            run_example_multi_window(frame_count);
            break;
        default:
            throw std::invalid_argument("Unknown Horizon example mode.");
    }
}
