#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#define HORIZON_NATIVE_WINDOW(window) glfwGetWin32Window(window)
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#define HORIZON_NATIVE_WINDOW(window) glfwGetCocoaView(window)
#endif
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Codegen/BuiltinVariate.h"
#include "Codegen/ControlFlows.h"
#include "Codegen/CustomLibrary.h"
#include "Codegen/TypeAlias.h"
#include "common.h"
#include "scene_pass.h"
#include "legacy_common.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <utility>

struct BaselineEdslVertexProxy
{
    EmbeddedShader::Float3 pos;
    EmbeddedShader::Float3 color;
    EmbeddedShader::Float2 tex_coord;
};

namespace
{
constexpr uint32_t edsl_width = 800;
constexpr uint32_t edsl_height = 600;

const std::filesystem::path viking_room_model_path =
    std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "models" / "viking_room.obj";
const std::filesystem::path viking_room_texture_path =
    std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "textures" / "viking_room.png";

ktm::fmat4x4 to_edsl_matrix(const glm::mat4& matrix)
{
    static_assert(sizeof(ktm::fmat4x4) == sizeof(glm::mat4));

    ktm::fmat4x4 result;
    std::memcpy(&result, &matrix, sizeof(result));
    return result;
}
} // namespace

void run_example_edsl()
{
    legacy_example::Window owner(edsl_width, edsl_height, "Horizon Baseline [EDSL]");
    auto* window = owner.get();

    baseline::Mesh mesh = baseline::load_mesh(viking_room_model_path);
    HardwareImage texture_image = loadTexture(viking_room_texture_path.string()).texture;

    HardwareImage final_output_image(example::imageInfo(edsl_width, edsl_height, ImageFormat::RGBA16_FLOAT, ImageUsage::SampledStorageImage));
    final_output_image.setClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    HardwareImage depth_image(
        example::imageInfo(edsl_width, edsl_height, ImageFormat::D32_FLOAT, ImageUsage::DepthImage));

    auto vertex_buffer = HardwareBuffer::uploadDeviceLocal(mesh.vertices, BufferUsage::VertexBuffer);
    auto index_buffer = HardwareBuffer::uploadDeviceLocal(mesh.indices, BufferUsage::IndexBuffer);


    HardwareExecutor render_executor;
    HardwareDisplayer display(HORIZON_NATIVE_WINDOW(window), PresentDevicePreference::RenderDevice);

    using namespace EmbeddedShader;
    using namespace ktm;

    Texture2D<fvec4> texture_proxy = texture_image;
    Texture2D<fvec4> final_output_proxy = final_output_image;
    Float4x4 model;
    Float4x4 view;
    Float4x4 proj;

    auto vertex_shader = [&](Aggregate<BaselineEdslVertexProxy> vertex) -> Float4 {
        position() = mul(proj, mul(view, mul(model, Float4(vertex->pos, 1.0f))));
        Float color_weight = (vertex->color->x + vertex->color->y + vertex->color->z) / Float(3.0f);
        return Float4(vertex->tex_coord, color_weight, 1.0f);
    };

    auto fragment_shader = [&](Float4 input) {
        Float4 color = texture(texture_proxy, input->xy());
        final_output_proxy << color * Float4(input->z, input->z, input->z, 1.0f);
    };


    example::ScenePass<example::NoDrawParameters> rasterizer(edsl_width, edsl_height, {{true, true, RasterCompare::LessEqual, RasterBlend::Alpha}, {false, false}}, vertex_shader, fragment_shader, 1, example::shaderOptions());
    rasterizer.setDepthImage(depth_image);


    // VP 不随帧变化（相机固定），在循环外赋值一次
    view = to_edsl_matrix(
        glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f),
                    glm::vec3(0.0f, 0.0f, 0.0f),
                    glm::vec3(0.0f, 0.0f, 1.0f)));
    {
        glm::mat4 proj_mat = glm::perspective(glm::pi<float>() * 0.25f,
                                              edsl_width / static_cast<float>(edsl_height),
                                              0.1f, 10.0f);
        proj_mat[1][1] *= -1.0f; // Vulkan 裁剪空间 Y 翻转
        proj = to_edsl_matrix(proj_mat);
    }

    const auto start_time = std::chrono::high_resolution_clock::now();
    auto prev_time = start_time;
    double fps_accum_seconds = 0.0;
    int fps_frame_count = 0;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        const float time_seconds =
            std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - start_time).count();

        const auto now = std::chrono::high_resolution_clock::now();
        const float dt = std::chrono::duration<float>(now - prev_time).count();
        prev_time = now;

        fps_accum_seconds += dt;
        ++fps_frame_count;
        if (fps_accum_seconds >= 0.5)
        {
            const double fps = fps_frame_count / fps_accum_seconds;
            char title[160];
            std::snprintf(title, sizeof(title), "Horizon Baseline [EDSL] %.1f FPS (%.2f ms)", fps, 1000.0 / fps);
            glfwSetWindowTitle(window, title);
            fps_accum_seconds = 0.0;
            fps_frame_count = 0;
        }

        // Model 每帧更新（逐 draw 参数）；VP 已在循环外绑定
        model = to_edsl_matrix(
            glm::rotate(glm::mat4(1.0f),
                        time_seconds * glm::pi<float>() * 0.5f,
                        glm::vec3(0.0f, 0.0f, 1.0f)));

        rasterizer.draw({vertex_buffer, index_buffer});

        render_executor << rasterizer;
        render_executor.commit();

        display.wait(render_executor) << final_output_image;
    }
    render_executor.waitForDeferredResources();
}
