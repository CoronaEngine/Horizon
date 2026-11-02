//
// Created by Zero on 06/06/2022.
//

#include "renderpass.h"
#include "core/util/hash.h"

namespace ocarina {

RHIRenderPass::~RHIRenderPass() {
    for (auto &queue : pipeline_render_queues_) {
        ocarina::delete_with_allocator<PipelineRenderQueue>(queue.second);
    }
    pipeline_render_queues_.clear();
}

void RHIRenderPass::clear_draw_call_items() {
    for (auto &queue : pipeline_render_queues_) {
        queue.second->clear();
    }
}

void RHIRenderPass::add_draw_call(DrawCallItem &item) {
    if (item.pipeline != nullptr)
    {
        auto it = pipeline_render_queues_.find(item.pipeline);
        if (it != pipeline_render_queues_.end())
        {
            it->second->draw_call_items.push_back(item);
        } else {
            PipelineRenderQueue *queue = ocarina::new_with_allocator<PipelineRenderQueue>();
            queue->pipeline_line = item.pipeline;
            queue->draw_call_items.push_back(item);
            pipeline_render_queues_.insert(std::make_pair(item.pipeline, queue));
        }
    }
}

void RHIRenderPass::add_global_descriptor_set(const std::string &name, DescriptorSet *descriptor_set) {
    uint64_t hash = hash64(name);
    if (global_descriptor_sets_.find(hash) != global_descriptor_sets_.end()) {
        // Now allow multiple add global descriptor set
        return;
    }
    global_descriptor_sets_.insert(std::make_pair(hash, descriptor_set));
    global_descriptor_sets_array_.push_back(descriptor_set);
}

}// namespace ocarina