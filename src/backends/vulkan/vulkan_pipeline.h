#pragma once
#include "core/header.h"
#include "core/concepts.h"
#include "core/stl.h"
#include "core/util/util.h"
#include "rhi/graphics_descriptions.h"
#include "rhi/pipeline_state.h"
#include <vulkan/vulkan.h>
#include <functional>
#include "vulkan_buffer.h"
namespace ocarina {

class VulkanShader;
class VulkanDevice;

struct VulkanPipeline : public RHIPipeline {
    VkPipelineCache pipeline_cache_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
};


struct VertexInputAttributeDescription {
    VertexInputAttributeDescription &operator=(const VkVertexInputAttributeDescription &that) {
        location = that.location;
        binding = that.binding;
        format = that.format;
        offset = that.offset;
        return *this;
    }
    operator VkVertexInputAttributeDescription() const {
        return {location, binding, VkFormat(format), offset};
    }

    bool operator==(const VertexInputAttributeDescription &other) const {
        return location == other.location && binding == other.binding && format == other.format && offset == other.offset;
    }

    bool operator!=(const VertexInputAttributeDescription &other) const {
        return !(*this == other);
    }
    uint8_t location = 0;
    uint8_t binding = 0;
    uint16_t format = 0;
    uint32_t offset = 0;
};

// Equivalent to VkVertexInputBindingDescription but not as big.
struct VertexInputBindingDescription {
    VertexInputBindingDescription &operator=(const VkVertexInputBindingDescription &that) {
        binding = that.binding;
        stride = that.stride;
        inputRate = that.inputRate;
        return *this;
    }
    operator VkVertexInputBindingDescription() const {
        return {binding, stride, (VkVertexInputRate)inputRate};
    }

    bool operator==(const VkVertexInputBindingDescription &other) const {
        return binding == other.binding && inputRate == other.inputRate && stride == other.stride;
    }

    bool operator!=(const VkVertexInputBindingDescription &other) const {
        return !(*this == other);
    }

    uint16_t binding = 0;
    uint16_t inputRate = 0;
    uint32_t stride = 0;
};
struct PipelineKey
{
    static constexpr uint16_t MAX_VERTEX_ATTRIBUTES = 16;
    static constexpr uint16_t MAX_SHADER_STAGE = 2;
    VkShaderModule shaders[MAX_SHADER_STAGE] = {VK_NULL_HANDLE};
    VkRenderPass render_pass = VK_NULL_HANDLE;
    RasterState raster_state = {};
    BlendState blend_state = {};
    DepthStencilState depth_stencil_state = {};
    VkPipelineLayout pipeline_layout;
    VkPrimitiveTopology topology;
    VertexInputAttributeDescription vertex_input_attributes[MAX_VERTEX_ATTRIBUTES];
    VertexInputBindingDescription vertex_input_binding[MAX_VERTEX_ATTRIBUTES];
    MultiSampleState multi_sample_state = {};

    //we don't need to consider the vertex attribute here, because vertex attributes are assiociate to shader module.
    bool operator==(const PipelineKey &other) const {
        bool states_comp = *(int *)&raster_state == *(int *)&other.raster_state &&
               render_pass == other.render_pass &&
               shaders[0] == other.shaders[0] &&
               shaders[1] == other.shaders[1] &&
               *(int *)&blend_state == *(int *)&other.blend_state &&
               *(int *)&depth_stencil_state == *(int *)&other.depth_stencil_state && 
            topology == other.topology &&
            pipeline_layout == other.pipeline_layout &&
            *(int *)&multi_sample_state == *(int *)&other.multi_sample_state;

        if (states_comp)
        {
            for (uint8_t i = 0; i < MAX_VERTEX_ATTRIBUTES; ++i) {
                if (vertex_input_attributes[i] != other.vertex_input_attributes[i] ||
                    vertex_input_binding[i] != other.vertex_input_binding[i]) {
                    return false;
                }
            }
        }
        
        return states_comp;
    }
};

struct PipelineLayoutKey
{
    constexpr static uint8_t MAX_DESCRIPTOR_SET = 4;
    std::array < VkDescriptorSetLayout, MAX_DESCRIPTOR_SET> descriptor_set_layouts = {VK_NULL_HANDLE};
    uint8_t descriptor_set_count = 0;

    bool operator==(const PipelineLayoutKey &other) const {
        if (descriptor_set_count != other.descriptor_set_count) {
            return false;
        }
        for (uint8_t i = 0; i < descriptor_set_count; ++i) {
            if (descriptor_set_layouts[i] != other.descriptor_set_layouts[i]) {
                return false;
            }
        }
        return true;
    }
};

struct HashPipelineKeyFunction
{
    uint64_t operator()(const PipelineKey &pipeline_key) const {
        std::size_t res = 0;
        hash_combine(res, *((uint64_t*)&pipeline_key.raster_state));
        hash_combine(res, *((uint64_t *)&pipeline_key.blend_state));
        hash_combine(res, *((uint64_t *)&pipeline_key.depth_stencil_state));
        hash_combine(res, pipeline_key.render_pass);
        hash_combine(res, (uint64_t)pipeline_key.shaders[0] << 32 | (uint64_t)pipeline_key.shaders[1]);
        hash_combine(res, pipeline_key.pipeline_layout);
        hash_combine(res, pipeline_key.topology);
        return res;
    }
};

struct HashPipelineLayoutKeyFunction
{
    uint64_t operator()(const PipelineLayoutKey &pipeline_layout_key) const {
        std::size_t res = 0;
        for (uint8_t i = 0; i < pipeline_layout_key.descriptor_set_count; ++i) {
            hash_combine(res, pipeline_layout_key.descriptor_set_layouts[i]);
        }
        return res;
    }
};

struct VulkanVertexInfo {
    std::vector<VkVertexInputBindingDescription> binding_descriptions;
    std::vector<VkVertexInputAttributeDescription> attribute_descriptions;
};

class VulkanPipelineManager : public concepts::Noncopyable
{
public:
    void bind_shader(VkShaderModule shader, int stage);
    void bind_raster_state(const RasterState &raster_state);
    void bind_blend_state(const BlendState &blend_state);
    void bind_depth_stencil_state(const DepthStencilState &depth_stencil_state);
    void bind_pipeline_layout(VkPipelineLayout pipeline_layout);
    void bind_vertex_attributes(VkVertexInputAttributeDescription const *attributes,
                                VkVertexInputBindingDescription const *binds, uint8_t attr_count, uint8_t bind_desc_count);
    void bind_topology(PrimitiveType primitive_type);
    void bind_render_pass(VkRenderPass render_pass) {
        pipeline_key_cache_.render_pass = render_pass;
    }
    VulkanPipeline* get_or_create_pipeline(const PipelineState &pipeline_state, VulkanDevice *device, VkRenderPass render_pass);
    void clear(VulkanDevice *device);

    VkPipelineLayout get_pipeline_layout(VulkanDevice *device, VkDescriptorSetLayout *descriptset_layouts, uint8_t descriptset_layouts_count, uint32_t push_constant_size);

private:
    std::unordered_map<PipelineKey, VulkanPipeline*, HashPipelineKeyFunction> vulkan_pipelines_;
    PipelineKey pipeline_key_cache_;

    std::unordered_map<PipelineLayoutKey, VkPipelineLayout, HashPipelineLayoutKeyFunction> pipeline_layouts_;
};
}// namespace ocarina