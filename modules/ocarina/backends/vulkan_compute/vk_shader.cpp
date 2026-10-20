//
//

#include "vk_shader.h"
#include "vk_compute_device.h"
#include "vk_stream.h"

namespace ocarina {

// ── VkComputeShader ───────────────────────────────────────────────────────────

VkComputeShader::VkComputeShader(VulkanComputeDevice *device,
                                  const vector<uint32_t> &spirv,
                                  const Function &f)
    : VkShaderBase(device, f) {
    workgroup_size_ = device_->workgroup_size(f);
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device_->physical_device(), &properties);
    const auto &counts = properties.limits.maxComputeWorkGroupCount;
    max_group_count_ = make_uint3(counts[0], counts[1], counts[2]);
    create_pipeline_layout();
    create_pipeline(spirv);
}

VkComputeShader::~VkComputeShader() noexcept {
    if (pipeline_ != VK_NULL_HANDLE)
        vkDestroyPipeline(device_->logical_device(), pipeline_, nullptr);
    if (layout_ != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device_->logical_device(), layout_, nullptr);
    if (params_buf_.buffer != VK_NULL_HANDLE)
        device_->free_buffer(params_buf_);
}

void VkComputeShader::create_pipeline_layout() noexcept {
    VkPushConstantRange pc_range{};
    pc_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pc_range.offset     = 0;
    pc_range.size       = parameter_layout_.push_bytes();

    VkDescriptorSetLayout global_layout = device_->global_desc_layout();
    VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    ci.setLayoutCount         = 1u;
    ci.pSetLayouts            = &global_layout;
    ci.pushConstantRangeCount = 1u;
    ci.pPushConstantRanges    = &pc_range;
    OC_VK_CHECK(vkCreatePipelineLayout(device_->logical_device(), &ci, nullptr, &layout_));
}

void VkComputeShader::create_pipeline(const vector<uint32_t> &spirv) noexcept {
    VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smi.codeSize = spirv.size() * 4;
    smi.pCode    = spirv.data();
    VkShaderModule shader_mod = VK_NULL_HANDLE;
    OC_VK_CHECK(vkCreateShaderModule(device_->logical_device(), &smi, nullptr, &shader_mod));

    VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stage.stage               = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module              = shader_mod;
    stage.pName               = "main";

    VkComputePipelineCreateInfo pci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pci.stage  = stage;
    pci.layout = layout_;
    OC_VK_CHECK(vkCreateComputePipelines(
        device_->logical_device(), VK_NULL_HANDLE, 1u, &pci, nullptr, &pipeline_));
    vkDestroyShaderModule(device_->logical_device(), shader_mod, nullptr);
}

void VkComputeShader::compute_fit_size() noexcept {
    // numthreads is compiled into SPIR-V; dispatch must use that same size.
    workgroup_size_ = device_->workgroup_size(function_);
}

void VkComputeShader::launch(handle_ty stream, ShaderDispatchCommand *cmd) noexcept {
    device_->immediate_submit([&](VkCommandBuffer cb) { dispatch(cb, cmd); });
}

void VkComputeShader::dispatch(VkCommandBuffer cb, ShaderDispatchCommand *cmd, VkDeviceAddress params,
                               bool bind_pipeline) noexcept {
    const uint3 dim = cmd->dispatch_dim();
    if (!dim.x || !dim.y || !dim.z) return;
    auto data = cmd->argument_data();
    OC_ERROR_IF(data.size_bytes() < parameter_layout_.extent,
                "Vulkan shader argument data is smaller than its parameter layout");
    if (!parameters_inline() && !params && !data.empty()) {
        if (params_buf_.buffer == VK_NULL_HANDLE) {
            params_buf_ = device_->allocate_buffer(
                (data.size_bytes() + 3u) / 4u * 4u,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                true, "shader_params");
        }
        void *mapped = nullptr;
        vmaMapMemory(device_->vma_allocator(), params_buf_.alloc, &mapped);
        memcpy(mapped, data.data(), data.size_bytes());
        vmaUnmapMemory(device_->vma_allocator(), params_buf_.alloc);
    }

    if (bind_pipeline) {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
        VkDescriptorSet gs = device_->global_desc_set();
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, layout_, 0, 1, &gs, 0, nullptr);
    }

    uint8_t pc[VkShaderParameterLayout::max_push_bytes]{};
    if (parameters_inline()) {
        if (parameter_layout_.extent)
            memcpy(pc + VkShaderParameterLayout::inline_offset, data.data(), parameter_layout_.extent);
    } else {
        uint64_t addr = params ? params : params_buf_.address;
        memcpy(pc + VkShaderParameterLayout::params_offset, &addr, sizeof(addr));
    }
    memcpy(pc + VkShaderParameterLayout::dim_offset, &dim, 12);

    uint3 wgs = workgroup_size_;
    uint3 grid = function_.grid_dim();
    // ray tracing launches exactly dispatch_dim rays and ignores compute launch
    // configuration. Ray queries run in a compute pipeline here, so derive
    // enough groups to cover that same raygen domain even for a configured grid.
    if (function_.is_raytracing_kernel() || !grid.x || !grid.y || !grid.z)
        grid = make_uint3(dim.x / wgs.x + uint(dim.x % wgs.x != 0u),
                          dim.y / wgs.y + uint(dim.y % wgs.y != 0u),
                          dim.z / wgs.z + uint(dim.z % wgs.z != 0u));
    for (uint64_t z = 0; z < grid.z; z += max_group_count_.z)
        for (uint64_t y = 0; y < grid.y; y += max_group_count_.y)
            for (uint64_t x = 0; x < grid.x; x += max_group_count_.x) {
                const uint3 base = make_uint3(uint(x), uint(y), uint(z));
                const uint3 count = make_uint3(std::min(grid.x - base.x, max_group_count_.x),
                                              std::min(grid.y - base.y, max_group_count_.y),
                                              std::min(grid.z - base.z, max_group_count_.z));
                memcpy(pc + VkShaderParameterLayout::group_base_offset, &base, 12);
                vkCmdPushConstants(cb, layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, parameter_layout_.push_bytes(), pc);
                vkCmdDispatch(cb, count.x, count.y, count.z);
            }
}

// ── VkShaderFactory ───────────────────────────────────────────────────────────

VkShaderBase *VkShaderFactory::create(VulkanComputeDevice *device,
                                       const vector<uint32_t> &spirv,
                                       const Function &f) {
    // Inline ray queries execute from the same compute entry point as other DSL
    // kernels, so there are no missing miss/closest-hit entry points or SBT.
    return new VkComputeShader(device, spirv, f);
}

}// namespace ocarina
