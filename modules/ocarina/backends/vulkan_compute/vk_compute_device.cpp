//
// Created during CUDA→Slang+Vulkan migration.
// Replaces cuda_device.cpp — Vulkan compute-only device, no swapchain.
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


namespace ocarina {

// ── Constructor ───────────────────────────────────────────────────────────────

VulkanComputeDevice::VulkanComputeDevice(RHIContext *context)
    : Device::Impl(context) {
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
    vkGetPhysicalDeviceFeatures2(phys_device_, &feats2);
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
    dci.pNext                   = &feats2;
    dci.queueCreateInfoCount    = 1u;
    dci.pQueueCreateInfos       = &qci;
    dci.enabledExtensionCount   = (uint32_t)exts.size();
    dci.ppEnabledExtensionNames = exts.data();
    OC_VK_CHECK(vkCreateDevice(phys_device_, &dci, nullptr, &device_));
    vkGetDeviceQueue(device_, compute_queue_family_, 0u, &compute_queue_);
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
    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, max_texture_slots * 2},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, max_texture_slots * 4},
        {VK_DESCRIPTOR_TYPE_SAMPLER, c_sampler_count}};
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pci.maxSets = 1; pci.poolSizeCount = 3; pci.pPoolSizes = sizes;
    OC_VK_CHECK(vkCreateDescriptorPool(device_, &pci, nullptr, &desc_pool_));
    VkDescriptorSetLayoutBinding bindings[7]{};
    VkDescriptorBindingFlags flags[7]{};
    for (uint32_t i = 0; i < 7; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = i == 4 ? VK_DESCRIPTOR_TYPE_SAMPLER :
            (i == 0 || i == 2 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        bindings[i].descriptorCount = i == 4 ? c_sampler_count : max_texture_slots;
        bindings[i].stageFlags = VK_SHADER_STAGE_ALL;
        if (i != 4) flags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    }
    VkDescriptorSetLayoutBindingFlagsCreateInfo f{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    f.bindingCount = 7; f.pBindingFlags = flags;
    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.pNext = &f; lci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    lci.bindingCount = 7; lci.pBindings = bindings;
    OC_VK_CHECK(vkCreateDescriptorSetLayout(device_, &lci, nullptr, &global_layout_));
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = desc_pool_; ai.descriptorSetCount = 1; ai.pSetLayouts = &global_layout_;
    OC_VK_CHECK(vkAllocateDescriptorSets(device_, &ai, &global_set_));
}
uint32_t VulkanComputeDevice::allocate_texture_slot() noexcept {
    auto guard = buffer_map_guard_.lock();
    if (!free_texture_slots_.empty()) {
        auto slot = free_texture_slots_.back(); free_texture_slots_.pop_back(); return slot;
    }
    OC_ERROR_IF(next_texture_slot_ >= max_texture_slots, "Vulkan texture slots exhausted");
    return next_texture_slot_++;
}
void VulkanComputeDevice::release_texture_slot(uint32_t slot) noexcept {
    auto guard = buffer_map_guard_.lock();
    if (slot) free_texture_slots_.push_back(slot);
}

// ── init_samplers ─────────────────────────────────────────────────────────────

void VulkanComputeDevice::init_samplers() noexcept {
    for (uint32_t i = 0; i < c_sampler_count; ++i) {
        VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sci.magFilter    = VK_FILTER_LINEAR;
        sci.minFilter    = sci.magFilter;
        sci.mipmapMode   = (i & 2u) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
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

// ── Buffer allocation ─────────────────────────────────────────────────────────

VkBufferAllocation VulkanComputeDevice::allocate_buffer(size_t size,
                                                        VkBufferUsageFlags usage,
                                                        bool host_visible,
                                                        const string &, VkDeviceSize alignment) noexcept {
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size        = size > 0 ? size : 4;
    bci.usage       = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if (has_rt_) bci.usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo aci{};
    aci.usage = host_visible ? VMA_MEMORY_USAGE_CPU_TO_GPU : VMA_MEMORY_USAGE_GPU_ONLY;
    if (host_visible) aci.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (host_visible) aci.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBufferAllocation result{};
    OC_VK_CHECK(vmaCreateBufferWithAlignment(allocator_, &bci, &aci, alignment,
        &result.buffer, &result.alloc, nullptr));
    result.size = bci.size;
    VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    ai.buffer    = result.buffer;
    result.address = vkGetBufferDeviceAddress(device_, &ai);
    {
        auto g = buffer_map_guard_.lock();
        buffer_map_[result.address] = result;
    }
    return result;
}

void VulkanComputeDevice::free_buffer(VkBufferAllocation &alloc) noexcept {
    if (alloc.buffer == VK_NULL_HANDLE) return;
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
                                                       const string &, bool is_3d) noexcept {
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType   = is_3d ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    ici.format      = fmt;
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
    vci.subresourceRange.levelCount = 1;
    OC_VK_CHECK(vkCreateImageView(device_, &vci, nullptr, &result.storage_view));
    {
        auto g = buffer_map_guard_.lock();
        image_map_[reinterpret_cast<handle_ty>(result.image)] = result;
    }
    return result;
}

void VulkanComputeDevice::free_image(VkImageAllocation &alloc) noexcept {
    if (alloc.image == VK_NULL_HANDLE) return;
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
    // thread. Its GPU batch has finished and later work is still blocked by
    // the timeline; waiting here would wait for this same callback to return.
    // As with CUDA, resources used by another stream must be retained there.
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
                                              bool) noexcept {
    if (size == 0) return 0;
    auto alloc = allocate_buffer(size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false, desc);
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
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false, "managed");
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
    OC_NOT_IMPLEMENT_ERROR(export_handle);
    return 0;
}
#endif

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
    if (desc_pool_ != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device_, desc_pool_, nullptr);
    if (allocator_) vmaDestroyAllocator(allocator_);
    if (device_)   vkDestroyDevice(device_, nullptr);
    if (instance_) vkDestroyInstance(instance_, nullptr);
}

}// namespace ocarina

OC_EXPORT_API ocarina::VulkanComputeDevice *create_device(ocarina::RHIContext *context) {
    return ocarina::new_with_allocator<ocarina::VulkanComputeDevice>(context);
}

OC_EXPORT_API void destroy(ocarina::VulkanComputeDevice *device) {
    device->run_cleanup_callbacks();
    ocarina::delete_with_allocator(device);
}
