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
#include <utility>
#include <vector>

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

template <size_t Count>
std::array<Corona::Horizon::BindingSlot, Count> reflected_uniform_member_slots(const ShaderResources& resources, uint32_t type_size)
{
    std::vector<ShaderResources::ShaderBindInfo> members;
    for (const auto& info : resources.bindInfoPool)
    {
        if (info.bindType == ShaderResources::uniformBufferMembers && (info.typeSize == 0 || info.typeSize == type_size))
            members.push_back(info);
    }

    std::ranges::sort(members, [](const auto& lhs, const auto& rhs) {
        if (lhs.set != rhs.set)
            return lhs.set < rhs.set;
        if (lhs.binding != rhs.binding)
            return lhs.binding < rhs.binding;
        return lhs.byteOffset < rhs.byteOffset;
    });

    std::array<Corona::Horizon::BindingSlot, Count> slots {};
    for (size_t i = 0; i < Count; ++i)
    {
        slots[i].byte_offset = members[i].byteOffset;
        slots[i].type_size = members[i].typeSize;
        slots[i].bind_type = static_cast<int32_t>(members[i].bindType);
        slots[i].location = members[i].location;
        slots[i].set = members[i].set;
        slots[i].binding = members[i].binding;
    }
    return slots;
}
} // namespace

void run_example_edsl()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(edsl_width, edsl_height, "Horizon Baseline [EDSL]", nullptr, nullptr);

    baseline::Mesh mesh = baseline::load_mesh(viking_room_model_path);
    Corona::Horizon::HardwareImage texture_image = loadTexture(viking_room_texture_path.string()).texture;

    Corona::Horizon::HardwareImage final_output_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        edsl_width, edsl_height, Corona::Horizon::Format::RGBA16_FLOAT,
        Corona::Horizon::ImageUsageFlags::Storage | Corona::Horizon::ImageUsageFlags::ColorAttachment |
            Corona::Horizon::ImageUsageFlags::Sampled | Corona::Horizon::ImageUsageFlags::TransferSrc |
            Corona::Horizon::ImageUsageFlags::TransferDst,
        "example_edsl.output"));
    final_output_image.set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);

    Corona::Horizon::HardwareImage depth_image(
        Corona::Horizon::HardwareImageDesc::depth_attachment(edsl_width, edsl_height, Corona::Horizon::Format::D32, "example_edsl.depth"));
    depth_image.set_clear_depth(1.0f, 0);

    Corona::Horizon::HardwareBuffer vertex_buffer = Corona::Horizon::HardwareBuffer::vertex(mesh.vertices, "example_edsl.vertex");
    Corona::Horizon::HardwareBuffer index_buffer = Corona::Horizon::HardwareBuffer::index(mesh.indices, "example_edsl.index");
    Corona::Horizon::HardwareExecutor render_executor;
    Corona::Horizon::HardwareExecutor display_executor;
    Corona::Horizon::HardwareDisplayer display(glfwGetWin32Window(window));

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

    Corona::Horizon::RasterizerPipelineDesc rasterizer_desc =
        Corona::Horizon::RasterizerPipelineDesc::from_edsl(vertex_shader, fragment_shader);
    rasterizer_desc.depth_attachment = Corona::Horizon::DepthAttachmentDesc::with_format(Corona::Horizon::Format::D32, "example_edsl.depth");

    const auto uniform_bindings =
        reflected_uniform_member_slots<3>(rasterizer_desc.vertex_shader.module.shaderResources, static_cast<uint32_t>(sizeof(glm::mat4)));

    Corona::Horizon::RasterizerPipeline rasterizer(std::move(rasterizer_desc));
    rasterizer.bind_depth_target(depth_image);

    Corona::Horizon::DrawIndexedParams draw_params;
    draw_params.index_type = Corona::Horizon::IndexType::UInt32;
    draw_params.index_count = static_cast<uint32_t>(mesh.indices.size());

    const auto start_time = std::chrono::high_resolution_clock::now();
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        const float time_seconds =
            std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - start_time).count();
        // EDSL 按行主序喂入 shader，故对 glm（列主序）结果转置。
        baseline::UniformBufferObject ubo = baseline::make_ubo(time_seconds, edsl_width / static_cast<float>(edsl_height));
        rasterizer[uniform_bindings[0]] = glm::transpose(ubo.model);
        rasterizer[uniform_bindings[1]] = glm::transpose(ubo.view);
        rasterizer[uniform_bindings[2]] = glm::transpose(ubo.proj);

        rasterizer.clear_records();
        rasterizer.record(index_buffer, vertex_buffer, draw_params);

        Corona::Horizon::SubmitReceipt render_receipt = render_executor << rasterizer(edsl_width, edsl_height) << Corona::Horizon::submit;

        display_executor.wait(render_receipt);
        (void)(display_executor.stream() << Corona::Horizon::present(display, final_output_image) << Corona::Horizon::commit());
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
