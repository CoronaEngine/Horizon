#include "imgui_horizon.h"

#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>

#include "Codegen/ControlFlows.h"

#include GLSL(shaders/imgui_vert.glsl)
#include GLSL(shaders/imgui_frag.glsl)

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace
{
using namespace Corona::Horizon;

// 与 shaders/imgui_vert.glsl 的顶点输入一一对应（按声明顺序紧凑排布）。
struct UiVertex
{
    std::array<float, 2> position;
    std::array<float, 2> uv;
    std::array<float, 4> color;
};

HardwareImage upload_font_atlas()
{
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    if (pixels == nullptr || width <= 0 || height <= 0)
        throw std::runtime_error("ImGui font atlas build failed.");

    HardwareImageDesc desc = HardwareImageDesc::texture_2d(
        static_cast<uint32_t>(width), static_cast<uint32_t>(height), Format::RGBA8_UNORM,
        ImageUsage_Sampled | ImageUsage_TransferDst, "imgui.font_atlas");
    HardwareImage image(desc);

    const size_t byte_count = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    HardwareBufferDesc staging_desc;
    staging_desc.element_count = byte_count;
    staging_desc.element_size = 1;
    staging_desc.usage = BufferUsage_TransferSrc;
    staging_desc.cpu_access = CpuAccessMode::Write;
    HardwareBuffer staging(staging_desc, std::as_bytes(std::span<const unsigned char>(pixels, byte_count)));

    HardwareExecutor executor;
    (void)(executor.stream() << image.copy_from(staging) << commit());
    return image;
}

RasterizerPipelineDesc make_ui_pipeline_desc()
{
    RasterizerPipelineDesc desc;
    desc.debug_name = "imgui.pipeline";
    desc.clear_color_target = false; // 叠加渲染：保留场景内容，不清屏
    desc.depth_test_enabled = false;
    desc.depth_write_enabled = false;
    // 默认即 alpha 混合
    return desc;
}
} // namespace

struct HorizonImGuiLayer::Impl
{
    uint32_t width;
    uint32_t height;
    HardwareImage font_image;
    decltype(RasterizerPipeline(imgui_vert_glsl, imgui_frag_glsl, RasterizerPipelineDesc {})) pipeline;

    Impl(uint32_t target_width, uint32_t target_height)
        : width(target_width)
        , height(target_height)
        , font_image(upload_font_atlas())
        , pipeline(imgui_vert_glsl, imgui_frag_glsl, make_ui_pipeline_desc())
    {
        // 字体图集存入 bindless 表（set 0），索引经 push constant 传入 shader。
        pipeline.pc.texIndex = font_image.store_descriptor();
    }
};

HorizonImGuiLayer::HorizonImGuiLayer(GLFWwindow* window, uint32_t target_width, uint32_t target_height)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
#if !HORIZON_IMGUI_RENDER_ENABLED
    // 渲染关闭时 UI 不可见，但窗口逻辑仍在运行；
    // 屏蔽鼠标输入，避免看不见的窗口拦截示例自己的相机/滚轮操作。
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
#endif
    ImGui_ImplGlfw_InitForOther(window, true);
    impl_ = std::make_unique<Impl>(target_width, target_height);
}

HorizonImGuiLayer::~HorizonImGuiLayer()
{
    impl_.reset();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void HorizonImGuiLayer::new_frame()
{
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void HorizonImGuiLayer::draw_overlay(HardwareExecutor& display_executor,
                                     HardwareImage& target,
                                     const SubmitReceipt& scene_receipt)
{
    ImGui::Render();
#if !HORIZON_IMGUI_RENDER_ENABLED
    // HORIZON_ENABLE_IMGUI_RENDER=OFF：UI 帧照常构建与关闭（保持 NewFrame/Render 配对），
    // 但不绘制、不提交，示例行为与未接入 UI 时一致。
    return;
#endif
    const ImDrawData* draw_data = ImGui::GetDrawData();

    if (draw_data != nullptr && draw_data->TotalVtxCount > 0 && draw_data->CmdListsCount > 0)
    {
        Impl& impl = *impl_;

        // 汇总所有 draw list 到一对 buffer；颜色从 packed u32 展开成 float4。
        std::vector<UiVertex> vertices;
        std::vector<uint16_t> indices;
        vertices.reserve(static_cast<size_t>(draw_data->TotalVtxCount));
        indices.reserve(static_cast<size_t>(draw_data->TotalIdxCount));
        for (int list_index = 0; list_index < draw_data->CmdListsCount; ++list_index)
        {
            const ImDrawList* list = draw_data->CmdLists[list_index];
            for (const ImDrawVert& v : list->VtxBuffer)
            {
                const ImU32 c = v.col;
                vertices.push_back(UiVertex {
                    { v.pos.x, v.pos.y },
                    { v.uv.x, v.uv.y },
                    { static_cast<float>((c >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f,
                      static_cast<float>((c >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
                      static_cast<float>((c >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f,
                      static_cast<float>((c >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f } });
            }
            static_assert(sizeof(ImDrawIdx) == sizeof(uint16_t), "ImGui backend assumes 16-bit indices.");
            indices.insert(indices.end(), list->IdxBuffer.begin(), list->IdxBuffer.end());
        }

        HardwareBuffer vertex_buffer =
            HardwareBuffer::vertex(std::span<const UiVertex>(vertices), "imgui.vertices");
        HardwareBuffer index_buffer =
            HardwareBuffer::index(std::span<const uint16_t>(indices), "imgui.indices");

        // ImGui 像素坐标（左上原点）→ NDC。
        const ImVec2 display_pos = draw_data->DisplayPos;
        const ImVec2 display_size = draw_data->DisplaySize;
        const std::array<float, 2> scale { 2.0f / display_size.x, 2.0f / display_size.y };
        impl.pipeline.ubo.scale = scale;
        impl.pipeline.ubo.translate =
            std::array<float, 2> { -1.0f - display_pos.x * scale[0], -1.0f - display_pos.y * scale[1] };
        impl.pipeline.outColor = target;

        impl.pipeline.clear_records();
        uint32_t global_index_offset = 0;
        int32_t global_vertex_offset = 0;
        for (int list_index = 0; list_index < draw_data->CmdListsCount; ++list_index)
        {
            const ImDrawList* list = draw_data->CmdLists[list_index];
            for (const ImDrawCmd& cmd : list->CmdBuffer)
            {
                if (cmd.UserCallback != nullptr)
                    continue;

                const float clip_min_x = std::max(cmd.ClipRect.x - display_pos.x, 0.0f);
                const float clip_min_y = std::max(cmd.ClipRect.y - display_pos.y, 0.0f);
                const float clip_max_x = std::min(cmd.ClipRect.z - display_pos.x, static_cast<float>(impl.width));
                const float clip_max_y = std::min(cmd.ClipRect.w - display_pos.y, static_cast<float>(impl.height));
                if (clip_max_x <= clip_min_x || clip_max_y <= clip_min_y || cmd.ElemCount == 0)
                    continue;

                DrawIndexedParams params;
                params.index_count = cmd.ElemCount;
                params.first_index = global_index_offset + cmd.IdxOffset;
                params.vertex_offset = global_vertex_offset + static_cast<int32_t>(cmd.VtxOffset);
                params.enable_scissor = true;
                params.scissor = ScissorRect {
                    static_cast<int32_t>(clip_min_x),
                    static_cast<int32_t>(clip_min_y),
                    static_cast<uint32_t>(clip_max_x - clip_min_x),
                    static_cast<uint32_t>(clip_max_y - clip_min_y),
                };
                params.debug_label = "imgui.draw";
                impl.pipeline.record(index_buffer, vertex_buffer, params);
            }
            global_index_offset += static_cast<uint32_t>(list->IdxBuffer.Size);
            global_vertex_offset += list->VtxBuffer.Size;
        }

        // GPU 顺序：场景 → UI。
        display_executor.wait(scene_receipt);
        SubmitReceipt ui_receipt = display_executor.stream()
            << impl.pipeline.extent(impl.width, impl.height)
            << commit();

        // 挂到 pending waits：同一 executor 上示例随后的 present commit 会等待 UI 完成。
        display_executor.wait(ui_receipt);

        // 这里曾有一处 wait_for_completion(ui_receipt)：把 CPU 钳到 GPU 进度，
        // 防止提交洪水耗尽交换链帧槽。节流已上移到 DisplayManager —— present
        // 结束时等第 N 帧前的那一帧（N = 交换链图像数），所以 CPU 可以领先
        // GPU 至多 N 帧而不是必须逐帧同步。此处不能再等，否则退回串行。
    }
}
