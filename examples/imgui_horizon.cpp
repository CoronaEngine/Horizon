#include "imgui_horizon.h"
#include "HardwareCommands_old.h"
#include "Codegen/ControlFlows.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include GLSL(shaders/imgui_vert.glsl)
#include GLSL(shaders/imgui_frag.glsl)
#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

namespace {
struct UiVertex {
    std::array<float, 2> position, uv;
    std::array<float, 4> color;
};
HardwareImage upload_font() {
    unsigned char* pixels{};
    int width{}, height{};
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    // Atlas RGB is white, so sRGB decoding leaves its RGB unchanged; alpha is linear.
    HardwareImage font(width, height, ImageFormat::RGBA8_SRGB, ImageUsage::SampledImage);
    HardwareBuffer staging(static_cast<uint32_t>(width * height * 4), BufferUsage::StorageBuffer, pixels);
    HardwareExecutor upload;
    upload << staging.copyTo(font);
    upload.commit().waitForDeferredResources();
    return font;
}
}

struct HorizonImGuiLayer::Impl {
    uint32_t width, height;
#if HORIZON_IMGUI_RENDER_ENABLED
    HardwareImage font;
    RasterizerPipeline<imgui_vert_glsl_t, imgui_frag_glsl_t> ui;
    HardwareExecutor ui_executor;
#endif
    Impl(uint32_t w, uint32_t h) : width(w), height(h) {
        // ImGui needs a built atlas even when overlay rendering is disabled.
        unsigned char* pixels{}; int fw{}, fh{};
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &fw, &fh);
#if HORIZON_IMGUI_RENDER_ENABLED
        font = upload_font();
        ui.setDepthEnabled(false);
#endif
    }
    ~Impl() {
#if HORIZON_IMGUI_RENDER_ENABLED
        ui_executor.waitForDeferredResources();
#endif
    }
};

HorizonImGuiLayer::HorizonImGuiLayer(GLFWwindow* window, uint32_t width, uint32_t height) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr; // deterministic benchmark layout
    ImGui::StyleColorsDark();
#if !HORIZON_IMGUI_RENDER_ENABLED
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
#endif
    ImGui_ImplGlfw_InitForOther(window, true);
    impl_ = std::make_unique<Impl>(width, height);
}
HorizonImGuiLayer::~HorizonImGuiLayer() {
    impl_.reset();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}
void HorizonImGuiLayer::new_frame() {
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}
LegacyFrameOutput HorizonImGuiLayer::draw_overlay(HardwareImage& scene, HardwareExecutor& ready) {
    ImGui::Render();
#if HORIZON_IMGUI_RENDER_ENABLED
    const auto* data = ImGui::GetDrawData();
    if (data && data->TotalVtxCount && data->DisplaySize.x > 0 && data->DisplaySize.y > 0) {
        auto& impl = *impl_;
        impl.ui_executor.waitForDeferredResources();
        std::vector<UiVertex> vertices;
        std::vector<uint16_t> indices;
        vertices.reserve(data->TotalVtxCount);
        indices.reserve(data->TotalIdxCount);
        for (int i = 0; i < data->CmdListsCount; ++i) {
            const auto* list = data->CmdLists[i];
            for (const auto& v : list->VtxBuffer) {
                const ImU32 c = v.col;
                vertices.push_back({{v.pos.x,v.pos.y}, {v.uv.x,v.uv.y},
                    {float((c >> IM_COL32_R_SHIFT) & 255)/255, float((c >> IM_COL32_G_SHIFT) & 255)/255,
                     float((c >> IM_COL32_B_SHIFT) & 255)/255, float((c >> IM_COL32_A_SHIFT) & 255)/255}});
            }
            static_assert(sizeof(ImDrawIdx) == sizeof(uint16_t));
            indices.insert(indices.end(), list->IdxBuffer.begin(), list->IdxBuffer.end());
        }
        HardwareBuffer vb(vertices, BufferUsage::VertexBuffer);
        HardwareBuffer ib(indices, BufferUsage::IndexBuffer);
        const std::array<float, 2> scale{2 / data->DisplaySize.x, 2 / data->DisplaySize.y};
        impl.ui.ubo.scale = scale;
        impl.ui.ubo.translate = std::array<float, 2>{-1 - data->DisplayPos.x * scale[0], -1 - data->DisplayPos.y * scale[1]};
        impl.ui.uses({{impl.font, ResourceAccess::Read}});
        impl.ui.outColor = scene;
        auto batch = impl.ui.beginBatch(impl.width, impl.height, {.loadColor = true});
        const RasterGeometry geometry{vb, ib};
        const uint32_t font = impl.font.storeDescriptor();
        uint32_t index_offset = 0;
        int32_t vertex_offset = 0;
        for (int i = 0; i < data->CmdListsCount; ++i) {
            const auto* list = data->CmdLists[i];
            for (const auto& command : list->CmdBuffer) {
                if (command.UserCallback || command.ElemCount == 0) continue;
                const float x0 = std::max(command.ClipRect.x - data->DisplayPos.x, 0.0f);
                const float y0 = std::max(command.ClipRect.y - data->DisplayPos.y, 0.0f);
                const float x1 = std::min(command.ClipRect.z - data->DisplayPos.x, float(impl.width));
                const float y1 = std::min(command.ClipRect.w - data->DisplayPos.y, float(impl.height));
                if (x1 <= x0 || y1 <= y0) continue;
                // Match current example overlay, which only rejects empty clips.
                RasterDraw params;
                params.indexCount = command.ElemCount;
                params.firstIndex = index_offset + command.IdxOffset;
                params.vertexOffset = vertex_offset + static_cast<int32_t>(command.VtxOffset);
                batch.draw(geometry, params, font);
            }
            index_offset += list->IdxBuffer.Size;
            vertex_offset += list->VtxBuffer.Size;
        }
        impl.ui_executor.wait(ready);
        impl.ui_executor << std::move(batch);
        impl.ui_executor.commit();
        return {scene, &impl.ui_executor};
    }
#endif
    return {scene, &ready};
}
