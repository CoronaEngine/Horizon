//
//

#define VMA_IMPLEMENTATION
#include "vk_compute_device.h"
#include "vk_stream.h"
#include "vk_texture.h"
#include "vk_shader.h"
#include "vk_mesh.h"
#include "vk_bindless_array.h"
#include "vk_accel.h"
#include "vk_command_visitor.h"
#include "rhi/context.h"
#ifdef _WIN32
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#endif
#include <stdexcept>
#include <cstdlib>
#include <fstream>
#include <iomanip>


namespace ocarina {

// ── Constructor ───────────────────────────────────────────────────────────────

VulkanComputeDevice::VulkanComputeDevice(RHIContext *context)
    : Device::Impl(context) {
    if (const char *path = std::getenv("CORONA_GPU_ALLOCATION_TRACE"); path && *path) {
        allocation_trace_ = std::fopen(path, "wb");
        if (allocation_trace_) {
            std::fputs("event_id\tevent\tkind\tresource_handle\tvma_allocation\tname\trequested_buffer_bytes\tbuffer_bytes\twidth\theight\tdepth\tvk_format\tmip_levels\tvma_size\tvma_offset\tdevice_memory\tmemory_type\tmemory_property_flags\n", allocation_trace_);
        } else {
            OC_WARNING_FORMAT("CORONA_GPU_ALLOCATION_TRACE: cannot open {}", path);
        }
    }
    init_instance();
    init_physical_device();
    init_logical_device();
    init_vma();
    init_descriptor_pool();
    init_samplers();
    compiler_     = std::make_unique<SlangShaderCompiler>(this);
    cmd_visitor_  = std::make_unique<VkComputeCommandVisitor>(this);
}

// ── init_instance ─────────────────────────────────────────────────────────────

void VulkanComputeDevice::init_instance() noexcept {
    VkApplicationInfo app_info{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app_info.apiVersion = VK_API_VERSION_1_2;

    vector<const char *> extensions{
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME};

    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo        = &app_info;
    ci.enabledExtensionCount   = (uint32_t)extensions.size();
    ci.ppEnabledExtensionNames = extensions.data();
    OC_VK_CHECK(vkCreateInstance(&ci, nullptr, &instance_));
}

// ── init_physical_device ──────────────────────────────────────────────────────

void VulkanComputeDevice::init_physical_device() noexcept {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    OC_ERROR_IF(count == 0, "No Vulkan physical device found");
    vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(instance_, &count, devs.data());

    phys_device_ = devs[0];
    for (auto d : devs) {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(d, &p);
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { phys_device_ = d; break; }
    }
    VkPhysicalDeviceProperties selected{};
    vkGetPhysicalDeviceProperties(phys_device_, &selected);
    OC_INFO_FORMAT("Created Vulkan device: {} (vendor = {}, device = {})",
            selected.deviceName, selected.vendorID, selected.deviceID);

    VkPhysicalDeviceSubgroupProperties subgroup{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceProperties2 subgroup_properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    subgroup_properties.pNext = &subgroup;
    vkGetPhysicalDeviceProperties2(phys_device_, &subgroup_properties);
    OC_ERROR_IF(subgroup.subgroupSize < 32u || subgroup.subgroupSize > 128u || subgroup.subgroupSize % 32u != 0u ||
                !(subgroup.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) ||
                !(subgroup.supportedOperations & VK_SUBGROUP_FEATURE_BALLOT_BIT),
        "Vulkan backend requires compute ballot subgroups of 32, 64, 96 or 128 lanes to preserve DSL warp32 semantics");

    // RT support probe
    uint32_t ec = 0;
    vkEnumerateDeviceExtensionProperties(phys_device_, nullptr, &ec, nullptr);
    vector<VkExtensionProperties> exts(ec);
    vkEnumerateDeviceExtensionProperties(phys_device_, nullptr, &ec, exts.data());
    auto supports = [&](const char *name) {
        return std::any_of(exts.begin(), exts.end(), [&](const auto &e) {
            return strcmp(e.extensionName, name) == 0;
        });
    };
    has_rt_ = supports(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
              supports(VK_KHR_RAY_QUERY_EXTENSION_NAME) &&
              supports(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    if (const char *path = std::getenv("REBLUR_GPU_PIPELINE_STATS"); path && *path) {
        pipeline_statistics_path_ = path;
        has_pipeline_statistics_ = supports(VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME);
        if (!has_pipeline_statistics_)
            OC_WARNING("REBLUR_GPU_PIPELINE_STATS: VK_KHR_pipeline_executable_properties is unavailable");
    }
#ifdef _WIN32
    has_external_memory_ = supports(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
    if (has_external_memory_) {
        VkPhysicalDeviceExternalBufferInfo external{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO};
        external.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        external.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
        VkExternalBufferProperties properties{VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};
        vkGetPhysicalDeviceExternalBufferProperties(phys_device_, &external, &properties);
        has_external_memory_ = (properties.externalMemoryProperties.externalMemoryFeatures &
                                VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) != 0;
    }
#endif
    if (has_rt_) {
        VkPhysicalDeviceAccelerationStructurePropertiesKHR as_props{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
        VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        props.pNext = &as_props;
        vkGetPhysicalDeviceProperties2(phys_device_, &props);
        scratch_alignment_ = as_props.minAccelerationStructureScratchOffsetAlignment;
    }

    // Find compute queue
    uint32_t qfc = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys_device_, &qfc, nullptr);
    vector<VkQueueFamilyProperties> qfp(qfc);
    vkGetPhysicalDeviceQueueFamilyProperties(phys_device_, &qfc, qfp.data());
    for (uint32_t i = 0; i < qfc; ++i) {
        if (qfp[i].queueCount) shared_queue_families_.push_back(i);
    }
    compute_queue_family_ = UINT32_MAX;
    for (uint32_t i = 0; i < qfc; ++i)
        if (qfp[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { compute_queue_family_ = i; break; }
    OC_ERROR_IF(compute_queue_family_ == UINT32_MAX, "No compute queue family");
}

// ── init_logical_device ───────────────────────────────────────────────────────

void VulkanComputeDevice::init_logical_device() noexcept {
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = compute_queue_family_;
    qci.queueCount       = 1u;
    qci.pQueuePriorities = &priority;

    vector<const char *> exts;
    if (has_rt_) {
        exts.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        exts.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        exts.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
    }

    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan11Features f11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    f11.pNext = &f12;
    VkPhysicalDeviceRayQueryFeaturesKHR query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accel{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    if (has_rt_) { f12.pNext = &accel; accel.pNext = &query; }
    VkPhysicalDeviceFeatures2 feats2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    feats2.pNext = &f11;
    VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR pipeline_features{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR};
    if (has_pipeline_statistics_) {
        pipeline_features.pNext = &f11;
        feats2.pNext = &pipeline_features;
    }
    vkGetPhysicalDeviceFeatures2(phys_device_, &feats2);
    if (has_pipeline_statistics_ && !pipeline_features.pipelineExecutableInfo) {
        has_pipeline_statistics_ = false;
        feats2.pNext = &f11;
        OC_WARNING("REBLUR_GPU_PIPELINE_STATS: pipelineExecutableInfo feature is unavailable");
    }
    OC_ERROR_IF(!f12.bufferDeviceAddress || !f12.runtimeDescriptorArray || !f12.scalarBlockLayout || !f12.timelineSemaphore ||
                    !f12.descriptorBindingPartiallyBound || !f12.descriptorBindingSampledImageUpdateAfterBind ||
                    !f12.descriptorBindingStorageImageUpdateAfterBind ||
                    !f12.shaderSampledImageArrayNonUniformIndexing || !f12.shaderStorageImageArrayNonUniformIndexing,
        "Vulkan backend requires buffer device addresses, descriptor indexing and scalar block layout");
    if (has_rt_ && (!accel.accelerationStructure || !query.rayQuery)) {
        has_rt_ = false;
        f12.pNext = nullptr;
        exts.clear();
    }

    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    // Append after the RT feature fallback, which clears the RT extensions.
    if (has_pipeline_statistics_)
        exts.push_back(VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME);
#ifdef _WIN32
    if (has_external_memory_) exts.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
#endif
    dci.pNext                   = &feats2;
    dci.queueCreateInfoCount    = 1u;
    dci.pQueueCreateInfos       = &qci;
    dci.enabledExtensionCount   = (uint32_t)exts.size();
    dci.ppEnabledExtensionNames = exts.data();
    OC_VK_CHECK(vkCreateDevice(phys_device_, &dci, nullptr, &device_));
    vkGetDeviceQueue(device_, compute_queue_family_, 0u, &compute_queue_);
    if (has_pipeline_statistics_) {
        get_pipeline_executable_properties_ = reinterpret_cast<PFN_vkGetPipelineExecutablePropertiesKHR>(
            vkGetDeviceProcAddr(device_, "vkGetPipelineExecutablePropertiesKHR"));
        get_pipeline_executable_statistics_ = reinterpret_cast<PFN_vkGetPipelineExecutableStatisticsKHR>(
            vkGetDeviceProcAddr(device_, "vkGetPipelineExecutableStatisticsKHR"));
        if (!get_pipeline_executable_properties_ || !get_pipeline_executable_statistics_) {
            has_pipeline_statistics_ = false;
            OC_WARNING("REBLUR_GPU_PIPELINE_STATS: executable statistics entry points are unavailable");
        }
    }
}

void VulkanComputeDevice::report_pipeline_statistics(VkPipeline pipeline, const string &description) noexcept {
    if (!has_pipeline_statistics_) return;
    VkPipelineInfoKHR info{VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR};
    info.pipeline = pipeline;
    uint32_t count{};
    auto result = get_pipeline_executable_properties_(device_, &info, &count, nullptr);
    if (result != VK_SUCCESS || !count) {
        OC_WARNING_FORMAT("REBLUR_GPU_PIPELINE_STATS: no executable properties for {} (VkResult {})",
            description, int(result));
        return;
    }
    vector<VkPipelineExecutablePropertiesKHR> executables(count);
    for (auto &executable : executables)
        executable.sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR;
    result = get_pipeline_executable_properties_(device_, &info, &count, executables.data());
    if (result != VK_SUCCESS) {
        OC_WARNING_FORMAT("REBLUR_GPU_PIPELINE_STATS: executable properties query failed for {} (VkResult {})",
            description, int(result));
        return;
    }
    // Pipeline compilation can run on several host threads. Protect the complete
    // CSV append, including the header, without introducing any GPU submission.
    static std::mutex output_mutex;
    std::lock_guard lock(output_mutex);
    std::ofstream output(std::filesystem::path(pipeline_statistics_path_), std::ios::app);
    if (!output) {
        OC_WARNING_FORMAT("REBLUR_GPU_PIPELINE_STATS: cannot open {}", pipeline_statistics_path_);
        return;
    }
    if (output.tellp() == std::streampos(0))
        output << "shader,executable_index,executable_name,subgroup_size,stages,statistic_name,statistic_description,format,value\n";
    output << std::setprecision(17);
    auto quoted = [&](const char *value) {
        output << '"';
        for (; *value; ++value) {
            if (*value == '"') output << '"';
            output << *value;
        }
        output << '"';
    };
    for (uint32_t i = 0; i < count; ++i) {
        VkPipelineExecutableInfoKHR executable_info{VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR};
        executable_info.pipeline = pipeline;
        executable_info.executableIndex = i;
        uint32_t statistic_count{};
        result = get_pipeline_executable_statistics_(device_, &executable_info, &statistic_count, nullptr);
        if (result != VK_SUCCESS || !statistic_count) {
            OC_WARNING_FORMAT("REBLUR_GPU_PIPELINE_STATS: no statistics for {} executable {} (VkResult {})",
                description, i, int(result));
            continue;
        }
        vector<VkPipelineExecutableStatisticKHR> statistics(statistic_count);
        for (auto &statistic : statistics)
            statistic.sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR;
        result = get_pipeline_executable_statistics_(device_, &executable_info, &statistic_count, statistics.data());
        if (result != VK_SUCCESS) {
            OC_WARNING_FORMAT("REBLUR_GPU_PIPELINE_STATS: statistics query failed for {} executable {} (VkResult {})",
                description, i, int(result));
            continue;
        }
        for (uint32_t j = 0; j < statistic_count; ++j) {
            const auto &statistic = statistics[j];
            quoted(description.c_str()); output << ',' << i << ',';
            quoted(executables[i].name);
            output << ',' << executables[i].subgroupSize << ',' << executables[i].stages << ',';
            quoted(statistic.name); output << ',';
            quoted(statistic.description); output << ',' << int(statistic.format) << ',';
            switch (statistic.format) {
                case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR:
                    output << (statistic.value.b32 != VK_FALSE); break;
                case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR:
                    output << statistic.value.i64; break;
                case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR:
                    output << statistic.value.u64; break;
                case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR:
                    output << statistic.value.f64; break;
                default: break;
            }
            output << '\n';
        }
    }
}

// ── init_vma ──────────────────────────────────────────────────────────────────

void VulkanComputeDevice::init_vma() noexcept {
    VmaAllocatorCreateInfo ai{};
    ai.physicalDevice   = phys_device_;
    ai.device           = device_;
    ai.instance         = instance_;
    ai.vulkanApiVersion = VK_API_VERSION_1_2;
    ai.flags            = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    OC_VK_CHECK(vmaCreateAllocator(&ai, &allocator_));
}

// ── init_descriptor_pool ──────────────────────────────────────────────────────

void VulkanComputeDevice::init_descriptor_pool() noexcept {
    VkPhysicalDeviceDescriptorIndexingProperties indexing{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext = &indexing;
    vkGetPhysicalDeviceProperties2(phys_device_, &properties);
    parameter_uniform_alignment_ = std::max(VkDeviceSize(16u), properties.properties.limits.minUniformBufferOffsetAlignment);
    // One dynamic UBO at set1 and range832 fit Vulkan1.2's guaranteed minima.
    // No optional feature or new device-wide failure is introduced here.
    VkDescriptorSetLayoutBinding parameter_binding{};
    parameter_binding.binding = 0u;
    parameter_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    parameter_binding.descriptorCount = 1u;
    parameter_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo parameter_layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    parameter_layout.bindingCount = 1u;
    parameter_layout.pBindings = &parameter_binding;
    OC_VK_CHECK(vkCreateDescriptorSetLayout(device_, &parameter_layout, nullptr, &parameter_uniform_layout_));
    constexpr uint32_t sampled_count = max_texture_slots * 2u;
    constexpr uint32_t storage_count = max_texture_slots * 4u;
    constexpr uint32_t resource_count = sampled_count + storage_count + c_sampler_count + 1u;
    OC_ERROR_IF(indexing.maxPerStageDescriptorUpdateAfterBindSampledImages < sampled_count ||
                indexing.maxDescriptorSetUpdateAfterBindSampledImages < sampled_count ||
                indexing.maxPerStageDescriptorUpdateAfterBindStorageImages < storage_count ||
                indexing.maxDescriptorSetUpdateAfterBindStorageImages < storage_count ||
                indexing.maxPerStageUpdateAfterBindResources < resource_count ||
                indexing.maxUpdateAfterBindDescriptorsInAllPools < resource_count,
        "Vulkan device cannot support the public 50000-texture capacity: requires 100002 sampled and 200004 storage update-after-bind image descriptors");
    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, max_texture_slots * 2},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, max_texture_slots * 4},
        {VK_DESCRIPTOR_TYPE_SAMPLER, c_sampler_count},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pci.maxSets = 1; pci.poolSizeCount = 4; pci.pPoolSizes = sizes;
    OC_VK_CHECK(vkCreateDescriptorPool(device_, &pci, nullptr, &desc_pool_));
    VkDescriptorSetLayoutBinding bindings[8]{};
    VkDescriptorBindingFlags flags[8]{};
    for (uint32_t i = 0; i < 8; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = i == 7 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER :
            i == 4 ? VK_DESCRIPTOR_TYPE_SAMPLER :
            (i == 0 || i == 2 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        bindings[i].descriptorCount = i == 7 ? 1u : i == 4 ? c_sampler_count : max_texture_slots;
        bindings[i].stageFlags = VK_SHADER_STAGE_ALL;
        if (i != 4 && i != 7) flags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    }
    VkDescriptorSetLayoutBindingFlagsCreateInfo f{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    f.bindingCount = 8; f.pBindingFlags = flags;
    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.pNext = &f; lci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    lci.bindingCount = 8; lci.pBindings = bindings;
    OC_VK_CHECK(vkCreateDescriptorSetLayout(device_, &lci, nullptr, &global_layout_));
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = desc_pool_; ai.descriptorSetCount = 1; ai.pSetLayouts = &global_layout_;
    OC_VK_CHECK(vkAllocateDescriptorSets(device_, &ai, &global_set_));
    // Component-sized DSL surface writes require a read/modify/write on Vulkan
    // images. Hash texture/texel pairs into a shared lock table to preserve independent
    // component writes without serializing ordinary full-texel stores.
    texture_write_locks_ = allocate_buffer(65536u * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        false, "texture-write-locks");
    immediate_submit([&](VkCommandBuffer cb) {
        vkCmdFillBuffer(cb, texture_write_locks_.buffer, 0, texture_write_locks_.size, 0);
    });
    VkDescriptorBufferInfo locks{texture_write_locks_.buffer, 0, texture_write_locks_.size};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = global_set_;
    write.dstBinding = 7;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &locks;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
}
uint32_t VulkanComputeDevice::allocate_texture_slot(bool is_3d) noexcept {
    auto guard = buffer_map_guard_.lock();
    auto &free_slots = free_texture_slots_[is_3d];
    auto &next_slot = next_texture_slot_[is_3d];
    if (!free_slots.empty()) {
        auto slot = free_slots.back(); free_slots.pop_back(); return slot;
    }
    OC_ERROR_IF(next_slot >= max_texture_slots, "Vulkan texture slots exhausted for this dimension");
    return next_slot++;
}
void VulkanComputeDevice::release_texture_slot(uint32_t slot, bool is_3d) noexcept {
    auto guard = buffer_map_guard_.lock();
    if (slot) free_texture_slots_[is_3d].push_back(slot);
}

// ── init_samplers ─────────────────────────────────────────────────────────────

void VulkanComputeDevice::init_samplers() noexcept {
    for (uint32_t i = 0; i < c_sampler_count; ++i) {
        VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sci.magFilter    = i == 2u ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        sci.minFilter    = sci.magFilter;
        sci.mipmapMode   = (i & 2u) && i != 2u ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        // Slot zero retains the public DSL sampler contract. NRD's typed
        // textures use slots one and two for linear and point clamp sampling.
        sci.addressModeU = (i == 1u || i == 2u) ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                                             : VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        sci.addressModeV = sci.addressModeU;
        sci.addressModeW = sci.addressModeU;
        sci.maxLod       = VK_LOD_CLAMP_NONE;
        OC_VK_CHECK(vkCreateSampler(device_, &sci, nullptr, &samplers_[i]));
    }
    VkDescriptorImageInfo images[c_sampler_count]{};
    for (uint32_t i = 0; i < c_sampler_count; ++i) images[i].sampler = samplers_[i];
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = global_set_; write.dstBinding = 4;
    write.descriptorCount = c_sampler_count; write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    write.pImageInfo = images;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

}

// Alloc/free rows join by kind + resource_handle + vma_allocation. Names and
// descriptors are written on alloc rows; no additional resource map is needed.
void VulkanComputeDevice::trace_allocation(const char *event, const char *kind,
                                           handle_ty handle, VmaAllocation allocation,
                                           const char *name, uint64_t requested_bytes,
                                           uint64_t buffer_bytes, VkExtent3D extent,
                                           VkFormat format, uint32_t mip_levels) noexcept {
    if (!allocation_trace_) return;
    std::lock_guard lock(allocation_trace_mutex_);
    VmaAllocationInfo info{};
    vmaGetAllocationInfo(allocator_, allocation, &info);
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(phys_device_, &memory);
    const auto properties = memory.memoryTypes[info.memoryType].propertyFlags;
    std::fprintf(allocation_trace_, "%llu\t%s\t%s\t%llu\t%llu\t",
        static_cast<unsigned long long>(++allocation_trace_event_), event, kind,
        static_cast<unsigned long long>(handle),
        static_cast<unsigned long long>(reinterpret_cast<handle_ty>(allocation)));
    for (const char *p = name; *p; ++p) {
        switch (*p) {
            case '\\': std::fputs("\\\\", allocation_trace_); break;
            case '\t': std::fputs("\\t", allocation_trace_); break;
            case '\r': std::fputs("\\r", allocation_trace_); break;
            case '\n': std::fputs("\\n", allocation_trace_); break;
            default: std::fputc(static_cast<unsigned char>(*p), allocation_trace_); break;
        }
    }
    std::fprintf(allocation_trace_, "\t%llu\t%llu\t%u\t%u\t%u\t%u\t%u\t%llu\t%llu\t%llu\t%u\t%u\n",
        static_cast<unsigned long long>(requested_bytes),
        static_cast<unsigned long long>(buffer_bytes),
        extent.width, extent.height, extent.depth, static_cast<uint32_t>(format), mip_levels,
        static_cast<unsigned long long>(info.size),
        static_cast<unsigned long long>(info.offset),
        static_cast<unsigned long long>(reinterpret_cast<handle_ty>(info.deviceMemory)),
        info.memoryType, properties);
    std::fflush(allocation_trace_);
}

// ── Buffer allocation ─────────────────────────────────────────────────────────

VkBufferAllocation VulkanComputeDevice::allocate_buffer(size_t size,
                                                        VkBufferUsageFlags usage,
                                                        bool host_visible,
                                                        const string &name, VkDeviceSize alignment,
                                                        bool readback, bool exported) noexcept {
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size        = size > 0 ? size : 4;
    bci.usage       = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if (has_rt_ && !exported) bci.usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkExternalMemoryBufferCreateInfo external{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO};
    VkExportMemoryAllocateInfo export_memory{VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO};
    if (exported) {
        OC_ERROR_IF(!has_external_memory_, "Vulkan device does not support buffer export");
#ifdef _WIN32
        external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
        export_memory.handleTypes = external.handleTypes;
#endif
        bci.pNext = &external;
        if (shared_queue_families_.size() > 1) {
            bci.sharingMode = VK_SHARING_MODE_CONCURRENT;
            bci.queueFamilyIndexCount = uint32_t(shared_queue_families_.size());
            bci.pQueueFamilyIndices = shared_queue_families_.data();
        }
        VkPhysicalDeviceExternalBufferInfo query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO};
        query.usage = bci.usage;
        query.handleType = VkExternalMemoryHandleTypeFlagBits(external.handleTypes);
        VkExternalBufferProperties props{VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};
        vkGetPhysicalDeviceExternalBufferProperties(phys_device_, &query, &props);
        OC_ERROR_IF(!(props.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT),
                    "Vulkan storage buffer is not exportable");
    }
    VmaAllocationCreateInfo aci{};
    aci.usage = host_visible ? VMA_MEMORY_USAGE_CPU_TO_GPU : VMA_MEMORY_USAGE_GPU_ONLY;
    if (readback) aci.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
    if (host_visible) aci.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (host_visible) aci.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    if (readback) {
        aci.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        aci.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    }
    VkBufferAllocation result{};
    if (exported) {
        aci.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        OC_VK_CHECK(vmaCreateDedicatedBuffer(allocator_, &bci, &aci, &export_memory,
            &result.buffer, &result.alloc, nullptr));
    } else {
        OC_VK_CHECK(vmaCreateBufferWithAlignment(allocator_, &bci, &aci, alignment,
            &result.buffer, &result.alloc, nullptr));
    }
    result.exported = exported;
    result.size = bci.size;
    VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    ai.buffer    = result.buffer;
    result.address = vkGetBufferDeviceAddress(device_, &ai);
    {
        auto g = buffer_map_guard_.lock();
        buffer_map_[result.address] = result;
    }
    trace_allocation("alloc", "buffer", result.address, result.alloc, name.c_str(), size, bci.size);
    return result;
}

void VulkanComputeDevice::free_buffer(VkBufferAllocation &alloc) noexcept {
    if (alloc.buffer == VK_NULL_HANDLE) return;
    trace_allocation("free", "buffer", alloc.address, alloc.alloc);
    {
        auto g = buffer_map_guard_.lock();
        buffer_map_.erase(alloc.address);
    }
    vmaDestroyBuffer(allocator_, alloc.buffer, alloc.alloc);
    alloc = {};
}

VkBuffer VulkanComputeDevice::get_vk_buffer(handle_ty addr, VkDeviceSize *offset) noexcept {
    auto g = buffer_map_guard_.lock();
    auto it = buffer_map_.upper_bound(addr);
    OC_ERROR_IF(it == buffer_map_.begin(), "Unknown Vulkan buffer address");
    --it;
    auto delta = addr - it->first;
    OC_ERROR_IF(delta >= it->second.size, "Vulkan buffer address is outside its allocation");
    if (offset) *offset = delta;
    return it->second.buffer;
}

VkImageAllocation VulkanComputeDevice::allocate_image(uint3 extent, VkFormat fmt,
                                                       uint32_t levels,
                                                       VkImageUsageFlags usage,
                                                       const string &name, bool is_3d) noexcept {
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType   = is_3d ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    ici.format      = fmt;
    // DSL surface uint loads/stores reinterpret FLOAT image bits. A compatible
    // integer view preserves them without a floating-point load/conversion.
    if (fmt == VK_FORMAT_R8_UNORM || fmt == VK_FORMAT_R8G8_UNORM ||
        fmt == VK_FORMAT_R8G8B8A8_UNORM || fmt == VK_FORMAT_R32_SFLOAT ||
        fmt == VK_FORMAT_R32G32_SFLOAT || fmt == VK_FORMAT_R32G32B32A32_SFLOAT)
        ici.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    ici.extent      = {extent.x, extent.y, std::max(extent.z, 1u)};
    ici.mipLevels   = levels;
    ici.arrayLayers = 1u;
    ici.samples     = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling      = VK_IMAGE_TILING_OPTIMAL;
    ici.usage       = usage;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    VkImageAllocation result{};
    OC_VK_CHECK(vmaCreateImage(allocator_, &ici, &aci, &result.image, &result.alloc, nullptr));
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image    = result.image;
    vci.viewType = is_3d ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D;
    vci.format   = fmt;
    // DSL fills every absent sampled channel with zero, including alpha.
    // Apply this only to the sampled view: storage views require identity.
    if (fmt == VK_FORMAT_R8_UNORM || fmt == VK_FORMAT_R8G8_UNORM || fmt == VK_FORMAT_R16_SFLOAT ||
        fmt == VK_FORMAT_R32_SFLOAT || fmt == VK_FORMAT_R32G32_SFLOAT)
        vci.components.a = VK_COMPONENT_SWIZZLE_ZERO;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
    OC_VK_CHECK(vkCreateImageView(device_, &vci, nullptr, &result.view));
    immediate_submit([&](VkCommandBuffer cb) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.image = result.image;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    });
    // Storage image descriptors address only the base mip.
    vci.components = {};
    vci.subresourceRange.levelCount = 1;
    OC_VK_CHECK(vkCreateImageView(device_, &vci, nullptr, &result.storage_view));
    {
        auto g = buffer_map_guard_.lock();
        image_map_[reinterpret_cast<handle_ty>(result.image)] = result;
    }
    trace_allocation("alloc", "image", reinterpret_cast<handle_ty>(result.image), result.alloc,
                     name.c_str(), 0, 0, ici.extent, ici.format, ici.mipLevels);
    return result;
}

void VulkanComputeDevice::free_image(VkImageAllocation &alloc) noexcept {
    if (alloc.image == VK_NULL_HANDLE) return;
    trace_allocation("free", "image", reinterpret_cast<handle_ty>(alloc.image), alloc.alloc);
    {
        auto g = buffer_map_guard_.lock();
        image_map_.erase(reinterpret_cast<handle_ty>(alloc.image));
    }
    if (alloc.storage_view != VK_NULL_HANDLE) vkDestroyImageView(device_, alloc.storage_view, nullptr);
    if (alloc.view != VK_NULL_HANDLE) vkDestroyImageView(device_, alloc.view, nullptr);
    vmaDestroyImage(allocator_, alloc.image, alloc.alloc);
    alloc = {};
}

VkImageAllocation &VulkanComputeDevice::get_vk_image(handle_ty handle) noexcept {
    auto g = buffer_map_guard_.lock();
    return image_map_.at(handle);
}

void VulkanComputeDevice::immediate_submit(std::function<void(VkCommandBuffer)> &&fn) noexcept {
    std::lock_guard lock(submission_mutex_);
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.queueFamilyIndex = compute_queue_family_;
    pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    VkCommandPool pool = VK_NULL_HANDLE;
    OC_VK_CHECK(vkCreateCommandPool(device_, &pci, nullptr, &pool));
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    OC_VK_CHECK(vkAllocateCommandBuffers(device_, &ai, &cb));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    OC_VK_CHECK(vkBeginCommandBuffer(cb, &bi));
    VkMemoryBarrier memory_barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    memory_barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    memory_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    auto barrier = [&] {
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 1, &memory_barrier, 0, nullptr, 0, nullptr);
    };
    barrier();
    fn(cb);
    barrier();
    OC_VK_CHECK(vkEndCommandBuffer(cb));
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = VK_NULL_HANDLE;
    OC_VK_CHECK(vkCreateFence(device_, &fi, nullptr, &fence));
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    OC_VK_CHECK(vkQueueSubmit(compute_queue_, 1, &si, fence));
    OC_VK_CHECK(vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX));
    vkDestroyFence(device_, fence, nullptr);
    vkDestroyCommandPool(device_, pool, nullptr);
}

void VulkanComputeDevice::submit(VkCommandBuffer cb, VkFence fence,
                                 VkSemaphore timeline, uint64_t wait_value) noexcept {
    std::lock_guard lock(submission_mutex_);
    VkTimelineSemaphoreSubmitInfo values{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    values.waitSemaphoreValueCount = 1;
    values.pWaitSemaphoreValues = &wait_value;
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    if (wait_value) {
        submit.pNext = &values;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &timeline;
        submit.pWaitDstStageMask = &stage;
    }
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cb;
    OC_VK_CHECK(vkQueueSubmit(compute_queue_, 1, &submit, fence));
}
void VulkanComputeDevice::register_stream(VkComputeStream *stream) noexcept {
    std::lock_guard lock(streams_mutex_);
    streams_.push_back(stream);
}
void VulkanComputeDevice::unregister_stream(VkComputeStream *stream) noexcept {
    std::lock_guard lock(streams_mutex_);
    auto it = std::find(streams_.begin(), streams_.end(), stream);
    if (it != streams_.end()) streams_.erase(it);
}
void VulkanComputeDevice::synchronize_streams() noexcept {
    // keep_alive may release its final resource reference on a completion
    // thread. Its GPU batch has finished and dependent submissions are deferred;
    // waiting here would wait for this same callback to return.
    // As with DSL, resources used by another stream must be retained there.
    if (VkComputeStream::is_completion_thread(this)) return;
    std::lock_guard lock(streams_mutex_);
    for (auto *stream : streams_) stream->wait_idle();
}
void VulkanComputeDevice::run_commit_callback(const std::function<void(void *)> &callback) noexcept {
    std::lock_guard lock(submission_mutex_);
    callback(reinterpret_cast<void *>(compute_queue_));
}

// ── Device::Impl create/destroy ───────────────────────────────────────────────

handle_ty VulkanComputeDevice::create_buffer(size_t size, const string &desc,
                                              bool exported) noexcept {
    if (size == 0) return 0;
    auto alloc = allocate_buffer(size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false, desc, 1, false, exported);
    return alloc.address;
}

void VulkanComputeDevice::destroy_buffer(handle_ty handle) noexcept {
    synchronize_streams();
    auto g = buffer_map_guard_.lock();
    auto it = buffer_map_.find(handle);
    if (it == buffer_map_.end()) return;
    auto alloc = it->second;
    buffer_map_.erase(it);
    g.unlock();
    trace_allocation("free", "buffer", alloc.address, alloc.alloc);
    vmaDestroyBuffer(allocator_, alloc.buffer, alloc.alloc);
}

handle_ty VulkanComputeDevice::create_texture3d(uint3 res, PixelStorage ps,
                                                 uint level_num,
                                                 const string &desc) noexcept {
    auto *tex = new VkTexture3D(this, res, ps, level_num);
    return reinterpret_cast<handle_ty>(tex);
}

handle_ty VulkanComputeDevice::create_texture2d(uint2 res, PixelStorage ps,
                                                 uint level_num,
                                                 const string &desc) noexcept {
    auto *tex = new VkTexture2D(this, make_uint3(res.x, res.y, 1u), ps, level_num);
    return reinterpret_cast<handle_ty>(tex);
}

handle_ty VulkanComputeDevice::create_texture2d_from_external(uint handle,
                                                               const string &) noexcept {
    auto *tex = new VkTexture2D(this, handle);
    return reinterpret_cast<handle_ty>(tex);
}

void VulkanComputeDevice::destroy_texture3d(handle_ty handle) noexcept {
    synchronize_streams();
    delete reinterpret_cast<VkTexture3D *>(handle);
}

void VulkanComputeDevice::destroy_texture2d(handle_ty handle) noexcept {
    synchronize_streams();
    delete reinterpret_cast<VkTexture2D *>(handle);
}

uint3 VulkanComputeDevice::workgroup_size(const Function &function) const noexcept {
    uint3 dim = function.block_dim();
    if (!dim.x || !dim.y || !dim.z) dim = make_uint3(64u, 1u, 1u);
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(phys_device_, &props);
    const auto &limits = props.limits;
    OC_ERROR_IF(dim.x > limits.maxComputeWorkGroupSize[0] ||
                dim.y > limits.maxComputeWorkGroupSize[1] ||
                dim.z > limits.maxComputeWorkGroupSize[2] ||
                uint64_t(dim.x) * dim.y * dim.z > limits.maxComputeWorkGroupInvocations,
                "Vulkan workgroup exceeds device limits");
    return dim;
}

handle_ty VulkanComputeDevice::create_shader(const Function &function) noexcept {
    uint3 wg = workgroup_size(function);
    auto spirv = compiler_->compile(function, wg);
    if (spirv.empty()) return InvalidUI64;
    auto *shader = VkShaderFactory::create(this, spirv, function);
    return reinterpret_cast<handle_ty>(shader);
}

void VulkanComputeDevice::destroy_shader(handle_ty handle) noexcept {
    synchronize_streams();
    delete reinterpret_cast<VkShaderBase *>(handle);
}

handle_ty VulkanComputeDevice::create_accel(AccelUsageTag usage_tag) noexcept {
    OC_ERROR_IF(!has_rt_, "Vulkan device does not support ray queries");
    auto *accel = new VkAccel(this, usage_tag);
    return reinterpret_cast<handle_ty>(accel);
}

void VulkanComputeDevice::destroy_accel(handle_ty handle) noexcept {
    synchronize_streams();
    delete reinterpret_cast<VkAccel *>(handle);
}

handle_ty VulkanComputeDevice::create_stream() noexcept {
    auto *stream = new VkComputeStream(this);
    return reinterpret_cast<handle_ty>(stream);
}

void VulkanComputeDevice::destroy_stream(handle_ty handle) noexcept {
    delete reinterpret_cast<VkComputeStream *>(handle);
}

handle_ty VulkanComputeDevice::create_mesh(const MeshParams &params) noexcept {
    auto *mesh = new VkMesh(this, params);
    return reinterpret_cast<handle_ty>(mesh);
}

void VulkanComputeDevice::destroy_mesh(handle_ty handle) noexcept {
    synchronize_streams();
    delete reinterpret_cast<VkMesh *>(handle);
}

handle_ty VulkanComputeDevice::create_bindless_array() noexcept {
    auto *arr = new VkBindlessArray(this);
    return reinterpret_cast<handle_ty>(arr);
}

void VulkanComputeDevice::destroy_bindless_array(handle_ty handle) noexcept {
    synchronize_streams();
    delete reinterpret_cast<VkBindlessArray *>(handle);
}

handle_ty VulkanComputeDevice::create_texture_from_external(uint handle) noexcept {
    return create_texture2d_from_external(handle, "external");
}

handle_ty VulkanComputeDevice::create_buffer_from_external(uint) noexcept {
    OC_NOT_IMPLEMENT_ERROR(create_buffer_from_external);
    return InvalidUI64;
}

void VulkanComputeDevice::init_rtx() noexcept {
    // RT was probed in init_physical_device(); nothing extra needed at runtime.
}

CommandVisitor *VulkanComputeDevice::command_visitor() noexcept {
    return cmd_visitor_.get();
}

void VulkanComputeDevice::memory_allocate(handle_ty *handle, size_t size, bool exported) {
    if (size == 0) { *handle = 0; return; }
    auto alloc = allocate_buffer(size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false, "managed", 1, false, exported);
    *handle = alloc.address;
}

void VulkanComputeDevice::memory_free(handle_ty *handle) {
    if (!handle || !*handle) return;
    destroy_buffer(*handle);
    *handle = 0;
}

DevicePrecisionCaps VulkanComputeDevice::precision_caps() const noexcept {
    VkPhysicalDeviceFeatures feats{};
    vkGetPhysicalDeviceFeatures(phys_device_, &feats);
    DevicePrecisionCaps caps{};
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &f12;
    vkGetPhysicalDeviceFeatures2(phys_device_, &f2);
    caps.has_native_fp16 = f12.shaderFloat16 == VK_TRUE;
    caps.has_fast_fp16 = caps.has_native_fp16;
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(phys_device_, &memory);
    for (uint32_t i = 0; i < memory.memoryHeapCount; ++i) {
        if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            caps.total_vram_bytes += memory.memoryHeaps[i].size;
    }
    return caps;
}

uint64_t VulkanComputeDevice::get_aligned_memory_size(handle_ty handle) const {
    auto g = const_cast<VulkanComputeDevice *>(this)->buffer_map_guard_.lock();
    auto it = buffer_map_.find(handle);
    if (it == buffer_map_.end()) return 0;
    VmaAllocationInfo info{};
    vmaGetAllocationInfo(allocator_, it->second.alloc, &info);
    return info.size;
}

#if _WIN32 || _WIN64
handle_ty VulkanComputeDevice::import_handle(handle_ty handle, size_t size) {
    OC_NOT_IMPLEMENT_ERROR(import_handle);
    return InvalidUI64;
}

uint64_t VulkanComputeDevice::export_handle(handle_ty handle_) {
    auto g = buffer_map_guard_.lock();
    auto it = buffer_map_.find(handle_);
    if (it == buffer_map_.end() || !it->second.exported) throw std::runtime_error("Buffer is not exportable");
    VmaAllocationInfo allocation{};
    vmaGetAllocationInfo(allocator_, it->second.alloc, &allocation);
    auto get_handle = reinterpret_cast<PFN_vkGetMemoryWin32HandleKHR>(
        vkGetDeviceProcAddr(device_, "vkGetMemoryWin32HandleKHR"));
    if (!get_handle) throw std::runtime_error("vkGetMemoryWin32HandleKHR is unavailable");
    VkMemoryGetWin32HandleInfoKHR info{VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR};
    info.memory = allocation.deviceMemory;
    info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    HANDLE handle{};
    if (get_handle(device_, &info, &handle) != VK_SUCCESS) throw std::runtime_error("Vulkan memory export failed");
    return reinterpret_cast<uint64_t>(handle);
}
#endif

BufferExportMetadata VulkanComputeDevice::buffer_export_metadata(handle_ty handle) const {
    auto g = const_cast<VulkanComputeDevice*>(this)->buffer_map_guard_.lock();
    const auto& buffer = buffer_map_.at(handle);
    if (!buffer.exported) throw std::runtime_error("Buffer is not exportable");
    VmaAllocationInfo allocation{};
    vmaGetAllocationInfo(allocator_, buffer.alloc, &allocation);
    VkPhysicalDeviceIDProperties ids{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext = &ids;
    vkGetPhysicalDeviceProperties2(phys_device_, &properties);
    BufferExportMetadata info;
    info.memory_type_index = allocation.memoryType;
    info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    info.queue_families = shared_queue_families_;
    std::copy_n(ids.deviceUUID, 16, info.device_uuid.begin());
    std::copy_n(ids.driverUUID, 16, info.driver_uuid.begin());
    return info;
}

void VulkanComputeDevice::set_buffer_external_ownership(handle_ty handle, bool external) {
    auto g = buffer_map_guard_.lock();
    auto& buffer = buffer_map_.at(handle);
    if (!buffer.exported || buffer.external_owned == external) return;
    const auto native_buffer = buffer.buffer;
    const auto local_family = shared_queue_families_.size() > 1 ? VK_QUEUE_FAMILY_IGNORED : compute_queue_family_;
    g.unlock();
    immediate_submit([&](VkCommandBuffer cb) {
        VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        barrier.srcAccessMask = external ? VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT : 0;
        barrier.dstAccessMask = external ? 0 : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.srcQueueFamilyIndex = external ? local_family : VK_QUEUE_FAMILY_EXTERNAL;
        barrier.dstQueueFamilyIndex = external ? VK_QUEUE_FAMILY_EXTERNAL : local_family;
        barrier.buffer = native_buffer;
        barrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 0, nullptr, 1, &barrier, 0, nullptr);
    });
    auto update = buffer_map_guard_.lock();
    buffer_map_.at(handle).external_owned = external;
}

// ── Destructor ────────────────────────────────────────────────────────────────

VulkanComputeDevice::~VulkanComputeDevice() noexcept {
    vkDeviceWaitIdle(device_);
    for (auto &s : samplers_) {
        if (s != VK_NULL_HANDLE) vkDestroySampler(device_, s, nullptr);
    }
    if (global_set_ != VK_NULL_HANDLE)
        vkFreeDescriptorSets(device_, desc_pool_, 1, &global_set_);
    if (global_layout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device_, global_layout_, nullptr);
    if (parameter_uniform_layout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device_, parameter_uniform_layout_, nullptr);
    if (desc_pool_ != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device_, desc_pool_, nullptr);
    free_buffer(texture_write_locks_);
    if (allocator_) vmaDestroyAllocator(allocator_);
    if (device_)   vkDestroyDevice(device_, nullptr);
    if (instance_) vkDestroyInstance(instance_, nullptr);
    if (allocation_trace_) {
        if (std::ferror(allocation_trace_)) OC_WARNING("CORONA_GPU_ALLOCATION_TRACE: output write failed");
        std::fclose(allocation_trace_);
    }
}

}// namespace ocarina

OC_EXPORT_API ocarina::VulkanComputeDevice *create_device(ocarina::RHIContext *context) {
    return ocarina::new_with_allocator<ocarina::VulkanComputeDevice>(context);
}

OC_EXPORT_API void destroy(ocarina::VulkanComputeDevice *device) {
    device->run_cleanup_callbacks();
    ocarina::delete_with_allocator(device);
}
