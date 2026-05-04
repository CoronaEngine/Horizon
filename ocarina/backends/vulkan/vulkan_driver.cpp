#include "vulkan_driver.h"
#include "vulkan_device.h"
#include "util.h"
#include "vulkan_pipeline.h"
#include "rhi/context.h"
#include "rhi/graphics_descriptions.h"
#include "vulkan_shader.h"
#include "vulkan_descriptorset.h"
#include "vulkan_renderpass.h"
#include "vulkan_vertex_buffer.h"
#include "vulkan_index_buffer.h"
#include "vulkan_texture.h"

namespace ocarina {

VulkanDriver::VulkanDriver() {
    
}

VulkanDriver::~VulkanDriver() {
   
}

VulkanDevice* VulkanDriver::create_device(RHIContext* context, const InstanceCreation& instance_creation)
{
    vulkan_device_ = ocarina::new_with_allocator<ocarina::VulkanDevice>(context, instance_creation);

    // Semaphore used to ensures that image presentation is complete before starting to submit again
    VkSemaphoreCreateInfo semaphoreCreateInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    vkCreateSemaphore(device(), &semaphoreCreateInfo, nullptr, &semaphores.presentComplete);
    vkCreateSemaphore(device(), &semaphoreCreateInfo, nullptr, &semaphores.renderComplete);

    initialize();

    return vulkan_device_;
}

void VulkanDriver::bind_pipeline(const VulkanPipeline &pipeline) {
    VkCommandBuffer current_buffer = get_current_command_buffer();

    vkCmdBindPipeline(current_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline_);
}

void VulkanDriver::terminate()
{
    release_frame_buffer();
    for (auto &it : global_descriptor_sets) {
        ocarina::delete_with_allocator(it.second);
    }
    global_descriptor_sets.clear();

    for (auto &it : render_passes_) {
        ocarina::delete_with_allocator(it);
    }
    render_passes_.clear();

    vulkan_pipeline_manager->clear(vulkan_device_);
    vulkan_descriptor_manager->clear();
    vulkan_shader_manager->clear(vulkan_device_);
    vkDestroySemaphore(device(), semaphores.presentComplete, nullptr);
    vkDestroySemaphore(device(), semaphores.renderComplete, nullptr);
    release_command_buffers();
    release_command_pool();
}

void VulkanDriver::submit_frame() {
    //prepare_frame();
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submit_info.pWaitDstStageMask = wait_stages;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &draw_cmd_buffers_[current_buffer_];

    // Submit to queue
    vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE);

    vulkan_device_->get_swapchain()->queue_present(graphics_queue, current_buffer_, semaphores.renderComplete);

    VK_CHECK_RESULT(vkQueueWaitIdle(graphics_queue));
}

inline VkDevice VulkanDriver::device() const {
    return (*vulkan_device_)();
}

VulkanShader *VulkanDriver::create_shader(ShaderType shader_type,
                                          const std::string &filename,
                                          const std::set<std::string> &options,
                                          const std::string &entry_point){
    return vulkan_shader_manager->get_or_create_from_HLSL(vulkan_device_, shader_type, filename, options, entry_point);
}

VulkanShader* VulkanDriver::get_shader(handle_ty shader) const
{
    return vulkan_shader_manager->get_shader(shader);
}

VkCommandBuffer VulkanDriver::begin_one_time_command_buffer()
{
    VkCommandBufferAllocateInfo cmd_buffer_allocate{};
    cmd_buffer_allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_buffer_allocate.commandPool = command_pool_;
    cmd_buffer_allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_buffer_allocate.commandBufferCount = 1;

    VkCommandBuffer cmd_buffer;
    VK_CHECK_RESULT(vkAllocateCommandBuffers(device(), &cmd_buffer_allocate, &cmd_buffer));

    VkCommandBufferBeginInfo cmd_buffer_begin{};
    cmd_buffer_begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd_buffer, &cmd_buffer_begin);

    return cmd_buffer;
}

void VulkanDriver::end_one_time_command_buffer(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    flush_command_buffer(cmd);
    vkFreeCommandBuffers(device(), command_pool_, 1, &cmd);
}

void VulkanDriver::setup_frame_buffer()
{
    VulkanSwapchain *swapchain = vulkan_device_->get_swapchain();
    uint2 resolution = swapchain->resolution();

    //first create the frame buffer attach renderpass
    std::array<VkAttachmentDescription, 2> attachments = {};
    // Color attachment
    attachments[0].format = swapchain->color_format();
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    // Depth attachment
    VkFormat depth_format = swapchain->depth_format();
    attachments[1].format = depth_format;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorReference = {};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthReference = {};
    depthReference.attachment = 1;
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpassDescription = {};
    subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassDescription.colorAttachmentCount = 1;
    subpassDescription.pColorAttachments = &colorReference;
    subpassDescription.pDepthStencilAttachment = &depthReference;
    subpassDescription.inputAttachmentCount = 0;
    subpassDescription.pInputAttachments = nullptr;
    subpassDescription.preserveAttachmentCount = 0;
    subpassDescription.pPreserveAttachments = nullptr;
    subpassDescription.pResolveAttachments = nullptr;

    // Subpass dependencies for layout transitions
    std::array<VkSubpassDependency, 2> dependencies;

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    dependencies[0].dependencyFlags = 0;

    dependencies[1].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].dstSubpass = 0;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].srcAccessMask = 0;
    dependencies[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    dependencies[1].dependencyFlags = 0;

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpassDescription;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    VkResult err = vkCreateRenderPass(device(), &renderPassInfo, nullptr, &renderpass_framebuffer);
    VK_CHECK_RESULT(err);

    //create framebuffer
    VkImageView imageview_attachments[2];

    VulkanSwapchain::DepthStencil depth_stencil = swapchain->get_depth_stencil();

    // Depth/Stencil attachment is the same for all frame buffers
    imageview_attachments[1] = depth_stencil.view;

    VkFramebufferCreateInfo frameBufferCreateInfo = {};
    frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    frameBufferCreateInfo.pNext = NULL;
    frameBufferCreateInfo.renderPass = renderpass_framebuffer;
    frameBufferCreateInfo.attachmentCount = 2;
    frameBufferCreateInfo.pAttachments = imageview_attachments;
    frameBufferCreateInfo.width = resolution.x;
    frameBufferCreateInfo.height = resolution.y;
    frameBufferCreateInfo.layers = 1;

    // Create frame buffers for every swap chain image
    frame_buffers.resize(swapchain->backbuffer_size());
    for (uint32_t i = 0; i < frame_buffers.size(); i++) {
        imageview_attachments[0] = swapchain->get_swapchain_buffer(i).imageView_;
        VK_CHECK_RESULT(vkCreateFramebuffer(device(), &frameBufferCreateInfo, nullptr, &frame_buffers[i]));
    }
}

/*
void VulkanDriver::setup_depth_stencil(uint32_t width, uint32_t height) {
    get_supported_depth_format(vulkan_device_->physicalDevice(), &depth_stencil_format);
    VkImageCreateInfo imageCI{};
    imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCI.imageType = VK_IMAGE_TYPE_2D;
    imageCI.format = depth_stencil_format;
    imageCI.extent = {width, height, 1};
    imageCI.mipLevels = 1;
    imageCI.arrayLayers = 1;
    imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VK_CHECK_RESULT(vkCreateImage(device(), &imageCI, nullptr, &depth_stencil.image));
    VkMemoryRequirements memReqs{};
    vkGetImageMemoryRequirements(device(), depth_stencil.image, &memReqs);

    VkMemoryAllocateInfo memAllloc{};
    memAllloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAllloc.allocationSize = memReqs.size;
    memAllloc.memoryTypeIndex = vulkan_device_->get_memory_type(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK_RESULT(vkAllocateMemory(device(), &memAllloc, nullptr, &depth_stencil.mem));
    VK_CHECK_RESULT(vkBindImageMemory(device(), depth_stencil.image, depth_stencil.mem, 0));

    VkImageViewCreateInfo imageViewCI{};
    imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCI.image = depth_stencil.image;
    imageViewCI.format = depth_stencil_format;
    imageViewCI.subresourceRange.baseMipLevel = 0;
    imageViewCI.subresourceRange.levelCount = 1;
    imageViewCI.subresourceRange.baseArrayLayer = 0;
    imageViewCI.subresourceRange.layerCount = 1;
    imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    // Stencil aspect should only be set on depth + stencil formats (VK_FORMAT_D16_UNORM_S8_UINT..VK_FORMAT_D32_SFLOAT_S8_UINT
    if (depth_stencil_format >= VK_FORMAT_D16_UNORM_S8_UINT) {
        imageViewCI.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    VK_CHECK_RESULT(vkCreateImageView(device(), &imageViewCI, nullptr, &depth_stencil.view));
}
*/

void VulkanDriver::release_frame_buffer()
{
    if (renderpass_framebuffer != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device(), renderpass_framebuffer, nullptr);
    }
    for (uint32_t i = 0; i < frame_buffers.size(); i++) {
        vkDestroyFramebuffer(device(), frame_buffers[i], nullptr);
    }
}

void VulkanDriver::create_command_pool()
{
    VulkanSwapchain *swapchain = vulkan_device_->get_swapchain();
    uint32_t graphic_queue_index = vulkan_device_->get_queue_family_index(QueueType::Graphics);

    VkCommandPoolCreateInfo cmd_pool_info = {};
    cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmd_pool_info.queueFamilyIndex = graphic_queue_index;
    cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK_RESULT(vkCreateCommandPool(device(), &cmd_pool_info, nullptr, &command_pool_));
}

void VulkanDriver::release_command_pool()
{
    vkDestroyCommandPool(device(), command_pool_, nullptr);
    command_pool_ = VK_NULL_HANDLE;
}

void VulkanDriver::create_command_buffers()
{
    VulkanSwapchain *swapchain = vulkan_device_->get_swapchain();
    draw_cmd_buffers_.resize(swapchain->backbuffer_size());

    VkCommandBufferAllocateInfo const allocateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = (uint32_t)draw_cmd_buffers_.size(),
    };

    VK_CHECK_RESULT(vkAllocateCommandBuffers(device(), &allocateInfo, draw_cmd_buffers_.data()));
}

void VulkanDriver::release_command_buffers() {
    vkFreeCommandBuffers(device(), command_pool_, static_cast<uint32_t>(draw_cmd_buffers_.size()), draw_cmd_buffers_.data());
    draw_cmd_buffers_.clear();
}

void VulkanDriver::initialize()
{
    vulkan_pipeline_manager = std::make_unique<VulkanPipelineManager>();
    vulkan_shader_manager = std::make_unique<VulkanShaderManager>();
    vulkan_descriptor_manager = std::make_unique<VulkanDescriptorManager>(vulkan_device_);
    setup_frame_buffer();
    //get the graphics queue
    vkGetDeviceQueue(device(), vulkan_device_->get_queue_family_index(QueueType::Graphics), 0, &graphics_queue);
    create_command_pool();
    create_command_buffers();

    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &semaphores.presentComplete;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &semaphores.renderComplete;
}

void VulkanDriver::window_resize()
{

}

void VulkanDriver::flush_command_buffer(VkCommandBuffer cmd)
{
    // Create fence to ensure that the command buffer has finished executing
    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = 0;
    fence_info.pNext = nullptr;
    VkFence fence;
    VK_CHECK_RESULT(vkCreateFence(device(), &fence_info, nullptr, &fence));
    // Submit to the queue
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    VK_CHECK_RESULT(vkQueueSubmit(graphics_queue, 1, &submitInfo, fence));
    // Wait for the fence to signal that command buffer has finished executing
    VK_CHECK_RESULT(vkWaitForFences(device(), 1, &fence, VK_TRUE, std::numeric_limits<uint64_t>::max()));
    vkDestroyFence(device(), fence, nullptr);
}

VulkanPipeline* VulkanDriver::get_pipeline(const PipelineState &pipeline_state, VkRenderPass render_pass) {
    return vulkan_pipeline_manager->get_or_create_pipeline(pipeline_state, vulkan_device_, render_pass);
}

void VulkanDriver::begin_frame()
{
    VulkanSwapchain *swapchain = vulkan_device_->get_swapchain();
    VkResult result = swapchain->aquire_next_image(semaphores.presentComplete, &current_buffer_);

    VkCommandBufferBeginInfo infoCmd{};
    infoCmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    VK_CHECK_RESULT(vkBeginCommandBuffer(draw_cmd_buffers_[current_buffer_], &infoCmd));
}

void VulkanDriver::end_frame()
{
    vkEndCommandBuffer(draw_cmd_buffers_[current_buffer_]);
}

VulkanRenderPass* VulkanDriver::create_render_pass(const RenderPassCreation& render_pass_creation) {
    VulkanRenderPass* render_pass = ocarina::new_with_allocator<VulkanRenderPass>(vulkan_device_, render_pass_creation);
    render_passes_.push_back(render_pass);
    return render_pass;
}

void VulkanDriver::destroy_render_pass(VulkanRenderPass* render_pass) {
    auto it = std::find(render_passes_.begin(), render_passes_.end(), render_pass);
    if (it != render_passes_.end()) {
        render_passes_.erase(it);
    }
    ocarina::delete_with_allocator<VulkanRenderPass>(render_pass);
}

VkResult VulkanDriver::copy_buffer(VulkanBuffer* src, VulkanBuffer* dst)
{
    VkCommandBuffer cmd_buffer = begin_one_time_command_buffer();

    VkBufferCopy buffer_copy{};

    buffer_copy.size = src->size();


    vkCmdCopyBuffer(cmd_buffer, src->buffer_handle(), dst->buffer_handle(), 1, &buffer_copy);

    end_one_time_command_buffer(cmd_buffer);

    return VK_SUCCESS;
}

VkResult VulkanDriver::copy_buffer(VulkanBuffer* src, VkBuffer dst)
{
    VkCommandBuffer cmd_buffer = begin_one_time_command_buffer();

    VkBufferCopy buffer_copy{};

    buffer_copy.size = src->size();

    vkCmdCopyBuffer(cmd_buffer, src->buffer_handle(), dst, 1, &buffer_copy);

    end_one_time_command_buffer(cmd_buffer);

    return VK_SUCCESS;
}

VkResult VulkanDriver::copy_image(VulkanBuffer* src, VulkanTexture* dst)
{
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = {0, 0, 0};
    region.imageExtent = {dst->resolution().x, dst->resolution().y, 1};

    VkImage image = reinterpret_cast<VkImage>(dst->tex_handle());

    VkCommandBuffer command_buffer = begin_one_time_command_buffer();
    vkCmdCopyBufferToImage(
        command_buffer,
        src->buffer_handle(),
        image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region);

    end_one_time_command_buffer(command_buffer);

    return VK_SUCCESS;
}

void VulkanDriver::set_vertex_buffer(const VulkanVertexStreamBinding& vertex_stream) {
    VkCommandBuffer current_buffer = get_current_command_buffer();
    vkCmdBindVertexBuffers(current_buffer, 0, vertex_stream.buffers_.size(), vertex_stream.buffers_.data(), vertex_stream.offsets_.data());
}

void VulkanDriver::draw_triangles(VulkanIndexBuffer* index_buffer) {
    VkCommandBuffer current_buffer = get_current_command_buffer();
    vkCmdBindIndexBuffer(current_buffer, index_buffer->buffer_handle(), 0, index_buffer->is_16_bit() ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(current_buffer, index_buffer->get_index_count(), 1, 0, 0, 0);
}

void VulkanDriver::push_constants(VkPipelineLayout pipeline_layout, void *data, uint32_t size, uint32_t offset) {
    VkCommandBuffer current_buffer = get_current_command_buffer();
    vkCmdPushConstants(current_buffer, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, offset, size, data);
}

void VulkanDriver::add_global_descriptor_set(uint64_t name_id, VulkanDescriptorSet *descriptor_set) {
    auto it = global_descriptor_sets.find(name_id);
    if (it != global_descriptor_sets.end()) {
        //now allow multiple add global descriptor set
        if (descriptor_set == it->second) {
            return;
        }
        ocarina::delete_with_allocator<VulkanDescriptorSet>(it->second);
    }
    global_descriptor_sets[name_id] = descriptor_set;
}

void VulkanDriver::bind_descriptor_sets(VulkanDescriptorSet **descriptor_sets, uint32_t descriptor_sets_num, VkPipelineLayout pipeline_layout) {
    VkCommandBuffer current_buffer = get_current_command_buffer();
    std::array<VkDescriptorSet, MAX_DESCRIPTOR_SETS_PER_SHADER> descriptor_set_handles = {VK_NULL_HANDLE};
    uint32_t first_set = -1;
    for (uint32_t i = 0; i < descriptor_sets_num; ++i) {
        if (descriptor_sets[i] == nullptr) {
            continue;
        }
        if (first_set == -1) {
            first_set = descriptor_sets[i]->get_layout()->get_descriptor_set_index();
        }
        descriptor_set_handles[i] = descriptor_sets[i]->descriptor_set();
    }
    vkCmdBindDescriptorSets(current_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, first_set, descriptor_sets_num, descriptor_set_handles.data(), 0, nullptr);
}

std::array<DescriptorSetLayout *, MAX_DESCRIPTOR_SETS_PER_SHADER> VulkanDriver::create_descriptor_set_layout(VulkanShader *shaders[], uint32_t shaders_count) {
    return vulkan_descriptor_manager->create_descriptor_set_layout(shaders, shaders_count);
}

VkPipelineLayout VulkanDriver::get_pipeline_layout(VkDescriptorSetLayout *descriptset_layouts, uint8_t descriptset_layouts_count, uint32_t push_constant_size) {
    return vulkan_pipeline_manager->get_pipeline_layout(vulkan_device_, descriptset_layouts, descriptset_layouts_count, push_constant_size);
}

}// namespace ocarina


