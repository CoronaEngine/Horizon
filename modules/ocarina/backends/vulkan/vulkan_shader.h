//
// Created by Zero on 06/08/2022.
//

#pragma once

#include "core/stl.h"
#include "core/concepts.h"
#include "rhi/resources/shader.h"
#include <vulkan/vulkan.h>
#include "shader_reflection.h"
namespace ocarina {

class VulkanDevice;
class VulkanDescriptorSetLayout;
struct ShaderKey;

enum class DescriptorSetIndex : uint8_t {
    GLOBAL_SET = 0,
    MATERIAL_SET = 1,
    PER_OBJECT_SET = 2,

    MAX_DESCRIPTOR_SET = MAX_DESCRIPTOR_SETS_PER_SHADER
};

struct VulkanShaderVariableBinding
{
    
    char name[256] = { 0 };
    uint8_t binding = 0;
    uint8_t descriptor_set = 0;
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    VkShaderStageFlags shader_stage = VK_SHADER_STAGE_VERTEX_BIT;
    uint32_t count = 1;
    uint32_t size = 0;// size in bytes, only used for constant buffer
    VulkanShaderVariableBinding() = default;
    VulkanShaderVariableBinding(const VulkanShaderVariableBinding& other)
    {
        binding = other.binding;
        descriptor_set = other.descriptor_set;
        type = other.type;
        count = other.count;
        shader_stage = other.shader_stage;
        size = other.size;
        shader_variables_ = other.shader_variables_;
        strcpy(name, other.name);
    }

    VulkanShaderVariableBinding& operator = (const VulkanShaderVariableBinding& other)
    {
        binding = other.binding;
        descriptor_set = other.descriptor_set;
        type = other.type;
        count = other.count;
        shader_stage = other.shader_stage;
        size = other.size;
        strcpy(name, other.name);
        shader_variables_ = other.shader_variables_;
        return *this;
    }

    std::vector<ShaderReflection::ShaderVariable> shader_variables_;
};

class VulkanShader : public Shader<>::Impl {
public:
    
private:
    VkShaderModule shader_module_ = VK_NULL_HANDLE;
    std::string entry_;
    VulkanDevice *device_ = nullptr;
    VkShaderStageFlagBits stage_;
    std::vector< VulkanShaderVariableBinding> variables_;
    std::vector<VertexAttribute> vertex_attributes_;  //only exist in vertex shader
    static bool HLSLToSPRIV(std::span<char> hlsl, VkShaderStageFlagBits stage, const std::string_view &entryPoint, bool outputSymbols, std::vector<uint32_t> &outSpriv, std::string &errorLog);
    void get_shader_variables(const ShaderReflection &reflection);
    void get_vertex_attributes(const ShaderReflection &reflection);
public:
    VulkanShader(VulkanDevice *device, std::span<uint32_t> shaderCode, const std::string_view &entryPoint, VkShaderStageFlagBits stage);
    ~VulkanShader() override;

    size_t get_vertex_attribute_count() {
        return vertex_attributes_.size();
    }

    VertexAttribute get_vertex_attribute(uint32_t index) const {
        if (index < vertex_attributes_.size()) {
            return vertex_attributes_[index];
        }
        return VertexAttribute();
    }

    OC_MAKE_MEMBER_GETTER(shader_module, );
    OC_MAKE_MEMBER_GETTER(stage, );

    const char* get_entry_point() const
    {
        return entry_.c_str();
    }
    void launch(handle_ty stream, ShaderDispatchCommand *cmd) noexcept override {}
    static VulkanShader *create(Device::Impl *device,
                                ShaderType shader_type, 
                                std::span<uint32_t> shader_code, 
                                const std::string_view &entry_point);

    static VulkanShader *create_from_HLSL(Device::Impl *device, 
        ShaderType shader_type, 
        const std::string& filename, 
        const std::string& entry_point);

    static VkShaderStageFlagBits convert_vulkan_shader_stage(ShaderType shader_type)
    {
        switch (shader_type) {
            case ocarina::ShaderType::VertexShader:
                return VK_SHADER_STAGE_VERTEX_BIT;
                break;
            case ocarina::ShaderType::PixelShader:
                return VK_SHADER_STAGE_FRAGMENT_BIT;
                break;
            case ocarina::ShaderType::GeometryShader:
                return VK_SHADER_STAGE_GEOMETRY_BIT;
                break;
            case ocarina::ShaderType::ComputeShader:
                return VK_SHADER_STAGE_COMPUTE_BIT;
                break;
            case ocarina::ShaderType::MeshShader:
                return VK_SHADER_STAGE_MESH_BIT_EXT;
                break;
            default:
                return VK_SHADER_STAGE_VERTEX_BIT;
                break;
        }
    }
    
    uint32_t get_shader_variables_count() const
    {
        return variables_.size();
    }
    const VulkanShaderVariableBinding& get_shader_variable(size_t index)
    {
        return variables_[index];
    }
};

struct ShaderKey {
    ShaderType shader_type = ShaderType::VertexShader;
    std::string shader_code;
    std::string entry_point;
    std::set<std::string> options;

    bool operator==(const ShaderKey &other) const {
        return shader_type == other.shader_type && entry_point == other.entry_point && shader_code == other.shader_code && options == other.options;
    }
};

struct VulkanShaderEntry
{
    VkShaderModule shader_module = VK_NULL_HANDLE;
    VkShaderStageFlagBits stage;
    const char *entry = nullptr;
    bool is_valid() const
    {
        return shader_module != VK_NULL_HANDLE;
    }
};

struct HashShaderKeyFunction {
    uint64_t operator()(const ShaderKey &shader_key) const {
        std::size_t res = 0;
        hash_combine(res, *((uint64_t *)&shader_key.shader_type));
        hash_combine(res, std::hash<std::string>()(shader_key.shader_code));
        hash_combine(res, std::hash<std::string>()(shader_key.entry_point));
        for (auto iter : shader_key.options) {
            hash_combine(res, std::hash<std::string>()(iter));
        }
        return res;
    }
};

class VulkanShaderManager : concepts::Noncopyable {
public:
    VulkanShader* get_or_create_from_HLSL(VulkanDevice *device,
                                      ShaderType shader_type,
                                      const std::string &filename,
                                      const std::set<std::string> &options,
                                      const std::string &entry_point);

    VulkanShaderEntry get_shader_entry(handle_ty shader_handle) const;
    void clear(VulkanDevice *device);
    VulkanShader* get_shader(handle_ty shader_handle) const
    {
        auto it = shaders_.find(shader_handle);
        if (it != shaders_.end())
        {
            return it->second;
        }

        return nullptr;
    }
private:
    std::unordered_map<ShaderKey, VulkanShader*, HashShaderKeyFunction> vulkan_shaders_;
    std::unordered_map<handle_ty, VulkanShader*> shaders_;
    std::map<handle_ty, VulkanShaderEntry> vulkan_shader_entries_;
};
}// namespace ocarina