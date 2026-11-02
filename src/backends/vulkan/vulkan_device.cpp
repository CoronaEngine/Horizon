//
// Created by Zero on 06/06/2022.
//

#include "vulkan_device.h"
#include "rhi/context.h"
#include "util.h"
#include "vulkan_shader.h"
#include "vulkan_driver.h"
#include "vulkan_buffer.h"
#include "vulkan_vertex_buffer.h"
#include "vulkan_index_buffer.h"
#include "vulkan_renderpass.h"
#include "vulkan_descriptorset.h"
#include "vulkan_descriptorset_writer.h"
#include "vulkan_texture.h"

namespace ocarina {

VulkanDevice::VulkanDevice(RHIContext *context, const ocarina::InstanceCreation &instance_creation)
    : Device::Impl(context), m_instance(instance_creation), m_windowHandle(instance_creation.windowHandle) {

    init_vulkan();
}

VulkanDevice::~VulkanDevice()
{
    VulkanDriver::instance().terminate();
    shutdown();
}

void VulkanDevice::init_hardware_info() {

}

handle_ty VulkanDevice::create_buffer(size_t size, const string &desc) noexcept {
    return 0;
}

namespace detail {
void context_log_cb(unsigned int level, const char *tag, const char *message, void * /*cbdata */) {
    std::cerr << "[" << std::setw(2) << level << "][" << std::setw(12) << tag << "]: " << message << "\n";
}
}// namespace detail


handle_ty VulkanDevice::create_stream() noexcept {
    return 0;
}

handle_ty VulkanDevice::create_texture(uint3 res, PixelStorage pixel_storage,
                                     uint level_num,
                                     const string &desc) noexcept {
    return 0;
}

handle_ty VulkanDevice::create_texture(Image *image, const TextureViewCreation &texture_view) noexcept {
    auto texture = ocarina::new_with_allocator<VulkanTexture>(this, image, texture_view);
    return reinterpret_cast<handle_ty>(texture);
}

handle_ty VulkanDevice::create_shader(const Function &function) noexcept {
    return 0;
}

handle_ty VulkanDevice::create_shader_from_file(const std::string &file_name, ShaderType shader_type, const std::set<string> &options) noexcept {
    //VulkanShader *shader = VulkanShader::create_from_HLSL(this, shader_type, file_name, "main");
    //if (shader) {
    //    return (handle_ty)shader->shader_module();
    //}
    VulkanShader* shader = VulkanDriver::instance().create_shader(shader_type, file_name, options, "main");
    if (shader) {
        return (handle_ty)shader;
    }
    return 0;
}

handle_ty VulkanDevice::create_mesh(const MeshParams &params) noexcept {
    return 0;
}

void VulkanDevice::destroy_mesh(handle_ty handle) noexcept {
    
}

handle_ty VulkanDevice::create_bindless_array() noexcept {
    return 0;
}

void VulkanDevice::destroy_bindless_array(handle_ty handle) noexcept {
    
}

void VulkanDevice::destroy_buffer(handle_ty handle) noexcept {
    VulkanBuffer* buffer = (VulkanBuffer*)handle;
    if (buffer) {
        ocarina::delete_with_allocator<VulkanBuffer>(buffer);
    }
}

void VulkanDevice::destroy_shader(handle_ty handle) noexcept {

}

void VulkanDevice::destroy_texture(handle_ty handle) noexcept {
    ocarina::delete_with_allocator(reinterpret_cast<VulkanTexture *>(handle));
}

void VulkanDevice::destroy_stream(handle_ty handle) noexcept {

}
handle_ty VulkanDevice::create_accel(AccelUsageTag usage_tag) noexcept {
    return 0;
}
void VulkanDevice::destroy_accel(handle_ty handle) noexcept {
    
}
CommandVisitor *VulkanDevice::command_visitor() noexcept {
    return nullptr;
}

void VulkanDevice::init_vulkan()
{
    // Physical device
    uint32_t gpuCount = 0;
    // Get number of available physical devices
    VK_CHECK_RESULT(vkEnumeratePhysicalDevices(m_instance.instance(), &gpuCount, nullptr));
    if (gpuCount == 0) {
        OC_ERROR_FORMAT("No device with Vulkan support found", -1);
        return;
    }
    // Enumerate devices
    std::vector<VkPhysicalDevice> physicalDevices(gpuCount);
    VkResult err = vkEnumeratePhysicalDevices(m_instance.instance(), &gpuCount, physicalDevices.data());
    if (err) {
        OC_ERROR_FORMAT("Could not enumerate physical devices : {}\n", errorString(err));
        return;
    }

    // GPU selection
    // Select physical device to be used for the Vulkan example
    // Defaults to the first device unless specified by command line
    uint32_t selectedDevice = 0;


    physicalDevice_ = physicalDevices[selectedDevice];

    // Store properties (including limits), features and memory properties of the physical device (so that examples can check against them)
    vkGetPhysicalDeviceProperties(physicalDevice_, &m_deviceProperties);
    vkGetPhysicalDeviceFeatures(physicalDevice_, &m_deviceFeatures);
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &m_deviceMemoryProperties);

    // Get list of supported extensions
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extCount, nullptr);
    if (extCount > 0) {
        std::vector<VkExtensionProperties> extensions(extCount);
        if (vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extCount, &extensions.front()) == VK_SUCCESS) {
            for (auto ext : extensions) 
            {
                m_supportedExtensions.push_back(ext.extensionName);
            }
        }
    }


    // Retrieve Physical Device's Queue Families
    uint32_t queueFamilyPropertiesCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueFamilyPropertiesCount, nullptr);

    queueFamilyProperties_.resize(queueFamilyPropertiesCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueFamilyPropertiesCount, queueFamilyProperties_.data());

    static const uint32_t queueGraphicsIndex = uint32_t(QueueType::Graphics);
    static const uint32_t queueCopyIndex = uint32_t(QueueType::Copy);
    static const uint32_t queueComputeIndex = uint32_t(QueueType::Compute);

    uint32_t graphicsFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    uint32_t computeFlags = VK_QUEUE_COMPUTE_BIT;
    uint32_t copyFlags = VK_QUEUE_TRANSFER_BIT;

    queueFamilyIndices_[queueGraphicsIndex] = getQueueFamilyIndex(graphicsFlags);
    queueFamilyIndices_[queueCopyIndex] = getQueueFamilyIndex(copyFlags);
    queueFamilyIndices_[queueComputeIndex] = getQueueFamilyIndex(computeFlags);

    get_enable_features();
    get_enable_extentions();

    create_logical_device();

    m_swapChain.create_surface(m_instance.instance(), m_windowHandle);
    SwapChainCreation swapchain_creation{};
    m_swapChain.create_swapchain(swapchain_creation, this);
}

void VulkanDevice::create_logical_device()
{

    VkDeviceQueueCreateInfo queues[uint32_t(QueueType::NumQueueType)];
    memset(queues, 0, sizeof(VkDeviceQueueCreateInfo) * uint32_t(QueueType::NumQueueType));
    const float defaultQueuePriority(0.0f);
    for (int i = 0; i < uint32_t(QueueType::NumQueueType); ++i)
    {
        queues[i].queueFamilyIndex = queueFamilyIndices_[i];
        queues[i].queueCount = 1;
        queues[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queues[i].pQueuePriorities = &defaultQueuePriority;
    }
    

    VkDeviceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount = uint32_t(QueueType::NumQueueType);
    info.pQueueCreateInfos = queues;
    info.enabledLayerCount = 0;
    info.ppEnabledLayerNames = nullptr;
    info.enabledExtensionCount = m_enableExtensions.size();
    info.ppEnabledExtensionNames = m_enableExtensions.data();
    info.pEnabledFeatures = &m_enabledFeatures;
    //VkPhysicalDeviceFeatures physicalDeviceFeatures = m_Adapter.GetVulkanPhysicalDeviceFeatures();

    VkResult result = vkCreateDevice(physicalDevice_, &info, nullptr, &logicalDevice_);
    
}

void VulkanDevice::get_enable_features() {
    
}

void VulkanDevice::get_enable_extentions()
{
    for (auto &extension : m_supportedExtensions) {
        // Swap chain extension - required
        if (!strcmp(VK_KHR_SWAPCHAIN_EXTENSION_NAME, extension.c_str())) {
            m_enableExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        }
    }
}

void VulkanDevice::shutdown()
{
    m_swapChain.release();
    vkDestroyDevice(logicalDevice_, nullptr);
}

void VulkanDevice::submit_frame() noexcept {
    return VulkanDriver::instance().submit_frame();
}

VertexBuffer* VulkanDevice::create_vertex_buffer() noexcept
{
    VulkanVertexBuffer *vulkan_vertex_buffer = ocarina::new_with_allocator<VulkanVertexBuffer>(this);
    return vulkan_vertex_buffer;
}

IndexBuffer* VulkanDevice::create_index_buffer(const void* initial_data, uint32_t indices_count, bool bit16) noexcept
{
    VulkanIndexBuffer *index_buffer = ocarina::new_with_allocator<VulkanIndexBuffer>(this, initial_data, indices_count, bit16);
    return index_buffer;
}

void VulkanDevice::begin_frame() noexcept
{
    VulkanDriver::instance().begin_frame();
}

void VulkanDevice::end_frame() noexcept
{
    VulkanDriver::instance().end_frame();
}

RHIRenderPass *VulkanDevice::create_render_pass(const RenderPassCreation &render_pass_creation) noexcept {
    return VulkanDriver::instance().create_render_pass(render_pass_creation);
}

void VulkanDevice::destroy_render_pass(RHIRenderPass *render_pass) noexcept {
    VulkanDriver::instance().destroy_render_pass(static_cast<VulkanRenderPass*>(render_pass));
}

std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER> VulkanDevice::create_descriptor_set_layout(void **shaders, uint32_t shaders_count) noexcept {
    //VulkanShader **vulkan_shaders = reinterpret_cast<VulkanShader **>(shaders);
    VulkanShader *vulkan_shaders[2] = {reinterpret_cast<VulkanShader *>(shaders[0]), reinterpret_cast<VulkanShader *>(shaders[1])};
    return VulkanDriver::instance().create_descriptor_set_layout(vulkan_shaders, shaders_count);
}

//DescriptorSetWriter *VulkanDevice::create_descriptor_set_writer(DescriptorSet *descriptor_set, void **shaders, uint32_t shaders_count) noexcept {
//    VulkanDescriptorSet *vulkan_descriptor_set = static_cast<VulkanDescriptorSet *>(descriptor_set);
//    VulkanShader *vulkan_shaders[2] = {reinterpret_cast<VulkanShader *>(shaders[0]), reinterpret_cast<VulkanShader *>(shaders[1])};
//    return ocarina::new_with_allocator<VulkanDescriptorSetWriter>(this, vulkan_shaders, shaders_count, vulkan_descriptor_set);
//}

void VulkanDevice::bind_pipeline(const handle_ty pipeline) noexcept {
    VulkanPipeline *vulkan_pipeline = reinterpret_cast<VulkanPipeline *>(pipeline);
    VulkanDriver::instance().bind_pipeline(*vulkan_pipeline);
}

RHIPipeline *VulkanDevice::get_pipeline(const PipelineState &pipeline_state, RHIRenderPass *render_pass) noexcept {
    VulkanRenderPass *vulkan_render_pass = static_cast<VulkanRenderPass *>(render_pass);
    auto pipeline = VulkanDriver::instance().get_pipeline(pipeline_state, vulkan_render_pass->render_pass());
    
    return pipeline;
}

DescriptorSet* VulkanDevice::get_global_descriptor_set(const string& name) noexcept {
    return VulkanDriver::instance().get_global_descriptor_set(name);
}

void VulkanDevice::bind_descriptor_sets(DescriptorSet **descriptor_set, uint32_t descriptor_sets_num, RHIPipeline* pipeline) noexcept {
    std::array<VulkanDescriptorSet *, MAX_DESCRIPTOR_SETS_PER_SHADER> vulkan_descriptor_sets;
    for (uint32_t i = 0; i < descriptor_sets_num; ++i) {
        vulkan_descriptor_sets[i] = static_cast<VulkanDescriptorSet *>(descriptor_set[i]);
    }
    VulkanPipeline *vulkan_pipeline = static_cast<VulkanPipeline *>(pipeline);
    VulkanDriver::instance().bind_descriptor_sets(vulkan_descriptor_sets.data(), descriptor_sets_num, vulkan_pipeline->pipeline_layout_);
}

VulkanBuffer *VulkanDevice::create_vulkan_buffer(VkBufferUsageFlags usage_flags, VkMemoryPropertyFlags memory_property_flags, VkDeviceSize size, const void *data) {
    //return VulkanBufferManager::instance()->create_vulkan_buffer(this, usage_flags, memory_property_flags, size, data);
    return ocarina::new_with_allocator<VulkanBuffer>(this, usage_flags, memory_property_flags, size, data);
}

uint32_t VulkanDevice::getQueueFamilyIndex(uint32_t queueFlags) const {

    if ((queueFlags & VK_QUEUE_COMPUTE_BIT) == queueFlags) {
        for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties_.size()); i++) {
            if ((queueFamilyProperties_[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && ((queueFamilyProperties_[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)) {
                return i;
            }
        }
    }

    // Dedicated queue for transfer
    // Try to find a queue family index that supports transfer but not graphics and compute
    if ((queueFlags & VK_QUEUE_TRANSFER_BIT) == queueFlags) {
        for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties_.size()); i++) {
            if ((queueFamilyProperties_[i].queueFlags & VK_QUEUE_TRANSFER_BIT) && ((queueFamilyProperties_[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) && ((queueFamilyProperties_[i].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0)) {
                return i;
            }
        }
    }

    for (uint32_t i = 0; i < queueFamilyProperties_.size(); i++) {
        if ((queueFamilyProperties_[i].queueFlags & queueFlags) == queueFlags)
        {
            return i;
        }
    }

    return InvalidUI32;
}

uint32_t VulkanDevice::get_memory_type(uint32_t typeBits, VkMemoryPropertyFlags properties, VkBool32* memTypeFound) const
{
    for (uint32_t i = 0; i < m_deviceMemoryProperties.memoryTypeCount; i++) {
        if ((typeBits & 1) == 1) {
            if ((m_deviceMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                if (memTypeFound) {
                    *memTypeFound = true;
                }
                return i;
            }
        }
        typeBits >>= 1;
    }

    if (memTypeFound) {
        *memTypeFound = false;
        return 0;
    }

    return 0;
}

}// namespace ocarina

OC_EXPORT_API ocarina::VulkanDevice *create_device(ocarina::RHIContext *context, const ocarina::InstanceCreation& instance_creation) {
    //return ocarina::new_with_allocator<ocarina::VulkanDevice>(context, instance_creation);
    return ocarina::VulkanDriver::instance().create_device(context, instance_creation);
}

OC_EXPORT_API void destroy(ocarina::VulkanDevice *device) {
    ocarina::delete_with_allocator(device);
}

