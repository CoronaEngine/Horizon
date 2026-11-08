#include "imgui_horizon_old.h"
#include "HardwareCommands_old.h"
#include "Codegen/ControlFlows.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include GLSL(shaders/imgui_old_vert.glsl)
#include GLSL(shaders/imgui_old_frag.glsl)
#include GLSL(shaders/overlay_old_vert.glsl)
#include GLSL(shaders/overlay_old_frag.glsl)
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

struct HorizonImGuiLayerOld::Impl {
    uint32_t width, height;
#if HORIZON_IMGUI_RENDER_ENABLED
    HardwareImage font, overlay, composited;
    RasterizerPipeline<imgui_old_vert_glsl_t, imgui_old_frag_glsl_t> ui;
    RasterizerPipeline<overlay_old_vert_glsl_t, overlay_old_frag_glsl_t> blend;
    HardwareBuffer triangle, indices;
    HardwareExecutor ui_executor, blend_executor;
#endif
    Impl(uint32_t w, uint32_t h) : width(w), height(h) {
        // ImGui needs a built atlas even when overlay rendering is disabled.
        unsigned char* pixels{}; int fw{}, fh{};
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &fw, &fh);
#if HORIZON_IMGUI_RENDER_ENABLED
        font = upload_font();
        overlay = HardwareImage(w, h, ImageFormat::RGBA16_FLOAT, ImageUsage::StorageImage);
        overlay.setClearColor(0, 0, 0, 0);
        composited = HardwareImage(w, h, ImageFormat::RGBA16_FLOAT, ImageUsage::StorageImage);
        ui.outColor = overlay;
        ui.setDepthEnabled(false);
        blend.outColor = composited;
        blend.setDepthEnabled(false);
        const std::array<std::array<float, 2>, 3> vertices{{{-1,-1}, {3,-1}, {-1,3}}};
        const std::array<uint16_t, 3> idx{0,1,2};
        triangle = HardwareBuffer(vertices, BufferUsage::VertexBuffer);
        indices = HardwareBuffer(idx, BufferUsage::IndexBuffer);
#endif
    }
    ~Impl() {
#if HORIZON_IMGUI_RENDER_ENABLED
        blend_executor.waitForDeferredResources();
        ui_executor.waitForDeferredResources();
#endif
    }
};

HorizonImGuiLayerOld::HorizonImGuiLayerOld(GLFWwindow* window, uint32_t width, uint32_t height) {
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
HorizonImGuiLayerOld::~HorizonImGuiLayerOld() {
    impl_.reset();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}
void HorizonImGuiLayerOld::new_frame() {
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}
LegacyFrameOutput HorizonImGuiLayerOld::draw_overlay(HardwareImage& scene, HardwareExecutor& ready) {
    ImGui::Render();
#if HORIZON_IMGUI_RENDER_ENABLED
    const auto* data = ImGui::GetDrawData();
    if (data && data->TotalVtxCount && data->DisplaySize.x > 0 && data->DisplaySize.y > 0) {
        auto& impl = *impl_;
        impl.blend_executor.waitForDeferredResources();
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
                DrawIndexedParams params;
                params.indexCount = command.ElemCount;
                params.firstIndex = index_offset + command.IdxOffset;
                params.vertexOffset = vertex_offset + static_cast<int32_t>(command.VtxOffset);
                params.indexType = IndexType::UInt16;
                impl.ui.pc.texIndex = impl.font.storeDescriptor();
                impl.ui.record(ib, vb, params);
            }
            index_offset += list->IdxBuffer.Size;
            vertex_offset += list->VtxBuffer.Size;
        }
        impl.ui_executor.wait(ready);
        impl.ui_executor << impl.ui(impl.width, impl.height);
        impl.ui_executor.commit();
        impl.blend.pc.sceneIndex = scene.storeDescriptor();
        impl.blend.pc.overlayIndex = impl.overlay.storeDescriptor();
        impl.blend.record(impl.indices, impl.triangle);
        impl.blend_executor.wait(impl.ui_executor);
        impl.blend_executor << impl.blend(impl.width, impl.height);
        impl.blend_executor.commit();
        return {impl.composited, &impl.blend_executor};
    }
    // The Legacy displayer keeps the first source image for a surface. Even an
    // empty ImGui frame (including initial auto-sizing) must use that same image.
    auto& impl = *impl_;
    impl.blend_executor.waitForDeferredResources();
    impl.blend_executor.wait(ready);
    impl.blend_executor << scene.copyTo(impl.composited);
    impl.blend_executor.commit();
    return {impl.composited, &impl.blend_executor};
#else
    return {scene, &ready};
#endif
}
