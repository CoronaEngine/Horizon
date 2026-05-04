//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/stl.h"
#include "graphics_descriptions.h"

namespace ocarina {
struct PipelineState;
class IndexBuffer;
class RenderTarget;
class DescriptorSetWriter;
class DescriptorSet;
struct RHIPipeline;

struct DrawCallItem {
    PipelineState* pipeline_state = nullptr;
    IndexBuffer* index_buffer = nullptr;
    //float4x4 world_matrix;
    //DescriptorSetWriter *descriptor_set_writer = nullptr;
    std::array<DescriptorSet *, max_descriptor_sets_per_shader> descriptor_sets = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    uint32_t descriptor_set_count = 0;
     
    using PreRenderFunction = ocarina::function<void(const DrawCallItem&)>;

    PreRenderFunction pre_render_function = nullptr;
    RHIPipeline *pipeline = nullptr;
    void *push_constant_data = nullptr;
    uint8_t push_constant_size = 0;
};

struct PipelineRenderQueue
{
    std::list<DrawCallItem> draw_call_items;
    RHIPipeline *pipeline_line = nullptr;

    void clear()
    {
        draw_call_items.clear();
    }
};

struct GlobalUBO
{
    float4x4 view_matrix = {1.0f};
    float4x4 projection_matrix = {1.0f};
};

class OC_RHI_API RHIRenderPass {
public:
    RHIRenderPass(const RenderPassCreation &render_pass_creation) {}
    virtual ~RHIRenderPass();

    void clear_draw_call_items();

    void add_draw_call(DrawCallItem &item);

    void add_render_target(RenderTarget* render_target) {
        OC_ASSERT(render_target_count_ < kMaxRenderTargets);
        render_target_[render_target_count_++] = render_target;
    }

    virtual void begin_render_pass() = 0;
    virtual void end_render_pass() = 0;
    virtual void draw_items() = 0;

    using BeginRenderPassCallback = ocarina::function<void(RHIRenderPass *)>;
    void set_begin_renderpass_callback(BeginRenderPassCallback callback)
    {
        begin_render_pass_callback_ = callback;
    }

    void add_global_descriptor_set(const std::string &name, DescriptorSet *descriptor_set);

protected:
    bool is_use_swapchain_framebuffer() const {
        return render_target_count_ == 0;
    }

    //std::list<DrawCallItem> draw_call_items_;
    float4 viewport_ = {0, 0, 0, 0};
    int4 scissor_ = {0, 0, 0, 0};
    uint2 size_ = {0, 0};

    std::string name_ = "RHIRenderPass";

    //if render_target_count == 0, use the swapchain backbuffer as render target
    uint32_t render_target_count_ = 0;
    constexpr static const int kMaxRenderTargets = 8;
    RenderTarget *render_target_[kMaxRenderTargets] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    RenderTarget* depth_stencil_target_ = nullptr;
    BeginRenderPassCallback begin_render_pass_callback_ = nullptr;

    std::unordered_map<RHIPipeline *, PipelineRenderQueue *> pipeline_render_queues_;
    std::unordered_map<uint64_t, DescriptorSet*> global_descriptor_sets_;
    std::vector<DescriptorSet *> global_descriptor_sets_array_;
    GlobalUBO global_ubo_data_ = {};
};

}// namespace ocarina