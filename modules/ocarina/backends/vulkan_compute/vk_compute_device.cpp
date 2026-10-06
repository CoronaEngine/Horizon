//
// Created during CUDA→Slang+Vulkan migration.
// Replaces cuda_device.cpp — Vulkan compute-only device, no swapchain.
//

#include "vk_compute_device.h"
#include "vk_stream.h"
#include "vk_texture.h"
#include "vk_shader.h"
#include "vk_mesh.h"
#include "vk_bindless_array.h"
#include "vk_accel.h"
#include "vk_command_visitor.h"
#include "rhi/context.h"

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

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

    // RT support probe
    uint32_t ec = 0;
    vkEnumerateDeviceExtensionProperties(phys_device_, nullptr, &ec, nullptr);
    vector<VkExtensionProperties> exts(ec);
    vkEnumerateDeviceExtensionProperties(phys_device_, nullptr, &ec, exts.data());
    for (const auto &e : exts)
        if (strcmp(e.extensionName, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) == 0)
            has_rt_ = true;

    if (has_rt_) {
        rt_props_ = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
        VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        p2.pNext = &rt_props_;
        vkGetPhysicalDeviceProperties2(phys_device_, &p2);
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
// PLACEHOLDER_DEV_1

// ── init_logical_device ───────────────────────────────────────────────────────

void VulkanComputeDevice::init_logical_device() noexcept {
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = compute_queue_family_;
    qci.queueCount       = 1u;
    qci.pQueuePriorities = &priority;

    vector<const char *> exts{
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
    };
    if (has_rt_) {
        exts.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        exts.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
        exts.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        exts.push_back(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
        exts.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
    }

    VkPhysicalDeviceBufferDeviceAddressFeatures bda_feat{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
    bda_feat.bufferDeviceAddress = VK_TRUE;

    VkPhysicalDeviceDescriptorIndexingFeatures idx_feat{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
    idx_feat.pNext = &bda_feat;
    idx_feat.descriptorBindingPartiallyBound            = VK_TRUE;
    idx_feat.runtimeDescriptorArray                     = VK_TRUE;
    idx_feat.descriptorBindingVariableDescriptorCount   = VK_TRUE;
    idx_feat.descriptorBindingSampledImageUpdateAfterBind  = VK_TRUE;
    idx_feat.descriptorBindingStorageImageUpdateAfterBind  = VK_TRUE;

    VkPhysicalDeviceFeatures2 feats2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    feats2.pNext = &idx_feat;
    feats2.features.shaderInt64 = VK_TRUE;

    void *rt_feat_ptr = nullptr;
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rt_feat{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR as_feat{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    if (has_rt_) {
        rt_feat.rayTracingPipeline    = VK_TRUE;
        as_feat.accelerationStructure = VK_TRUE;
        rt_feat.pNext = bda_feat.pNext;
        bda_feat.pNext = &as_feat;
        as_feat.pNext  = &rt_feat;
        rt_feat_ptr    = &rt_feat;
        (void)rt_feat_ptr;
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
// PLACEHOLDER_DEV_2

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
    constexpr uint32_t c_max_descriptors = 65536u;

    VkDescriptorPoolSize pool_sizes[2]{};
    pool_sizes[0].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_sizes[0].descriptorCount = c_max_descriptors;
    pool_sizes[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    pool_sizes[1].descriptorCount = c_max_descriptors;

    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.flags         = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT |
                        VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pci.maxSets       = 1u;
    pci.poolSizeCount = 2u;
    pci.pPoolSizes    = pool_sizes;
    OC_VK_CHECK(vkCreateDescriptorPool(device_, &pci, nullptr, &desc_pool_));

    // Binding 0: combined image sampler array (for sampling)
    // Binding 1: storage image array (for read/write)
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding         = 0u;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = c_max_descriptors;
    bindings[0].stageFlags      = VK_SHADER_STAGE_ALL;
    bindings[1].binding         = 1u;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = c_max_descriptors;
    bindings[1].stageFlags      = VK_SHADER_STAGE_ALL;

    VkDescriptorBindingFlags binding_flags[2]{
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT};
    VkDescriptorSetLayoutBindingFlagsCreateInfo flags_ci{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    flags_ci.bindingCount  = 2u;
    flags_ci.pBindingFlags = binding_flags;

    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.pNext        = &flags_ci;
    lci.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    lci.bindingCount = 2u;
    lci.pBindings    = bindings;
    OC_VK_CHECK(vkCreateDescriptorSetLayout(device_, &lci, nullptr, &global_layout_));

    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool     = desc_pool_;
    alloc.descriptorSetCount = 1u;
    alloc.pSetLayouts        = &global_layout_;
    OC_VK_CHECK(vkAllocateDescriptorSets(device_, &alloc, &global_set_));
}
// PLACEHOLDER_DEV_3

// ── init_samplers ─────────────────────────────────────────────────────────────

void VulkanComputeDevice::init_samplers() noexcept {
    static constexpr VkSamplerAddressMode addr_modes[4] = {
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
    };
    for (uint32_t i = 0; i < c_sampler_count; ++i) {
        VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sci.magFilter    = (i & 1u) ? VK_FILTER_LINEAR   : VK_FILTER_NEAREST;
        sci.minFilter    = sci.magFilter;
        sci.mipmapMode   = (i & 2u) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sci.addressModeU = addr_modes[(i >> 2u) & 3u];
        sci.addressModeV = sci.addressModeU;
        sci.addressModeW = sci.addressModeU;
        sci.maxLod       = VK_LOD_CLAMP_NONE;
        OC_VK_CHECK(vkCreateSampler(device_, &sci, nullptr, &samplers_[i]));
    }
}
// PLACEHOLDER_DEV_4

// ── Buffer allocation ─────────────────────────────────────────────────────────

VkBufferAllocation VulkanComputeDevice::allocate_buffer(size_t size,
                                                        VkBufferUsageFlags usage,
                                                        bool host_visible,
                                                        const string &) noexcept {
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size        = size > 0 ? size : 4;
    bci.usage       = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo aci{};
    aci.usage = host_visible ? VMA_MEMORY_USAGE_CPU_TO_GPU : VMA_MEMORY_USAGE_GPU_ONLY;
    if (host_visible) aci.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBufferAllocation result{};
    OC_VK_CHECK(vmaCreateBuffer(allocator_, &bci, &aci, &result.buffer, &result.alloc, nullptr));
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

VkBuffer VulkanComputeDevice::get_vk_buffer(handle_ty addr) noexcept {
    auto g = buffer_map_guard_.lock();
    auto it = buffer_map_.find(addr);
    return it != buffer_map_.end() ? it->second.buffer : VK_NULL_HANDLE;
}
// PLACEHOLDER_DEV_5

VkImageAllocation VulkanComputeDevice::allocate_image(uint3 extent, VkFormat fmt,
                                                       uint32_t levels,
                                                       VkImageUsageFlags usage,
                                                       const string &) noexcept {
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType   = extent.z > 1u ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
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
    vci.viewType = extent.z > 1u ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D;
    vci.format   = fmt;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
    OC_VK_CHECK(vkCreateImageView(device_, &vci, nullptr, &result.view));
    // storage view (same, for non-depth/stencil formats storage access is the same view)
    result.storage_view = result.view;
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
    if (alloc.view != VK_NULL_HANDLE) vkDestroyImageView(device_, alloc.view, nullptr);
    vmaDestroyImage(allocator_, alloc.image, alloc.alloc);
    alloc = {};
}

VkImageAllocation &VulkanComputeDevice::get_vk_image(handle_ty handle) noexcept {
    auto g = buffer_map_guard_.lock();
    return image_map_.at(handle);
}

void VulkanComputeDevice::immediate_submit(std::function<void(VkCommandBuffer)> &&fn) noexcept {
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
    fn(cb);
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
// PLACEHOLDER_DEV_6

// ── Device::Impl create/destroy ───────────────────────────────────────────────

handle_ty VulkanComputeDevice::create_buffer(size_t size, const string &desc,
                                              bool) noexcept {
    auto alloc = allocate_buffer(size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false, desc);
    return alloc.address;
}

void VulkanComputeDevice::destroy_buffer(handle_ty handle) noexcept {
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
    delete reinterpret_cast<VkTexture3D *>(handle);
}

void VulkanComputeDevice::destroy_texture2d(handle_ty handle) noexcept {
    delete reinterpret_cast<VkTexture2D *>(handle);
}

handle_ty VulkanComputeDevice::create_shader(const Function &function) noexcept {
    uint3 wg = choose_block_shape(
        function.dispatch_hint().dim.x > 0 ? function.dispatch_hint().dim : make_uint3(64u,1u,1u),
        1024u);
    auto spirv = compiler_->compile(function, wg);
    if (spirv.empty()) return InvalidUI64;
    auto *shader = VkShaderFactory::create(this, spirv, function);
    return reinterpret_cast<handle_ty>(shader);
}

void VulkanComputeDevice::destroy_shader(handle_ty handle) noexcept {
    delete reinterpret_cast<VkShaderBase *>(handle);
}
// PLACEHOLDER_DEV_7

handle_ty VulkanComputeDevice::create_accel(AccelUsageTag usage_tag) noexcept {
    auto *accel = new VkAccel(this, usage_tag);
    return reinterpret_cast<handle_ty>(accel);
}

void VulkanComputeDevice::destroy_accel(handle_ty handle) noexcept {
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
    delete reinterpret_cast<VkMesh *>(handle);
}

handle_ty VulkanComputeDevice::create_bindless_array() noexcept {
    auto *arr = new VkBindlessArray(this);
    return reinterpret_cast<handle_ty>(arr);
}

void VulkanComputeDevice::destroy_bindless_array(handle_ty handle) noexcept {
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
// PLACEHOLDER_DEV_8

void VulkanComputeDevice::memory_allocate(handle_ty *handle, size_t size, bool exported) {
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
    caps.support_float16 = true;   // Vulkan 1.2 + SPIR-V always supports float16 in shaders
    caps.support_float64 = feats.shaderFloat64 == VK_TRUE;
    caps.support_int8     = true;
    caps.support_int16    = true;
    caps.support_int64    = feats.shaderInt64 == VK_TRUE;
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

}// namespace ocarina

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

