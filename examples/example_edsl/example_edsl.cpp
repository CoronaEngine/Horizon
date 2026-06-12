#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Codegen/BuiltinVariate.h"
#include "Codegen/ControlFlows.h"
#include "Codegen/CustomLibrary.h"
#include "Codegen/TypeAlias.h"
#include "common.h"
#include "hardware_wrapper_vulkan/hardware_context.h"
#include "horizon.h"
#include GLSL(shaders/edsl_header.glsl)

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace H = Corona::Horizon;

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

using ShaderResources = EmbeddedShader::ShaderCodeModule::ShaderResources;

H::BindingSlot make_binding_slot(const ShaderResources::ShaderBindInfo& info) noexcept
{
    H::BindingSlot slot;
    slot.byte_offset = info.byteOffset;
    slot.type_size = info.typeSize;
    slot.bind_type = static_cast<int32_t>(info.bindType);
    slot.location = info.location;
    slot.set = info.set;
    slot.binding = info.binding;
    return slot;
}

template <size_t Count>
std::array<H::BindingSlot, Count> reflected_uniform_member_slots(const ShaderResources& resources, uint32_t type_size)
{
    std::vector<ShaderResources::ShaderBindInfo> members;
    for (const auto& info : resources.bindInfoPool)
    {
        if (info.bindType == ShaderResources::uniformBufferMembers &&
            (info.typeSize == 0 || info.typeSize == type_size))
        {
            members.push_back(info);
        }
    }

    std::ranges::sort(members, [](const auto& lhs, const auto& rhs) {
        if (lhs.set != rhs.set)
            return lhs.set < rhs.set;
        if (lhs.binding != rhs.binding)
            return lhs.binding < rhs.binding;
        return lhs.byteOffset < rhs.byteOffset;
    });

    if (members.size() < Count)
    {
        throw std::runtime_error("example_edsl expected " + std::to_string(Count) +
                                 " reflected UBO members, got " + std::to_string(members.size()) + ".");
    }

    std::array<H::BindingSlot, Count> slots {};
    for (size_t i = 0; i < Count; ++i)
        slots[i] = make_binding_slot(members[i]);

    return slots;
}

void check_assets()
{
    if (std::filesystem::exists(viking_room_model_path) && std::filesystem::exists(viking_room_texture_path))
        return;

    std::string message = "EDSL baseline assets are missing.";
    if (!std::filesystem::exists(viking_room_model_path))
        message += "\n  Missing model: " + viking_room_model_path.string();
    if (!std::filesystem::exists(viking_room_texture_path))
        message += "\n  Missing texture: " + viking_room_texture_path.string();
    throw std::runtime_error(message);
}

// EDSL 路径按行主序喂入 shader，故对 glm（列主序）结果转置。
baseline::UniformBufferObject make_ubo(float time_seconds)
{
    baseline::UniformBufferObject ubo = baseline::make_ubo(time_seconds, edsl_width / static_cast<float>(edsl_height));
    ubo.model = glm::transpose(ubo.model);
    ubo.view = glm::transpose(ubo.view);
    ubo.proj = glm::transpose(ubo.proj);
    return ubo;
}

H::HardwareImage create_output_image()
{
    H::HardwareImage image(
        H::HardwareImageDesc::texture_2d(edsl_width,
                                         edsl_height,
                                         H::Format::RGBA16_FLOAT,
                                         H::ImageUsageFlags::Storage | H::ImageUsageFlags::ColorAttachment |
                                             H::ImageUsageFlags::Sampled | H::ImageUsageFlags::TransferSrc |
                                             H::ImageUsageFlags::TransferDst,
                                         "example_edsl.output"));
    image.set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);
    return image;
}

H::HardwareImage create_depth_image()
{
    H::HardwareImage image(
        H::HardwareImageDesc::depth_attachment(edsl_width, edsl_height, H::Format::D32, "example_edsl.depth"));
    image.set_clear_depth(1.0f, 0);
    return image;
}
} // namespace

void run_example_edsl()
{
    check_assets();

    if (glfwInit() < 0)
        return;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(edsl_width, edsl_height, "Horizon Baseline [EDSL]", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        return;
    }

    try
    {
        baseline::Mesh mesh = baseline::load_mesh(viking_room_model_path);
        TextureLoadResult texture_result = loadTexture(viking_room_texture_path.string());
        if (!texture_result.success)
            throw std::runtime_error("failed to load EDSL baseline texture: " + viking_room_texture_path.string());

        H::HardwareImage texture_image = texture_result.texture;
        H::HardwareImage final_output_image = create_output_image();
        H::HardwareImage depth_image = create_depth_image();
        H::HardwareBuffer vertex_buffer = H::HardwareBuffer::vertex(mesh.vertices, "example_edsl.vertex");
        H::HardwareBuffer index_buffer = H::HardwareBuffer::index(mesh.indices, "example_edsl.index");
        H::HardwareExecutor render_executor;
        H::HardwareExecutor display_executor;
        H::HardwareDisplayer display(glfwGetWin32Window(window));

        using namespace EmbeddedShader;
        using namespace ktm;

        Texture2D<fvec4> texture_proxy = texture_image;
        Texture2D<fvec4> final_output_proxy = final_output_image;
        Float4x4 model;
        Float4x4 view;
        Float4x4 proj;

        auto vertex_shader = [&](Aggregate<BaselineEdslVertexProxy> vertex) -> Float4 {
            position() = mul(proj, mul(view, mul(model, Float4(vertex->pos, 1.0f))));
            Float color_weight = edsl_header_glsl::get_color_weight(vertex->color);
            return Float4(vertex->tex_coord, color_weight, 1.0f);
        };

        auto fragment_shader = [&](Float4 input) {
            Float4 color = texture(texture_proxy, input->xy());
            final_output_proxy << color * Float4(input->z, input->z, input->z, 1.0f);
        };

        H::RasterizerPipelineDesc rasterizer_desc =
            H::RasterizerPipelineDesc::from_edsl(vertex_shader, fragment_shader);
        rasterizer_desc.set_depth_attachment(H::DepthAttachmentDesc::with_format(H::Format::D32, "example_edsl.depth"));
        rasterizer_desc.set_debug_name("example_edsl.baseline_rasterizer");

        const auto uniform_bindings =
            reflected_uniform_member_slots<3>(rasterizer_desc.vertex_shader.module.shaderResources,
                                              static_cast<uint32_t>(sizeof(glm::mat4)));
        const H::BindingSlot& model_binding = uniform_bindings[0];
        const H::BindingSlot& view_binding = uniform_bindings[1];
        const H::BindingSlot& proj_binding = uniform_bindings[2];

        H::RasterizerPipeline rasterizer(std::move(rasterizer_desc));
        rasterizer.bind_depth_target(depth_image);

        H::DrawIndexedParams draw_params;
        draw_params.index_type = H::IndexType::UInt32;
        draw_params.index_count = static_cast<uint32_t>(mesh.indices.size());

        const auto start_time = std::chrono::high_resolution_clock::now();
        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();

            const float time_seconds =
                std::chrono::duration<float, std::chrono::seconds::period>(
                    std::chrono::high_resolution_clock::now() - start_time)
                    .count();
            baseline::UniformBufferObject ubo = make_ubo(time_seconds);
            rasterizer[model_binding] = ubo.model;
            rasterizer[view_binding] = ubo.view;
            rasterizer[proj_binding] = ubo.proj;

            rasterizer.clear_records();
            rasterizer.record(index_buffer, vertex_buffer, draw_params);

            H::SubmitReceipt render_receipt = render_executor
                << rasterizer(edsl_width, edsl_height)
                << H::submit;

            display_executor.wait(render_receipt);
            (void)(display_executor.stream()
                << H::present(display, final_output_image)
                << H::commit());
        }
    }
    catch (...)
    {
        glfwDestroyWindow(window);
        glfwTerminate();
        throw;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
