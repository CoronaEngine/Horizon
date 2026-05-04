//
// Created by Zero on 06/08/2022.
//

#pragma once

#include "core/stl.h"
#include "core/concepts.h"
#include "rhi/descriptor_set.h"
#include <vulkan/vulkan.h>
#include "vulkan_shader.h"

namespace ocarina {

class VulkanDevice;
class VulkanShader;
class VulkanBuffer;
class VulkanDescriptorSet;
class VulkanDescriptorSetWriter;

class VulkanDescriptor
{
public:
    virtual ~VulkanDescriptor() = default;
    uint32_t binding = 0;
    std::string name_;
    bool is_buffer_ = false;
};

class VulkanDescriptorBuffer : public VulkanDescriptor {
public:
    VulkanDescriptorBuffer()
    {
        is_buffer_ = true;
    }
    //VkDescriptorBufferInfo buffer_info = {};
    VulkanBuffer *buffer_ = nullptr;
    
};

class VulkanDescriptorImage : public VulkanDescriptor{
public:
    std::string default_sampler_name_;
};

class VulkanDescriptorSampler : public VulkanDescriptor {
};

class VulkanDescriptorPushConstants : public VulkanDescriptor {
};

class VulkanDescriptorSetLayout : public DescriptorSetLayout {
    static constexpr uint8_t MAX_BINDINGS = 16;
public:
    VulkanDescriptorSetLayout(VulkanDevice* device, uint8_t descriptor_set_index);
    ~VulkanDescriptorSetLayout() override;
    void add_binding(const char* name,
        uint32_t binding,
        VkDescriptorType descriptor_type,
        VkShaderStageFlags stage_flags,
        uint32_t size,
        uint32_t count = 1);


    bool build_layout();

    DescriptorCount get_descriptor_count() const noexcept {
        return descriptor_count_;
    }

    OC_MAKE_MEMBER_GETTER(layout, );
    //OC_MAKE_MEMBER_GETTER(descriptor_pool, );
    void set_is_global_ubo(bool is_global) {
        is_global_ubo_ = is_global;
    }
    //OC_MAKE_MEMBER_GETTER(pipeline_layout, );

    //void set_pipeline_layout(VkPipelineLayout pipeline_layout) {
    //    pipeline_layout_ = pipeline_layout;
    //}

    DescriptorSet* allocate_descriptor_set() override;
    void free_descriptor_set(VkDescriptorSet descriptor_set);
    VulkanShaderVariableBinding get_binding(uint64_t binding);
    size_t get_bindings_count() const {
        return bindings_.size();
    }

    bool free_descriptor_set() const {
        return free_descriptor_set_;
    }

    void set_free_descriptor_set(bool free_set) {
        free_descriptor_set_ = free_set;
    }

    uint8_t get_descriptor_set_index() const {
        return descriptor_set_index_;
    }

private:
    DescriptorCount descriptor_count_;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    //VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;

    std::unordered_map<uint64_t, uint32_t> name_to_bindings_;
    std::unordered_map<uint64_t, VulkanShaderVariableBinding> bindings_;

    VulkanDevice* device_ = nullptr;

    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    bool layout_built_ = false;
    bool free_descriptor_set_ = false;  
    uint32_t allocated_sets_count_ = 0;
    uint32_t pool_max_sets_count_ = 1;
    uint8_t descriptor_set_index_ = 0;
};

class VulkanDescriptorSet : public DescriptorSet {
private:
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
    VulkanDescriptorSetLayout *layout_ = nullptr;
    VulkanDevice *device_ = nullptr;
    VulkanDescriptorSetWriter *writer_ = nullptr;

public:
    VulkanDescriptorSet(VulkanDevice *device, VulkanDescriptorSetLayout* layout, VkDescriptorSet descriptor_set);
    ~VulkanDescriptorSet() override;
    OC_MAKE_MEMBER_GETTER(descriptor_set, );
    OC_MAKE_MEMBER_GETTER(layout, );
    //void copy_descriptors(VulkanDescriptor *descriptor);
    void update_buffer(uint64_t name_id, void *data, uint32_t size) override;
    void update_texture(uint64_t name_id, Texture *texture) override;
    VulkanDescriptorSetLayout *get_layout() const {
        return layout_;
    }
};

struct DescriptorPoolCreation {
    uint32_t ubo;
    uint32_t srv;
    uint32_t uav;
    uint32_t samplers;

    bool operator==(DescriptorPoolCreation const &right) const noexcept {
        return ubo == right.ubo && srv == right.srv && uav == right.uav &&
               samplers == right.samplers;
    }

    DescriptorCount to_descriptor_count() const noexcept {
        DescriptorCount count;
        
        count.ubo = this->ubo;
        count.srv = this->srv;
        count.uav = this->uav;
        count.samplers = this->samplers;
        return count;
    }
};

class VulkanDescriptorPool : public concepts::Noncopyable
{
public:
    VulkanDescriptorPool(const DescriptorPoolCreation &creation, VulkanDevice* device);
    ~VulkanDescriptorPool();
    VkDescriptorSet get_descriptor_set(VkDescriptorSetLayout layout);
    OC_MAKE_MEMBER_GETTER(descriptor_pool, );
    OC_MAKE_MEMBER_GETTER(descriptor_pool_creation, );

    bool can_allocate(const DescriptorCount &count) const
    {
        return descriptor_pool_creation_.to_descriptor_count() == count;
    }
private :
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VulkanDevice *device_ = nullptr;
    std::map<VkDescriptorSetLayout, VkDescriptorSet> descriptor_sets;
    DescriptorPoolCreation descriptor_pool_creation_;
};


class VulkanDescriptorManager : public concepts::Noncopyable {
public:
    VulkanDescriptorManager(VulkanDevice *device) : device_(device) {
    }
    ~VulkanDescriptorManager(){};

    //VkDescriptorSet get_descriptor_set(const VulkanDescriptorSetLayout &layout, VulkanDevice *device);
    void clear();

    std::array<DescriptorSetLayout*, MAX_DESCRIPTOR_SETS_PER_SHADER> create_descriptor_set_layout(VulkanShader **shaders, uint32_t shaders_count);

    struct DescriptorLayoutKey {
        std::vector<VulkanShaderVariableBinding> bindings;

        // Sort bindings for deterministic comparison
        void Normalize() {
            std::sort(bindings.begin(), bindings.end(), [](const auto &a, const auto &b) {
                return a.binding < b.binding;
            });
        }

        bool operator==(const DescriptorLayoutKey &other) const {
            //return bindings == other.bindings;
            if (bindings.size() != other.bindings.size()) {
                return false;
            }

            for (size_t i = 0; i < bindings.size(); ++i) {
                if (bindings[i].binding != other.bindings[i].binding ||
                    bindings[i].descriptor_set != other.bindings[i].descriptor_set ||
                    bindings[i].count != other.bindings[i].count ||
                    bindings[i].shader_stage != other.bindings[i].shader_stage ||
                    bindings[i].size != other.bindings[i].size ||
                    bindings[i].type != other.bindings[i].type) {
                    return false;
                }
            }

            return true;
        }
    };

    struct HashDescriptorLayoutKeyFunction {
        uint64_t operator()(const DescriptorLayoutKey &key) const {
            std::size_t h = 0;
            for (const auto &b : key.bindings) {
                h ^= std::hash<uint64_t>()(b.binding) ^
                     std::hash<uint64_t>()(b.descriptor_set) ^
                     std::hash<uint64_t>()(b.count) ^
                     std::hash<uint64_t>()(b.shader_stage) ^
                     std::hash<uint64_t>()(b.size) ^
                     std::hash<uint64_t>()(b.type);
            }
            return h;
        }
    };

private:
    VulkanDevice *device_ = nullptr;
    //std::unordered_map<DescriptorLayoutKey, VulkanDescriptorSetLayout *, HashDescriptorLayoutKeyFunction> descriptor_set_layouts_;
    std::array<DescriptorSetLayout *, MAX_DESCRIPTOR_SETS_PER_SHADER> descriptor_set_layouts_ = {};
    VulkanDescriptorSetLayout *global_descriptor_set_layouts_ = nullptr;
};

}// namespace ocarina