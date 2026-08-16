// Port of bgfx example 37-gpudrivenrendering: Hi-Z occlusion culling, stream compaction,
// and MultiDrawIndirect rendering on Horizon.

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Codegen/ControlFlows.h"
#include "common.h"
#include "horizon.h"
#include "imgui_horizon.h"

#include <imgui.h>

#include GLSL(shaders/gdr_occlusion_vert.glsl)
#include GLSL(shaders/gdr_occlusion_frag.glsl)
#include GLSL(shaders/gdr_downscale_hi_z.glsl)
#include GLSL(shaders/gdr_occlude_props.glsl)
#include GLSL(shaders/gdr_stream_compaction.glsl)
#include GLSL(shaders/gdr_main_vert.glsl)
#include GLSL(shaders/gdr_main_frag.glsl)

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <random>
#include <span>
#include <vector>

namespace
{
constexpr uint32_t gdr_width = 1280;
constexpr uint32_t gdr_height = 720;
constexpr uint32_t s_max_noof_props = 10;
constexpr uint32_t k_num_props = 4;
constexpr uint32_t k_total_instances = 526; // 1 + 25 + 200 + 300
constexpr uint32_t k_instances_pow2 = 1024;
constexpr uint32_t k_wall_start_instance = 1;
constexpr float k_float_smallest = 1.0e-37f;

// Port of bgfx 37-gpudrivenrendering Camera / Mouse (orbit + dolly).
glm::vec3 from_lat_long(float u, float v)
{
    const float phi = u * glm::two_pi<float>();
    const float theta = v * glm::pi<float>();
    const float st = std::sin(theta);
    const float sp = std::sin(phi);
    const float ct = std::cos(theta);
    const float cp = std::cos(phi);
    return { -st * sp, ct, -st * cp };
}

void to_lat_long(float* out_u, float* out_v, const glm::vec3& dir)
{
    const float phi = std::atan2(dir.x, dir.z);
    const float theta = std::acos(dir.y);
    *out_u = (glm::pi<float>() + phi) / glm::two_pi<float>();
    *out_v = theta * (1.0f / glm::pi<float>());
}

struct OrbitCamera
{
    struct Interp3f
    {
        glm::vec3 curr { 0.0f };
        glm::vec3 dest { 0.0f };
    };

    OrbitCamera() { reset(); }

    void reset()
    {
        target.curr = target.dest = { 0.0f, 0.0f, 0.0f };
        pos.curr = pos.dest = { 55.0f, 20.0f, 65.0f };
        orbit_delta[0] = 0.0f;
        orbit_delta[1] = 0.0f;
    }

    void orbit(float dx, float dy)
    {
        orbit_delta[0] += dx;
        orbit_delta[1] += dy;
    }

    void dolly(float dz)
    {
        constexpr float cnear = 1.0f;
        constexpr float cfar = 100.0f;

        const glm::vec3 to_target = target.dest - pos.dest;
        const float to_target_len = glm::length(to_target);
        const float inv_to_target_len = 1.0f / (to_target_len + k_float_smallest);
        const glm::vec3 to_target_norm = to_target * inv_to_target_len;

        const float delta = to_target_len * dz;
        const float new_len = to_target_len + delta;
        if ((cnear < new_len || dz < 0.0f) && (new_len < cfar || dz > 0.0f))
            pos.dest = pos.dest + to_target_norm * delta;
    }

    void consume_orbit(float amount)
    {
        const float consume0 = orbit_delta[0] * amount;
        const float consume1 = orbit_delta[1] * amount;
        orbit_delta[0] -= consume0;
        orbit_delta[1] -= consume1;

        const glm::vec3 to_pos = pos.curr - target.curr;
        const float to_pos_len = glm::length(to_pos);
        const float inv_to_pos_len = 1.0f / (to_pos_len + k_float_smallest);
        const glm::vec3 to_pos_norm = to_pos * inv_to_pos_len;

        float ll[2];
        to_lat_long(&ll[0], &ll[1], to_pos_norm);
        ll[0] += consume0;
        ll[1] -= consume1;
        ll[1] = std::clamp(ll[1], 0.02f, 0.98f);

        const glm::vec3 tmp = from_lat_long(ll[0], ll[1]);
        const glm::vec3 diff = (tmp - to_pos_norm) * to_pos_len;
        pos.curr += diff;
        pos.dest += diff;
    }

    void update(float dt)
    {
        const float amount = std::min(dt / 0.12f, 1.0f);
        consume_orbit(amount);
        target.curr = glm::mix(target.curr, target.dest, amount);
        pos.curr = glm::mix(pos.curr, pos.dest, amount);
    }

    [[nodiscard]] glm::mat4 view_matrix() const
    {
        return glm::lookAtLH(pos.curr, target.curr, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    Interp3f target;
    Interp3f pos;
    float orbit_delta[2] {};
};

struct MouseState
{
    float dx = 0.0f;
    float dy = 0.0f;
    float prev_mx = 0.0f;
    float prev_my = 0.0f;
    int32_t scroll = 0;
    int32_t scroll_prev = 0;

    void update(float mx, float my, int32_t mz, uint32_t width, uint32_t height)
    {
        const float width_f = static_cast<float>(static_cast<int32_t>(width));
        const float height_f = static_cast<float>(static_cast<int32_t>(height));
        dx = (mx - prev_mx) / width_f;
        dy = (my - prev_my) / height_f;
        prev_mx = mx;
        prev_my = my;
        scroll = mz - scroll_prev;
        scroll_prev = mz;
    }
};

struct CameraInput
{
    OrbitCamera camera;
    MouseState mouse;
    int32_t scroll_z = 0;
    GLFWscrollfun imgui_scroll = nullptr;
};

struct PosVertex
{
    std::array<float, 3> position;
};

const std::vector<PosVertex> cube_vertices = {
    { { -0.5f, 0.5f, 0.5f } },
    { { 0.5f, 0.5f, 0.5f } },
    { { -0.5f, -0.5f, 0.5f } },
    { { 0.5f, -0.5f, 0.5f } },
    { { -0.5f, 0.5f, -0.5f } },
    { { 0.5f, 0.5f, -0.5f } },
    { { -0.5f, -0.5f, -0.5f } },
    { { 0.5f, -0.5f, -0.5f } },
};

const std::vector<uint32_t> cube_indices = {
    0, 1, 2, 1, 3, 2,
    4, 6, 5, 5, 6, 7,
    0, 2, 4, 4, 2, 6,
    1, 5, 3, 5, 7, 3,
    0, 4, 1, 4, 5, 1,
    2, 3, 6, 6, 3, 7,
};

struct PropDesc
{
    uint32_t instance_count = 0;
    uint32_t first_instance = 0;
    uint32_t index_count = 0;
    uint32_t first_index = 0;
    int32_t base_vertex = 0;
    bool main_pass = false;
    bool occlusion_pass = false;
    glm::vec4 material_color { 1.0f, 1.0f, 1.0f, 1.0f };
};

uint32_t floor_pow2(uint32_t value)
{
    uint32_t result = 1;
    while (result * 2u <= value)
        result *= 2u;
    return result;
}

uint32_t next_pow2(uint32_t value)
{
    if (value <= 1u)
        return 1u;
    return 1u << (32u - std::countl_zero(value - 1u));
}

uint32_t mip_count(uint32_t width, uint32_t height)
{
    const uint32_t largest = std::max(width, height);
    return 1u + static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(largest))));
}

glm::mat4 make_srt(const glm::vec3& scale, const glm::vec3& rotation, const glm::vec3& translation)
{
    // Match bx::mtxSRT (column-major, ZYX Euler, scale ΓåÆ rotate ΓåÆ translate).
    const float sx = std::sin(rotation.x);
    const float cx = std::cos(rotation.x);
    const float sy = std::sin(rotation.y);
    const float cy = std::cos(rotation.y);
    const float sz = std::sin(rotation.z);
    const float cz = std::cos(rotation.z);

    const float sxsz = sx * sz;
    const float cycz = cy * cz;

    glm::mat4 result(1.0f);
    result[0][0] = scale.x * (cycz - sxsz * sy);
    result[0][1] = scale.x * (-cx * sz);
    result[0][2] = scale.x * (cz * sy + cy * sxsz);
    result[0][3] = 0.0f;

    result[1][0] = scale.y * (cz * sx * sy + cy * sz);
    result[1][1] = scale.y * (cx * cz);
    result[1][2] = scale.y * (sy * sz - cycz * sx);
    result[1][3] = 0.0f;

    result[2][0] = scale.z * (-cx * sy);
    result[2][1] = scale.z * sx;
    result[2][2] = scale.z * (cx * cy);
    result[2][3] = 0.0f;

    result[3][0] = translation.x;
    result[3][1] = translation.y;
    result[3][2] = translation.z;
    result[3][3] = 1.0f;
    return result;
}

glm::vec3 transform_point(const glm::mat4& matrix, const glm::vec3& point)
{
    const glm::vec4 transformed = matrix * glm::vec4(point, 1.0f);
    return glm::vec3(transformed);
}

void pack_instance_row(std::array<glm::vec4, 4>& rows, const glm::mat4& world, float drawcall_id)
{
    rows[0] = glm::vec4(world[0].x, world[0].y, world[0].z, drawcall_id);
    rows[1] = world[1];
    rows[2] = world[2];
    rows[3] = world[3];
}

Corona::Horizon::HardwareImage make_hi_z_image(uint32_t width, uint32_t height, const std::string& name)
{
    return Corona::Horizon::HardwareImage(Corona::Horizon::HardwareImageDesc::texture_2d(
        width,
        height,
        Corona::Horizon::Format::R32_FLOAT,
        Corona::Horizon::ImageUsageFlags::Storage | Corona::Horizon::ImageUsageFlags::ColorAttachment |
            Corona::Horizon::ImageUsageFlags::Sampled | Corona::Horizon::ImageUsageFlags::TransferSrc |
            Corona::Horizon::ImageUsageFlags::TransferDst,
        name));
}

struct SceneData
{
    std::vector<PropDesc> props;
    std::vector<glm::vec4> instance_data;
    std::vector<glm::vec4> bbox_data;
    std::vector<uint32_t> indirect_const;
    std::vector<PosVertex> merged_vertices;
    std::vector<uint32_t> merged_indices;
};

SceneData build_scene(std::mt19937& rng)
{
    SceneData scene;
    scene.props.reserve(k_num_props);
    scene.indirect_const.resize(k_num_props * 3u);

    std::uniform_real_distribution<float> rand01(0.0f, 1.0f);

    auto add_instances = [&](const PropDesc& prop_template,
                             const std::function<glm::mat4(uint32_t local_index)>& make_world) {
        PropDesc prop = prop_template;
        prop.first_instance = static_cast<uint32_t>(scene.instance_data.size() / 4u);
        prop.instance_count = 0;

        const uint32_t drawcall_id = static_cast<uint32_t>(scene.props.size());
        for (uint32_t local = 0; local < prop_template.instance_count; ++local)
        {
            const glm::mat4 world = make_world(local);
            std::array<glm::vec4, 4> rows {};
            pack_instance_row(rows, world, static_cast<float>(drawcall_id));
            scene.instance_data.insert(scene.instance_data.end(), rows.begin(), rows.end());

            glm::vec3 bbox_min(std::numeric_limits<float>::max());
            glm::vec3 bbox_max(std::numeric_limits<float>::lowest());
            for (int corner = 0; corner < 8; ++corner)
            {
                const glm::vec3 local_corner(
                    (corner & 1) ? 0.5f : -0.5f,
                    (corner & 2) ? 0.5f : -0.5f,
                    (corner & 4) ? 0.5f : -0.5f);
                const glm::vec3 world_corner = transform_point(world, local_corner);
                bbox_min = glm::min(bbox_min, world_corner);
                bbox_max = glm::max(bbox_max, world_corner);
            }
            scene.bbox_data.push_back(glm::vec4(bbox_min, static_cast<float>(drawcall_id)));
            scene.bbox_data.push_back(glm::vec4(bbox_max, 0.0f));
            ++prop.instance_count;
        }

        scene.props.push_back(prop);
    };

    // Ground (1 instance).
    add_instances(
        PropDesc {
            .instance_count = 1,
            .main_pass = true,
            .material_color = glm::vec4(0.0f, 0.6f, 0.0f, 1.0f),
        },
        [](uint32_t) {
            return make_srt(glm::vec3(100.0f, 0.1f, 100.0f), glm::vec3(0.0f), glm::vec3(0.0f));
        });

    // Walls (25 instances, occlusion + main, blue dithered).
    add_instances(
        PropDesc {
            .instance_count = 25,
            .main_pass = true,
            .occlusion_pass = true,
            .material_color = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f),
        },
        [&](uint32_t) {
            const float rot_y = (rand01(rng) * 120.0f - 60.0f) * glm::pi<float>() / 180.0f;
            return make_srt(glm::vec3(40.0f, 10.0f, 0.1f),
                            glm::vec3(0.0f, rot_y, 0.0f),
                            glm::vec3(rand01(rng) * 100.0f - 50.0f, 5.0f, rand01(rng) * 100.0f - 50.0f));
        });

    // Short cubes (200 yellow).
    add_instances(
        PropDesc {
            .instance_count = 200,
            .main_pass = true,
            .material_color = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f),
        },
        [&](uint32_t) {
            return make_srt(glm::vec3(2.0f),
                            glm::vec3(0.0f),
                            glm::vec3(rand01(rng) * 100.0f - 50.0f, 1.0f, rand01(rng) * 100.0f - 50.0f));
        });

    // Tall cubes (300 red).
    add_instances(
        PropDesc {
            .instance_count = 300,
            .main_pass = true,
            .material_color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),
        },
        [&](uint32_t) {
            return make_srt(glm::vec3(2.0f, 4.0f, 2.0f),
                            glm::vec3(0.0f),
                            glm::vec3(rand01(rng) * 100.0f - 50.0f, 2.0f, rand01(rng) * 100.0f - 50.0f));
        });

    // Match bgfx: master VB/IB concatenates each prop's local mesh. Indirect args use
    // firstIndex + vertexOffset; do NOT bake vertexOffset into the indices.
    scene.merged_vertices.clear();
    scene.merged_indices.clear();
    uint32_t vertex_offset = 0;
    uint32_t index_offset = 0;

    for (uint32_t prop_index = 0; prop_index < k_num_props; ++prop_index)
    {
        PropDesc& prop = scene.props[prop_index];
        prop.base_vertex = static_cast<int32_t>(vertex_offset);
        prop.first_index = index_offset;
        prop.index_count = static_cast<uint32_t>(cube_indices.size());

        scene.merged_vertices.insert(scene.merged_vertices.end(), cube_vertices.begin(), cube_vertices.end());
        scene.merged_indices.insert(scene.merged_indices.end(), cube_indices.begin(), cube_indices.end());

        scene.indirect_const[prop_index * 3u + 0u] = prop.index_count;
        scene.indirect_const[prop_index * 3u + 1u] = prop.first_index;
        scene.indirect_const[prop_index * 3u + 2u] = static_cast<uint32_t>(prop.base_vertex);

        vertex_offset += static_cast<uint32_t>(cube_vertices.size());
        index_offset += static_cast<uint32_t>(cube_indices.size());
    }

    return scene;
}

template <typename OccludePipeline>
void fill_hi_z_mip_ids(OccludePipeline& pipeline, const std::vector<Corona::Horizon::HardwareImage>& hi_z_mips)
{
    std::array<glm::uvec4, 4> packed {};
    for (uint32_t mip = 0; mip < hi_z_mips.size() && mip < 16u; ++mip)
        packed[mip / 4u][mip % 4u] = hi_z_mips[mip].store_descriptor();

    pipeline.u.hiZMipIDs0 = packed[0];
    pipeline.u.hiZMipIDs1 = packed[1];
    pipeline.u.hiZMipIDs2 = packed[2];
    pipeline.u.hiZMipIDs3 = packed[3];
}

} // namespace

void run_example_gpudrivenrendering()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window =
        glfwCreateWindow(gdr_width, gdr_height, "Horizon GPUDrivenRendering [Vulkan]", nullptr, nullptr);

    std::mt19937 rng(12345u);
    const SceneData scene = build_scene(rng);

    const uint32_t hi_z_width = floor_pow2(gdr_width);
    const uint32_t hi_z_height = floor_pow2(gdr_height);
    const uint32_t num_hi_z_mips = mip_count(hi_z_width, hi_z_height);
    // Match bgfx: pow(2, floor(log2(count))+1) ΓÇö scan size for stream compaction.
    const uint32_t instances_pow2 =
        1u << (static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(k_total_instances)))) + 1u);

    Corona::Horizon::HardwareBuffer cube_vb =
        Corona::Horizon::HardwareBuffer::vertex(cube_vertices, "example_gpudrivenrendering.cube_vb");
    Corona::Horizon::HardwareBuffer cube_ib =
        Corona::Horizon::HardwareBuffer::index(cube_indices, "example_gpudrivenrendering.cube_ib");
    Corona::Horizon::HardwareBuffer merged_vb =
        Corona::Horizon::HardwareBuffer::vertex(scene.merged_vertices, "example_gpudrivenrendering.merged_vb");
    Corona::Horizon::HardwareBuffer merged_ib =
        Corona::Horizon::HardwareBuffer::index(scene.merged_indices, "example_gpudrivenrendering.merged_ib");

    Corona::Horizon::HardwareBuffer instance_in_buffer(
        Corona::Horizon::HardwareBufferDesc::typed<glm::vec4>(
            scene.instance_data.size(),
            Corona::Horizon::BufferUsageFlags::Storage | Corona::Horizon::BufferUsageFlags::TransferDst,
            "example_gpudrivenrendering.instances_in"),
        std::as_bytes(std::span(scene.instance_data)));

    Corona::Horizon::HardwareBuffer instance_out_buffer(
        Corona::Horizon::HardwareBufferDesc::typed<glm::vec4>(
            scene.instance_data.size(),
            Corona::Horizon::BufferUsageFlags::Storage | Corona::Horizon::BufferUsageFlags::TransferDst,
            "example_gpudrivenrendering.instances_out"),
        {});

    Corona::Horizon::HardwareBuffer bbox_buffer(
        Corona::Horizon::HardwareBufferDesc::typed<glm::vec4>(
            scene.bbox_data.size(),
            Corona::Horizon::BufferUsageFlags::Storage | Corona::Horizon::BufferUsageFlags::TransferDst,
            "example_gpudrivenrendering.bboxes"),
        std::as_bytes(std::span(scene.bbox_data)));

    std::vector<uint32_t> zero_counts(s_max_noof_props, 0u);
    Corona::Horizon::HardwareBuffer count_buffer(
        Corona::Horizon::HardwareBufferDesc::typed<uint32_t>(
            s_max_noof_props,
            Corona::Horizon::BufferUsageFlags::Storage | Corona::Horizon::BufferUsageFlags::TransferDst,
            "example_gpudrivenrendering.counts"),
        std::as_bytes(std::span(zero_counts)));

    // Stream compaction scans a power-of-two range (and bgfx uses s_maxNoofInstances=2048).
    constexpr uint32_t k_max_instances = 2048;
    std::vector<uint32_t> zero_predicates(k_max_instances, 0u);
    Corona::Horizon::HardwareBuffer predicate_buffer(
        Corona::Horizon::HardwareBufferDesc::typed<uint32_t>(
            k_max_instances,
            Corona::Horizon::BufferUsageFlags::Storage | Corona::Horizon::BufferUsageFlags::TransferDst,
            "example_gpudrivenrendering.predicates"),
        std::as_bytes(std::span(zero_predicates)));

    Corona::Horizon::HardwareBuffer indirect_const_buffer(
        Corona::Horizon::HardwareBufferDesc::typed<uint32_t>(
            scene.indirect_const.size(),
            Corona::Horizon::BufferUsageFlags::Storage | Corona::Horizon::BufferUsageFlags::TransferDst,
            "example_gpudrivenrendering.indirect_const"),
        std::as_bytes(std::span(scene.indirect_const)));

    std::vector<Corona::Horizon::DrawIndexedIndirectCommand> initial_indirect(k_num_props);
    for (uint32_t i = 0; i < k_num_props; ++i)
    {
        initial_indirect[i].index_count = scene.props[i].index_count;
        initial_indirect[i].instance_count = scene.props[i].instance_count;
        initial_indirect[i].first_index = scene.props[i].first_index;
        initial_indirect[i].vertex_offset = scene.props[i].base_vertex;
        initial_indirect[i].first_instance = scene.props[i].first_instance;
    }
    Corona::Horizon::HardwareBuffer indirect_buffer =
        Corona::Horizon::HardwareBuffer::indirect(initial_indirect, "example_gpudrivenrendering.indirect");
    // Untouched by stream compaction ΓÇö used when Hi-Z is disabled but indirect draw stays on.
    Corona::Horizon::HardwareBuffer indirect_full_buffer =
        Corona::Horizon::HardwareBuffer::indirect(initial_indirect, "example_gpudrivenrendering.indirect_full");

    std::vector<Corona::Horizon::HardwareImage> hi_z_mips;
    hi_z_mips.reserve(num_hi_z_mips);
    uint32_t mip_w = hi_z_width;
    uint32_t mip_h = hi_z_height;
    for (uint32_t mip = 0; mip < num_hi_z_mips; ++mip)
    {
        hi_z_mips.push_back(make_hi_z_image(mip_w, mip_h, "example_gpudrivenrendering.hiz.mip" + std::to_string(mip)));
        hi_z_mips.back().set_clear_color(1.0f, 1.0f, 1.0f, 1.0f);
        mip_w = std::max(1u, mip_w / 2u);
        mip_h = std::max(1u, mip_h / 2u);
    }

    Corona::Horizon::HardwareImage hi_z_depth(Corona::Horizon::HardwareImageDesc::depth_attachment(
        hi_z_width, hi_z_height, Corona::Horizon::Format::D32, "example_gpudrivenrendering.hiz.depth"));
    hi_z_depth.set_clear_depth(1.0f, 0);

    Corona::Horizon::HardwareImage final_output_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        gdr_width,
        gdr_height,
        Corona::Horizon::Format::RGBA16_FLOAT,
        Corona::Horizon::ImageUsageFlags::Storage | Corona::Horizon::ImageUsageFlags::ColorAttachment |
            Corona::Horizon::ImageUsageFlags::Sampled | Corona::Horizon::ImageUsageFlags::TransferSrc |
            Corona::Horizon::ImageUsageFlags::TransferDst,
        "example_gpudrivenrendering.output"));
    final_output_image.set_clear_color(0.19f, 0.19f, 0.19f, 1.0f);

    Corona::Horizon::HardwareImage main_depth(Corona::Horizon::HardwareImageDesc::depth_attachment(
        gdr_width, gdr_height, Corona::Horizon::Format::D32, "example_gpudrivenrendering.depth"));
    main_depth.set_clear_depth(1.0f, 0);

    Corona::Horizon::RasterizerPipelineDesc opaque_desc;
    opaque_desc.blend_enabled = false;

    Corona::Horizon::RasterizerPipelineDesc main_desc = opaque_desc;
    main_desc.cull_mode = Corona::Horizon::CullMode::Front;

    Corona::Horizon::RasterizerPipeline occlusion_rasterizer(
        gdr_occlusion_vert_glsl, gdr_occlusion_frag_glsl, opaque_desc);
    occlusion_rasterizer.outDepth = hi_z_mips[0];
    occlusion_rasterizer.bind_depth_target(hi_z_depth);

    Corona::Horizon::RasterizerPipeline main_rasterizer(gdr_main_vert_glsl, gdr_main_frag_glsl, main_desc);
    main_rasterizer.outColor = final_output_image;
    main_rasterizer.bind_depth_target(main_depth);

    main_rasterizer.materials.color0 = scene.props[0].material_color;
    main_rasterizer.materials.color1 = scene.props[1].material_color;
    main_rasterizer.materials.color2 = scene.props[2].material_color;
    main_rasterizer.materials.color3 = scene.props[3].material_color;

    Corona::Horizon::ComputePipeline downscale(gdr_downscale_hi_z_glsl, ktm::uvec3(16, 16, 1));
    Corona::Horizon::ComputePipeline occlude(gdr_occlude_props_glsl, ktm::uvec3(64, 1, 1));
    Corona::Horizon::ComputePipeline compact(gdr_stream_compaction_glsl, ktm::uvec3(1024, 1, 1));

    Corona::Horizon::HardwareExecutor render_executor;
    Corona::Horizon::HardwareExecutor display_executor;
    Corona::Horizon::HardwareDisplayer display(glfwGetWin32Window(window));

    HorizonImGuiLayer ui(window, gdr_width, gdr_height);

    CameraInput camera_input;
    glfwSetWindowUserPointer(window, &camera_input);
    // Chain onto ImGui's scroll callback so both UI and camera receive wheel events.
    camera_input.imgui_scroll = glfwSetScrollCallback(
        window,
        [](GLFWwindow* win, double dx, double dy)
        {
            auto* input = static_cast<CameraInput*>(glfwGetWindowUserPointer(win));
            if (input == nullptr)
                return;
            input->scroll_z += static_cast<int32_t>(dy);
            if (input->imgui_scroll != nullptr)
                input->imgui_scroll(win, dx, dy);
        });

    const float main_aspect = static_cast<float>(gdr_width) / static_cast<float>(gdr_height);
    const float hi_z_aspect = static_cast<float>(hi_z_width) / static_cast<float>(hi_z_height);
    glm::mat4 main_proj = glm::perspectiveLH(glm::radians(60.0f), main_aspect, 0.1f, 500.0f);
    main_proj[1][1] *= -1.0f;
    glm::mat4 hi_z_proj = glm::perspectiveLH(glm::radians(60.0f), hi_z_aspect, 0.1f, 500.0f);
    hi_z_proj[1][1] *= -1.0f;

    occlusion_rasterizer.vp.proj = hi_z_proj;
    main_rasterizer.vp.proj = main_proj;

    occlude.u.inputRTSize =
        glm::vec4(float(hi_z_width), float(hi_z_height), 1.0f / float(hi_z_width), 1.0f / float(hi_z_height));
    occlude.u.cullingConfig =
        glm::vec4(float(k_total_instances),
                  float(instances_pow2),
                  float(num_hi_z_mips > 0 ? num_hi_z_mips - 1u : 0u),
                  float(k_num_props));
    fill_hi_z_mip_ids(occlude, hi_z_mips);

    compact.u.cullingConfig =
        glm::vec4(float(k_total_instances), float(instances_pow2), 0.0f, float(k_num_props));

    Corona::Horizon::DrawIndexedIndirectParams indirect_params;
    indirect_params.draw_count = k_num_props;
    indirect_params.index_type = Corona::Horizon::IndexType::UInt32;

    bool use_indirect = true;
    bool hiz_enable = true; // default: current full Hi-Z ΓåÆ occlude ΓåÆ compact path
    bool first_frame = true;

    auto prev_time = std::chrono::high_resolution_clock::now();
    double fps_accum_seconds = 0.0;
    int fps_frame_count = 0;
    Corona::Horizon::SubmitReceipt render_receipt;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        ui.new_frame();

        ImGui::Begin("GPU Driven Rendering");
        ImGui::Checkbox("Use Indirect", &use_indirect);
        ImGui::Checkbox("Hi-Z Occlusion", &hiz_enable);
        ImGui::Text("Instances: ground %u, walls %u, short %u, tall %u (total %u)",
                    scene.props[0].instance_count,
                    scene.props[1].instance_count,
                    scene.props[2].instance_count,
                    scene.props[3].instance_count,
                    k_total_instances);
        ImGui::Text("Hi-Z: %ux%u, mips %u, instancesPow2 %u%s",
                    hi_z_width,
                    hi_z_height,
                    num_hi_z_mips,
                    instances_pow2,
                    hiz_enable ? "" : " (disabled)");
        ImGui::TextUnformatted("LMB orbit  RMB/scroll dolly");
        ImGui::End();

        const auto now = std::chrono::high_resolution_clock::now();
        const float dt = std::chrono::duration<float>(now - prev_time).count();
        prev_time = now;
        fps_accum_seconds += dt;
        ++fps_frame_count;
        if (fps_accum_seconds >= 0.5)
        {
            const double fps = fps_frame_count / fps_accum_seconds;
            char title[160];
            std::snprintf(title,
                          sizeof(title),
                          "Horizon GPUDrivenRendering [Vulkan] - %u instances - %.1f FPS",
                          k_total_instances,
                          fps);
            glfwSetWindowTitle(window, title);
            fps_accum_seconds = 0.0;
            fps_frame_count = 0;
        }

        {
            double mx = 0.0;
            double my = 0.0;
            glfwGetCursorPos(window, &mx, &my);
            camera_input.mouse.update(static_cast<float>(mx),
                                      static_cast<float>(my),
                                      camera_input.scroll_z,
                                      gdr_width,
                                      gdr_height);

            if (!ImGui::GetIO().WantCaptureMouse)
            {
                if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
                    camera_input.camera.orbit(camera_input.mouse.dx, camera_input.mouse.dy);
                else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
                    camera_input.camera.dolly(camera_input.mouse.dx + camera_input.mouse.dy);
                else if (camera_input.mouse.scroll != 0)
                    camera_input.camera.dolly(static_cast<float>(camera_input.mouse.scroll) * 0.05f);
            }

            camera_input.camera.update(dt);
        }

        const glm::mat4 view = camera_input.camera.view_matrix();
        occlusion_rasterizer.vp.view = view;
        main_rasterizer.vp.view = view;
        occlude.u.viewProj = hi_z_proj * view;

        const uint32_t instance_in_id = instance_in_buffer.store_descriptor();
        const uint32_t instance_out_id = instance_out_buffer.store_descriptor();
        const uint32_t bbox_id = bbox_buffer.store_descriptor();
        const uint32_t count_id = count_buffer.store_descriptor();
        const uint32_t predicate_id = predicate_buffer.store_descriptor();
        const uint32_t indirect_const_id = indirect_const_buffer.store_descriptor();
        const uint32_t indirect_id = indirect_buffer.store_descriptor();

        occlusion_rasterizer.pc.instanceBufferID = instance_in_id;
        occlusion_rasterizer.clear_records();
        if (hiz_enable)
        {
            Corona::Horizon::DrawIndexedParams wall_params;
            wall_params.index_type = Corona::Horizon::IndexType::UInt32;
            wall_params.index_count = static_cast<uint32_t>(cube_indices.size());
            wall_params.instance_count = scene.props[1].instance_count;
            wall_params.first_instance = k_wall_start_instance;
            occlusion_rasterizer.record(cube_ib, cube_vb, wall_params);
        }

        main_rasterizer.clear_records();
        const bool indirect_ready = use_indirect && (!hiz_enable || !first_frame);
        if (indirect_ready)
        {
            if (hiz_enable)
            {
                // Culled instances + compacted indirect args.
                main_rasterizer.pc.instanceBufferID = instance_out_id;
                main_rasterizer.record_indirect(merged_ib, merged_vb, indirect_buffer, indirect_params);
            }
            else
            {
                // Hi-Z off: still DrawIndirect, but use the full unculled instance/indirect buffers.
                main_rasterizer.pc.instanceBufferID = instance_in_id;
                main_rasterizer.record_indirect(merged_ib, merged_vb, indirect_full_buffer, indirect_params);
            }
        }
        else
        {
            main_rasterizer.pc.instanceBufferID = instance_in_id;
            for (const PropDesc& prop : scene.props)
            {
                if (!prop.main_pass)
                    continue;

                Corona::Horizon::DrawIndexedParams params;
                params.index_type = Corona::Horizon::IndexType::UInt32;
                params.index_count = prop.index_count;
                params.first_index = prop.first_index;
                params.vertex_offset = prop.base_vertex;
                params.instance_count = prop.instance_count;
                params.first_instance = prop.first_instance;
                main_rasterizer.record(merged_ib, merged_vb, params);
            }
        }

        occlude.pc.bboxBufferID = bbox_id;
        occlude.pc.countBufferID = count_id;
        occlude.pc.predicateBufferID = predicate_id;

        compact.pc.constBufferID = indirect_const_id;
        compact.pc.instanceInID = instance_in_id;
        compact.pc.predicateID = predicate_id;
        compact.pc.countBufferID = count_id;
        compact.pc.indirectBufferID = indirect_id;
        compact.pc.instanceOutID = instance_out_id;

        const uint32_t occlude_groups_x = (k_total_instances + 63u) / 64u;

        Corona::Horizon::HardwareStream stream = render_executor.stream();
        if (hiz_enable)
        {
            stream << occlusion_rasterizer(hi_z_width, hi_z_height);

            uint32_t src_w = hi_z_width;
            uint32_t src_h = hi_z_height;
            uint32_t dst_w = std::max(1u, src_w / 2u);
            uint32_t dst_h = std::max(1u, src_h / 2u);
            for (uint32_t mip = 1; mip < num_hi_z_mips; ++mip)
            {
                downscale.pc.srcID = hi_z_mips[mip - 1u].store_descriptor();
                downscale.pc.dstID = hi_z_mips[mip].store_descriptor();
                downscale.pc.srcWidth = src_w;
                downscale.pc.srcHeight = src_h;
                const Corona::Horizon::ComputePipelineBase::DispatchGroups groups =
                    downscale.dispatch_groups(dst_w, dst_h);
                stream << downscale(groups.x, groups.y, 1);

                src_w = dst_w;
                src_h = dst_h;
                dst_w = std::max(1u, src_w / 2u);
                dst_h = std::max(1u, src_h / 2u);
            }

            stream << occlude(occlude_groups_x, 1, 1) << compact(1, 1, 1);
        }

        render_receipt = (stream << main_rasterizer(gdr_width, gdr_height) << Corona::Horizon::commit());

        first_frame = false;

        ui.draw_overlay(display_executor, final_output_image, render_receipt);
        // Serialize CPU submit rate against the GPU (also covers IMGUI-render-off path
        // where draw_overlay returns without waiting).
        display_executor.wait(render_receipt);
        (void)(display_executor.stream() << Corona::Horizon::present(display, final_output_image)
                                         << Corona::Horizon::commit());
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
