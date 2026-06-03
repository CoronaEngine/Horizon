#include "example_default.h"

#if defined(_WIN32) || defined(_WIN64)
#define GLFW_EXPOSE_NATIVE_WIN32
#endif

#include <GLFW/glfw3.h>
#if defined(_WIN32) || defined(_WIN64)
#include <GLFW/glfw3native.h>
#endif

#include "horizon.h"
#include "Codegen/BuiltinVariate.h"
#include "Codegen/TypeAlias.h"
#include "hardware_wrapper_vulkan/hardware/device_manager.h"
#include "hardware_wrapper_vulkan/hardware_context.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cmath>
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

    struct CubeVertex
    {
        std::array<float, 3> position {};
        std::array<float, 3> color {};
    };

    struct Vec3
    {
        float x { 0.0f };
        float y { 0.0f };
        float z { 0.0f };
    };

    struct MeshFrameSnapshot
    {
        uint32_t frame_index { 0 };
        std::vector<CubeVertex> vertices;
        std::vector<uint32_t> indices;
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

    struct StressWindowState
    {
        std::deque<MeshFrameSnapshot> mesh_frames;
        std::optional<Corona::Horizon::HardwareDisplayer> displayer;
        bool mesh_finished { false };
        bool render_finished { false };
        bool open { true };
    };

    struct StressState
    {
        std::mutex mutex;
        std::condition_variable cv;
        std::vector<StressWindowState> windows;
        std::exception_ptr failure;
        bool display_ready { false };
        bool stop_requested { false };
    };

    struct StressRenderSlot
    {
        size_t window_index { 0 };
        Corona::Horizon::HardwareDisplayer displayer;
        ExampleRenderResources resources;
        bool finished { false };
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

    [[nodiscard]] Corona::Horizon::RasterizerPipelineDesc cube_mesh_desc()
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

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;

layout(location = 0) out vec3 fragment_color;

void main()
{
    gl_Position = vec4(in_position, 1.0);
    fragment_color = in_color;
}
)glsl",
            R"glsl(
#version 450

layout(location = 0) in vec3 fragment_color;
layout(location = 0) out vec4 out_color;

void main()
{
    out_color = vec4(fragment_color, 1.0);
}
)glsl",
            EmbeddedShader::ShaderLanguage::GLSL,
            EmbeddedShader::ShaderLanguage::GLSL,
            compiler_option);

        desc.rasterizer.cull_mode = Corona::Horizon::CullMode::None;
        desc.depth_stencil.depth_test_enabled = false;
        desc.depth_stencil.depth_write_enabled = false;
        desc.blend.set_opaque();
        return desc;
    }

    [[nodiscard]] Vec3 rotate_vec(Vec3 value, float x_angle, float y_angle, float z_angle) noexcept
    {
        const float sin_x = std::sin(x_angle);
        const float cos_x = std::cos(x_angle);
        Vec3 rotated {
            value.x,
            value.y * cos_x - value.z * sin_x,
            value.y * sin_x + value.z * cos_x,
        };

        const float sin_y = std::sin(y_angle);
        const float cos_y = std::cos(y_angle);
        rotated = {
            rotated.x * cos_y + rotated.z * sin_y,
            rotated.y,
            -rotated.x * sin_y + rotated.z * cos_y,
        };

        const float sin_z = std::sin(z_angle);
        const float cos_z = std::cos(z_angle);
        return {
            rotated.x * cos_z - rotated.y * sin_z,
            rotated.x * sin_z + rotated.y * cos_z,
            rotated.z,
        };
    }

    [[nodiscard]] CubeVertex project_vertex(Vec3 world, const std::array<float, 3>& color) noexcept
    {
        constexpr float camera_distance = 4.8f;
        constexpr float focal_length = 1.75f;
        constexpr float aspect = static_cast<float>(example_width) / static_cast<float>(example_height);

        const float camera_z = std::max(0.25f, world.z + camera_distance);
        return {
            {
                (world.x * focal_length / camera_z) / aspect,
                world.y * focal_length / camera_z,
                0.5f,
            },
            color,
        };
    }

    [[nodiscard]] std::array<float, 3> shaded_color(std::array<float, 3> color, float shade, float pulse) noexcept
    {
        const float brightness = std::clamp(shade * pulse, 0.20f, 1.0f);
        return {
            std::clamp(color[0] * brightness, 0.0f, 1.0f),
            std::clamp(color[1] * brightness, 0.0f, 1.0f),
            std::clamp(color[2] * brightness, 0.0f, 1.0f),
        };
    }

    [[nodiscard]] MeshFrameSnapshot make_mesh_frame(uint32_t frame_index) noexcept
    {
        struct CubeFaceTemplate
        {
            std::array<uint32_t, 4> corners {};
            std::array<float, 3> color {};
            float shade { 1.0f };
        };

        struct ProjectedFace
        {
            float depth { 0.0f };
            std::array<CubeVertex, 4> vertices {};
        };

        constexpr std::array<Vec3, 8> cube_corners {
            Vec3 { -0.5f, -0.5f, -0.5f },
            Vec3 { 0.5f, -0.5f, -0.5f },
            Vec3 { 0.5f, 0.5f, -0.5f },
            Vec3 { -0.5f, 0.5f, -0.5f },
            Vec3 { -0.5f, -0.5f, 0.5f },
            Vec3 { 0.5f, -0.5f, 0.5f },
            Vec3 { 0.5f, 0.5f, 0.5f },
            Vec3 { -0.5f, 0.5f, 0.5f },
        };

        constexpr std::array<CubeFaceTemplate, 6> cube_faces {
            CubeFaceTemplate { { 4, 5, 6, 7 }, { 0.96f, 0.22f, 0.16f }, 1.00f },
            CubeFaceTemplate { { 1, 0, 3, 2 }, { 0.12f, 0.54f, 0.95f }, 0.54f },
            CubeFaceTemplate { { 0, 4, 7, 3 }, { 0.13f, 0.82f, 0.46f }, 0.70f },
            CubeFaceTemplate { { 5, 1, 2, 6 }, { 0.98f, 0.72f, 0.18f }, 0.82f },
            CubeFaceTemplate { { 7, 6, 2, 3 }, { 0.73f, 0.35f, 0.96f }, 0.92f },
            CubeFaceTemplate { { 0, 1, 5, 4 }, { 0.14f, 0.76f, 0.82f }, 0.62f },
        };

        constexpr std::array<Vec3, 7> cube_centers {
            Vec3 { -1.75f, -0.82f, 0.15f },
            Vec3 { -0.55f, -0.86f, -0.18f },
            Vec3 { 0.70f, -0.78f, 0.06f },
            Vec3 { 1.88f, -0.64f, -0.10f },
            Vec3 { -1.18f, 0.72f, -0.08f },
            Vec3 { 0.25f, 0.64f, 0.20f },
            Vec3 { 1.55f, 0.62f, -0.22f },
        };

        constexpr float camera_distance = 4.8f;
        const float time = static_cast<float>(frame_index) * (1.0f / 60.0f);
        std::vector<ProjectedFace> faces;
        faces.reserve(cube_centers.size() * cube_faces.size());

        for (uint32_t cube_index = 0; cube_index < cube_centers.size(); ++cube_index)
        {
            const float cube = static_cast<float>(cube_index);
            Vec3 center = cube_centers[cube_index];
            center.x += 0.10f * std::sin(time * 1.15f + cube * 0.73f);
            center.y += 0.08f * std::cos(time * 0.92f + cube * 0.61f);
            center.z += 0.30f * std::sin(time * 0.77f + cube * 0.47f);

            const float scale = 0.68f + 0.08f * std::sin(cube * 1.31f);
            const float x_angle = time * (0.62f + cube * 0.015f) + cube * 0.33f;
            const float y_angle = time * (0.94f + cube * 0.020f) + cube * 0.71f;
            const float z_angle = time * (0.31f + cube * 0.010f) + cube * 0.18f;

            std::array<Vec3, cube_corners.size()> transformed {};
            for (size_t corner_index = 0; corner_index < cube_corners.size(); ++corner_index)
            {
                Vec3 corner = rotate_vec(cube_corners[corner_index], x_angle, y_angle, z_angle);
                transformed[corner_index] = {
                    center.x + corner.x * scale,
                    center.y + corner.y * scale,
                    center.z + corner.z * scale,
                };
            }

            const float pulse = 0.82f + 0.18f * std::sin(time * 1.7f + cube * 0.41f);
            for (const CubeFaceTemplate& face : cube_faces)
            {
                float depth = 0.0f;
                for (uint32_t corner : face.corners)
                    depth += transformed[corner].z + camera_distance;
                depth *= 0.25f;

                const std::array<float, 3> color = shaded_color(face.color, face.shade, pulse);
                faces.push_back({
                    depth,
                    {
                        project_vertex(transformed[face.corners[0]], color),
                        project_vertex(transformed[face.corners[1]], color),
                        project_vertex(transformed[face.corners[2]], color),
                        project_vertex(transformed[face.corners[3]], color),
                    },
                });
            }
        }

        std::ranges::sort(faces, [](const ProjectedFace& left, const ProjectedFace& right) {
            return left.depth > right.depth;
        });

        MeshFrameSnapshot frame;
        frame.frame_index = frame_index;
        frame.vertices.reserve(faces.size() * 4u);
        frame.indices.reserve(faces.size() * 6u);

        for (const ProjectedFace& face : faces)
        {
            const uint32_t base_vertex = static_cast<uint32_t>(frame.vertices.size());
            frame.vertices.insert(frame.vertices.end(), face.vertices.begin(), face.vertices.end());
            frame.indices.push_back(base_vertex);
            frame.indices.push_back(base_vertex + 1u);
            frame.indices.push_back(base_vertex + 2u);
            frame.indices.push_back(base_vertex);
            frame.indices.push_back(base_vertex + 2u);
            frame.indices.push_back(base_vertex + 3u);
        }
        return frame;
    }

    [[nodiscard]] ExampleRenderResources create_fullscreen_resources(uint32_t width,
                                                                     uint32_t height,
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

        const std::array<uint16_t, 3> indices { 0, 1, 2 };
        const std::array<uint32_t, 1> vertex_stub { 0 };
        Corona::Horizon::HardwareBuffer index_buffer =
            Corona::Horizon::HardwareBuffer::index(indices, name + ".index");
        Corona::Horizon::HardwareBuffer vertex_buffer =
            Corona::Horizon::HardwareBuffer::vertex(vertex_stub, name + ".vertex");

        Corona::Horizon::DrawIndexedParams draw;
        draw.index_count = static_cast<uint32_t>(indices.size());
        draw.index_type = Corona::Horizon::IndexType::UInt16;
        resources.pipeline.record(index_buffer, vertex_buffer, draw);

        return resources;
    }

    [[nodiscard]] ExampleRenderResources create_cube_resources(uint32_t width,
                                                               uint32_t height,
                                                               std::string name = "example.default.cubes")
    {
        ExampleRenderResources resources {
            Corona::Horizon::HardwareImage(
                Corona::Horizon::HardwareImageDesc::color_attachment(width,
                                                                     height,
                                                                     Corona::Horizon::Format::BGRA8_UNORM,
                                                                     name + ".color")),
            Corona::Horizon::RasterizerPipeline(cube_mesh_desc()),
        };
        resources.color.set_clear_color(0.012f, 0.016f, 0.026f, 1.0f);

        resources.pipeline.bind_render_target(0, resources.color);
        resources.pipeline(static_cast<uint16_t>(width), static_cast<uint16_t>(height));
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

            VkSemaphore timeline = Corona::Horizon::native_timeline_from_handle(token.timeline_handle);
            if (timeline == VK_NULL_HANDLE)
            {
                queue->mark_completed_for_tests(token.value);
                queue->retire_completed();
                continue;
            }

            VkSemaphoreWaitInfo wait_info {};
            wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
            wait_info.semaphoreCount = 1;
            wait_info.pSemaphores = &timeline;
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

    [[nodiscard]] bool reached_frame_limit(uint32_t frame_count, uint32_t completed_frames) noexcept
    {
        return frame_count != 0 && completed_frames >= frame_count;
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

        state.mesh_frames.push_back(std::move(frame));
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

        MeshFrameSnapshot frame = std::move(state.mesh_frames.front());
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

    [[nodiscard]] Corona::Horizon::SubmitReceipt submit_mesh_render_present(Corona::Horizon::HardwareExecutor& executor,
                                                                            const Corona::Horizon::HardwareDisplayer& displayer,
                                                                            ExampleRenderResources& resources,
                                                                            const MeshFrameSnapshot& mesh)
    {
        if (mesh.vertices.empty() || mesh.indices.empty())
        {
            throw std::runtime_error("Horizon default mesh frame is empty.");
        }

        Corona::Horizon::HardwareBuffer vertex_buffer =
            Corona::Horizon::HardwareBuffer::vertex(mesh.vertices,
                                                    "example.default.mesh.vertex." + std::to_string(mesh.frame_index));
        Corona::Horizon::HardwareBuffer index_buffer =
            Corona::Horizon::HardwareBuffer::index(mesh.indices,
                                                   "example.default.mesh.index." + std::to_string(mesh.frame_index));

        Corona::Horizon::DrawIndexedParams draw;
        draw.index_count = static_cast<uint32_t>(mesh.indices.size());
        draw.index_type = Corona::Horizon::IndexType::UInt32;

        resources.pipeline.clear_records();
        resources.pipeline.record(index_buffer, vertex_buffer, draw);

        return executor.stream()
               << resources.pipeline.command_batch()
               << Corona::Horizon::present(displayer, resources.color)
               << Corona::Horizon::commit();
    }

    void stress_request_stop(StressState& state)
    {
        {
            std::lock_guard lock(state.mutex);
            state.stop_requested = true;
        }
        state.cv.notify_all();
    }

    void stress_publish_failure(StressState& state)
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

    [[nodiscard]] bool stress_all_render_finished_unlocked(const StressState& state) noexcept
    {
        return std::ranges::all_of(state.windows, [](const StressWindowState& window) {
            return window.render_finished;
        });
    }

    void stress_mark_mesh_finished(StressState& state, size_t window_index)
    {
        {
            std::lock_guard lock(state.mutex);
            if (window_index < state.windows.size())
            {
                state.windows[window_index].mesh_finished = true;
            }
        }
        state.cv.notify_all();
    }

    void stress_mark_render_finished(StressState& state, size_t window_index)
    {
        {
            std::lock_guard lock(state.mutex);
            if (window_index < state.windows.size())
            {
                state.windows[window_index].render_finished = true;
            }

            if (stress_all_render_finished_unlocked(state))
            {
                state.stop_requested = true;
            }
        }
        state.cv.notify_all();
    }

    [[nodiscard]] bool stress_should_stop(StressState& state)
    {
        std::lock_guard lock(state.mutex);
        return state.stop_requested;
    }

    [[nodiscard]] bool stress_push_mesh_frame(StressState& state, size_t window_index, MeshFrameSnapshot frame)
    {
        std::unique_lock lock(state.mutex);
        state.cv.wait(lock, [&] {
            return state.stop_requested ||
                   window_index >= state.windows.size() ||
                   !state.windows[window_index].open ||
                   state.windows[window_index].mesh_frames.size() < 2;
        });

        if (state.stop_requested || window_index >= state.windows.size() || !state.windows[window_index].open)
        {
            return false;
        }

        state.windows[window_index].mesh_frames.push_back(std::move(frame));
        lock.unlock();
        state.cv.notify_all();
        return true;
    }

    [[nodiscard]] std::optional<MeshFrameSnapshot> stress_pop_mesh_frame(StressState& state, size_t window_index)
    {
        std::unique_lock lock(state.mutex);
        state.cv.wait(lock, [&] {
            return state.stop_requested ||
                   window_index >= state.windows.size() ||
                   !state.windows[window_index].mesh_frames.empty() ||
                   state.windows[window_index].mesh_finished;
        });

        if (state.stop_requested || window_index >= state.windows.size() || state.windows[window_index].mesh_frames.empty())
        {
            return std::nullopt;
        }

        MeshFrameSnapshot frame = std::move(state.windows[window_index].mesh_frames.front());
        state.windows[window_index].mesh_frames.pop_front();
        lock.unlock();
        state.cv.notify_all();
        return frame;
    }

    [[nodiscard]] std::vector<std::pair<size_t, Corona::Horizon::HardwareDisplayer>>
    stress_wait_for_displayers(StressState& state, uint32_t render_thread_index, uint32_t render_thread_count)
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

        if (!state.display_ready)
        {
            throw std::runtime_error("Stress display thread stopped before creating GLFW displayers.");
        }

        std::vector<std::pair<size_t, Corona::Horizon::HardwareDisplayer>> result;
        for (size_t index = 0; index < state.windows.size(); ++index)
        {
            if (index % render_thread_count != render_thread_index)
            {
                continue;
            }

            if (!state.windows[index].displayer)
            {
                throw std::runtime_error("Stress display thread did not publish a displayer for every window.");
            }

            result.emplace_back(index, *state.windows[index].displayer);
        }
        return result;
    }

    void stress_mesh_thread_main(StressState& state, size_t window_index, uint32_t frame_count) noexcept
    {
        try
        {
            for (uint32_t frame = 0; !reached_frame_limit(frame_count, frame); ++frame)
            {
                MeshFrameSnapshot mesh = make_mesh_frame(frame + static_cast<uint32_t>(window_index * 97u));
                if (!stress_push_mesh_frame(state, window_index, std::move(mesh)))
                {
                    break;
                }
            }
        }
        catch (...)
        {
            stress_publish_failure(state);
        }

        stress_mark_mesh_finished(state, window_index);
    }

    void stress_display_thread_main(StressState& state, uint32_t window_count) noexcept
    {
        try
        {
#if !defined(_WIN32) && !defined(_WIN64)
            (void)window_count;
            throw std::runtime_error("Horizon stress example currently supports Win32 GLFW windows only.");
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

            std::vector<GLFWwindow*> windows;
            windows.reserve(window_count);

            struct WindowGuard
            {
                std::vector<GLFWwindow*>& windows;
                ~WindowGuard()
                {
                    for (GLFWwindow* window : windows)
                    {
                        if (window != nullptr)
                        {
                            glfwDestroyWindow(window);
                        }
                    }
                }
            } window_guard { windows };

            const uint32_t columns = std::max(1u, static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(window_count)))));
            for (uint32_t index = 0; index < window_count; ++index)
            {
                GLFWwindow* window = glfwCreateWindow(example_width,
                                                      example_height,
                                                      ("Horizon stress " + std::to_string(index)).c_str(),
                                                      nullptr,
                                                      nullptr);
                if (window == nullptr)
                {
                    throw std::runtime_error("glfwCreateWindow failed during Horizon stress example.");
                }

                glfwSetWindowPos(window,
                                 32 + static_cast<int>((index % columns) * 90u),
                                 32 + static_cast<int>((index / columns) * 70u));
                windows.push_back(window);
            }

            {
                std::lock_guard lock(state.mutex);
                for (uint32_t index = 0; index < window_count; ++index)
                {
                    state.windows[index].displayer = Corona::Horizon::HardwareDisplayer(glfwGetWin32Window(windows[index]));
                    state.windows[index].open = true;
                }
                state.display_ready = true;
            }
            state.cv.notify_all();

            for (;;)
            {
                glfwPollEvents();
                bool any_closed = false;
                for (uint32_t index = 0; index < window_count; ++index)
                {
                    if (windows[index] != nullptr && glfwWindowShouldClose(windows[index]))
                    {
                        any_closed = true;
                        std::lock_guard lock(state.mutex);
                        state.windows[index].open = false;
                        state.stop_requested = true;
                    }
                }

                if (any_closed)
                {
                    state.cv.notify_all();
                }

                {
                    std::lock_guard lock(state.mutex);
                    if (stress_all_render_finished_unlocked(state))
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
            stress_publish_failure(state);
        }

        stress_request_stop(state);
    }

    void stress_render_thread_main(StressState& state,
                                   uint32_t render_thread_index,
                                   uint32_t render_thread_count) noexcept
    {
        std::vector<size_t> owned_windows;
        for (size_t index = render_thread_index; index < state.windows.size(); index += render_thread_count)
        {
            owned_windows.push_back(index);
        }

        try
        {
            std::vector<std::pair<size_t, Corona::Horizon::HardwareDisplayer>> displayers =
                stress_wait_for_displayers(state, render_thread_index, render_thread_count);

            std::vector<StressRenderSlot> slots;
            slots.reserve(displayers.size());
            for (const auto& [window_index, displayer] : displayers)
            {
                slots.push_back(StressRenderSlot {
                    window_index,
                    displayer,
                    create_cube_resources(example_width,
                                          example_height,
                                          "example.stress.window." + std::to_string(window_index)),
                    false,
                });
            }

            Corona::Horizon::HardwareExecutor executor;
            size_t unfinished = slots.size();
            while (unfinished != 0 && !stress_should_stop(state))
            {
                for (StressRenderSlot& slot : slots)
                {
                    if (slot.finished)
                    {
                        continue;
                    }

                    std::optional<MeshFrameSnapshot> frame = stress_pop_mesh_frame(state, slot.window_index);
                    if (!frame)
                    {
                        slot.finished = true;
                        --unfinished;
                        stress_mark_render_finished(state, slot.window_index);
                        continue;
                    }

                    Corona::Horizon::SubmitReceipt receipt =
                        submit_mesh_render_present(executor, slot.displayer, slot.resources, *frame);
                    wait_for_receipt(receipt);
                }
            }
        }
        catch (...)
        {
            stress_publish_failure(state);
        }

        for (size_t window_index : owned_windows)
        {
            stress_mark_render_finished(state, window_index);
        }
    }

    void mesh_thread_main(ThreeThreadState& state, uint32_t frame_count) noexcept
    {
        try
        {
            for (uint32_t frame = 0; !reached_frame_limit(frame_count, frame); ++frame)
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
            ExampleRenderResources resources = create_cube_resources(example_width, example_height);
            Corona::Horizon::HardwareExecutor executor;

            uint32_t rendered_frames = 0;
            while (!reached_frame_limit(frame_count, rendered_frames))
            {
                if (should_stop(state))
                {
                    break;
                }

                std::optional<MeshFrameSnapshot> frame = pop_mesh_frame(state);
                if (!frame)
                {
                    break;
                }

                Corona::Horizon::SubmitReceipt receipt =
                    submit_mesh_render_present(executor, displayer, resources, *frame);
                wait_for_receipt(receipt);
                ++rendered_frames;
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

    void stress_rethrow_recorded_failure(StressState& state)
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

        for (uint32_t frame = 0; !reached_frame_limit(frame_count, frame) && !glfwWindowShouldClose(window); ++frame)
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
            create_cube_resources(example_width, example_height);
        run_single_window_loop(frame_count,
                               "Horizon default",
                               [&resources](Corona::Horizon::HardwareExecutor& executor,
                                            const Corona::Horizon::HardwareDisplayer& displayer,
                                            uint32_t frame) {
                                   MeshFrameSnapshot mesh = make_mesh_frame(frame);
                                   return submit_mesh_render_present(executor, displayer, resources, mesh);
                               });
    }

    void run_example_glsl(uint32_t frame_count)
    {
        ExampleRenderResources resources =
            create_fullscreen_resources(example_width,
                                        example_height,
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
                                        fullscreen_triangle_desc({ 0.92f, 0.18f, 0.20f, 1.0f }),
                                        "example.multi_window.left");
        ExampleRenderResources right =
            create_fullscreen_resources(example_width,
                                        example_height,
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

    void run_example_stress(uint32_t frame_count, ExampleDefaultStressConfig config)
    {
        constexpr uint32_t max_stress_windows = 16;
        constexpr uint32_t max_stress_render_threads = 16;

        if (config.window_count == 0 || config.render_thread_count == 0)
        {
            throw std::invalid_argument("Horizon stress example requires positive window and render thread counts.");
        }
        if (config.window_count > max_stress_windows)
        {
            throw std::invalid_argument("Horizon stress example supports at most " + std::to_string(max_stress_windows) + " windows.");
        }
        if (config.render_thread_count > max_stress_render_threads)
        {
            throw std::invalid_argument("Horizon stress example supports at most " + std::to_string(max_stress_render_threads) + " render threads.");
        }

        const uint32_t window_count = config.window_count;
        const uint32_t render_thread_count = std::min(config.render_thread_count, window_count);

        StressState state;
        state.windows.resize(window_count);

        std::thread display_thread;
        std::vector<std::thread> mesh_threads;
        std::vector<std::thread> render_threads;

        try
        {
            display_thread = std::thread(stress_display_thread_main, std::ref(state), window_count);

            mesh_threads.reserve(window_count);
            for (uint32_t window_index = 0; window_index < window_count; ++window_index)
            {
                mesh_threads.emplace_back(stress_mesh_thread_main,
                                          std::ref(state),
                                          static_cast<size_t>(window_index),
                                          frame_count);
            }

            render_threads.reserve(render_thread_count);
            for (uint32_t thread_index = 0; thread_index < render_thread_count; ++thread_index)
            {
                render_threads.emplace_back(stress_render_thread_main,
                                            std::ref(state),
                                            thread_index,
                                            render_thread_count);
            }
        }
        catch (...)
        {
            stress_request_stop(state);
            join_if_needed(display_thread);
            for (std::thread& thread : mesh_threads)
                join_if_needed(thread);
            for (std::thread& thread : render_threads)
                join_if_needed(thread);
            throw;
        }

        for (std::thread& thread : mesh_threads)
            join_if_needed(thread);
        for (std::thread& thread : render_threads)
            join_if_needed(thread);
        join_if_needed(display_thread);
        stress_rethrow_recorded_failure(state);
    }
}

void run_example_default(uint32_t frame_count,
                         ExampleDefaultThreadMode thread_mode,
                         ExampleDefaultMode mode,
                         ExampleDefaultStressConfig stress_config)
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
        case ExampleDefaultMode::Stress:
            run_example_stress(frame_count, stress_config);
            break;
        default:
            throw std::invalid_argument("Unknown Horizon example mode.");
    }
}
