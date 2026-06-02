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
#include <cstdint>
#include <stdexcept>
#include <string>

namespace
{
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
}

void run_example_default(uint32_t frame_count)
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

    constexpr uint32_t width = 800;
    constexpr uint32_t height = 480;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(width, height, "Horizon default", nullptr, nullptr);
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

    Corona::Horizon::HardwareImage color(
        Corona::Horizon::HardwareImageDesc::color_attachment(width,
                                                             height,
                                                             Corona::Horizon::Format::BGRA8_UNORM,
                                                             "example.default.color"));
    color.set_clear_color(0.02f, 0.03f, 0.04f, 1.0f);

    Corona::Horizon::RasterizerPipeline pipeline(fullscreen_triangle_desc());
    pipeline.bind_render_target(0, color);
    pipeline(width, height);

    const std::array<uint16_t, 3> indices { 0, 1, 2 };
    const std::array<uint32_t, 1> vertex_stub { 0 };
    Corona::Horizon::HardwareBuffer index_buffer = Corona::Horizon::HardwareBuffer::index(indices, "example.default.index");
    Corona::Horizon::HardwareBuffer vertex_buffer = Corona::Horizon::HardwareBuffer::vertex(vertex_stub, "example.default.vertex");

    Corona::Horizon::DrawIndexedParams draw;
    draw.index_count = static_cast<uint32_t>(indices.size());
    draw.index_type = Corona::Horizon::IndexType::UInt16;
    pipeline.record(index_buffer, vertex_buffer, draw);

    Corona::Horizon::HardwareExecutor executor;
    Corona::Horizon::HardwareDisplayer displayer(glfwGetWin32Window(window));

    for (uint32_t frame = 0; frame < frame_count && !glfwWindowShouldClose(window); ++frame)
    {
        Corona::Horizon::SubmitReceipt receipt =
            executor.stream()
            << pipeline.command_batch()
            << Corona::Horizon::present(displayer, color)
            << Corona::Horizon::commit();

        wait_for_receipt(receipt);
        glfwPollEvents();
    }
#endif
}
