// 屏幕空间反射（SSR）示例的 EDSL 版本，移植自 example_ssr（GLSL 版）。
//
// 材质参数升级：完整 Disney Principled BRDF（对齐 example_disney_pbr）。
// 所有非 albedo 参数通过 imgui 实时可调，以 EDSL proxy 变量（shared uniform）
// 传入 shader，不再是 constexpr 编译期常量。
//
// EDSL 已知限制（与 GLSL 版的差异，不因本次升级改变）：
// - trace/refine 步进循环仍为 C++ 侧定长展开（kTraceSteps/kRefineSteps）；
//   Steps/Refine 因此仍不可通过 imgui 调节。
// - VNDF 采样现已升级为各向异性 GGX（aniso 参数生效）。
// - clearcoat lobe 已加入 trace 的三路 lobe 选择。

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "common.h"
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
constexpr uint32_t essr_width  = 1280;
constexpr uint32_t essr_height = 720;
constexpr float    essr_near   = 0.1f;
constexpr float    essr_far    = 100.0f;

// EDSL 无循环 AST，步进次数只能是编译期常量
constexpr int kTraceSteps  = 48;
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

std::vector<EssrVertex> build_cube_vertices()
{
    struct Face { glm::vec3 normal; glm::vec3 corners[4]; };
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
        for (const glm::vec3& corner : face.corners)
            vertices.push_back({ {corner.x, corner.y, corner.z}, {face.normal.x, face.normal.y, face.normal.z} });
    return vertices;
}

const std::vector<uint16_t> essr_cube_indices = {
    0, 2, 1, 1, 2, 3,   4, 5, 6, 5, 7, 6,   8, 10, 9, 9, 10, 11,
    12, 13, 14, 13, 15, 14,  16, 18, 17, 17, 18, 19,  20, 21, 22, 21, 23, 22,
};

// 完整 Disney 材质（批次共享）
struct DisneyMaterial
{
    float metallic        = 0.5f;
    float roughness       = 0.25f;
    float specular        = 0.5f;
    float specular_tint   = 0.0f;
    float subsurface      = 0.0f;
    float anisotropic     = 0.0f;
    float sheen           = 0.0f;
    float sheen_tint      = 0.5f;
    float clearcoat       = 0.0f;
    float clearcoat_gloss = 1.0f;
};

// 场景物体：仅保留几何变换 + albedo
struct EssrInstance
{
    glm::mat4 model;
    glm::vec3 albedo;
};

void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
    if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}

} // namespace

using namespace EmbeddedShader;

struct EssrVertexIn  { Float3 pos; Float3 normal; };
struct EssrVertOut   { Float3 v_normal_vs; Float3 v_pos_vs; Float4 v_clip; Float4 v_albedo; };

void run_example_edsl_ssr()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(essr_width, essr_height, "Horizon SSR [EDSL]", nullptr, nullptr);
    glfwSetKeyCallback(window, key_callback);

    const std::vector<EssrVertex> cube_vertices = build_cube_vertices();
    horizon::HardwareBuffer cube_vb = horizon::HardwareBuffer::vertex(cube_vertices, "example_edsl_ssr.cube.vb");
    horizon::HardwareBuffer cube_ib = horizon::HardwareBuffer::index(essr_cube_indices, "example_edsl_ssr.cube.ib");

    const auto rt_usage = horizon::ImageUsage_ColorAttachment |
                          horizon::ImageUsage_Sampled |
                          horizon::ImageUsage_Storage;

    horizon::HardwareImage color_image(horizon::HardwareImageDesc::texture_2d(
        essr_width, essr_height, horizon::Format::RGBA16_FLOAT, rt_usage, "example_edsl_ssr.color"));
    color_image.set_clear_color(0.14f, 0.19f, 0.28f, 0.0f);

    horizon::HardwareImage normal_image(horizon::HardwareImageDesc::texture_2d(
        essr_width, essr_height, horizon::Format::RGBA8_UNORM, rt_usage, "example_edsl_ssr.normal"));
    normal_image.set_clear_color(0.5f, 0.5f, 1.0f, 1.0f);

    horizon::HardwareImage depth_val_image(horizon::HardwareImageDesc::texture_2d(
        essr_width, essr_height, horizon::Format::R32_FLOAT, rt_usage, "example_edsl_ssr.depthval"));
    depth_val_image.set_clear_color(1.0f, 0.0f, 0.0f, 0.0f);

    horizon::HardwareImage albedo_met_image(horizon::HardwareImageDesc::texture_2d(
        essr_width, essr_height, horizon::Format::RGBA8_UNORM, rt_usage, "example_edsl_ssr.albedo_met"));
    albedo_met_image.set_clear_color(0.0f, 0.0f, 0.0f, 0.0f);

    horizon::HardwareImage linear_depth_image(horizon::HardwareImageDesc::texture_2d(
        essr_width, essr_height, horizon::Format::R32_FLOAT,
        horizon::ImageUsage_Storage | horizon::ImageUsage_Sampled,
        "example_edsl_ssr.lineardepth"));

    horizon::HardwareImage ssr_image(horizon::HardwareImageDesc::texture_2d(
        essr_width, essr_height, horizon::Format::RGBA16_FLOAT,
        horizon::ImageUsage_Storage | horizon::ImageUsage_Sampled,
        "example_edsl_ssr.ssr"));

    horizon::HardwareImage final_output_image(horizon::HardwareImageDesc::texture_2d(
        essr_width, essr_height, horizon::Format::RGBA16_FLOAT,
        horizon::ImageUsage_Storage | horizon::ImageUsage_ColorAttachment |
            horizon::ImageUsage_Sampled | horizon::ImageUsage_TransferSrc |
            horizon::ImageUsage_TransferDst,
        "example_edsl_ssr.output"));

    horizon::HardwareImage scene_depth(horizon::HardwareImageDesc::depth_attachment(
        essr_width, essr_height, horizon::Format::D32, "example_edsl_ssr.depth"));
    scene_depth.set_clear_depth(1.0f, 0);

    // ================================================================
    // Pass 1：几何 → G-buffer（EDSL 光栅）
    // ================================================================

    // 批次共享 uniform（对应 GLSL vsp）
    Float4x4 u_view_proj;
    Float4x4 u_view;
    Float4   u_light_dir_vs;   // xyz: view 空间指向光源, w: 环境光强度
    // Disney 材质参数（全部运行期 proxy，不再是 constexpr）
    Float4   u_disney_a;       // x=metallic, y=roughness, z=specular, w=specular_tint
    Float4   u_disney_b;       // x=subsurface, y=anisotropic, z=sheen, w=sheen_tint
    Float4   u_disney_c;       // x=clearcoat, y=clearcoat_gloss

    // per-draw push constant
    Float4x4 pc_model;
    pc_model.as_push_constant();
    Float4 pc_material;    // rgb: albedo, w: 未用
    pc_material.as_push_constant();

    Texture2D<ktm::fvec4> gbuf_out_color      = color_image;
    Texture2D<ktm::fvec4> gbuf_out_normal     = normal_image;
    Texture2D<float>      gbuf_out_depthval   = depth_val_image;
    Texture2D<ktm::fvec4> gbuf_out_albedo_met = albedo_met_image;

    auto geom_vert = [&](Aggregate<EssrVertexIn> vin) {
        Aggregate<EssrVertOut> out;
        Float4x4 mv; mv = mul(u_view, pc_model);
        Float4 clip; clip = mul(mul(u_view_proj, pc_model), Float4(vin->pos, 1.f));
        position() = clip;
        out->v_pos_vs    = mul(mv, Float4(vin->pos, 1.f))->xyz();
        out->v_normal_vs = normalize(mul(mv, Float4(vin->normal, 0.f))->xyz());
        out->v_clip      = clip;
        Float3 alb       = pc_material->xyz();
        out->v_albedo    = Float4(alb, 0.f);
        return out;
    };

    // sqr helper（EDSL 内联 lambda）
    auto sqr_f = [](Float x) { return x * x; };

    auto geom_frag = [&](Aggregate<EssrVertOut> in) {
        Float3 n; n = normalize(in->v_normal_vs);
        Float3 l; l = normalize(u_light_dir_vs->xyz());
        Float3 v; v = Float3(0.f, 0.f, 0.f) - normalize(in->v_pos_vs);

        Float3 albedo; albedo = Float3(in->v_albedo->xyz());
        Float metallic;       metallic       = u_disney_a->x;
        Float roughness;      roughness      = clamp(u_disney_a->y, Float(0.001f), Float(1.0f));
        Float specular;       specular       = u_disney_a->z;
        Float specTint;       specTint       = u_disney_a->w;
        Float subsurface;     subsurface     = u_disney_b->x;
        Float aniso;          aniso          = u_disney_b->y;
        Float sheen;          sheen          = u_disney_b->z;
        Float sheenTint;      sheenTint      = u_disney_b->w;
        Float clearcoat;      clearcoat      = u_disney_c->x;
        Float ccGloss;        ccGloss        = u_disney_c->y;

        // sRGB → linear（pow 2.2）
        Float3 Cdlin; Cdlin = pow(abs(albedo), Float3(2.2f, 2.2f, 2.2f));
        Float  Cdlum; Cdlum = max(dot(Cdlin, Float3(0.3f, 0.6f, 0.1f)), Float(1e-4f));
        Float3 Ctint; Ctint = Cdlin / Cdlum;
        Float3 Cspec0;
        Cspec0 = mix(specular * Float(0.08f) * mix(Float3(1.f,1.f,1.f), Ctint, specTint), Cdlin, metallic);
        Float3 Csheen; Csheen = mix(Float3(1.f,1.f,1.f), Ctint, sheenTint);

        // 各向异性切线帧（view 空间 up=(0,1,0)×N）
        Float3 upv; upv = Float3(0.f, 1.f, 0.f);
        $IF(abs(n->y) > Float(0.99f)) { upv = Float3(1.f, 0.f, 0.f); };
        auto edsl_cross3 = [&](Float3 ca, Float3 cb) {
            Float3 cc; cc = Float3(ca->y*cb->z - ca->z*cb->y,
                                   ca->z*cb->x - ca->x*cb->z,
                                   ca->x*cb->y - ca->y*cb->x); return cc; };
        Float3 Xv; Xv = normalize(edsl_cross3(upv, n));
        Float3 Yv; Yv = edsl_cross3(n, Xv);

        Float NdotL; NdotL = dot(n, l);
        Float NdotV; NdotV = dot(n, v);

        Float3 lit; lit = Float3(0.f, 0.f, 0.f);
        $IF((NdotL >= Float(0.f)) && (NdotV >= Float(0.f)))
        {
            Float3 h; h = normalize(l + v);
            Float NdotH; NdotH = dot(n, h);
            Float LdotH; LdotH = dot(l, h);

            auto SchlickF = [&](Float u) {
                Float m; m = clamp(Float(1.f) - u, Float(0.f), Float(1.f));
                return m * m * m * m * m; };

            Float FL; FL = SchlickF(NdotL);
            Float FV; FV = SchlickF(NdotV);
            Float Fd90; Fd90 = Float(0.5f) + Float(2.f) * LdotH * LdotH * roughness;
            Float Fd; Fd = (Float(1.f) + (Fd90 - Float(1.f)) * FL) *
                           (Float(1.f) + (Fd90 - Float(1.f)) * FV);

            Float Fss90; Fss90 = LdotH * LdotH * roughness;
            Float Fss; Fss = (Float(1.f) + (Fss90 - Float(1.f)) * FL) *
                             (Float(1.f) + (Fss90 - Float(1.f)) * FV);
            Float ss; ss = Float(1.25f) * (Fss * (Float(1.f) / (NdotL + NdotV) - Float(0.5f)) + Float(0.5f));

            Float aspect; aspect = sqrt(Float(1.f) - aniso * Float(0.9f));
            Float ax; ax = max(Float(0.001f), sqr_f(roughness) / aspect);
            Float ay; ay = max(Float(0.001f), sqr_f(roughness) * aspect);

            // GTR2 aniso
            Float HdotX; HdotX = dot(h, Xv);
            Float HdotY; HdotY = dot(h, Yv);
            Float Ds; Ds = Float(1.f) / (Float(3.14159265f) * ax * ay *
                sqr_f(sqr_f(HdotX / ax) + sqr_f(HdotY / ay) + NdotH * NdotH));

            Float FH; FH = SchlickF(LdotH);
            Float3 Fs; Fs = mix(Cspec0, Float3(1.f,1.f,1.f), FH);

            // Smith G aniso
            auto smithAniso = [&](Float NdotW, Float WdotX, Float WdotY, Float axx, Float ayy) {
                return Float(1.f) / (NdotW + sqrt(sqr_f(WdotX*axx) + sqr_f(WdotY*ayy) + sqr_f(NdotW))); };
            Float Gs; Gs = smithAniso(NdotL, dot(l,Xv), dot(l,Yv), ax, ay) *
                           smithAniso(NdotV, dot(v,Xv), dot(v,Yv), ax, ay);

            Float3 Fsheen; Fsheen = FH * sheen * Csheen;

            // clearcoat GTR1
            Float ac_alpha; ac_alpha = mix(Float(0.1f), Float(0.001f), ccGloss);
            Float a2c; a2c = ac_alpha * ac_alpha;
            Float Dr;
            $IF(ac_alpha >= Float(1.f)) Dr = Float(1.f / 3.14159265f);
            $ELSE {
                Float tc; tc = Float(1.f) + (a2c - Float(1.f)) * NdotH * NdotH;
                Dr = (a2c - Float(1.f)) / (Float(3.14159265f) * log(a2c) * tc); };
            Float Fr_cc; Fr_cc = mix(Float(0.04f), Float(1.f), FH);
            auto smithGGX = [&](Float NdotW, Float alphaG) {
                Float ag; ag = alphaG * alphaG; Float bg; bg = NdotW * NdotW;
                return Float(1.f) / (NdotW + sqrt(ag + bg - ag * bg)); };
            Float Gr; Gr = smithGGX(NdotL, Float(0.25f)) * smithGGX(NdotV, Float(0.25f));

            Float3 diffPart; diffPart = (mix(Fd, ss, subsurface) * Float(1.f/3.14159265f) * Cdlin + Fsheen)
                                       * (Float(1.f) - metallic);
            Float3 specPart; specPart = Gs * Fs * Ds + Float(0.25f) * clearcoat * Gr * Fr_cc * Dr;
            lit = (diffPart + specPart) * NdotL;
        };
        lit = lit + albedo * u_light_dir_vs->w;   // ambient

        gbuf_out_color      << Float4(lit, Float(1.0f));
        gbuf_out_normal     << Float4(n * Float(0.5f) + Float(0.5f), roughness);
        gbuf_out_depthval   << (in->v_clip->z / in->v_clip->w);
        gbuf_out_albedo_met << Float4(albedo, metallic);
    };

    // ================================================================
    // Pass 2：器件深度 → view 空间线性深度
    // ================================================================

    Texture2D<float> ld_depth_in  = depth_val_image;
    Texture2D<float> ld_linear_out = linear_depth_image;
    Float4 u_depth_unpack; // x=p32, y=-p22
    Float4 u_ld_params;    // z: far

    auto linear_depth_cs = [&] {
        auto coord = dispatchThreadID()->xy();
        Float device_z = ld_depth_in[coord];
        Float denom; denom = device_z + u_depth_unpack->y;
        Float view_z = u_ld_params->z;
        $IF(abs(denom) >= 1e-6f) view_z = u_depth_unpack->x / denom;
        ld_linear_out[coord] = clamp(view_z, 0.f, Float(u_ld_params->z));
    };

    // ================================================================
    // Pass 3：屏幕空间射线步进（完整 Disney BRDF 三 lobe 采样）
    // ================================================================

    Texture2D<float>      tr_linear_depth = linear_depth_image;
    Texture2D<ktm::fvec4> tr_normal       = normal_image;
    Texture2D<ktm::fvec4> tr_color        = color_image;
    Texture2D<ktm::fvec4> tr_ssr_out      = ssr_image;
    Texture2D<ktm::fvec4> tr_albedo_met   = albedo_met_image;

    Float4 u_ndc_to_view;
    Float4 u_tr_params0;  // x=p00, y=p11, z=maxDistance
    Float4 u_tr_params1;  // x=thickness, y=frameIdx
    // Disney 参数（运行期 proxy，与 GLSL 版 push_constants 对应）
    Float4 u_tr_disney_a; // x=metallic(from gbuf), y=roughness(from gbuf) – 保留对齐，实际从gbuf读
    Float4 u_tr_disney_b; // x=specular, y=subsurface — 对应params2.z/w
    Float4 u_tr_disney_c; // x=specTint, y=aniso, z=sheen, w=sheenTint
    Float4 u_tr_disney_d; // x=clearcoat, y=ccGloss
    Float  u_tr_use_jitter;

    auto view_to_uv = [&](Float3 p) -> Float2 {
        Float inv_z; inv_z = Float(1.f) / p->z;
        return Float2(u_tr_params0->x * p->x * inv_z * Float(0.5f) + Float(0.5f),
                      u_tr_params0->y * p->y * inv_z * Float(0.5f) + Float(0.5f));
    };

    auto uv_to_coord = [&](Float2 uv) -> Uint2 {
        Float px; px = clamp(uv->x * Float(float(essr_width)),  0.f, float(essr_width)  - 1.f);
        Float py; py = clamp(uv->y * Float(float(essr_height)), 0.f, float(essr_height) - 1.f);
        return Uint2(px, py);
    };

    auto edge01 = [&](Float x, float e0, float e1) {
        Float t; t = clamp((x - Float(e0)) * Float(1.0f / (e1 - e0)), 0.f, 1.f);
        return t * t * (Float(3.0f) - 2.0f * t);
    };

    auto trace_cs = [&] {
        Uint2 tid = dispatchThreadID()->xy();
        tr_ssr_out[tid] = Float4(0.f, 0.f, 0.f, 0.f);

        Float2 uv0;
        uv0 = (Float2(tid->x, tid->y) + Float2(0.5f, 0.5f)) *
              Float2(1.0f / float(essr_width), 1.0f / float(essr_height));

        Float4 center_color; center_color = tr_color[tid];
        $IF(center_color->w > 0.5f)
        {
            Float view_z; view_z = tr_linear_depth[tid];
            Float3 view_pos;
            view_pos = Float3((uv0->x * u_ndc_to_view->x + u_ndc_to_view->z) * view_z,
                              (uv0->y * u_ndc_to_view->y + u_ndc_to_view->w) * view_z,
                              view_z);

            Float4 packed_normal; packed_normal = tr_normal[tid];
            Float3 n; n = normalize(packed_normal->xyz() * Float(2.0f) - Float3(1.f,1.f,1.f));
            Float roughness; roughness = packed_normal->w;

            Float4 albedo_met; albedo_met = tr_albedo_met[tid];
            Float3 albedo; albedo = Float3(albedo_met->xyz());
            Float metallic; metallic = albedo_met->w;

            // Disney 批次共享参数
            Float specular;   specular   = u_tr_disney_b->x;
            Float subsurface; subsurface = u_tr_disney_b->y;
            Float specTint;   specTint   = u_tr_disney_c->x;
            Float aniso;      aniso      = u_tr_disney_c->y;
            Float sheen_v;    sheen_v    = u_tr_disney_c->z;
            Float sheenTint;  sheenTint  = u_tr_disney_c->w;
            Float clearcoat;  clearcoat  = u_tr_disney_d->x;
            Float ccGloss;    ccGloss    = u_tr_disney_d->y;

            // PRNG
            Float seed;
            {
                Float2 pixc; pixc = Float2(tid->x, tid->y);
                seed = fract(sin(pixc->x * Float(12.9898f) + pixc->y * Float(78.233f)) *
                             Float(43758.5453f) + u_tr_params1->y * Float(0.6180339887f));
            }
            auto frand = [&](Float& st) {
                st = fract(sin(st * Float(91.3458f) + Float(47.9898f)) * Float(43758.5453123f));
                Float r; r = st; return r;
            };
            auto schlick5 = [&](Float u) {
                Float m; m = clamp(Float(1.f)-u, Float(0.f), Float(1.f));
                Float m2; m2 = m*m; Float r; r = m2*m2*m; return r;
            };
            auto cross3 = [&](Float3 ca, Float3 cb) {
                Float3 cc; cc = Float3(ca->y*cb->z-ca->z*cb->y,
                                       ca->z*cb->x-ca->x*cb->z,
                                       ca->x*cb->y-ca->y*cb->x); return cc; };
            auto sqr3 = [](Float x) { return x * x; };

            // Fresnel F0（完整 Disney）
            Float3 Cdlin; Cdlin = pow(abs(albedo), Float3(2.2f,2.2f,2.2f));
            Float  Cdlum; Cdlum = max(dot(Cdlin, Float3(0.3f,0.6f,0.1f)), Float(1e-4f));
            Float3 Ctint; Ctint = Cdlin / Cdlum;
            Float3 Cspec0;
            Cspec0 = mix(specular * Float(0.08f) * mix(Float3(1.f,1.f,1.f), Ctint, specTint),
                         Cdlin, metallic);
            Float3 Csheen; Csheen = mix(Float3(1.f,1.f,1.f), Ctint, sheenTint);

            // 切线帧
            Float3 upv; upv = Float3(0.f,1.f,0.f);
            $IF(abs(n->y) > Float(0.99f)) { upv = Float3(1.f,0.f,0.f); };
            Float3 tb; tb = normalize(cross3(upv, n));
            Float3 bb; bb = cross3(n, tb);

            // 各向异性参数
            Float aspect; aspect = sqrt(Float(1.f) - aniso * Float(0.9f));
            Float ax; ax = max(Float(0.001f), sqr3(roughness) / aspect);
            Float ay; ay = max(Float(0.001f), sqr3(roughness) * aspect);

            Float3 v; v = normalize(view_pos);
            Float3 wo; wo = Float3(0.f,0.f,0.f) - v;

            // 各向异性 VNDF 采样半向量
            Float3 Vl; Vl = Float3(dot(wo,tb), dot(wo,bb), dot(wo,n));
            Float r1; r1 = frand(seed); Float r2; r2 = frand(seed);
            Float3 Vh; Vh = normalize(Float3(Vl->x*ax, Vl->y*ay, Vl->z));
            Float lq; lq = Vh->x*Vh->x + Vh->y*Vh->y;
            Float3 T1; T1 = Float3(1.f,0.f,0.f);
            $IF(lq > Float(1e-7f)) {
                Float invl; invl = Float(1.f)/sqrt(lq);
                T1 = Float3(Float(0.f)-Vh->y, Vh->x, Float(0.f)) * invl;
            };
            Float3 T2; T2 = cross3(Vh, T1);
            Float rrs; rrs = sqrt(r1);
            Float phi; phi = r2 * Float(6.28318530718f);
            Float t1c; t1c = rrs * cos(phi);
            Float t2c; t2c = rrs * sin(phi);
            Float sblend; sblend = Float(0.5f) * (Float(1.f) + Vh->z);
            t2c = (Float(1.f)-sblend)*sqrt(max(Float(0.f), Float(1.f)-t1c*t1c)) + sblend*t2c;
            Float nzc; nzc = sqrt(max(Float(0.f), Float(1.f)-t1c*t1c-t2c*t2c));
            Float3 Nh; Nh = T1*t1c + T2*t2c + Vh*nzc;
            Float3 hl; hl = normalize(Float3(Nh->x*ax, Nh->y*ay, max(Float(0.f), Nh->z)));
            $IF(hl->z < Float(0.f)) { hl = Float3(0.f,0.f,0.f) - hl; };
            Float3 hw; hw = tb*hl->x + bb*hl->y + n*hl->z;

            // Fresnel & lobe weights
            Float vhd; vhd = dot(wo, hw);
            Float3 Fr_h; Fr_h = Cspec0 + (Float3(1.f,1.f,1.f)-Cspec0) * schlick5(vhd);
            Float specW; specW = dot(Fr_h, Float3(0.299f,0.587f,0.114f));
            Float diffW; diffW = Float(1.f) - metallic;
            Float ccW;   ccW   = clearcoat * Float(0.25f);
            Float totalW; totalW = diffW + specW + ccW;
            Float invTW; invTW = Float(1.f) / max(totalW, Float(1e-6f));
            diffW = diffW * invTW; specW = specW * invTW; ccW = ccW * invTW;

            Float3 ray_dir; ray_dir = v - n*(Float(2.f)*dot(n,v));
            Float3 brdfRGB; brdfRGB = Float3(0.f,0.f,0.f);
            Float pdfW; pdfW = Float(0.f);

            Float rnd; rnd = frand(seed);
            $IF(rnd < diffW)
            {
                // diffuse lobe：余弦半球
                Float zc; zc = frand(seed)*Float(2.f)-Float(1.f);
                Float phid; phid = frand(seed)*Float(6.28318530718f);
                Float rc; rc = sqrt(max(Float(0.f), Float(1.f)-zc*zc));
                Float3 sp; sp = Float3(rc*cos(phid), rc*sin(phid), zc);
                ray_dir = normalize(n*Float(1.0001f) + sp);
                Float3 hh; hh = normalize(ray_dir + wo);
                Float NoL; NoL = dot(n, ray_dir);
                Float NoV; NoV = dot(n, wo);
                $IF((NoL > Float(0.f)) && (NoV > Float(0.f)))
                {
                    Float LoH; LoH = dot(ray_dir, hh);
                    Float pdf; pdf = NoL * Float(0.31830988618f);
                    Float FD90; FD90 = Float(0.5f) + Float(2.f)*roughness*LoH*LoH;
                    Float fa; fa = Float(1.f) + (FD90-Float(1.f))*schlick5(NoL);
                    Float fb; fb = Float(1.f) + (FD90-Float(1.f))*schlick5(NoV);
                    Float Fss90v; Fss90v = LoH*LoH*roughness;
                    Float Fssv; Fssv = (Float(1.f)+(Fss90v-Float(1.f))*schlick5(NoL)) *
                                      (Float(1.f)+(Fss90v-Float(1.f))*schlick5(NoV));
                    Float ssv; ssv = Float(1.25f)*(Fssv*(Float(1.f)/(NoL+NoV)-Float(0.5f))+Float(0.5f));
                    Float3 diffv; diffv = Cdlin * (mix(fa*fb, ssv, subsurface) * Float(0.31830988618f));
                    Float FH_d; FH_d = schlick5(LoH);
                    Float3 Fr_d; Fr_d = Cspec0 + (Float3(1.f,1.f,1.f)-Cspec0)*FH_d;
                    Float3 Fsh; Fsh = FH_d * sheen_v * Csheen;
                    brdfRGB = (diffv * (Float3(1.f,1.f,1.f)-Fr_d) + Fsh) * NoL;
                    pdfW = diffW * pdf;
                };
            }
            $ELSE { $IF(rnd < diffW + specW)
            {
                // aniso specular lobe
                Float3 Iv; Iv = Float3(0.f,0.f,0.f) - wo;
                Float dnh; dnh = dot(hw, Iv);
                ray_dir = Iv - hw*(Float(2.f)*dnh);
                Float NoL; NoL = dot(n, ray_dir);
                Float NoV; NoV = dot(n, wo);
                $IF((NoL > Float(0.f)) && (NoV > Float(0.f)))
                {
                    Float NoH; NoH = min(dot(n,hw), Float(0.99f));
                    Float HdotX; HdotX = dot(hw, tb); Float HdotY; HdotY = dot(hw, bb);
                    Float Ds_v; Ds_v = Float(1.f)/(Float(3.14159265f)*ax*ay*
                        sqr3(sqr3(HdotX/ax)+sqr3(HdotY/ay)+NoH*NoH));
                    Float pdf; pdf = Ds_v * NoH / max(Float(4.f)*NoV, Float(1e-5f));
                    auto sAniso = [&](Float NdotW, Float WdotX, Float WdotY, Float axx, Float ayy) {
                        return Float(1.f)/(NdotW+sqrt(sqr3(WdotX*axx)+sqr3(WdotY*ayy)+sqr3(NdotW))); };
                    Float Gs_v; Gs_v = sAniso(NoL,dot(ray_dir,tb),dot(ray_dir,bb),ax,ay) *
                                      sAniso(NoV,dot(wo,tb),dot(wo,bb),ax,ay);
                    brdfRGB = Fr_h * (Ds_v * Gs_v / max(Float(4.f)*NoL*NoV, Float(1e-5f))) * NoL;
                    pdfW = specW * pdf;
                };
            }
            $ELSE
            {
                // clearcoat lobe（GTR1）
                Float ac_a; ac_a = mix(Float(0.1f), Float(0.001f), ccGloss);
                Float3 Vl_cc; Vl_cc = Float3(dot(wo,tb),dot(wo,bb),dot(wo,n));
                Float3 Vh_cc; Vh_cc = normalize(Float3(Vl_cc->x*ac_a, Vl_cc->y*ac_a, Vl_cc->z));
                Float lq_cc; lq_cc = Vh_cc->x*Vh_cc->x+Vh_cc->y*Vh_cc->y;
                Float3 T1c; T1c = Float3(1.f,0.f,0.f);
                $IF(lq_cc > Float(1e-7f)) {
                    Float invlc; invlc = Float(1.f)/sqrt(lq_cc);
                    T1c = Float3(Float(0.f)-Vh_cc->y, Vh_cc->x, Float(0.f)) * invlc;
                };
                Float3 T2c; T2c = cross3(Vh_cc, T1c);
                Float r1c; r1c = frand(seed); Float r2c; r2c = frand(seed);
                Float rrc; rrc = sqrt(r1c);
                Float phc; phc = r2c * Float(6.28318530718f);
                Float t1cc; t1cc = rrc*cos(phc); Float t2cc; t2cc = rrc*sin(phc);
                Float sbc; sbc = Float(0.5f)*(Float(1.f)+Vh_cc->z);
                t2cc = (Float(1.f)-sbc)*sqrt(max(Float(0.f),Float(1.f)-t1cc*t1cc))+sbc*t2cc;
                Float nzcc; nzcc = sqrt(max(Float(0.f),Float(1.f)-t1cc*t1cc-t2cc*t2cc));
                Float3 Nhc; Nhc = T1c*t1cc + T2c*t2cc + Vh_cc*nzcc;
                Float3 hlc; hlc = normalize(Float3(Nhc->x*ac_a, Nhc->y*ac_a, max(Float(0.f),Nhc->z)));
                $IF(hlc->z < Float(0.f)) { hlc = Float3(0.f,0.f,0.f)-hlc; };
                Float3 hwc; hwc = tb*hlc->x + bb*hlc->y + n*hlc->z;
                Float3 Ivc; Ivc = Float3(0.f,0.f,0.f)-wo;
                Float dnhc; dnhc = dot(hwc, Ivc);
                ray_dir = Ivc - hwc*(Float(2.f)*dnhc);
                Float NoLc; NoLc = dot(n, ray_dir); Float NoVc; NoVc = dot(n, wo);
                $IF((NoLc > Float(0.f)) && (NoVc > Float(0.f)))
                {
                    Float NoHc; NoHc = min(dot(n,hwc), Float(0.99f));
                    Float a2c; a2c = ac_a*ac_a;
                    Float Dr; // GTR1
                    $IF(ac_a >= Float(1.f)) Dr = Float(1.f/3.14159265f);
                    $ELSE {
                        Float tc; tc = Float(1.f)+(a2c-Float(1.f))*NoHc*NoHc;
                        Dr = (a2c-Float(1.f))/(Float(3.14159265f)*log(a2c)*tc); };
                    Float FHc; FHc = schlick5(dot(ray_dir,hwc));
                    Float Frc; Frc = mix(Float(0.04f), Float(1.f), FHc);
                    auto smithGv = [&](Float NdotW, Float alphaG) {
                        Float agv; agv = alphaG*alphaG; Float bgv; bgv = NdotW*NdotW;
                        return Float(1.f)/(NdotW+sqrt(agv+bgv-agv*bgv)); };
                    Float Grc; Grc = smithGv(NoLc, Float(0.25f))*smithGv(NoVc, Float(0.25f));
                    Float pdf_cc; pdf_cc = Dr * NoHc / max(Float(4.f)*NoVc, Float(1e-5f));
                    brdfRGB = Float3(Float(0.25f)*clearcoat*Frc*Grc*Dr /
                                    max(Float(4.f)*NoLc*NoVc, Float(1e-5f))) * NoLc;
                    pdfW = ccW * pdf_cc;
                };
            };};

            $IF(pdfW > Float(0.f))
            {
                Float3 throughput;
                {
                    Float invp; invp = Float(1.f)/pdfW;
                    throughput = min(brdfRGB * invp, Float3(4.f,4.f,4.f));
                }
                Float step_len; step_len = u_tr_params0->z * Float(1.0f / float(kTraceSteps));
                Float3 ray_step; ray_step = ray_dir * step_len;
                Float3 sample_pos; sample_pos = view_pos + n*(step_len*Float(0.5f));

                Float2 noise_p = Float2(tid->x, tid->y) + Float2(314.0f, 159.0f)*Float(u_tr_params1->y);
                Float rand01 = fract(Float(52.9829189f)*fract(dot(noise_p, Float2(0.06711056f, 0.00583715f))));
                Float initial_offset = mix(Float(1.f), Float(0.5f)+rand01, Float(u_tr_use_jitter));
                sample_pos = sample_pos + ray_step * initial_offset;

                Float thickness_v = u_tr_params1->x;
                Bool marching = true; Bool hit = false;
                Float hit_step = float(kTraceSteps);
                Float3 prev_pos = sample_pos;

                for (int i = 0; i < kTraceSteps; ++i)
                {
                    $IF(marching)
                    {
                        $IF(sample_pos->z <= 0.01f) marching = false;
                        $ELSE {
                            Float2 s_uv; s_uv = view_to_uv(sample_pos);
                            $IF(any(s_uv < Float2(0.f,0.f)) || any(s_uv > Float2(1.f,1.f)))
                                marching = false;
                            $ELSE {
                                Float scene_z; scene_z = tr_linear_depth[uv_to_coord(s_uv)];
                                Float delta; delta = sample_pos->z - scene_z;
                                $IF((delta > 1e-4f) && (delta < thickness_v))
                                { hit = true; hit_step = float(i); marching = false; }
                                $ELSE { prev_pos = sample_pos; sample_pos = sample_pos + ray_step; }
                            }
                        }
                    }
                }

                $IF(hit)
                {
                    Float3 lo = prev_pos; Float3 hi = sample_pos;
                    for (int k = 0; k < kRefineSteps; ++k)
                    {
                        Float3 mid; mid = (lo + hi) * Float(0.5f);
                        Float mid_scene_z; mid_scene_z = tr_linear_depth[uv_to_coord(view_to_uv(mid))];
                        $IF(mid->z > mid_scene_z) hi = mid; $ELSE lo = mid;
                    }
                    Float2 hit_uv; hit_uv = view_to_uv(hi);
                    Float3 refl_color; refl_color = tr_color[uv_to_coord(hit_uv)]->xyz();

                    Float fade_x = edge01(hit_uv->x,0.0f,0.12f)*(Float(1.f)-edge01(hit_uv->x,0.88f,1.0f));
                    Float fade_y = edge01(hit_uv->y,0.0f,0.12f)*(Float(1.f)-edge01(hit_uv->y,0.88f,1.0f));
                    Float edge_fade = fade_x * fade_y;
                    Float dist_fade = mix(Float(0.45f), Float(1.f),
                                         Float(1.f)-clamp(hit_step*Float(1.0f/float(kTraceSteps)),0.f,1.f));
                    Float confidence; confidence = edge_fade * dist_fade;
                    Float3 bounce; bounce = refl_color * throughput * confidence;
                    tr_ssr_out[tid] = Float4(bounce, clamp(confidence, 0.f, 1.f));
                }
            }; // $IF pdfW > 0
        }
    };

    // ================================================================
    // Pass 4：合成
    // ================================================================

    Texture2D<ktm::fvec4> cp_color  = color_image;
    Texture2D<ktm::fvec4> cp_ssr    = ssr_image;
    Texture2D<ktm::fvec4> cp_output = final_output_image;
    Float4 u_cp_params; // z=intensity, w=debug

    auto composite_cs = [&] {
        auto coord = dispatchThreadID()->xy();
        Float4 base; base = cp_color[coord];
        Float4 ssr;  ssr  = cp_ssr[coord];
        Float3 result; result = Float3(base->xyz()) + Float3(ssr->xyz()) * u_cp_params->z;
        Float mode = u_cp_params->w;
        $IF((mode > 0.5f) && (mode < 1.5f)) result = ssr->xyz();
        $IF((mode > 1.5f) && (mode < 2.5f)) result = Float3(ssr->w, ssr->w, ssr->w);
        $IF(mode > 2.5f) result = Float3(base->xyz());
        cp_output[coord] = Float4(result, 1.f);
    };

    // ================================================================
    // 管线构造
    // ================================================================
    horizon::RasterizerPipelineDesc geom_desc;
    geom_desc.blend_enabled = false;

    horizon::RasterizerPipeline geom_rasterizer(geom_vert, geom_frag, geom_desc);
    geom_rasterizer.bind_depth_target(scene_depth);

    horizon::ComputePipeline linear_depth_compute(linear_depth_cs, ktm::uvec3(8, 8, 1));
    horizon::ComputePipeline trace_compute(trace_cs, ktm::uvec3(8, 8, 1));
    horizon::ComputePipeline composite_compute(composite_cs, ktm::uvec3(8, 8, 1));

    const bool pathtrace_env = std::getenv("SSR_PATHTRACE") != nullptr;

    horizon::HardwareExecutor render_executor;
    horizon::HardwareExecutor display_executor;
    horizon::HardwareDisplayer display(glfwGetWin32Window(window));

    horizon::DrawIndexedParams cube_params;
    cube_params.index_count = static_cast<uint32_t>(essr_cube_indices.size());

    // ---- 相机（与 GLSL 版一致）----
    constexpr float aspect = static_cast<float>(essr_width) / static_cast<float>(essr_height);
    const glm::vec3 eye(0.0f, 2.6f, -10.5f);
    const glm::vec3 target_pt(0.0f, 0.7f, 0.5f);
    const glm::mat4 view = glm::lookAtLH(eye, target_pt, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = [] {
        glm::mat4 m = glm::perspectiveLH(glm::radians(60.0f), aspect, essr_near, essr_far);
        m[1][1] *= -1.0f;
        return m;
    }();
    const glm::mat4 view_proj_mat = proj * view;

    const float p00 = proj[0][0], p11 = proj[1][1];
    const float p22 = proj[2][2], p32 = proj[3][2];

    const glm::vec3 light_dir_world = glm::normalize(glm::vec3(0.45f, 0.85f, -0.35f));
    const glm::vec3 light_dir_vs    = glm::normalize(glm::vec3(view * glm::vec4(light_dir_world, 0.0f)));
    constexpr float ambient         = 0.22f;


    // ---- Disney 材质（全局共享，imgui 可调）----
    DisneyMaterial mat;

    // ---- SSR 可调参数 ----
    bool  ssr_enabled  = true;
    float max_distance = 12.0f;
    float thickness    = 0.55f;
    bool  use_jitter   = true;
    float intensity    = 1.0f;
    int   debug_mode   = 0;

    bool pathtrace_mode = pathtrace_env;

    HorizonImGuiLayer ui(window, essr_width, essr_height);

    const auto start_time = std::chrono::high_resolution_clock::now();
    auto prev_time = start_time;
    double   fps_accum_seconds = 0.0;
    int      fps_frame_count   = 0;
    uint32_t frame_index       = 0;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        const auto  now  = std::chrono::high_resolution_clock::now();
        const float dt   = std::chrono::duration<float>(now - prev_time).count();
        const float time = std::chrono::duration<float>(now - start_time).count();
        prev_time = now;

        ui.new_frame();
        ImGui::Begin("SSR (EDSL)");

        ImGui::SeparatorText("SSR");
        ImGui::Checkbox("Enable SSR", &ssr_enabled);
        ImGui::SliderFloat("Max Distance", &max_distance, 1.0f, 40.0f);
        ImGui::SliderFloat("Thickness",    &thickness,    0.02f, 3.0f);
        ImGui::Checkbox("Jitter Start",    &use_jitter);
        ImGui::SliderFloat("Intensity",    &intensity,    0.0f,  2.0f);
        ImGui::Combo("Debug View", &debug_mode, "Final\0SSR Color\0SSR Weight\0Reflectivity\0");
        ImGui::Checkbox("Path Trace (Disney)", &pathtrace_mode);
        ImGui::Text("Steps: %d  Refine: %d (compiled in)", kTraceSteps, kRefineSteps);

        ImGui::SeparatorText("Disney Material (shared)");
        ImGui::SliderFloat("Metallic",        &mat.metallic,        0.0f, 1.0f);
        ImGui::SliderFloat("Roughness",       &mat.roughness,       0.0f, 1.0f);
        ImGui::SliderFloat("Specular",        &mat.specular,        0.0f, 1.0f);
        ImGui::SliderFloat("Specular Tint",   &mat.specular_tint,   0.0f, 1.0f);
        ImGui::SliderFloat("Subsurface",      &mat.subsurface,      0.0f, 1.0f);
        ImGui::SliderFloat("Anisotropic",     &mat.anisotropic,     0.0f, 1.0f);
        ImGui::SliderFloat("Sheen",           &mat.sheen,           0.0f, 1.0f);
        ImGui::SliderFloat("Sheen Tint",      &mat.sheen_tint,      0.0f, 1.0f);
        ImGui::SliderFloat("Clearcoat",       &mat.clearcoat,       0.0f, 1.0f);
        ImGui::SliderFloat("Clearcoat Gloss", &mat.clearcoat_gloss, 0.0f, 1.0f);

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
            fps_frame_count   = 0;
        }

        // ---- 场景 ----
        std::vector<EssrInstance> instances;
        instances.reserve(32);
        instances.push_back({
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.2f, 0.0f)) *
                glm::scale(glm::mat4(1.0f), glm::vec3(16.0f, 0.2f, 16.0f)),
            glm::vec3(0.10f, 0.11f, 0.13f) });

        constexpr int   dim         = 4;
        constexpr float spacing     = 2.9f;
        constexpr float grid_offset = (dim - 1) * spacing * 0.5f;
        const glm::vec3 palette[4]  = {
            { 0.90f, 0.32f, 0.28f }, { 0.98f, 0.76f, 0.24f },
            { 0.32f, 0.72f, 0.55f }, { 0.38f, 0.55f, 0.92f },
        };
        for (int zz = 0; zz < dim; ++zz)
        {
            for (int xx = 0; xx < dim; ++xx)
            {
                const int   idx    = zz * dim + xx;
                const float height = 1.05f + std::sin(time * 0.9f + idx * 0.7f) * 0.35f;
                glm::mat4 model = glm::eulerAngleYX(time * 0.35f + idx * 0.4f, time * 0.22f + idx * 0.25f);
                model = glm::translate(glm::mat4(1.0f),
                            glm::vec3(-grid_offset + xx * spacing, height, -grid_offset + zz * spacing)) *
                        model * glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));
                instances.push_back({ model, palette[idx & 3] });
            }
        }
        for (int side = 0; side < 2; ++side)
        {
            const float x = (side == 0) ? -6.4f : 6.4f;
            instances.push_back({
                glm::translate(glm::mat4(1.0f), glm::vec3(x, 2.2f, 2.0f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(0.42f, 2.2f, 0.42f)),
                glm::vec3(0.82f, 0.80f, 0.76f) });
        }

        // ---- Pass 1：几何 → G-buffer ----
        geom_rasterizer.clear_records();
        u_view_proj    = to_edsl_matrix(view_proj_mat);
        u_view         = to_edsl_matrix(view);
        u_light_dir_vs = ktm::fvec4(light_dir_vs.x, light_dir_vs.y, light_dir_vs.z, ambient);
        u_disney_a     = ktm::fvec4(mat.metallic, mat.roughness, mat.specular, mat.specular_tint);
        u_disney_b     = ktm::fvec4(mat.subsurface, mat.anisotropic, mat.sheen, mat.sheen_tint);
        u_disney_c     = ktm::fvec4(mat.clearcoat, mat.clearcoat_gloss, 0.0f, 0.0f);

        // Convert instance loop to multi-draw indirect
        std::vector<horizon::DrawIndexedIndirectCommand> indirect_cmds;
        indirect_cmds.reserve(instances.size());

        for (const EssrInstance& inst : instances)
        {
            pc_model    = to_edsl_matrix(inst.model);
            pc_material = ktm::fvec4(inst.albedo.x, inst.albedo.y, inst.albedo.z, 0.0f);

            horizon::DrawIndexedIndirectCommand cmd;
            cmd.index_count = static_cast<uint32_t>(essr_cube_indices.size());
            cmd.instance_count = 1;
            cmd.first_index = 0;
            cmd.vertex_offset = 0;
            cmd.first_instance = static_cast<uint32_t>(indirect_cmds.size());
            indirect_cmds.push_back(cmd);
        }

        if (!indirect_cmds.empty())
        {
            horizon::HardwareBuffer indirect_buffer = horizon::HardwareBuffer::from_bytes(
                std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(indirect_cmds.data()),
                    indirect_cmds.size() * sizeof(horizon::DrawIndexedIndirectCommand)),
                static_cast<uint32_t>(indirect_cmds.size() * sizeof(horizon::DrawIndexedIndirectCommand)),
                horizon::BufferUsage_TransferDst | horizon::BufferUsage_Indirect,
                "example_edsl_ssr.geom_indirect");

            horizon::DrawIndexedIndirectParams indirect_params;
            indirect_params.draw_count = static_cast<uint32_t>(indirect_cmds.size());
            indirect_params.indirect_offset = 0;
            indirect_params.stride = sizeof(horizon::DrawIndexedIndirectCommand);
            geom_rasterizer.record_indirect(cube_ib, cube_vb, indirect_buffer, indirect_params);
        }

        // ---- Pass 2：线性深度 ----
        u_depth_unpack = ktm::fvec4(p32, -p22, 0.0f, 0.0f);
        u_ld_params    = ktm::fvec4(float(essr_width), float(essr_height), essr_far, 0.0f);

        // ---- Pass 3：trace ----
        u_ndc_to_view   = ktm::fvec4(2.0f / p00, 2.0f / p11, -1.0f / p00, -1.0f / p11);
        u_tr_params0    = ktm::fvec4(p00, p11, max_distance, float(kTraceSteps));
        u_tr_params1    = ktm::fvec4(thickness, float(frame_index), float(essr_width), float(essr_height));
        u_tr_disney_b   = ktm::fvec4(mat.specular, mat.subsurface, 0.0f, 0.0f);
        u_tr_disney_c   = ktm::fvec4(mat.specular_tint, mat.anisotropic, mat.sheen, mat.sheen_tint);
        u_tr_disney_d   = ktm::fvec4(mat.clearcoat, mat.clearcoat_gloss, 0.0f, 0.0f);
        u_tr_use_jitter = use_jitter ? 1.0f : 0.0f;

        // ---- Pass 4：合成 ----
        u_cp_params = ktm::fvec4(float(essr_width), float(essr_height),
                                 ssr_enabled ? intensity : 0.0f, float(debug_mode));

        horizon::SubmitReceipt render_receipt =
            render_executor << geom_rasterizer.extent(essr_width, essr_height)
                            << linear_depth_compute.dispatch_extent(essr_width, essr_height)
                            << trace_compute.dispatch_extent(essr_width, essr_height)
                            << composite_compute.dispatch_extent(essr_width, essr_height)
                            << horizon::commit();

        ui.draw_overlay(display_executor, final_output_image, render_receipt);
        display_executor.wait(render_receipt);
        (void)(display_executor.stream() << horizon::present(display, final_output_image)
                                         << horizon::commit());
        ++frame_index;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
