//
// Created during CUDA→Slang+Vulkan migration.
// Replaces cuda_shader.cpp — compute + raytracing pipelines.
//

#include "vk_shader.h"
#include "vk_compute_device.h"
#include "vk_stream.h"
#include <algorithm>
#include <limits>

namespace ocarina {

// ── choose_block_shape (copied verbatim from cuda_shader.cpp) ─────────────────

namespace {

[[nodiscard]] bool valid_dim(uint3 dim) noexcept {
    return dim.x != 0u && dim.y != 0u && dim.z != 0u;
}
[[nodiscard]] uint ceil_div(uint num, uint den) noexcept {
    return (num + den - 1u) / den;
}
[[nodiscard]] uint3 ceil_div3(uint3 num, uint3 den) noexcept {
    return make_uint3(ceil_div(num.x,den.x), ceil_div(num.y,den.y), ceil_div(num.z,den.z));
}
[[nodiscard]] uint min_u(uint a, uint b) noexcept { return a < b ? a : b; }
[[nodiscard]] uint max_u(uint a, uint b) noexcept { return a > b ? a : b; }
[[nodiscard]] uint volume(uint3 d) noexcept { return d.x*d.y*d.z; }
[[nodiscard]] uint3 min_dim(uint3 a, uint3 b) noexcept {
    return make_uint3(min_u(a.x,b.x), min_u(a.y,b.y), min_u(a.z,b.z));
}
[[nodiscard]] int score_candidate(uint3 cand, uint3 disp, uint max_t) noexcept {
    uint t = volume(cand);
    if (!t || t > max_t) return std::numeric_limits<int>::min();
    uint act = volume(min_dim(cand, disp));
    return (int)(act*4096u + t*4u - (t-act));
}

}// namespace

uint3 choose_block_shape(uint3 dispatch_dim, uint max_threads) noexcept {
    max_threads = max_u(1u, min_u(max_threads, 1024u));
    if (dispatch_dim.z > 1u) {
        static constexpr std::array<uint3,7> cands{
            make_uint3(8,8,4), make_uint3(8,4,4), make_uint3(4,4,4),
            make_uint3(8,4,2), make_uint3(4,4,2), make_uint3(2,2,2), make_uint3(1,1,1)};
        uint3 best = make_uint3(1u); int best_s = std::numeric_limits<int>::min();
        for (auto c : cands) { int s = score_candidate(c, dispatch_dim, max_threads);
            if (s > best_s) { best_s = s; best = c; } }
        return best;
    }
    if (dispatch_dim.y > 1u) {
        static constexpr std::array<uint3,14> cands{
            make_uint3(32,32,1), make_uint3(32,16,1), make_uint3(16,32,1),
            make_uint3(16,16,1), make_uint3(32,8,1),  make_uint3(8,32,1),
            make_uint3(16,8,1),  make_uint3(8,16,1),  make_uint3(8,8,1),
            make_uint3(8,4,1),   make_uint3(4,8,1),   make_uint3(4,4,1),
            make_uint3(2,2,1),   make_uint3(1,1,1)};
        uint3 best = make_uint3(1u); int best_s = std::numeric_limits<int>::min();
        for (auto c : cands) { int s = score_candidate(c, dispatch_dim, max_threads);
            if (s > best_s) { best_s = s; best = c; } }
        return best;
    }
    static constexpr std::array<uint,11> cands{1024,512,256,128,64,32,16,8,4,2,1};
    for (auto c : cands) if (c <= max_threads)
        return make_uint3(min_u(dispatch_dim.x, c), 1u, 1u);
    return make_uint3(1u);
}

// ── VkComputeShader ───────────────────────────────────────────────────────────

VkComputeShader::VkComputeShader(VulkanComputeDevice *device,
                                  const vector<uint32_t> &spirv,
                                  const Function &f)
    : VkShaderBase(device, f) {
    // maxComputeWorkGroupInvocations lives in VkPhysicalDeviceLimits, not in the
    // ray-tracing pipeline properties struct; query it from physical device props.
    VkPhysicalDeviceProperties dev_props{};
    vkGetPhysicalDeviceProperties(device_->physical_device(), &dev_props);
    workgroup_size_ = choose_block_shape(
        f.dispatch_hint().dim.x > 0 ? f.dispatch_hint().dim : make_uint3(64u, 1u, 1u),
        dev_props.limits.maxComputeWorkGroupInvocations);
    create_pipeline_layout();
    create_pipeline(spirv, workgroup_size_);
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
    pc_range.size       = c_pc_total_bytes;

    VkDescriptorSetLayout global_layout = device_->global_desc_layout();
    VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    ci.setLayoutCount         = 1u;
    ci.pSetLayouts            = &global_layout;
    ci.pushConstantRangeCount = 1u;
    ci.pPushConstantRanges    = &pc_range;
    OC_VK_CHECK(vkCreatePipelineLayout(device_->logical_device(), &ci, nullptr, &layout_));
}

void VkComputeShader::create_pipeline(const vector<uint32_t> &spirv,
                                       uint3 wg_size) noexcept {
    VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smi.codeSize = spirv.size() * 4;
    smi.pCode    = spirv.data();
    VkShaderModule shader_mod = VK_NULL_HANDLE;
    OC_VK_CHECK(vkCreateShaderModule(device_->logical_device(), &smi, nullptr, &shader_mod));

    // Specialization constants for workgroup size
    uint32_t sc_data[3] = {wg_size.x, wg_size.y, wg_size.z};
    VkSpecializationMapEntry sc_entries[3]{};
    for (uint32_t i = 0; i < 3; ++i) {
        sc_entries[i].constantID = i;
        sc_entries[i].offset     = i * sizeof(uint32_t);
        sc_entries[i].size       = sizeof(uint32_t);
    }
    VkSpecializationInfo sc_info{};
    sc_info.mapEntryCount = 3u;
    sc_info.pMapEntries   = sc_entries;
    sc_info.dataSize      = sizeof(sc_data);
    sc_info.pData         = sc_data;

    VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stage.stage               = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module              = shader_mod;
    stage.pName               = "main";
    stage.pSpecializationInfo = &sc_info;

    VkComputePipelineCreateInfo pci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pci.stage  = stage;
    pci.layout = layout_;
    OC_VK_CHECK(vkCreateComputePipelines(
        device_->logical_device(), VK_NULL_HANDLE, 1u, &pci, nullptr, &pipeline_));
    vkDestroyShaderModule(device_->logical_device(), shader_mod, nullptr);
}
// PLACEHOLDER_SHADER_1

void VkComputeShader::compute_fit_size() noexcept {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(device_->physical_device(), &props);
    uint3 hint = function_.dispatch_hint().dim.x > 0
                     ? function_.dispatch_hint().dim : make_uint3(64u, 1u, 1u);
    workgroup_size_ = choose_block_shape(hint, props.limits.maxComputeWorkGroupInvocations);
}

void VkComputeShader::launch(handle_ty stream, ShaderDispatchCommand *cmd) noexcept {
    auto *vk_stream = reinterpret_cast<VkComputeStream *>(stream);
    dispatch(vk_stream->cmd_buf(), cmd);
}

void VkComputeShader::dispatch(VkCommandBuffer cb, ShaderDispatchCommand *cmd) noexcept {
    auto data = cmd->argument_data();
    if (!data.empty()) {
        if (params_buf_.buffer == VK_NULL_HANDLE) {
            params_buf_ = device_->allocate_buffer(
                data.size_bytes(),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                true, "shader_params");
        }
        void *mapped = nullptr;
        vmaMapMemory(device_->vma_allocator(), params_buf_.alloc, &mapped);
        memcpy(mapped, data.data(), data.size_bytes());
        vmaUnmapMemory(device_->vma_allocator(), params_buf_.alloc);
    }

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    VkDescriptorSet gs = device_->global_desc_set();
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, layout_, 0, 1, &gs, 0, nullptr);

    uint8_t pc[c_pc_total_bytes]{};
    uint64_t addr = params_buf_.address;
    memcpy(pc + c_pc_params_offset, &addr, 8);
    uint3 dim = cmd->dispatch_dim();
    memcpy(pc + c_pc_dim_offset, &dim, 12);
    vkCmdPushConstants(cb, layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, c_pc_total_bytes, pc);

    uint3 wgs = workgroup_size_;
    vkCmdDispatch(cb, (dim.x + wgs.x - 1) / wgs.x,
                      (dim.y + wgs.y - 1) / wgs.y,
                      (dim.z + wgs.z - 1) / wgs.z);
}
// PLACEHOLDER_SHADER_2

// ── VkRTShader ────────────────────────────────────────────────────────────────

VkRTShader::VkRTShader(VulkanComputeDevice *device,
                        const vector<uint32_t> &spirv,
                        const Function &f)
    : VkShaderBase(device, f) {
    create_pipeline_layout();
    create_pipeline(spirv);
    build_sbt();
}

VkRTShader::~VkRTShader() noexcept {
    if (rt_pipeline_ != VK_NULL_HANDLE)
        vkDestroyPipeline(device_->logical_device(), rt_pipeline_, nullptr);
    if (layout_ != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device_->logical_device(), layout_, nullptr);
    if (sbt_buf_.buffer != VK_NULL_HANDLE) device_->free_buffer(sbt_buf_);
    if (params_buf_.buffer != VK_NULL_HANDLE) device_->free_buffer(params_buf_);
}

void VkRTShader::create_pipeline_layout() noexcept {
    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
                    VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
    pc.size = c_pc_total_bytes;
    VkDescriptorSetLayout global_layout = device_->global_desc_layout();
    VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    ci.setLayoutCount = 1u; ci.pSetLayouts = &global_layout;
    ci.pushConstantRangeCount = 1u; ci.pPushConstantRanges = &pc;
    OC_VK_CHECK(vkCreatePipelineLayout(device_->logical_device(), &ci, nullptr, &layout_));
}

void VkRTShader::create_pipeline(const vector<uint32_t> &spirv) noexcept {
    auto vkCreateRTP = (PFN_vkCreateRayTracingPipelinesKHR)vkGetDeviceProcAddr(
        device_->logical_device(), "vkCreateRayTracingPipelinesKHR");

    VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smi.codeSize = spirv.size() * 4; smi.pCode = spirv.data();
    VkShaderModule mod = VK_NULL_HANDLE;
    OC_VK_CHECK(vkCreateShaderModule(device_->logical_device(), &smi, nullptr, &mod));

    // Shader stages: raygen=0, miss=1, closest_hit=2 (by convention our codegen emits in this order)
    VkPipelineShaderStageCreateInfo stages[3]{};
    const char *entry_names[3] = {"main_raygen", "main_miss", "main_closesthit"};
    VkShaderStageFlagBits stage_flags[3] = {
        VK_SHADER_STAGE_RAYGEN_BIT_KHR,
        VK_SHADER_STAGE_MISS_BIT_KHR,
        VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};
    for (int i = 0; i < 3; ++i) {
        stages[i].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage  = stage_flags[i];
        stages[i].module = mod;
        stages[i].pName  = entry_names[i];
    }

    VkRayTracingShaderGroupCreateInfoKHR groups[3]{};
    for (auto &g : groups) {
        g.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        g.generalShader      = VK_SHADER_UNUSED_KHR;
        g.closestHitShader   = VK_SHADER_UNUSED_KHR;
        g.anyHitShader       = VK_SHADER_UNUSED_KHR;
        g.intersectionShader = VK_SHADER_UNUSED_KHR;
    }
    // raygen
    groups[0].type          = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    groups[0].generalShader = 0u;
    // miss
    groups[1].type          = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    groups[1].generalShader = 1u;
    // hit
    groups[2].type            = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    groups[2].closestHitShader = 2u;

    VkRayTracingPipelineCreateInfoKHR rtpci{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
    rtpci.stageCount            = 3u;
    rtpci.pStages               = stages;
    rtpci.groupCount            = 3u;
    rtpci.pGroups               = groups;
    rtpci.maxPipelineRayRecursionDepth = 1u;
    rtpci.layout                = layout_;
    OC_VK_CHECK(vkCreateRTP(device_->logical_device(), VK_NULL_HANDLE, nullptr,
                             1u, &rtpci, nullptr, &rt_pipeline_));
    vkDestroyShaderModule(device_->logical_device(), mod, nullptr);
}
// PLACEHOLDER_SHADER_3

void VkRTShader::build_sbt() noexcept {
    auto vkGetRTSGH = (PFN_vkGetRayTracingShaderGroupHandlesKHR)vkGetDeviceProcAddr(
        device_->logical_device(), "vkGetRayTracingShaderGroupHandlesKHR");

    const auto &props = device_->rt_props();
    uint32_t handle_size        = props.shaderGroupHandleSize;
    uint32_t handle_stride      = props.shaderGroupHandleAlignment > handle_size
                                      ? props.shaderGroupHandleAlignment : handle_size;
    uint32_t base_alignment     = props.shaderGroupBaseAlignment;
    uint32_t raygen_size        = (handle_stride + base_alignment - 1) & ~(base_alignment - 1);
    uint32_t miss_size          = (handle_stride + base_alignment - 1) & ~(base_alignment - 1);
    uint32_t hit_size           = (handle_stride + base_alignment - 1) & ~(base_alignment - 1);
    uint32_t total_size         = raygen_size + miss_size + hit_size;

    vector<uint8_t> handles(handle_stride * 3);
    OC_VK_CHECK(vkGetRTSGH(device_->logical_device(), rt_pipeline_, 0, 3,
                            handles.size(), handles.data()));

    sbt_buf_ = device_->allocate_buffer(
        total_size,
        VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        true, "sbt");

    void *mapped = nullptr;
    vmaMapMemory(device_->vma_allocator(), sbt_buf_.alloc, &mapped);
    auto *dst = reinterpret_cast<uint8_t *>(mapped);
    memcpy(dst,                  handles.data(),                    handle_size);
    memcpy(dst + raygen_size,    handles.data() + handle_stride,    handle_size);
    memcpy(dst + raygen_size + miss_size, handles.data() + handle_stride * 2, handle_size);
    vmaUnmapMemory(device_->vma_allocator(), sbt_buf_.alloc);

    raygen_region_   = {sbt_buf_.address,                          handle_stride, raygen_size};
    miss_region_     = {sbt_buf_.address + raygen_size,            handle_stride, miss_size};
    hit_region_      = {sbt_buf_.address + raygen_size + miss_size, handle_stride, hit_size};
    callable_region_ = {};
}

void VkRTShader::launch(handle_ty stream, ShaderDispatchCommand *cmd) noexcept {
    auto *vk_stream = reinterpret_cast<VkComputeStream *>(stream);
    dispatch(vk_stream->cmd_buf(), cmd);
}

void VkRTShader::dispatch(VkCommandBuffer cb, ShaderDispatchCommand *cmd) noexcept {
    auto vkTraceRays = (PFN_vkCmdTraceRaysKHR)vkGetDeviceProcAddr(
        device_->logical_device(), "vkCmdTraceRaysKHR");

    auto data = cmd->argument_data();
    if (!data.empty()) {
        if (params_buf_.buffer == VK_NULL_HANDLE) {
            params_buf_ = device_->allocate_buffer(
                data.size_bytes(),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                true, "rt_params");
        }
        void *mapped = nullptr;
        vmaMapMemory(device_->vma_allocator(), params_buf_.alloc, &mapped);
        memcpy(mapped, data.data(), data.size_bytes());
        vmaUnmapMemory(device_->vma_allocator(), params_buf_.alloc);
    }

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rt_pipeline_);
    VkDescriptorSet gs = device_->global_desc_set();
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                            layout_, 0, 1, &gs, 0, nullptr);

    uint8_t pc[c_pc_total_bytes]{};
    uint64_t addr = params_buf_.address;
    memcpy(pc + c_pc_params_offset, &addr, 8);
    uint3 dim = cmd->dispatch_dim();
    memcpy(pc + c_pc_dim_offset, &dim, 12);
    vkCmdPushConstants(cb, layout_,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
        VK_SHADER_STAGE_MISS_BIT_KHR,
        0, c_pc_total_bytes, pc);

    vkTraceRays(cb, &raygen_region_, &miss_region_, &hit_region_, &callable_region_,
                dim.x, dim.y, dim.z);
}

// ── VkShaderFactory ───────────────────────────────────────────────────────────

VkShaderBase *VkShaderFactory::create(VulkanComputeDevice *device,
                                       const vector<uint32_t> &spirv,
                                       const Function &f) {
    if (f.is_raytracing_kernel() && device->has_rt())
        return new VkRTShader(device, spirv, f);
    return new VkComputeShader(device, spirv, f);
}

}// namespace ocarina
