#pragma once
#include "core/header.h"
#include "core/concepts.h"
#include "core/stl.h"
#include "rhi/graphics_descriptions.h"
#include <vulkan/vulkan.h>
#include "vulkan_pipeline.h"

namespace ocarina {
class VulkanPipelineManager;
class VulkanPipeline;
class RHIContext;
class VulkanDevice;
class VulkanShaderManager;
class VulkanDescriptorManager;
class VulkanShader;
struct InstanceCreation;
struct PipelineState;
class VulkanRenderPass;
class VulkanVertexBuffer;
class VulkanIndexBuffer;
struct VulkanVertexStreamBinding;
class VulkanDescriptorSetLayout;
class DescriptorSetLayout;
class VulkanDescriptorSet;
class VulkanTexture;

class VulkanDriver : public concepts::Noncopyable {
public:
    ~VulkanDriver();
    static VulkanDriver& instance()
    {
        static VulkanDriver s_instance;
        return s_instance;
    }
    VulkanDevice *create_device(RHIContext *context, const InstanceCreation &instance_creation);
    VulkanDevice *get_device() const { return vulkan_device_; }
    void bind_pipeline(const VulkanPipeline &pipeline);
    void terminate();
    void submit_frame();
    inline VkDevice device() const;
    VulkanShader *create_shader(ShaderType shader_type,
                                const std::string &filename,
                                const std::set<std::string> &options,
                                const std::string &entry_point);
    VulkanShader* get_shader(handle_ty shader) const;
    OC_MAKE_MEMBER_GETTER(current_buffer, )
    VkCommandBuffer get_current_command_buffer() const
    {
        return draw_cmd_buffers_[current_buffer_];
    }

    VkCommandBuffer begin_one_time_command_buffer();
    void end_one_time_command_buffer(VkCommandBuffer cmd);

    VulkanPipeline* get_pipeline(const PipelineState &pipeline_state, VkRenderPass render_pass);

    void begin_frame();
    void end_frame();

    std::array<DescriptorSetLayout *, MAX_DESCRIPTOR_SETS_PER_SHADER> create_descriptor_set_layout(VulkanShader *shaders[], uint32_t shaders_count);
    VkPipelineLayout get_pipeline_layout(VkDescriptorSetLayout *descriptset_layouts, uint8_t descriptset_layouts_count, uint32_t push_constant_size);

    VulkanRenderPass* create_render_pass(const RenderPassCreation& render_pass_creation);
    void destroy_render_pass(VulkanRenderPass* render_pass);

    VkFramebuffer get_frame_buffer(uint32_t index)
    {
        return frame_buffers[index];
    }

    VkResult copy_buffer(VulkanBuffer* src, VulkanBuffer* dst);
    VkResult copy_buffer(VulkanBuffer *src, VkBuffer dst);
    VkResult copy_image(VulkanBuffer *src, VulkanTexture *dst);

    void set_vertex_buffer(const VulkanVertexStreamBinding& vertex_stream);
    void draw_triangles(VulkanIndexBuffer* index_buffer);

    void push_constants(VkPipelineLayout pipeline, void *data, uint32_t size, uint32_t offset);

    void add_global_descriptor_set(uint64_t name_id, VulkanDescriptorSet *descriptor_set);

    VulkanDescriptorSet *get_global_descriptor_set(uint64_t name_id) {
        auto it = global_descriptor_sets.find(name_id);
        if (it != global_descriptor_sets.end()) {
            return it->second;
        }
        return nullptr;
    }

    VulkanDescriptorSet *get_global_descriptor_set(const std::string &name) {
        uint64_t name_id = hash64(name);
        return get_global_descriptor_set(name_id);
    }

    void bind_descriptor_sets(VulkanDescriptorSet **descriptor_sets, uint32_t descriptor_sets_num, VkPipelineLayout pipeline_layout);

    VkRenderPass get_framebuffer_render_pass() const {
        return renderpass_framebuffer;
    }

    void flush_command_buffer(VkCommandBuffer cmd);

private:
    void setup_frame_buffer();
    //void setup_depth_stencil(uint32_t width, uint32_t height);
    void release_frame_buffer();
    void create_command_pool();
    void release_command_pool();
    void create_command_buffers();
    void release_command_buffers();
    void initialize();
    void window_resize();
    

private:
    VulkanDriver();
    VulkanDevice *vulkan_device_;
    std::unique_ptr<VulkanPipelineManager> vulkan_pipeline_manager;
    std::unique_ptr<VulkanShaderManager> vulkan_shader_manager;
    std::unique_ptr<VulkanDescriptorManager> vulkan_descriptor_manager;

    VkQueue graphics_queue{VK_NULL_HANDLE};
    VkQueue present_queue{VK_NULL_HANDLE};
    // Contains command buffers and semaphores to be presented to the queue
    VkSubmitInfo submit_info;

    /** @brief Default command pool for the graphics queue family index */
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    // Command buffers used for rendering
    std::vector<VkCommandBuffer> draw_cmd_buffers_;
    // Active frame buffer index
    uint32_t current_buffer_ = 0;

    //struct {
    //    VkImage image;
    //    VkDeviceMemory mem;
    //    VkImageView view;
    //} depth_stencil;

    // Synchronization semaphores
    struct {
        // Swap chain image presentation
        VkSemaphore presentComplete;
        // Command buffer submission and execution
        VkSemaphore renderComplete;
    } semaphores;

    VkRenderPass renderpass_framebuffer{VK_NULL_HANDLE};
    //VkFormat depth_stencil_format;
    std::vector<VkFramebuffer> frame_buffers;

    std::unordered_map<uint64_t, VulkanDescriptorSet *> global_descriptor_sets;
    std::vector<VulkanRenderPass *> render_passes_;
};
}// namespace ocarina