//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "rhi/device.h"
#include <vulkan/vulkan.h>
#include "vulkan_instance.h"
#include "vulkan_swapchain.h"

namespace ocarina {
class VulkanBuffer;
class VulkanDevice : public Device::Impl {
private:
    /** @brief Physical device representation */
    VkPhysicalDevice physicalDevice_;
    /** @brief Logical device representation (application's view of the device) */
    VkDevice logicalDevice_;
    /** @brief Properties of the physical device including limits that the application can check against */
    VkPhysicalDeviceProperties m_deviceProperties{};
    /** @brief Features of the physical device that an application can use to check if a feature is supported */
    VkPhysicalDeviceFeatures m_deviceFeatures{};
    /** @brief Features that have been enabled for use on the physical device */
    VkPhysicalDeviceFeatures m_enabledFeatures{};
    /** @brief Memory types and heaps of the physical device */
    VkPhysicalDeviceMemoryProperties m_deviceMemoryProperties{};
    /** @brief Queue family properties of the physical device */
    std::vector<VkQueueFamilyProperties> m_queueFamilyProperties;
    /** @brief List of extensions supported by the device */
    std::vector<std::string> m_supportedExtensions;
    std::vector<const char *> m_enableExtensions;

public:
    explicit VulkanDevice(RHIContext *context, const ocarina::InstanceCreation &instance_creation);
    ~VulkanDevice();
    void init_hardware_info();

    [[nodiscard]] handle_ty create_buffer(size_t size, const string &desc) noexcept override;
    void destroy_buffer(handle_ty handle) noexcept override;
    [[nodiscard]] handle_ty create_texture(uint3 res, PixelStorage pixel_storage,
                                           uint level_num,
                                           const string &desc) noexcept override;
    [[nodiscard]] handle_ty create_texture(Image *image, const TextureViewCreation &texture_view) noexcept override;
    void destroy_texture(handle_ty handle) noexcept override;
    [[nodiscard]] handle_ty create_shader(const Function &function) noexcept override;
    [[nodiscard]] handle_ty create_shader_from_file(const std::string &file_name, ShaderType shader_type, const std::set<string>& options) noexcept override;
    void destroy_shader(handle_ty handle) noexcept override;
    [[nodiscard]] handle_ty create_accel(AccelUsageTag usage_tag) noexcept override;
    void destroy_accel(handle_ty handle) noexcept override;
    [[nodiscard]] handle_ty create_stream() noexcept override;
    void destroy_stream(handle_ty handle) noexcept override;
    [[nodiscard]] handle_ty create_mesh(const MeshParams &params) noexcept override;
    void destroy_mesh(handle_ty handle) noexcept override;
    [[nodiscard]] handle_ty create_bindless_array() noexcept override;
    void destroy_bindless_array(handle_ty handle) noexcept override;
    void init_rtx() noexcept override {  }
    [[nodiscard]] CommandVisitor *command_visitor() noexcept override;
    void shutdown();
    void submit_frame() noexcept override;
    VertexBuffer* create_vertex_buffer() noexcept override;
    IndexBuffer* create_index_buffer(const void *initial_data, uint32_t indices_count, bool bit16) noexcept override;
    VulkanBuffer *create_vulkan_buffer(VkBufferUsageFlags usage_flags, VkMemoryPropertyFlags memory_property_flags, VkDeviceSize size, const void *data = nullptr);
    void begin_frame() noexcept override;
    void end_frame() noexcept override;
    RHIRenderPass *create_render_pass(const RenderPassCreation &render_pass_creation) noexcept override;
    void destroy_render_pass(RHIRenderPass *render_pass) noexcept override;
    std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER> create_descriptor_set_layout(void **shaders, uint32_t shaders_count) noexcept override;    
    //DescriptorSetWriter *create_descriptor_set_writer(DescriptorSet *descriptor_set, void** shaders, uint32_t shaders_count) noexcept override;
    void bind_pipeline(const handle_ty pipeline) noexcept override;
    RHIPipeline *get_pipeline(const PipelineState &pipeline_state, RHIRenderPass *render_pass) noexcept override;
    DescriptorSet *get_global_descriptor_set(const string &name) noexcept override;
    void bind_descriptor_sets(DescriptorSet **descriptor_set, uint32_t descriptor_sets_num, RHIPipeline* pipeline) noexcept override;

    OC_MAKE_MEMBER_GETTER(logicalDevice, );

    OC_MAKE_MEMBER_GETTER(physicalDevice, );

    //uint32_t get_queue_family_index
    VulkanSwapchain* get_swapchain()
    {
        return &m_swapChain;
    }

    VkDevice operator()()
    {
        return logicalDevice_;
    }

    uint32_t get_memory_type(uint32_t typeBits, VkMemoryPropertyFlags properties, VkBool32 *memTypeFound = nullptr) const;
    uint32_t get_queue_family_index(QueueType queue_type) const
    {
        return queueFamilyIndices_[(uint)queue_type];
    }

    VkInstance get_instance() const { return m_instance.instance(); }
 private:
    void init_vulkan();
    void create_logical_device();
    void get_enable_features();
    void get_enable_extentions();

    VulkanInstance m_instance;
    VulkanSwapchain m_swapChain;
    uint64_t m_windowHandle = InvalidUI64;

    uint32_t queueFamilyIndices_[(uint32_t)QueueType::NumQueueType];
    //uint32_t queueFamilyIndexPerQueue_[(uint32_t)QueueType::NumQueueType];
    std::vector<VkQueueFamilyProperties> queueFamilyProperties_;
    uint32_t queueFamilyCount_ = 0;
    uint32_t getQueueFamilyIndex(uint32_t queueFlags) const;
};
}// namespace ocarina
