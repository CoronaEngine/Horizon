// 移植自参考示例 39-assao（自适应屏幕空间环境光遮蔽）的聚焦简化移植：
// 原版是 4 slice 半分辨率去交错 + 多质量级 + importance map 的完整 ASSAO，
// 这里取其核心链路做全分辨率简化实现（参数取原版默认：radius 1.2、
// multiplier 1.0、power 1.5、horizonAngleThreshold 0.06、fadeOut 50→200、
// sharpness 0.98、blur ×2）：
//   光栅 3 pass：场景颜色 / view 法线 / R32F 器件深度（框架单颜色附件限制）
//   compute 3 站：generate（12-tap 螺旋盘）→ smart blur ×2（ping-pong）→ apply
// 场景与原版对齐（原版侧同步使用相同的 minstd_rand(12345) 摆放和 3 网格池）：
// cube x10 地面（顶面 y=0）+ 120 个随机模型（orb/column/hollowcube，tree 和
// bunny_decimated 是 meshopt 压缩 chunk 加载器不支持已从两边池子排除）、
// 相机 (0,1.5,0) 俯视前方 fovy 60°。

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Codegen/ControlFlows.h"
#include "common.h"
#include "horizon.h"
#include "imgui_horizon.h"

#include <imgui.h>

#include GLSL(shaders/assao_scene_vert.glsl)
#include GLSL(shaders/assao_color_frag.glsl)
#include GLSL(shaders/assao_normal_frag.glsl)
#include GLSL(shaders/assao_depth_frag.glsl)
#include GLSL(shaders/assao_generate_compute.glsl)
#include GLSL(shaders/assao_blur_compute.glsl)
#include GLSL(shaders/assao_apply_compute.glsl)

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

namespace
{
constexpr uint32_t ao_width = 1280;
constexpr uint32_t ao_height = 720;

const std::filesystem::path ao_asset_root = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets";

struct AoVertex
{
    std::array<float, 3> position {};
    std::array<float, 3> normal {};
};

struct LoadedMesh
{
    std::vector<AoVertex> vertices;
    std::vector<uint32_t> indices;
};

uint32_t fourcc(char a, char b, char c, uint8_t d)
{
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) | (static_cast<uint32_t>(d) << 24);
}

template <typename T>
T read_pod(const std::vector<std::byte>& bytes, size_t& cursor)
{
    T value {};
    std::memcpy(&value, bytes.data() + cursor, sizeof(T));
    cursor += sizeof(T);
    return value;
}

// 与 example_ibl 相同的 .bin 网格解析（未压缩 VB/IB chunk）
LoadedMesh load_bin_mesh(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        throw std::runtime_error("Failed to open mesh file: " + path.string());

    const std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<size_t>(file_size));
    file.read(reinterpret_cast<char*>(bytes.data()), file_size);

    const uint32_t chunk_vb = fourcc('V', 'B', ' ', 0x1);
    const uint32_t chunk_ib = fourcc('I', 'B', ' ', 0x0);
    const uint32_t chunk_pri = fourcc('P', 'R', 'I', 0x0);

    constexpr uint16_t attrib_id_position = 0x0001;
    constexpr uint16_t attrib_id_normal = 0x0002;
    constexpr uint16_t attrib_type_id_uint8 = 0x0001;
    constexpr uint16_t attrib_type_id_float = 0x0004;

    LoadedMesh mesh;
    uint32_t group_base_vertex = 0;
    size_t cursor = 0;

    while (cursor + sizeof(uint32_t) <= bytes.size())
    {
        const uint32_t chunk = read_pod<uint32_t>(bytes, cursor);
        if (chunk == chunk_vb)
        {
            cursor += 16 + 24 + 64; // Sphere + Aabb + Obb

            const uint8_t num_attrs = read_pod<uint8_t>(bytes, cursor);
            const uint16_t stride = read_pod<uint16_t>(bytes, cursor);

            int32_t position_offset = -1;
            int32_t normal_offset = -1;
            uint16_t normal_type = 0;
            for (uint8_t i = 0; i < num_attrs; ++i)
            {
                const uint16_t attr_offset = read_pod<uint16_t>(bytes, cursor);
                const uint16_t attr_id = read_pod<uint16_t>(bytes, cursor);
                cursor += 1; // num
                const uint16_t type_id = read_pod<uint16_t>(bytes, cursor);
                cursor += 2; // normalized + asInt

                if (attr_id == attrib_id_position)
                    position_offset = attr_offset;
                if (attr_id == attrib_id_normal)
                {
                    normal_offset = attr_offset;
                    normal_type = type_id;
                }
            }

            if (position_offset < 0 || normal_offset < 0)
                throw std::runtime_error("Mesh misses position/normal attribute: " + path.string());

            const uint16_t num_vertices = read_pod<uint16_t>(bytes, cursor);
            group_base_vertex = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.reserve(mesh.vertices.size() + num_vertices);

            for (uint16_t v = 0; v < num_vertices; ++v)
            {
                const std::byte* vertex_data = bytes.data() + cursor + static_cast<size_t>(v) * stride;

                AoVertex vertex;
                std::memcpy(vertex.position.data(), vertex_data + position_offset, sizeof(float) * 3);

                if (normal_type == attrib_type_id_uint8)
                {
                    uint8_t packed[3];
                    std::memcpy(packed, vertex_data + normal_offset, 3);
                    for (int c = 0; c < 3; ++c)
                        vertex.normal[c] = static_cast<float>(packed[c]) / 255.0f * 2.0f - 1.0f;
                }
                else if (normal_type == attrib_type_id_float)
                {
                    std::memcpy(vertex.normal.data(), vertex_data + normal_offset, sizeof(float) * 3);
                }
                else
                {
                    throw std::runtime_error("Unsupported normal attribute type in mesh: " + path.string());
                }

                mesh.vertices.push_back(vertex);
            }
            cursor += static_cast<size_t>(num_vertices) * stride;
        }
        else if (chunk == chunk_ib)
        {
            const uint32_t num_indices = read_pod<uint32_t>(bytes, cursor);
            mesh.indices.reserve(mesh.indices.size() + num_indices);
            for (uint32_t i = 0; i < num_indices; ++i)
            {
                const uint16_t index = read_pod<uint16_t>(bytes, cursor);
                mesh.indices.push_back(group_base_vertex + index);
            }
        }
        else if (chunk == chunk_pri)
        {
            const uint16_t material_len = read_pod<uint16_t>(bytes, cursor);
            cursor += material_len;
            const uint16_t num_prims = read_pod<uint16_t>(bytes, cursor);
            for (uint16_t i = 0; i < num_prims; ++i)
            {
                const uint16_t name_len = read_pod<uint16_t>(bytes, cursor);
                cursor += name_len;
                cursor += 16 + 16 + 24 + 64; // 4×u32 + Sphere + Aabb + Obb
            }
        }
        else
        {
            throw std::runtime_error("Unsupported chunk in mesh (compressed?): " + path.string());
        }
    }

    return mesh;
}

struct GpuMesh
{
    Corona::Horizon::HardwareBuffer vb;
    Corona::Horizon::HardwareBuffer ib;
    uint32_t index_count = 0;
};

GpuMesh upload_mesh(const LoadedMesh& mesh, const std::string& name)
{
    return GpuMesh {
        Corona::Horizon::HardwareBuffer::vertex(mesh.vertices, name + ".vb"),
        Corona::Horizon::HardwareBuffer::index(mesh.indices, name + ".ib"),
        static_cast<uint32_t>(mesh.indices.size()),
    };
}

void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
    if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}

} // namespace

void run_example_assao()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(ao_width, ao_height, "Horizon ASSAO [Vulkan]", nullptr, nullptr);
    glfwSetKeyCallback(window, key_callback);

    GpuMesh ground = upload_mesh(load_bin_mesh(ao_asset_root / "meshes" / "cube.bin"), "example_assao.ground");
    GpuMesh orb = upload_mesh(load_bin_mesh(ao_asset_root / "meshes" / "orb.bin"), "example_assao.orb");
    GpuMesh column = upload_mesh(load_bin_mesh(ao_asset_root / "meshes" / "column.bin"), "example_assao.column");
    GpuMesh hollowcube = upload_mesh(load_bin_mesh(ao_asset_root / "meshes" / "hollowcube.bin"), "example_assao.hollowcube");

    // 随机模型摆放（固定种子，位置范围与原版一致 ±6.4）
    struct ModelInstance
    {
        const GpuMesh* mesh;
        float scale;
        glm::vec3 position;
    };
    const std::array<std::pair<const GpuMesh*, float>, 3> model_pool = { {
        { &orb, 0.5f },
        { &column, 0.05f },
        { &hollowcube, 0.25f },
    } };
    std::vector<ModelInstance> models;
    std::minstd_rand rng(12345);
    for (int i = 0; i < 120; ++i) // 原版 MODEL_COUNT
    {
        const auto& [mesh, scale] = model_pool[rng() % model_pool.size()];
        const float px = ((int(rng() % 256)) - 128.0f) / 20.0f;
        const float pz = ((int(rng() % 256)) - 128.0f) / 20.0f;
        models.push_back({ mesh, scale, glm::vec3(px, 0.0f, pz) });
    }

    // G-buffer 目标（compute 需 Storage）
    const auto rt_usage = Corona::Horizon::ImageUsageFlags::ColorAttachment |
                          Corona::Horizon::ImageUsageFlags::Sampled |
                          Corona::Horizon::ImageUsageFlags::Storage;
    Corona::Horizon::HardwareImage scene_color_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        ao_width, ao_height, Corona::Horizon::Format::RGBA16_FLOAT, rt_usage, "example_assao.color"));
    scene_color_image.set_clear_color(0.3f, 0.45f, 0.6f, 1.0f); // 天空底色
    Corona::Horizon::HardwareImage normal_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        ao_width, ao_height, Corona::Horizon::Format::RGBA8_UNORM, rt_usage, "example_assao.normal"));
    normal_image.set_clear_color(0.5f, 0.5f, 1.0f, 1.0f);
    Corona::Horizon::HardwareImage depth_val_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        ao_width, ao_height, Corona::Horizon::Format::R32_FLOAT, rt_usage, "example_assao.depthval"));
    depth_val_image.set_clear_color(1.0f, 0.0f, 0.0f, 0.0f);

    Corona::Horizon::HardwareImage ao_image_a(Corona::Horizon::HardwareImageDesc::texture_2d(
        ao_width, ao_height, Corona::Horizon::Format::R32_FLOAT,
        Corona::Horizon::ImageUsageFlags::Storage | Corona::Horizon::ImageUsageFlags::Sampled, "example_assao.ao_a"));
    Corona::Horizon::HardwareImage ao_image_b(Corona::Horizon::HardwareImageDesc::texture_2d(
        ao_width, ao_height, Corona::Horizon::Format::R32_FLOAT,
        Corona::Horizon::ImageUsageFlags::Storage | Corona::Horizon::ImageUsageFlags::Sampled, "example_assao.ao_b"));

    Corona::Horizon::HardwareImage final_output_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        ao_width, ao_height, Corona::Horizon::Format::RGBA16_FLOAT,
        Corona::Horizon::ImageUsageFlags::Storage | Corona::Horizon::ImageUsageFlags::ColorAttachment |
            Corona::Horizon::ImageUsageFlags::Sampled | Corona::Horizon::ImageUsageFlags::TransferSrc |
            Corona::Horizon::ImageUsageFlags::TransferDst,
        "example_assao.output"));

    // 三个几何 pass 各自的深度附件
    Corona::Horizon::HardwareImage depth_c(Corona::Horizon::HardwareImageDesc::depth_attachment(
        ao_width, ao_height, Corona::Horizon::Format::D32, "example_assao.depth_c"));
    depth_c.set_clear_depth(1.0f, 0);
    Corona::Horizon::HardwareImage depth_n(Corona::Horizon::HardwareImageDesc::depth_attachment(
        ao_width, ao_height, Corona::Horizon::Format::D32, "example_assao.depth_n"));
    depth_n.set_clear_depth(1.0f, 0);
    Corona::Horizon::HardwareImage depth_d(Corona::Horizon::HardwareImageDesc::depth_attachment(
        ao_width, ao_height, Corona::Horizon::Format::D32, "example_assao.depth_d"));
    depth_d.set_clear_depth(1.0f, 0);

    Corona::Horizon::RasterizerPipelineDesc scene_desc;
    scene_desc.blend.attachments = { Corona::Horizon::BlendStateDesc::opaque_attachment() };

    Corona::Horizon::RasterizerPipeline color_rasterizer(assao_scene_vert_glsl, assao_color_frag_glsl, scene_desc);
    color_rasterizer.outColor = scene_color_image;
    color_rasterizer.bind_depth_target(depth_c);

    Corona::Horizon::RasterizerPipelineDesc normal_desc = scene_desc;
    Corona::Horizon::RasterizerPipeline normal_rasterizer(assao_scene_vert_glsl, assao_normal_frag_glsl, normal_desc);
    normal_rasterizer.outNormal = normal_image;
    normal_rasterizer.bind_depth_target(depth_n);

    Corona::Horizon::RasterizerPipelineDesc depth_desc = scene_desc;
    Corona::Horizon::RasterizerPipeline depthval_rasterizer(assao_scene_vert_glsl, assao_depth_frag_glsl, depth_desc);
    depthval_rasterizer.outDepthVal = depth_val_image;
    depthval_rasterizer.bind_depth_target(depth_d);

    // compute 管线：generate → blur ×2（ping-pong 用两个实例）→ apply
    Corona::Horizon::ComputePipeline generate_compute(assao_generate_compute_glsl, ktm::uvec3(8, 8, 1));
    Corona::Horizon::ComputePipeline blur_compute_ab(assao_blur_compute_glsl, ktm::uvec3(8, 8, 1));
    Corona::Horizon::ComputePipeline blur_compute_ba(assao_blur_compute_glsl, ktm::uvec3(8, 8, 1));
    Corona::Horizon::ComputePipeline apply_compute(assao_apply_compute_glsl, ktm::uvec3(8, 8, 1));

    Corona::Horizon::HardwareExecutor render_executor;
    Corona::Horizon::HardwareExecutor display_executor;
    Corona::Horizon::HardwareDisplayer display(glfwGetWin32Window(window));

    constexpr float aspect = static_cast<float>(ao_width) / static_cast<float>(ao_height);
    // 相机 (0,1.5,0) 前视稍向下（垂直角 -0.3 rad）、fovy 60°
    const glm::vec3 eye(0.0f, 1.5f, 0.0f);
    const glm::vec3 forward(0.0f, std::sin(-0.3f), std::cos(-0.3f));
    const glm::mat4 view = glm::lookAtLH(eye, eye + forward, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = [] {
        glm::mat4 m = glm::perspectiveLH(glm::radians(60.0f), aspect, 0.1f, 100.0f);
        m[1][1] *= -1.0f; // Vulkan 裁剪空间 Y 翻转
        return m;
    }();
    const glm::mat4 view_proj = proj * view;

    // 深度重建常量：viewZ = P32 / (device_z - P22)
    const float p22 = proj[2][2];
    const float p32 = proj[3][2];
    const float p00 = proj[0][0];
    const float p11 = proj[1][1];

    const uint32_t dispatch_x = (ao_width + 7) / 8;
    const uint32_t dispatch_y = (ao_height + 7) / 8;

    HorizonImGuiLayer ui(window, ao_width, ao_height);

    const auto start_time = std::chrono::high_resolution_clock::now();
    auto prev_time = start_time;
    double fps_accum_seconds = 0.0;
    int fps_frame_count = 0;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        ui.new_frame();
        ImGui::Begin("Hello");
        ImGui::Text("hello world!");
        ImGui::End();

        const auto now = std::chrono::high_resolution_clock::now();
        const float dt = std::chrono::duration<float>(now - prev_time).count();
        const float time = std::chrono::duration<float>(now - start_time).count();
        prev_time = now;
        (void)time;

        fps_accum_seconds += dt;
        ++fps_frame_count;
        if (fps_accum_seconds >= 0.5)
        {
            const double fps = fps_frame_count / fps_accum_seconds;
            char title[160];
            std::snprintf(title, sizeof(title), "Horizon ASSAO [Vulkan] - %.1f FPS (%.2f ms)", fps, 1000.0 / fps);
            glfwSetWindowTitle(window, title);
            fps_accum_seconds = 0.0;
            fps_frame_count = 0;
        }

        // 三个几何 pass
        auto record_scene = [&](auto& pipeline) {
            pipeline.clear_records();
            // 共享矩阵（UBO，batch 内不变）；per-draw 数据（model、color）走 push constant
            pipeline.vsp.proj_view   = view_proj;
            pipeline.vsp.view_matrix = view;

            // 地面：压扁的 cube
            {
                Corona::Horizon::DrawIndexedParams params;
                params.index_type = Corona::Horizon::IndexType::UInt32;
                params.index_count = ground.index_count;

                const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -10.0f, 0.0f)) *
                                        glm::scale(glm::mat4(1.0f), glm::vec3(10.0f)); // 原版：cube x10，顶面 y=0
                pipeline.vpc.model = model;
                pipeline.vpc.color = glm::vec4(0.6f, 0.6f, 0.6f, 1.0f);
                pipeline.record(ground.ib, ground.vb, params);
            }

            for (const ModelInstance& m : models)
            {
                Corona::Horizon::DrawIndexedParams params;
                params.index_type = Corona::Horizon::IndexType::UInt32;
                params.index_count = m.mesh->index_count;

                const glm::mat4 model = glm::translate(glm::mat4(1.0f), m.position) *
                                        glm::scale(glm::mat4(1.0f), glm::vec3(m.scale));
                pipeline.vpc.model = model;
                pipeline.vpc.color = glm::vec4(192.0f / 255.0f, 192.0f / 255.0f, 192.0f / 255.0f, 1.0f); // 原版 0xc0 灰
                pipeline.record(m.mesh->ib, m.mesh->vb, params);
            }
        };
        record_scene(color_rasterizer);
        record_scene(normal_rasterizer);
        record_scene(depthval_rasterizer);

        // compute 参数
        const glm::vec4 resolution(float(ao_width), float(ao_height), 0.0f, 0.0f);

        generate_compute.pushConsts.depthID = depth_val_image.store_descriptor();
        generate_compute.pushConsts.normalID = normal_image.store_descriptor();
        generate_compute.pushConsts.aoID = ao_image_a.store_descriptor();
        generate_compute.pushConsts.depth_unpack = glm::vec4(p32, -p22, 0.0f, 0.0f);
        generate_compute.pushConsts.ndc_to_view = glm::vec4(2.0f / p00, 2.0f / p11, -1.0f / p00, -1.0f / p11);
        generate_compute.pushConsts.params0 = glm::vec4(1.2f, 1.0f, 1.5f, 0.06f);
        generate_compute.pushConsts.params1 = glm::vec4(50.0f, 200.0f, float(ao_width), float(ao_height));

        blur_compute_ab.pushConsts.srcID = ao_image_a.store_descriptor();
        blur_compute_ab.pushConsts.dstID = ao_image_b.store_descriptor();
        blur_compute_ab.pushConsts.depthID = depth_val_image.store_descriptor();
        blur_compute_ab.pushConsts.params0 = glm::vec4(0.98f, float(ao_width), float(ao_height), 0.0f);

        blur_compute_ba.pushConsts.srcID = ao_image_b.store_descriptor();
        blur_compute_ba.pushConsts.dstID = ao_image_a.store_descriptor();
        blur_compute_ba.pushConsts.depthID = depth_val_image.store_descriptor();
        blur_compute_ba.pushConsts.params0 = glm::vec4(0.98f, float(ao_width), float(ao_height), 0.0f);

        apply_compute.pushConsts.colorID = scene_color_image.store_descriptor();
        apply_compute.pushConsts.aoID = ao_image_a.store_descriptor();
        apply_compute.pushConsts.outputID = final_output_image.store_descriptor();
        apply_compute.pushConsts.params0 = glm::vec4(float(ao_width), float(ao_height), 0.0f, 0.0f);

        Corona::Horizon::SubmitReceipt render_receipt =
            render_executor << color_rasterizer(ao_width, ao_height)
                            << normal_rasterizer(ao_width, ao_height)
                            << depthval_rasterizer(ao_width, ao_height)
                            << generate_compute(dispatch_x, dispatch_y, 1)
                            << blur_compute_ab(dispatch_x, dispatch_y, 1)
                            << blur_compute_ba(dispatch_x, dispatch_y, 1)
                            << apply_compute(dispatch_x, dispatch_y, 1)
                            << Corona::Horizon::submit;

        ui.draw_overlay(display_executor, final_output_image, render_receipt);
        display_executor.wait(render_receipt);
        (void)(display_executor.stream() << Corona::Horizon::present(display, final_output_image)
                                         << Corona::Horizon::commit());
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
