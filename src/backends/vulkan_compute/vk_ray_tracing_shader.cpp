#include "vk_ray_tracing_shader.h"

namespace ocarina {

VkRayTracingShader::VkRayTracingShader(VulkanComputeDevice *device, const VkShaderProgram &program,
                                       const Function &function, const string &description)
    : VkShaderBase(device, function, program.execution_mode, description) {
    constexpr VkShaderStageFlagBits expected[] = {VK_SHADER_STAGE_RAYGEN_BIT_KHR, VK_SHADER_STAGE_MISS_BIT_KHR,
                                                   VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};
    constexpr uint32_t count = std::size(expected);
    OC_ERROR_IF(program.modules.size() != count, "Expected three ray tracing shader modules for ", description);
    VkPipelineShaderStageCreateInfo stages[count]{};
    VkRayTracingShaderGroupCreateInfoKHR groups[count]{};
    for (uint32_t i = 0; i < count; ++i) {
        auto it = std::find_if(program.modules.begin(), program.modules.end(),
                               [&](const auto &module) { return module.stage == expected[i]; });
        OC_ERROR_IF(it == program.modules.end(), "Missing ray tracing shader stage ", uint32_t(expected[i]), " for ", description);
        stages[i] = create_stage(*it);
        auto &group = groups[i];
        group.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        const bool hit = expected[i] == VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        group.type = hit ? VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR : VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        group.generalShader = hit ? VK_SHADER_UNUSED_KHR : i;
        group.closestHitShader = hit ? i : VK_SHADER_UNUSED_KHR;
        group.anyHitShader = VK_SHADER_UNUSED_KHR;
        group.intersectionShader = VK_SHADER_UNUSED_KHR;
    }
    VkRayTracingPipelineCreateInfoKHR info{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
    info.stageCount = count;
    info.pStages = stages;
    info.groupCount = count;
    info.pGroups = groups;
    info.maxPipelineRayRecursionDepth = 1;
    info.layout = layout_;
    const bool statistics = device_->has_pipeline_statistics();
    if (statistics) info.flags |= VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
    const auto &functions = device_->ray_tracing_pipeline_functions();
    OC_VK_CHECK(functions.create_ray_tracing_pipelines(device_->logical_device(), VK_NULL_HANDLE, VK_NULL_HANDLE,
                                                      1u, &info, nullptr, &pipeline_));
    if (statistics) device_->report_pipeline_statistics(pipeline_, description);
    for (const auto &stage : stages) vkDestroyShaderModule(device_->logical_device(), stage.module, nullptr);
    create_sbt();

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(device_->physical_device(), &props);
    limits_.max_invocations = device_->ray_tracing_pipeline_properties().maxRayDispatchInvocationCount;
    for (size_t i = 0; i < limits_.max_dimensions.size(); ++i)
        limits_.max_dimensions[i] = uint64_t(props.limits.maxComputeWorkGroupCount[i]) * props.limits.maxComputeWorkGroupSize[i];
}

VkRayTracingShader::~VkRayTracingShader() noexcept {
    device_->free_buffer(sbt_);
}

void VkRayTracingShader::create_sbt() noexcept {
    const auto &props = device_->ray_tracing_pipeline_properties();
    const VkDeviceSize handle_size = props.shaderGroupHandleSize;
    const VkDeviceSize base_alignment = props.shaderGroupBaseAlignment;
    const VkDeviceSize stride = mem_offset(handle_size, props.shaderGroupHandleAlignment);
    OC_ERROR_IF(stride > props.maxShaderGroupStride, "Shader binding table stride exceeds maxShaderGroupStride");
    const VkDeviceSize spacing = mem_offset(stride, base_alignment);
    // The buffer's absolute address and every region start must satisfy base alignment.
    const VkDeviceSize bytes = base_alignment - 1 + spacing * (regions_.size() - 1) + stride;
    sbt_ = device_->allocate_buffer(bytes, VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR, true, "trace-sbt", base_alignment);
    const VkDeviceSize first_offset = (base_alignment - sbt_.address % base_alignment) % base_alignment;
    vector<uint8_t> handles(handle_size * regions_.size());
    const auto &functions = device_->ray_tracing_pipeline_functions();
    OC_VK_CHECK(functions.get_ray_tracing_shader_group_handles(device_->logical_device(), pipeline_, 0,
                 uint32_t(regions_.size()), handles.size(), handles.data()));

    void *mapped = nullptr;
    OC_VK_CHECK(vmaMapMemory(device_->vma_allocator(), sbt_.alloc, &mapped));
    for (size_t i = 0; i < regions_.size(); ++i) {
        const VkDeviceSize offset = first_offset + i * spacing;
        memcpy(static_cast<uint8_t *>(mapped) + offset, handles.data() + i * handle_size, handle_size);
        regions_[i] = {sbt_.address + offset, stride, stride};
    }
    OC_VK_CHECK(vmaFlushAllocation(device_->vma_allocator(), sbt_.alloc, 0, VK_WHOLE_SIZE));
    vmaUnmapMemory(device_->vma_allocator(), sbt_.alloc);
}

void VkRayTracingShader::record_dispatch(VkCommandBuffer cb, uint3 dim, uint8_t *push_constants) const noexcept {
    const std::array dimensions{dim.x, dim.y, dim.z};
    if (auto axis = limits_.exceeded_axis(dimensions)) {
        OC_ERROR("Ray dispatch dimension ", dimensions[*axis], " exceeds axis ", *axis, " limit ", limits_.max_dimensions[*axis]);
    }
    OC_ERROR_IF(limits_.exceeds_invocation_count(dimensions), "Ray dispatch ", dim.x, " x ", dim.y, " x ", dim.z,
                " exceeds maxRayDispatchInvocationCount ", limits_.max_invocations);
    vkCmdPushConstants(cb, layout_, parameter_stage(), 0, parameter_layout_.push_bytes(), push_constants);
    constexpr VkStridedDeviceAddressRegionKHR callable{};
    device_->ray_tracing_pipeline_functions().cmd_trace_rays(cb, &regions_[0], &regions_[1], &regions_[2], &callable,
                                                             dim.x, dim.y, dim.z);
}

}// namespace ocarina
