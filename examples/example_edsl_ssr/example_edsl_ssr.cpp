// 屏幕空间反射（SSR）示例的 EDSL 版本，移植自 example_ssr（GLSL 版）。
//
// 管线（4 站，与 GLSL 版一致）：
//   raster  geom          → outColor(RGBA16F rgb=直接光, a=reflectivity)
//                           outNormal(RGBA8 rgb=view 法线, a=roughness)
//                           outDepthVal(R32F 器件深度)
//   compute linear_depth  → R32F view 空间线性深度
//   compute trace         → RGBA16F rgb=反射色, a=权重
//   compute composite     → RGBA16F 最终输出
//
// 与 GLSL 版的差异（均由 EDSL 当前能力决定）：
// - EDSL 的 $FOR/$WHILE 尚未生成循环 AST，trace 的步进循环和 binary search
//   在 C++ 侧定长展开（kTraceSteps/kRefineSteps 为编译期常量），用 Bool 标志位
//   模拟 break/return；imgui 里 Steps/Refine Steps 因此不再可调。
// - GLSL 版全程 bindless imageLoad；EDSL 同样全程 storage image：当前像素用
//   dispatchThreadID 直接索引，任意 uv 先换算像素坐标再经 Uint2() 截断强转
//   （与 GLSL 版 ivec2(uv*res)+clamp 同构，保持点采样一致）。
//   G-buffer 的器件深度也不再经 gl_FragCoord.z（EDSL 无此内建），
//   而是把 clip 坐标作为 varying 传给 FS 算 z/w（与 edsl_shadowmaps pack 同法）。
// - 1280x720 是 8 的整倍数，compute 里省掉出界 return（EDSL 无 return 语句）。

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "common.h"
#include "hardware_wrapper_vulkan/hardware_context.h"
#include "horizon.h"
#include "imgui_horizon.h"

#include <imgui.h>

#include "Codegen/BuiltinVariate.h"
#include "Codegen/ControlFlows.h"
#include "Codegen/CustomLibrary.h"
#include "Codegen/TypeAlias.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <cstdlib>
#include <cstring>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

namespace
{
constexpr uint32_t essr_width = 1280;
constexpr uint32_t essr_height = 720;
constexpr float essr_near = 0.1f;
constexpr float essr_far = 100.0f;

// EDSL 无循环 AST，步进次数只能是编译期常量（C++ 侧展开）
constexpr int kTraceSteps = 48;
constexpr int kRefineSteps = 5;

ktm::fmat4x4 to_edsl_matrix(const glm::mat4& matrix)
{
    static_assert(sizeof(ktm::fmat4x4) == sizeof(glm::mat4));

    ktm::fmat4x4 result;
    std::memcpy(&result, &matrix, sizeof(result));
    return result;
}

struct EssrVertex
{
    std::array<float, 3> position {};
    std::array<float, 3> normal {};
};

// 单位立方体：每面 4 顶点独立法线（面法线，不共享顶点）
std::vector<EssrVertex> build_cube_vertices()
{
    struct Face
    {
        glm::vec3 normal;
        glm::vec3 corners[4];
    };
    const Face faces[6] = {
        { { 0, 0, 1 }, { { -1, 1, 1 }, { 1, 1, 1 }, { -1, -1, 1 }, { 1, -1, 1 } } },
        { { 0, 0, -1 }, { { -1, 1, -1 }, { 1, 1, -1 }, { -1, -1, -1 }, { 1, -1, -1 } } },
        { { 0, 1, 0 }, { { -1, 1, 1 }, { 1, 1, 1 }, { -1, 1, -1 }, { 1, 1, -1 } } },
        { { 0, -1, 0 }, { { -1, -1, 1 }, { 1, -1, 1 }, { -1, -1, -1 }, { 1, -1, -1 } } },
        { { 1, 0, 0 }, { { 1, -1, 1 }, { 1, 1, 1 }, { 1, -1, -1 }, { 1, 1, -1 } } },
        { { -1, 0, 0 }, { { -1, -1, 1 }, { -1, 1, 1 }, { -1, -1, -1 }, { -1, 1, -1 } } },
    };

    std::vector<EssrVertex> vertices;
    vertices.reserve(24);
    for (const Face& face : faces)
    {
        for (const glm::vec3& corner : face.corners)
        {
            EssrVertex v {};
            v.position = { corner.x, corner.y, corner.z };
            v.normal = { face.normal.x, face.normal.y, face.normal.z };
            vertices.push_back(v);
        }
    }
    return vertices;
}

// 与 example_deferred 的立方体索引一致（左手系正面朝外）
const std::vector<uint32_t> essr_cube_indices = {
    0, 2, 1, 1, 2, 3,
    4, 5, 6, 5, 7, 6,
    8, 10, 9, 9, 10, 11,
    12, 13, 14, 13, 15, 14,
    16, 18, 17, 17, 18, 19,
    20, 21, 22, 21, 23, 22,
};

// 场景里的一个物体：变换 + 材质
struct EssrInstance
{
    glm::mat4 model;
    glm::vec3 albedo;
    float reflectivity;
    float roughness;
};

void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
    if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}

} // namespace

using namespace EmbeddedShader;

// 顶点输入用 Aggregate（location 与成员声明顺序一致，见 edsl_shadowmaps 的注释）
struct EssrVertexIn
{
    Float3 pos;    // location 0
    Float3 normal; // location 1
};

struct EssrVertOut
{
    Float3 v_normal_vs;
    Float3 v_pos_vs;
    Float4 v_clip;     // FS 用 z/w 还原器件深度（EDSL 无 gl_FragCoord）
    Float4 v_material; // rgb: albedo, w: reflectivity（PC 只在 VS 读，经 varying 转发给 FS）
    Float4 v_params;   // x: roughness
};

void run_example_edsl_ssr()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(essr_width, essr_height, "Horizon SSR [EDSL]", nullptr, nullptr);
    glfwSetKeyCallback(window, key_callback);

    const std::vector<EssrVertex> cube_vertices = build_cube_vertices();
    Corona::Horizon::HardwareBuffer cube_vb = Corona::Horizon::HardwareBuffer::vertex(cube_vertices, "example_edsl_ssr.cube.vb");
    Corona::Horizon::HardwareBuffer cube_ib = Corona::Horizon::HardwareBuffer::index(essr_cube_indices, "example_edsl_ssr.cube.ib");

    // ---- G-buffer / 中间目标（与 GLSL 版一致）----
    const auto rt_usage = Corona::Horizon::ImageUsageFlags::ColorAttachment |
                          Corona::Horizon::ImageUsageFlags::Sampled |
                          Corona::Horizon::ImageUsageFlags::Storage;

    // rgb: 直接光结果（SSR 的采样源）, a: reflectivity。清屏 alpha=0 → 天空不产生反射
    Corona::Horizon::HardwareImage color_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        essr_width, essr_height, Corona::Horizon::Format::RGBA16_FLOAT, rt_usage, "example_edsl_ssr.color"));
    color_image.set_clear_color(0.14f, 0.19f, 0.28f, 0.0f);

    // rgb: view 空间法线, a: roughness
    Corona::Horizon::HardwareImage normal_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        essr_width, essr_height, Corona::Horizon::Format::RGBA8_UNORM, rt_usage, "example_edsl_ssr.normal"));
    normal_image.set_clear_color(0.5f, 0.5f, 1.0f, 1.0f);

    Corona::Horizon::HardwareImage depth_val_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        essr_width, essr_height, Corona::Horizon::Format::R32_FLOAT, rt_usage, "example_edsl_ssr.depthval"));
    depth_val_image.set_clear_color(1.0f, 0.0f, 0.0f, 0.0f); // 远平面

    // SSSR:albedo + metallic(trace 需要重建 Disney 材质)
    Corona::Horizon::HardwareImage albedo_met_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        essr_width, essr_height, Corona::Horizon::Format::RGBA8_UNORM, rt_usage, "example_edsl_ssr.albedo_met"));
    albedo_met_image.set_clear_color(0.0f, 0.0f, 0.0f, 0.0f);

    Corona::Horizon::HardwareImage linear_depth_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        essr_width, essr_height, Corona::Horizon::Format::R32_FLOAT,
        Corona::Horizon::ImageUsageFlags::Storage | Corona::Horizon::ImageUsageFlags::Sampled,
        "example_edsl_ssr.lineardepth"));

    Corona::Horizon::HardwareImage ssr_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        essr_width, essr_height, Corona::Horizon::Format::RGBA16_FLOAT,
        Corona::Horizon::ImageUsageFlags::Storage | Corona::Horizon::ImageUsageFlags::Sampled,
        "example_edsl_ssr.ssr"));

    Corona::Horizon::HardwareImage final_output_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        essr_width, essr_height, Corona::Horizon::Format::RGBA16_FLOAT,
        Corona::Horizon::ImageUsageFlags::Storage | Corona::Horizon::ImageUsageFlags::ColorAttachment |
            Corona::Horizon::ImageUsageFlags::Sampled | Corona::Horizon::ImageUsageFlags::TransferSrc |
            Corona::Horizon::ImageUsageFlags::TransferDst,
        "example_edsl_ssr.output"));

    Corona::Horizon::HardwareImage scene_depth(Corona::Horizon::HardwareImageDesc::depth_attachment(
        essr_width, essr_height, Corona::Horizon::Format::D32, "example_edsl_ssr.depth"));
    scene_depth.set_clear_depth(1.0f, 0);

    // ================================================================
    // Pass 1：几何 → G-buffer（EDSL 光栅）
    // ================================================================

    // 批次共享 uniform
    Float4x4 u_view_proj;
    Float4x4 u_view;
    Float4 u_light_dir_vs; // xyz: view 空间指向光源, w: 环境光强度

    // per-draw push constant
    Float4x4 pc_model;
    pc_model.as_push_constant();
    Float4 pc_material; // rgb: albedo
    pc_material.as_push_constant();
    Float4 pc_params; // x: reflectivity, y: roughness
    pc_params.as_push_constant();

    // 注意：用表达式初始化的 proxy 走 move 构造，只是表达式别名——不可重新赋值
    // （生成端不是 l-value），且每次使用处会整体重展开。凡是要复用/重赋值的变量，
    // 一律先默认构造成真正的局部变量再赋值（同 edsl_shadowmaps 的 Float result 惯用法）。
    auto geom_vert = [&](Aggregate<EssrVertexIn> vin) {
        Aggregate<EssrVertOut> out;
        Float4x4 model_view;
        model_view = mul(u_view, pc_model);
        Float4 clip;
        clip = mul(mul(u_view_proj, pc_model), Float4(vin->pos, 1.f));
        position() = clip;
        out->v_pos_vs = mul(model_view, Float4(vin->pos, 1.f))->xyz();
        out->v_normal_vs = normalize(mul(model_view, Float4(vin->normal, 0.f))->xyz());
        out->v_clip = clip;
        Float3 mat_rgb = pc_material->xyz(); // swizzle 先落变量，Float4 变参构造不认 SwizzleProxy
        out->v_material = Float4(mat_rgb, pc_params->x);
        out->v_params = Float4(pc_params->y, 0.f, 0.f, 0.f);
        return out;
    };

    Texture2D<ktm::fvec4> gbuf_out_color = color_image;
    Texture2D<ktm::fvec4> gbuf_out_normal = normal_image;
    Texture2D<float> gbuf_out_depthval = depth_val_image;
    Texture2D<ktm::fvec4> gbuf_out_albedo_met = albedo_met_image;

    auto geom_frag = [&](Aggregate<EssrVertOut> in) {
        // SSSR:直射光换 Disney(diffuse*(1-F)+GGX spec),v_material.w 语义改为 metallic
        auto schlick5g = [&](Float u) {
            Float m;
            m = clamp(Float(1.f) - u, Float(0.f), Float(1.f));
            Float m2;
            m2 = m * m;
            Float r;
            r = m2 * m2 * m;
            return r;
        };

        Float3 n;
        n = normalize(in->v_normal_vs);
        Float3 l;
        l = normalize(u_light_dir_vs->xyz());
        Float3 v;
        v = -normalize(in->v_pos_vs); // 相机在 view 空间原点

        Float3 albedo;
        albedo = Float3(in->v_material->xyz());
        Float metallic = in->v_material->w;
        Float roughness;
        roughness = clamp(in->v_params->x, Float(0.05f), Float(1.0f));

        Float NoL;
        NoL = max(dot(n, l), Float(0.f));
        Float NoV;
        NoV = max(dot(n, v), Float(1e-4f));
        Float3 h;
        h = normalize(l + v);
        Float NoH;
        NoH = max(dot(n, h), Float(0.f));
        Float LoH;
        LoH = max(dot(l, h), Float(0.f));

        Float ga;
        ga = roughness * roughness;
        Float3 f0;
        f0 = mix(Float3(0.04f, 0.04f, 0.04f), albedo, metallic);
        Float3 F;
        F = f0 + (Float3(1.f, 1.f, 1.f) - f0) * schlick5g(LoH);

        Float FD90;
        FD90 = Float(0.5f) + Float(2.f) * roughness * LoH * LoH;
        Float fd;
        fd = (Float(1.f) + (FD90 - Float(1.f)) * schlick5g(NoL)) *
             (Float(1.f) + (FD90 - Float(1.f)) * schlick5g(NoV));
        Float3 diffuse;
        diffuse = albedo * (fd * Float(0.31830988618f)) * (Float(1.f) - metallic);

        Float a2;
        a2 = ga * ga;
        Float dt;
        dt = Float(1.f) + (a2 - Float(1.f)) * NoH * NoH;
        Float D;
        D = a2 / (Float(3.14159265359f) * dt * dt);
        Float g1l;
        g1l = Float(2.f) * NoL / (NoL + sqrt(a2 + (Float(1.f) - a2) * NoL * NoL));
        Float g1v;
        g1v = Float(2.f) * NoV / (NoV + sqrt(a2 + (Float(1.f) - a2) * NoV * NoV));
        Float3 spec;
        spec = F * (D * (g1l * g1v) / max(Float(4.f) * NoL * NoV, Float(1e-4f)));

        Float3 lit;
        lit = (diffuse * (Float3(1.f, 1.f, 1.f) - F) + spec) * NoL + albedo * u_light_dir_vs->w;

        gbuf_out_color << Float4(lit, Float(1.0f)); // a=1:有几何标记(清屏 0 → trace 跳过天空)
        gbuf_out_normal << Float4(n * Float(0.5f) + Float(0.5f), roughness);
        gbuf_out_depthval << (in->v_clip->z / in->v_clip->w);
        gbuf_out_albedo_met << Float4(albedo, metallic);
    };

    // ================================================================
    // Pass 2：器件深度 → view 空间线性深度（compute）
    // ================================================================

    Texture2D<float> ld_depth_in = depth_val_image;
    Texture2D<float> ld_linear_out = linear_depth_image;
    Float4 u_depth_unpack; // xy: viewZ = x / (device_z + y)，即 x=p32, y=-p22
    Float4 u_ld_params;    // z: far（无几何处的填充值）；其余分量为对齐 GLSL 版打包保留，未使用

    auto linear_depth_cs = [&] {
        auto coord = dispatchThreadID()->xy();
        Float device_z = ld_depth_in[coord];
        Float denom;
        denom = device_z + u_depth_unpack->y;

        // 远平面（清屏值）处 denom → 0，钳到 far 避免 inf 污染后续步进比较
        Float view_z = u_ld_params->z;
        $IF(abs(denom) >= 1e-6f) view_z = u_depth_unpack->x / denom;

        ld_linear_out[coord] = clamp(view_z, 0.f, Float(u_ld_params->z));
    };

    // ================================================================
    // Pass 3：屏幕空间射线步进（compute）
    // ================================================================

    Texture2D<float> tr_linear_depth = linear_depth_image; // storage 读（[coord]）
    Texture2D<ktm::fvec4> tr_normal = normal_image;        // storage 读
    Texture2D<ktm::fvec4> tr_color = color_image;          // storage 读
    Texture2D<ktm::fvec4> tr_ssr_out = ssr_image;
    Texture2D<ktm::fvec4> tr_albedo_met = albedo_met_image; // storage 读(SSSR Disney 材质)          // storage 写
    // 打包沿用 GLSL 版 pushConsts 布局；步数/精修/分辨率已编译进 shader，
    // 对应分量（params0.w / params1.zw / params2.z）保留但未使用
    Float4 u_ndc_to_view; // xy: mul, zw: add（uv → view.xy / viewZ）
    Float4 u_tr_params0;  // x: proj00, y: proj11, z: maxDistance
    Float4 u_tr_params1;  // x: thickness, y: frameIdx
    Float4 u_tr_params2;  // x: fresnelPower, y: fresnelF0, w: useJitter(0/1)

    // view 空间点 → uv（投影无斜切，只需 p00/p11；p11 已含 Vulkan Y 翻转）
    auto view_to_uv = [&](Float3 p) -> Float2 {
        Float inv_z;
        inv_z = Float(1.f) / p->z;
        return Float2(u_tr_params0->x * p->x * inv_z * Float(0.5f) + Float(0.5f),
                      u_tr_params0->y * p->y * inv_z * Float(0.5f) + Float(0.5f));
    };

    // uv → 钳到图内的整数像素坐标（与 GLSL 版 load_linear_depth 的 ivec2+clamp 同构；
    // Uint2() 变参构造生成 uint2(f, f)，做 float→uint 截断）
    auto uv_to_coord = [&](Float2 uv) -> Uint2 {
        Float px;
        px = clamp(uv->x * Float(float(essr_width)), 0.f, float(essr_width) - 1.f);
        Float py;
        py = clamp(uv->y * Float(float(essr_height)), 0.f, float(essr_height) - 1.f);
        return Uint2(px, py);
    };

    // smoothstep 手写（CustomLibrary 暂无），边界是编译期常量
    auto edge01 = [&](Float x, float e0, float e1) {
        Float t;
        t = clamp((x - Float(e0)) * Float(1.0f / (e1 - e0)), 0.f, 1.f);
        return t * t * (Float(3.0f) - 2.0f * t);
    };

    auto trace_cs = [&] {
        // swizzle 结果的 operator-> 被删除，先落到 Uint2 变量再取分量
        Uint2 tid = dispatchThreadID()->xy();

        // 先写零：所有「无反射」分支都落在这个默认值上
        tr_ssr_out[tid] = Float4(0.f, 0.f, 0.f, 0.f);

        Float2 uv0;
        uv0 = (Float2(tid->x, tid->y) + Float2(0.5f, 0.5f)) *
              Float2(1.0f / float(essr_width), 1.0f / float(essr_height));

        // reflectivity 存在颜色图 alpha；清屏 alpha=0，天空/空白处天然被跳过
        Float4 center_color;
        center_color = tr_color[tid];
        Float has_geo = center_color->w;

        $IF(has_geo > 0.5f)
        {
            // 重建 view 空间坐标（同 GLSL 版 / example_assao 的 load_view_pos）
            Float view_z;
            view_z = tr_linear_depth[tid];
            Float3 view_pos;
            view_pos = Float3((uv0->x * u_ndc_to_view->x + u_ndc_to_view->z) * view_z,
                              (uv0->y * u_ndc_to_view->y + u_ndc_to_view->w) * view_z,
                              view_z);

            Float4 packed_normal;
            packed_normal = tr_normal[tid];
            Float3 n;
            n = normalize(packed_normal->xyz() * Float(2.0f) - Float3(1.f, 1.f, 1.f));
            Float roughness = packed_normal->w;

            Float4 albedo_met;
            albedo_met = tr_albedo_met[tid];
            Float3 albedo;
            albedo = Float3(albedo_met->xyz());
            Float metallic = albedo_met->w;

            Float3 v;
            v = normalize(view_pos); // 相机在原点，view_pos 即视线方向(指向表面)
            Float3 wo;
            wo = Float3(0.f, 0.f, 0.f) - v; // BRDF 的观察方向

            // ---- SSSR:弹射方向按 Disney BRDF lobe 概率采样(参考 sampleDisneyBRDF) ----
            Float seed;
            {
                Float2 pixc;
                pixc = Float2(tid->x, tid->y);
                seed = fract(sin(pixc->x * Float(12.9898f) + pixc->y * Float(78.233f)) * Float(43758.5453f) +
                             u_tr_params1->y * Float(0.6180339887f));
            }
            auto frand = [&](Float& st) {
                st = fract(sin(st * Float(91.3458f) + Float(47.9898f)) * Float(43758.5453123f));
                Float r;
                r = st;
                return r;
            };
            auto schlick5 = [&](Float u) {
                Float m;
                m = clamp(Float(1.f) - u, Float(0.f), Float(1.f));
                Float m2;
                m2 = m * m;
                Float r;
                r = m2 * m2 * m;
                return r;
            };
            auto cross3 = [&](Float3 ca, Float3 cb) {
                Float3 cc;
                cc = Float3(ca->y * cb->z - ca->z * cb->y,
                            ca->z * cb->x - ca->x * cb->z,
                            ca->x * cb->y - ca->y * cb->x);
                return cc;
            };

            Float aa;
            aa = max(roughness * roughness, Float(1e-3f));

            // basis(n)
            Float3 upv;
            upv = Float3(0.f, 1.f, 0.f);
            $IF(abs(n->y) > Float(0.99f)) { upv = Float3(1.f, 0.f, 0.f); };
            Float3 tb;
            tb = normalize(cross3(upv, n));
            Float3 bb;
            bb = cross3(n, tb);

            // VNDF 采样半向量
            Float3 Vl;
            Vl = Float3(dot(wo, tb), dot(wo, bb), dot(wo, n));
            Float r1;
            r1 = frand(seed);
            Float r2;
            r2 = frand(seed);
            Float3 Vh;
            Vh = normalize(Float3(Vl->x * aa, Vl->y * aa, Vl->z));
            Float lq;
            lq = Vh->x * Vh->x + Vh->y * Vh->y;
            Float3 T1;
            T1 = Float3(1.f, 0.f, 0.f);
            $IF(lq > Float(1e-7f))
            {
                Float invl;
                invl = Float(1.f) / sqrt(lq);
                T1 = Float3(Float(0.f) - Vh->y, Vh->x, Float(0.f)) * invl;
            };
            Float3 T2;
            T2 = cross3(Vh, T1);
            Float rrs;
            rrs = sqrt(r1);
            Float phi;
            phi = r2 * Float(6.28318530718f);
            Float t1;
            t1 = rrs * cos(phi);
            Float t2;
            t2 = rrs * sin(phi);
            Float sblend;
            sblend = Float(0.5f) * (Float(1.f) + Vh->z);
            t2 = (Float(1.f) - sblend) * sqrt(max(Float(0.f), Float(1.f) - t1 * t1)) + sblend * t2;
            Float nzc;
            nzc = sqrt(max(Float(0.f), Float(1.f) - t1 * t1 - t2 * t2));
            Float3 Nh;
            Nh = T1 * t1 + T2 * t2 + Vh * nzc;
            Float3 hl;
            hl = normalize(Float3(Nh->x * aa, Nh->y * aa, max(Float(0.f), Nh->z)));
            $IF(hl->z < Float(0.f)) { hl = Float3(0.f, 0.f, 0.f) - hl; };
            Float3 hw;
            hw = tb * hl->x + bb * hl->y + n * hl->z;

            // Fresnel 与 lobe 权重
            Float3 f0v;
            f0v = mix(Float3(0.04f, 0.04f, 0.04f), albedo, metallic);
            Float vhd;
            vhd = dot(wo, hw);
            Float3 Fr;
            Fr = f0v + (Float3(1.f, 1.f, 1.f) - f0v) * schlick5(vhd);
            Float diffW;
            diffW = Float(1.f) - metallic;
            Float specW;
            specW = dot(Fr, Float3(0.299f, 0.587f, 0.114f));
            Float invWt;
            invWt = Float(1.f) / (diffW + specW);
            diffW = diffW * invWt;
            specW = specW * invWt;

            Float3 ray_dir;
            ray_dir = v - n * (Float(2.0f) * dot(n, v)); // 兜底(pdfW<=0 时不会用于输出)
            Float3 brdfRGB;
            brdfRGB = Float3(0.f, 0.f, 0.f);
            Float pdfW;
            pdfW = Float(0.f);

            Float rnd;
            rnd = frand(seed);
            $IF(rnd < diffW)
            {
                // diffuse lobe:余弦半球
                Float zc;
                zc = frand(seed) * Float(2.f) - Float(1.f);
                Float phid;
                phid = frand(seed) * Float(6.28318530718f);
                Float rc;
                rc = sqrt(max(Float(0.f), Float(1.f) - zc * zc));
                Float3 sp;
                sp = Float3(rc * cos(phid), rc * sin(phid), zc);
                ray_dir = normalize(n * Float(1.0001f) + sp);
                Float3 hh;
                hh = normalize(ray_dir + wo);
                Float NoL;
                NoL = dot(n, ray_dir);
                Float NoV;
                NoV = dot(n, wo);
                $IF((NoL > Float(0.f)) && (NoV > Float(0.f)))
                {
                    Float LoH;
                    LoH = dot(ray_dir, hh);
                    Float pdf;
                    pdf = NoL * Float(0.31830988618f);
                    Float FD90;
                    FD90 = Float(0.5f) + Float(2.f) * roughness * LoH * LoH;
                    Float fa;
                    fa = Float(1.f) + (FD90 - Float(1.f)) * schlick5(NoL);
                    Float fb;
                    fb = Float(1.f) + (FD90 - Float(1.f)) * schlick5(NoV);
                    Float3 diffv;
                    diffv = albedo * (fa * fb * Float(0.31830988618f));
                    diffv = diffv * (Float3(1.f, 1.f, 1.f) - Fr);
                    brdfRGB = diffv * NoL;
                    pdfW = diffW * pdf;
                };
            }
            $ELSE
            {
                // specular lobe:reflect(-wo, hw)
                Float3 Iv;
                Iv = Float3(0.f, 0.f, 0.f) - wo;
                Float dnh;
                dnh = dot(hw, Iv);
                ray_dir = Iv - hw * (Float(2.f) * dnh);
                Float NoL;
                NoL = dot(n, ray_dir);
                Float NoV;
                NoV = dot(n, wo);
                $IF((NoL > Float(0.f)) && (NoV > Float(0.f)))
                {
                    Float NoH;
                    NoH = min(dot(n, hw), Float(0.99f));
                    Float a2s;
                    a2s = aa * aa;
                    Float dts;
                    dts = Float(1.f) + (a2s - Float(1.f)) * NoH * NoH;
                    Float Dg;
                    Dg = a2s / (Float(3.14159265359f) * dts * dts);
                    Float pdf;
                    pdf = Dg * NoH / max(Float(4.f) * NoV, Float(1e-5f));
                    Float g1l;
                    g1l = Float(2.f) * NoL / (NoL + sqrt(a2s + (Float(1.f) - a2s) * NoL * NoL));
                    Float g1v;
                    g1v = Float(2.f) * NoV / (NoV + sqrt(a2s + (Float(1.f) - a2s) * NoV * NoV));
                    Float3 specv;
                    specv = Fr * (Dg * (g1l * g1v) / max(Float(4.f) * NoL * NoV, Float(1e-5f)));
                    brdfRGB = specv * NoL;
                    pdfW = specW * pdf;
                };
            };

            $IF(pdfW > Float(0.f))
            {
            Float3 throughput;
            {
                Float invp;
                invp = Float(1.f) / pdfW;
                throughput = min(brdfRGB * invp, Float3(4.f, 4.f, 4.f));
            }

            Float step_len;
            step_len = u_tr_params0->z * Float(1.0f / float(kTraceSteps));
            Float3 ray_step;
            ray_step = ray_dir * step_len;

            // 沿法线偏置半步，避免起点自相交（默认构造再赋值：后面循环里要反复重赋值）
            Float3 sample_pos;
            sample_pos = view_pos + n * (step_len * Float(0.5f));

            // interleaved gradient noise 逐帧抖动（同 GLSL 版）
            Float2 noise_p = Float2(tid->x, tid->y) +
                             Float2(314.0f, 159.0f) * Float(u_tr_params1->y);
            Float rand01 = fract(Float(52.9829189f) *
                                 fract(dot(noise_p, Float2(0.06711056f, 0.00583715f))));
            Float initial_offset = mix(Float(1.f), Float(0.5f) + rand01, Float(u_tr_params2->w));
            sample_pos = sample_pos + ray_step * initial_offset;

            Float thickness = u_tr_params1->x;

            // 步进循环：C++ 侧定长展开，marching 标志位模拟 break
            Bool marching = true;
            Bool hit = false;
            Float hit_step = float(kTraceSteps);
            Float3 prev_pos = sample_pos;

            for (int i = 0; i < kTraceSteps; ++i)
            {
                $IF(marching)
                {
                    $IF(sample_pos->z <= 0.01f) marching = false; // 越过相机平面
                    $ELSE
                    {
                        Float2 s_uv;
                        s_uv = view_to_uv(sample_pos);
                        $IF(any(s_uv < Float2(0.f, 0.f)) || any(s_uv > Float2(1.f, 1.f)))
                            marching = false; // 出屏
                        $ELSE
                        {
                            Float scene_z;
                            scene_z = tr_linear_depth[uv_to_coord(s_uv)];
                            Float delta;
                            delta = sample_pos->z - scene_z;

                            // 穿到表面之后、但没穿过太厚（与 GLSL 版同判据）
                            $IF((delta > 1e-4f) && (delta < thickness))
                            {
                                hit = true;
                                hit_step = float(i);
                                marching = false;
                            }
                            $ELSE
                            {
                                prev_pos = sample_pos;
                                sample_pos = sample_pos + ray_step;
                            }
                        }
                    }
                }
            }

            $IF(hit)
            {
                // binary search 精修（定长展开）
                Float3 lo = prev_pos;
                Float3 hi = sample_pos;
                for (int k = 0; k < kRefineSteps; ++k)
                {
                    Float3 mid;
                    mid = (lo + hi) * Float(0.5f);
                    Float2 mid_uv;
                    mid_uv = view_to_uv(mid);
                    Float mid_scene_z;
                    mid_scene_z = tr_linear_depth[uv_to_coord(mid_uv)];
                    $IF(mid->z > mid_scene_z) hi = mid;
                    $ELSE lo = mid;
                }
                Float2 hit_uv;
                hit_uv = view_to_uv(hi);
                Float3 refl_color;
                refl_color = tr_color[uv_to_coord(hit_uv)]->xyz();

                // ---- SSSR:BRDF 吞吐已含 Fresnel/lobe 权重,这里只叠屏幕空间置信度 ----
                Float fade_x = edge01(hit_uv->x, 0.0f, 0.12f) * (Float(1.f) - edge01(hit_uv->x, 0.88f, 1.0f));
                Float fade_y = edge01(hit_uv->y, 0.0f, 0.12f) * (Float(1.f) - edge01(hit_uv->y, 0.88f, 1.0f));
                Float edge_fade = fade_x * fade_y;

                // 步进越远置信度越低
                Float dist_fade = mix(Float(0.45f), Float(1.f),
                                      Float(1.f) - clamp(hit_step * Float(1.0f / float(kTraceSteps)), 0.f, 1.f));

                Float confidence;
                confidence = edge_fade * dist_fade;
                Float3 bounce;
                bounce = refl_color * throughput * confidence;
                tr_ssr_out[tid] = Float4(bounce, clamp(confidence, 0.f, 1.f));
            }
            }; // $IF(pdfW > 0)
        }
    };

    // ================================================================
    // Pass 4：合成（compute）
    // ================================================================

    Texture2D<ktm::fvec4> cp_color = color_image;
    Texture2D<ktm::fvec4> cp_ssr = ssr_image;
    Texture2D<ktm::fvec4> cp_output = final_output_image;
    Float4 u_cp_params; // z: intensity, w: debug 模式（0 最终 / 1 仅反射 / 2 权重 / 3 reflectivity）

    auto composite_cs = [&] {
        auto coord = dispatchThreadID()->xy();
        Float4 base;
        base = cp_color[coord];
        Float4 ssr;
        ssr = cp_ssr[coord];

        // SSSR:弹射贡献是加性的一次间接光(吞吐/置信度已折进 rgb)
        Float3 result; // 默认构造：$IF 分支里会重赋值
        result = Float3(base->xyz()) + Float3(ssr->xyz()) * u_cp_params->z;

        Float mode = u_cp_params->w;
        $IF((mode > 0.5f) && (mode < 1.5f)) result = ssr->xyz();
        $IF((mode > 1.5f) && (mode < 2.5f)) result = Float3(ssr->w, ssr->w, ssr->w);
        $IF(mode > 2.5f) result = Float3(base->xyz());

        cp_output[coord] = Float4(result, 1.f);
    };


    // ================================================================
    // Pass PT:Disney BRDF 路径追踪(compute,SSR_PATHTRACE 模式)
    // 与 GLSL 版 ssr_pathtrace_compute.glsl 逐行同构;差异声明:
    // GLSL 版为运行期循环 + break/early-return,本版因 EDSL 无运行期循环,
    // 为定长展开(5 弹跳 × 16 球)+ alive/命中掩码(同 trace_cs 的既有不对称)。
    // ================================================================

    Texture2D<ktm::fvec4> pt_output = final_output_image;
    Float4 u_pt0; // xyz: ro, w: frameIdx
    Float4 u_pt1; // xyz: cam_fwd, w: focal
    Float4 u_pt2; // xyz: cam_right, w: width
    Float4 u_pt3; // xyz: cam_up, w: height

    constexpr int kPtBounces = 5;
    constexpr int kPtSphereDim = 4;

    auto pathtrace_cs = [&] {
        Uint2 tid = dispatchThreadID()->xy();
        Float2 pix;
        pix = Float2(tid->x, tid->y);

        // ---- PRNG:浮点 hash 链(与 GLSL 版一致) ----
        Float seed;
        seed = fract(sin(pix->x * Float(12.9898f) + pix->y * Float(78.233f)) * Float(43758.5453f) +
                     u_pt0->w * Float(0.6180339887f));

        auto frand = [&](Float& st) {
            st = fract(sin(st * Float(91.3458f) + Float(47.9898f)) * Float(43758.5453123f));
            Float r;
            r = st;
            return r;
        };

        auto schlick5 = [&](Float u) {
            Float m;
            m = clamp(Float(1.f) - u, Float(0.f), Float(1.f));
            Float m2;
            m2 = m * m;
            Float r;
            r = m2 * m2 * m;
            return r;
        };

        auto cross3 = [&](Float3 a, Float3 b) {
            Float3 c;
            c = Float3(a->y * b->z - a->z * b->y,
                       a->z * b->x - a->x * b->z,
                       a->x * b->y - a->y * b->x);
            return c;
        };

        auto sky_color = [&](Float3 rdir) {
            Float tt;
            tt = clamp(rdir->y * Float(0.5f) + Float(0.5f), Float(0.f), Float(1.f));
            Float3 sky;
            sky = mix(Float3(0.55f, 0.62f, 0.75f), Float3(0.15f, 0.28f, 0.55f), tt);
            Float sd;
            sd = dot(rdir, Float3(0.98058067f, 0.19611613f, 0.f)); // normalize(5,1,0)
            Float sun;
            sun = pow(max(sd, Float(0.f)), Float(96.f));
            Float3 res;
            res = sky * Float(1.1f) + Float3(1.0f, 0.85f, 0.6f) * (sun * Float(8.f));
            return res;
        };

        auto cosine_sample = [&](Float3 nrm) {
            Float z;
            z = frand(seed) * Float(2.f) - Float(1.f);
            Float phi;
            phi = frand(seed) * Float(6.28318530718f);
            Float rr;
            rr = sqrt(max(Float(0.f), Float(1.f) - z * z));
            Float3 sp;
            sp = Float3(rr * cos(phi), rr * sin(phi), z);
            Float3 res;
            res = normalize(nrm * Float(1.0001f) + sp);
            return res;
        };

        // ---- 光线生成:抖动像素中心 → CPU 端算好的相机基向量 ----
        Float jx;
        jx = frand(seed);
        Float jy;
        jy = frand(seed);
        Float uu;
        uu = (pix->x + jx) * Float(1.0f / float(essr_width));
        Float vv0;
        vv0 = (pix->y + jy) * Float(1.0f / float(essr_height));
        Float ndcx;
        ndcx = (uu * Float(2.f) - Float(1.f)) * Float(float(essr_width) / float(essr_height));
        Float ndcy;
        ndcy = Float(1.f) - vv0 * Float(2.f); // Vulkan 图像 Y 向下
        Float3 rd;
        rd = normalize(Float3(u_pt1->x, u_pt1->y, u_pt1->z) * u_pt1->w +
                       Float3(u_pt2->x, u_pt2->y, u_pt2->z) * ndcx +
                       Float3(u_pt3->x, u_pt3->y, u_pt3->z) * ndcy);
        Float3 ro;
        ro = Float3(u_pt0->x, u_pt0->y, u_pt0->z);
        Float3 rd0;
        rd0 = rd;

        Float3 acc;
        acc = Float3(0.f, 0.f, 0.f);
        Float3 abso;
        abso = Float3(1.f, 1.f, 1.f);
        Float firstDepth;
        firstDepth = Float(1000.f);
        Bool alive = true;

        // ---- 5 次弹跳:定长展开 + alive 掩码 ----
        for (int bounce = 0; bounce < kPtBounces; ++bounce)
        {
            $IF(alive)
            {
                // -- trace:地板 + 16 球,全部展开,材质常量烙进每个展开块 --
                Float bestT;
                bestT = Float(1e30f);
                Float3 hn;
                hn = Float3(0.f, 1.f, 0.f);
                Float3 halb;
                halb = Float3(0.f, 0.f, 0.f);
                Float hmet;
                hmet = Float(0.f);
                Float hrgh;
                hrgh = Float(0.5f);

                // 地板 y=0:金属棋盘格
                $IF(rd->y < Float(-1e-6f))
                {
                    Float tf;
                    tf = (Float(0.f) - ro->y) / rd->y;
                    $IF((tf > Float(0.001f)) && (tf < bestT))
                    {
                        bestT = tf;
                        hn = Float3(0.f, 1.f, 0.f);
                        halb = Float3(0.75f, 0.75f, 0.75f);
                        hmet = Float(1.f);
                        Float px;
                        px = ro->x + rd->x * tf;
                        Float pz;
                        pz = ro->z + rd->z * tf;
                        // floor(x) = x - fract(x);mod(fx+fz,2) = fract((fx+fz)*0.5)*2
                        Float fx;
                        fx = px - fract(px);
                        Float fz;
                        fz = pz - fract(pz);
                        Float ck;
                        ck = fract((fx + fz) * Float(0.5f)) * Float(2.f);
                        hrgh = ck * Float(0.25f) + Float(0.25f);
                    };
                };

                // 球阵:roughness 沿 z 轴,metallic 沿 x 轴(常量展开)
                for (int iz = 0; iz < kPtSphereDim; ++iz)
                {
                    for (int ix = 0; ix < kPtSphereDim; ++ix)
                    {
                        const float scx = -4.5f + float(ix) * 3.0f;
                        const float scz = -4.5f + float(iz) * 3.0f;
                        const float smet = float(ix) / 3.0f;
                        const float srgh = 0.05f + (float(iz) / 3.0f) * 0.9f;
                        Float3 oc;
                        oc = ro - Float3(scx, 1.0f, scz);
                        Float bq;
                        bq = dot(oc, rd);
                        Float disc;
                        disc = bq * bq - dot(oc, oc) + Float(1.0f); // r=1
                        $IF(disc > Float(0.f))
                        {
                            Float tt;
                            tt = Float(0.f) - bq - sqrt(disc);
                            $IF((tt > Float(0.001f)) && (tt < bestT))
                            {
                                bestT = tt;
                                Float3 hp;
                                hp = ro + rd * tt;
                                hn = normalize(hp - Float3(scx, 1.0f, scz));
                                halb = Float3(0.9f, 0.9f, 0.9f);
                                hmet = Float(smet);
                                hrgh = Float(srgh);
                            };
                        };
                    }
                }

                if (bounce == 0)
                {
                    firstDepth = min(bestT, Float(1000.f));
                }

                $IF(bestT > Float(999.f))
                {
                    acc = acc + sky_color(rd) * abso;
                    alive = Float(1.f) < Float(0.f); // false
                }
                $ELSE
                {
                    Float3 hp;
                    hp = ro + rd * bestT;
                    Float3 wo;
                    wo = Float3(0.f, 0.f, 0.f) - rd;

                    // ---- sampleDisneyBRDF(照搬参考 lobe 概率结构) ----
                    Float aa;
                    aa = max(hrgh * hrgh, Float(1e-3f));

                    // basis(hn) → t/b(cross 手写)
                    Float3 upv;
                    upv = Float3(0.f, 1.f, 0.f);
                    $IF(abs(hn->y) > Float(0.99f)) { upv = Float3(1.f, 0.f, 0.f); };
                    Float3 tb;
                    tb = normalize(cross3(upv, hn));
                    Float3 bb;
                    bb = cross3(hn, tb);

                    // toLocal(v) → VNDF 采样 → toWorld
                    Float3 Vl;
                    Vl = Float3(dot(wo, tb), dot(wo, bb), dot(wo, hn));
                    Float r1;
                    r1 = frand(seed);
                    Float r2;
                    r2 = frand(seed);
                    Float3 Vh;
                    Vh = normalize(Float3(Vl->x * aa, Vl->y * aa, Vl->z));
                    Float lq;
                    lq = Vh->x * Vh->x + Vh->y * Vh->y;
                    Float3 T1;
                    T1 = Float3(1.f, 0.f, 0.f);
                    $IF(lq > Float(1e-7f))
                    {
                        Float invl;
                        invl = Float(1.f) / sqrt(lq);
                        T1 = Float3(Float(0.f) - Vh->y, Vh->x, Float(0.f)) * invl;
                    };
                    Float3 T2;
                    T2 = cross3(Vh, T1);
                    Float rr;
                    rr = sqrt(r1);
                    Float phi;
                    phi = r2 * Float(6.28318530718f);
                    Float t1;
                    t1 = rr * cos(phi);
                    Float t2;
                    t2 = rr * sin(phi);
                    Float sblend;
                    sblend = Float(0.5f) * (Float(1.f) + Vh->z);
                    t2 = (Float(1.f) - sblend) * sqrt(max(Float(0.f), Float(1.f) - t1 * t1)) + sblend * t2;
                    Float nz;
                    nz = sqrt(max(Float(0.f), Float(1.f) - t1 * t1 - t2 * t2));
                    Float3 Nh;
                    Nh = T1 * t1 + T2 * t2 + Vh * nz;
                    Float3 hl;
                    hl = normalize(Float3(Nh->x * aa, Nh->y * aa, max(Float(0.f), Nh->z)));
                    // h.z < 0 翻转(局部空间)
                    $IF(hl->z < Float(0.f)) { hl = Float3(0.f, 0.f, 0.f) - hl; };
                    // toWorld
                    Float3 hw;
                    hw = tb * hl->x + bb * hl->y + hn * hl->z;

                    // Fresnel 与 lobe 权重
                    Float3 f0;
                    f0 = mix(Float3(0.04f, 0.04f, 0.04f), halb, hmet);
                    Float vh;
                    vh = dot(wo, hw);
                    Float3 Fr;
                    Fr = f0 + (Float3(1.f, 1.f, 1.f) - f0) * schlick5(vh);
                    Float diffW;
                    diffW = Float(1.f) - hmet;
                    Float specW;
                    specW = dot(Fr, Float3(0.299f, 0.587f, 0.114f));
                    Float invW;
                    invW = Float(1.f) / (diffW + specW);
                    diffW = diffW * invW;
                    specW = specW * invW;

                    Float3 outDir;
                    outDir = rd;
                    Float3 brdfRGB;
                    brdfRGB = Float3(0.f, 0.f, 0.f);
                    Float pdfW;
                    pdfW = Float(0.f);

                    Float rnd;
                    rnd = frand(seed);
                    $IF(rnd < diffW)
                    {
                        // diffuse lobe
                        outDir = cosine_sample(hn);
                        Float3 hh;
                        hh = normalize(outDir + wo);
                        Float NoL;
                        NoL = dot(hn, outDir);
                        Float NoV;
                        NoV = dot(hn, wo);
                        $IF((NoL > Float(0.f)) && (NoV > Float(0.f)))
                        {
                            Float LoH;
                            LoH = dot(outDir, hh);
                            Float pdf;
                            pdf = NoL * Float(0.31830988618f); // NoL/PI
                            Float FD90;
                            FD90 = Float(0.5f) + Float(2.f) * hrgh * LoH * LoH;
                            Float fa;
                            fa = Float(1.f) + (FD90 - Float(1.f)) * schlick5(NoL);
                            Float fb;
                            fb = Float(1.f) + (FD90 - Float(1.f)) * schlick5(NoV);
                            Float3 diff;
                            diff = halb * (fa * fb * Float(0.31830988618f));
                            diff = diff * (Float3(1.f, 1.f, 1.f) - Fr);
                            brdfRGB = diff * NoL;
                            pdfW = diffW * pdf;
                        };
                    }
                    $ELSE
                    {
                        // specular lobe:reflect(-wo, hw)
                        Float3 Iv;
                        Iv = Float3(0.f, 0.f, 0.f) - wo;
                        Float dnh;
                        dnh = dot(hw, Iv);
                        outDir = Iv - hw * (Float(2.f) * dnh);
                        Float NoL;
                        NoL = dot(hn, outDir);
                        Float NoV;
                        NoV = dot(hn, wo);
                        $IF((NoL > Float(0.f)) && (NoV > Float(0.f)))
                        {
                            Float NoH;
                            NoH = min(dot(hn, hw), Float(0.99f));
                            Float a2;
                            a2 = aa * aa;
                            Float dt;
                            dt = Float(1.f) + (a2 - Float(1.f)) * NoH * NoH;
                            Float Dg;
                            Dg = a2 / (Float(3.14159265359f) * dt * dt);
                            Float pdf;
                            pdf = Dg * NoH / max(Float(4.f) * NoV, Float(1e-5f));
                            Float g1l;
                            g1l = Float(2.f) * NoL / (NoL + sqrt(a2 + (Float(1.f) - a2) * NoL * NoL));
                            Float g1v;
                            g1v = Float(2.f) * NoV / (NoV + sqrt(a2 + (Float(1.f) - a2) * NoV * NoV));
                            Float Gg;
                            Gg = g1l * g1v;
                            Float3 spec;
                            spec = Fr * (Dg * Gg / max(Float(4.f) * NoL * NoV, Float(1e-5f)));
                            brdfRGB = spec * NoL;
                            pdfW = specW * pdf;
                        };
                    };

                    // 吸收(pdf 在 pdfW)
                    $IF(pdfW > Float(0.f))
                    {
                        Float invp;
                        invp = Float(1.f) / pdfW;
                        abso = abso * (brdfRGB * invp);
                    };

                    // 续方向
                    ro = hp + hn * Float(0.01f);
                    rd = outDir;
                };
            };
        }

        // 雾(exp 用 exp2 表达,与 GLSL 版一致)
        Float fog;
        fog = Float(1.f) - exp2((Float(0.f) - firstDepth) * Float(0.004f * 1.442695f));
        Float3 sk;
        sk = sky_color(rd0);
        Float3 outc;
        outc = mix(acc, sk, fog);
        outc = min(outc, Float3(10.f, 10.f, 10.f));
        pt_output[tid] = Float4(outc, Float(1.f));
    };

    // ---- 管线 ----
    Corona::Horizon::RasterizerPipelineDesc geom_desc;
    geom_desc.blend.attachments = { Corona::Horizon::BlendStateDesc::opaque_attachment(),
                                    Corona::Horizon::BlendStateDesc::opaque_attachment(),
                                    Corona::Horizon::BlendStateDesc::opaque_attachment() };

    Corona::Horizon::RasterizerPipeline geom_rasterizer(geom_vert, geom_frag, geom_desc);
    geom_rasterizer.bind_depth_target(scene_depth);

    Corona::Horizon::ComputePipeline linear_depth_compute(linear_depth_cs, ktm::uvec3(8, 8, 1));
    Corona::Horizon::ComputePipeline trace_compute(trace_cs, ktm::uvec3(8, 8, 1));
    Corona::Horizon::ComputePipeline composite_compute(composite_cs, ktm::uvec3(8, 8, 1));
    // PT 管线只在 SSR_PATHTRACE 模式下构造(codegen 成本/风险与 SSSR 解耦)
    const bool pathtrace_env = std::getenv("SSR_PATHTRACE") != nullptr;
    std::optional<decltype(Corona::Horizon::ComputePipeline(pathtrace_cs, ktm::uvec3(8, 8, 1)))> pathtrace_pipe;
    if (pathtrace_env)
        pathtrace_pipe.emplace(pathtrace_cs, ktm::uvec3(8, 8, 1));

    Corona::Horizon::HardwareExecutor render_executor;
    Corona::Horizon::HardwareExecutor display_executor;
    Corona::Horizon::HardwareDisplayer display(glfwGetWin32Window(window));

    Corona::Horizon::DrawIndexedParams cube_params;
    cube_params.index_type = Corona::Horizon::IndexType::UInt32;
    cube_params.index_count = static_cast<uint32_t>(essr_cube_indices.size());

    // ---- 相机：低角度俯视地面，让反射拉出长条（与 GLSL 版一致）----
    constexpr float aspect = static_cast<float>(essr_width) / static_cast<float>(essr_height);
    const glm::vec3 eye(0.0f, 2.6f, -10.5f);
    const glm::vec3 target(0.0f, 0.7f, 0.5f);
    const glm::mat4 view = glm::lookAtLH(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = [] {
        glm::mat4 m = glm::perspectiveLH(glm::radians(60.0f), aspect, essr_near, essr_far);
        m[1][1] *= -1.0f; // Vulkan 裁剪空间 Y 翻转
        return m;
    }();
    const glm::mat4 view_proj = proj * view;

    // 深度/投影系数（同 example_assao）：
    //   viewZ    = p32 / (device_z - p22)
    //   view.xy  = (uv * (2/p00, 2/p11) + (-1/p00, -1/p11)) * viewZ
    const float p00 = proj[0][0];
    const float p11 = proj[1][1];
    const float p22 = proj[2][2];
    const float p32 = proj[3][2];

    // 方向光（世界空间指向光源）→ view 空间
    const glm::vec3 light_dir_world = glm::normalize(glm::vec3(0.45f, 0.85f, -0.35f));
    const glm::vec3 light_dir_vs = glm::normalize(glm::vec3(view * glm::vec4(light_dir_world, 0.0f)));
    constexpr float ambient = 0.22f;

    const uint32_t dispatch_x = (essr_width + 7) / 8;
    const uint32_t dispatch_y = (essr_height + 7) / 8;

    // ---- SSR 可调参数（步进/精修次数已编译进 shader，不可调）----
    bool ssr_enabled = true;
    float max_distance = 12.0f;
    float thickness = 0.55f;
    bool use_jitter = true;
    float fresnel_power = 4.0f;
    float fresnel_f0 = 0.04f;
    float intensity = 1.0f;
    int debug_mode = 0;

    // ---- Path Trace 模式(与 GLSL 版对应)----
    bool pathtrace_mode = std::getenv("SSR_PATHTRACE") != nullptr;
    const glm::vec3 pt_ro(0.0f, 3.2f, -9.0f);
    const glm::vec3 pt_ta(0.0f, 1.0f, 0.0f);
    const glm::vec3 pt_fwd = glm::normalize(pt_ta - pt_ro);
    const glm::vec3 pt_right = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), pt_fwd));
    const glm::vec3 pt_up = glm::cross(pt_fwd, pt_right);
    constexpr float pt_focal = 1.6f;

    HorizonImGuiLayer ui(window, essr_width, essr_height);

    const auto start_time = std::chrono::high_resolution_clock::now();
    auto prev_time = start_time;
    double fps_accum_seconds = 0.0;
    int fps_frame_count = 0;
    uint32_t frame_index = 0;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        const auto now = std::chrono::high_resolution_clock::now();
        const float dt = std::chrono::duration<float>(now - prev_time).count();
        const float time = std::chrono::duration<float>(now - start_time).count();
        prev_time = now;

        ui.new_frame();
        ImGui::Begin("SSR (EDSL)");
        ImGui::Checkbox("Enable SSR", &ssr_enabled);
        ImGui::SliderFloat("Max Distance", &max_distance, 1.0f, 40.0f);
        ImGui::SliderFloat("Thickness", &thickness, 0.02f, 3.0f);
        ImGui::Checkbox("Jitter Start", &use_jitter);
        ImGui::SliderFloat("Fresnel Power", &fresnel_power, 1.0f, 8.0f);
        ImGui::SliderFloat("Fresnel F0", &fresnel_f0, 0.0f, 1.0f);
        ImGui::SliderFloat("Intensity", &intensity, 0.0f, 2.0f);
        ImGui::Combo("Debug View", &debug_mode, "Final\0SSR Color\0SSR Weight\0Reflectivity\0");
        ImGui::Checkbox("Path Trace (Disney)", &pathtrace_mode);
        ImGui::Text("Steps: %d  Refine: %d (compiled in)", kTraceSteps, kRefineSteps);
        ImGui::End();

        fps_accum_seconds += dt;
        ++fps_frame_count;
        if (fps_accum_seconds >= 0.5)
        {
            const double fps = fps_frame_count / fps_accum_seconds;
            char title[160];
            std::snprintf(title, sizeof(title), "Horizon SSR [EDSL]%s - %d steps - %.1f FPS (%.2f ms)",
                          pathtrace_mode ? " PT" : "", kTraceSteps, fps, 1000.0 / fps);
            glfwSetWindowTitle(window, title);
            fps_accum_seconds = 0.0;
            fps_frame_count = 0;
        }

        // ---- 场景：镜面地板 + 4x4 起伏旋转立方体 + 两根立柱（与 GLSL 版一致）----
        std::vector<EssrInstance> instances;
        instances.reserve(32);

        // 地板：压扁的 cube，顶面正好 y=0
        instances.push_back({
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.2f, 0.0f)) *
                glm::scale(glm::mat4(1.0f), glm::vec3(16.0f, 0.2f, 16.0f)),
            glm::vec3(0.10f, 0.11f, 0.13f), 0.88f, 0.05f });

        constexpr int dim = 4;
        constexpr float spacing = 2.9f;
        constexpr float grid_offset = (dim - 1) * spacing * 0.5f;
        const glm::vec3 palette[4] = {
            { 0.90f, 0.32f, 0.28f },
            { 0.98f, 0.76f, 0.24f },
            { 0.32f, 0.72f, 0.55f },
            { 0.38f, 0.55f, 0.92f },
        };
        for (int zz = 0; zz < dim; ++zz)
        {
            for (int xx = 0; xx < dim; ++xx)
            {
                const int idx = zz * dim + xx;
                const float height = 1.05f + std::sin(time * 0.9f + idx * 0.7f) * 0.35f;
                glm::mat4 model = glm::eulerAngleYX(time * 0.35f + idx * 0.4f, time * 0.22f + idx * 0.25f);
                model = glm::translate(glm::mat4(1.0f),
                                       glm::vec3(-grid_offset + xx * spacing, height, -grid_offset + zz * spacing)) *
                        model * glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));
                instances.push_back({ model, palette[idx & 3], 0.14f, 0.38f });
            }
        }

        // 两根立柱：给画面一点竖向结构，反射里更容易看出拉伸
        for (int side = 0; side < 2; ++side)
        {
            const float x = (side == 0) ? -6.4f : 6.4f;
            instances.push_back({
                glm::translate(glm::mat4(1.0f), glm::vec3(x, 2.2f, 2.0f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(0.42f, 2.2f, 0.42f)),
                glm::vec3(0.82f, 0.80f, 0.76f), 0.10f, 0.30f });
        }

        // Pass 1：几何 → G-buffer
        geom_rasterizer.clear_records();
        u_view_proj = to_edsl_matrix(view_proj);
        u_view = to_edsl_matrix(view);
        u_light_dir_vs = ktm::fvec4(light_dir_vs.x, light_dir_vs.y, light_dir_vs.z, ambient);
        for (const EssrInstance& inst : instances)
        {
            pc_model = to_edsl_matrix(inst.model);
            pc_material = ktm::fvec4(inst.albedo.x, inst.albedo.y, inst.albedo.z, 0.0f);
            pc_params = ktm::fvec4(inst.reflectivity, inst.roughness, 0.0f, 0.0f);
            geom_rasterizer.record(cube_ib, cube_vb, cube_params);
        }

        // Pass 2：器件深度 → view 空间线性深度
        u_depth_unpack = ktm::fvec4(p32, -p22, 0.0f, 0.0f);
        u_ld_params = ktm::fvec4(float(essr_width), float(essr_height), essr_far, 0.0f);

        // Pass 3：屏幕空间射线步进
        u_ndc_to_view = ktm::fvec4(2.0f / p00, 2.0f / p11, -1.0f / p00, -1.0f / p11);
        u_tr_params0 = ktm::fvec4(p00, p11, max_distance, float(kTraceSteps));
        u_tr_params1 = ktm::fvec4(thickness, float(frame_index), float(essr_width), float(essr_height));
        u_tr_params2 = ktm::fvec4(fresnel_power, fresnel_f0, float(kRefineSteps), use_jitter ? 1.0f : 0.0f);

        // Pass 4：合成（SSR 关闭时 intensity 置 0，链路照跑便于对比开销）
        u_cp_params = ktm::fvec4(float(essr_width), float(essr_height),
                                 ssr_enabled ? intensity : 0.0f, float(debug_mode));

        // Path Trace 模式:单 compute pass 直写输出
        u_pt0 = ktm::fvec4(pt_ro.x, pt_ro.y, pt_ro.z, float(frame_index));
        u_pt1 = ktm::fvec4(pt_fwd.x, pt_fwd.y, pt_fwd.z, pt_focal);
        u_pt2 = ktm::fvec4(pt_right.x, pt_right.y, pt_right.z, float(essr_width));
        u_pt3 = ktm::fvec4(pt_up.x, pt_up.y, pt_up.z, float(essr_height));

        Corona::Horizon::SubmitReceipt render_receipt;
        if (pathtrace_mode && pathtrace_pipe.has_value())
        {
            render_receipt = render_executor.stream() << (*pathtrace_pipe)(dispatch_x, dispatch_y, 1)
                                                      << Corona::Horizon::submit;
        }
        else
        {
            render_receipt = render_executor << geom_rasterizer(essr_width, essr_height)
                                             << linear_depth_compute(dispatch_x, dispatch_y, 1)
                                             << trace_compute(dispatch_x, dispatch_y, 1)
                                             << composite_compute(dispatch_x, dispatch_y, 1)
                                             << Corona::Horizon::submit;
        }

        ui.draw_overlay(display_executor, final_output_image, render_receipt);
        display_executor.wait(render_receipt);
        (void)(display_executor.stream() << Corona::Horizon::present(display, final_output_image)
                                         << Corona::Horizon::commit());

        ++frame_index;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
