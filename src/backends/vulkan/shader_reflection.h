//
// Created by Zero on 06/08/2022.
//

#pragma once

#include "core/stl.h"
#include "core/concepts.h"
#include <vulkan/vulkan.h>
#include "rhi/graphics_descriptions.h"

namespace ocarina {


struct ShaderReflection{
    enum ResourceType {
        ConstantBuffer,
        Texture,
        SRV,
        UAV,
        Sampler,
        InputAttachment,
        Atomic
    };

    int thread_group_size[3] = {0, 0, 0};

    struct ShaderResource {
        ShaderResource() = default;

        ShaderResource(const ShaderResource &other) {
            shader_type = other.shader_type;
            register_ = other.register_;
            register_count = other.register_count;
            parameter_type = other.parameter_type;
            binding = other.binding;
            descriptor_set = other.descriptor_set;
            format = other.format;
            name = other.name;
            location = other.location;
            vertex_attribute_type = other.vertex_attribute_type;
            size = other.size;
        }

        ShaderResource &operator=(const ShaderResource &other) {
            shader_type = other.shader_type;
            register_ = other.register_;
            register_count = other.register_count;
            parameter_type = other.parameter_type;
            binding = other.binding;
            descriptor_set = other.descriptor_set;
            format = other.format;
            name = other.name;
            location = other.location;
            vertex_attribute_type = other.vertex_attribute_type;
            size = other.size;
            return *this;
        }

        ShaderResource(ShaderResource &&rvalue) noexcept
        {
            shader_type = rvalue.shader_type;
            register_ = rvalue.register_;
            register_count = rvalue.register_count;
            parameter_type = rvalue.parameter_type;
            binding = rvalue.binding;
            descriptor_set = rvalue.descriptor_set;
            format = rvalue.format;
            name = std::move(rvalue.name);
            location = rvalue.location;
            vertex_attribute_type = rvalue.vertex_attribute_type;
            size = rvalue.size;
        }

        ShaderResource& operator=(ShaderResource&& rvalue) noexcept
        {
            shader_type = rvalue.shader_type;
            register_ = rvalue.register_;
            register_count = rvalue.register_count;
            parameter_type = rvalue.parameter_type;
            binding = rvalue.binding;
            descriptor_set = rvalue.descriptor_set;
            format = rvalue.format;
            name = std::move(rvalue.name);
            location = rvalue.location;
            vertex_attribute_type = rvalue.vertex_attribute_type;
            size = rvalue.size;
            return *this;
        }

        uint32_t shader_type : 4 = 0;
        uint32_t register_ : 4 = 0;
        uint32_t register_count : 4 = 0;
        uint32_t location : 4 = 0;
        uint32_t offset : 5 = 0;
        uint32_t parameter_type : 3 = 0;
        uint32_t binding : 4 = 0;
        uint32_t descriptor_set : 4 = 0;
        uint32_t size = 0;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VertexAttributeType::Enum vertex_attribute_type = VertexAttributeType::Enum::Count;

        std::string name;

        // Keep inline with m_Register.
        static const uint32_t s_max_register_size = 1 << 15;
    };

    struct ShaderVariable {
        std::string name;
        uint32_t offset = 0;
        uint32_t size = 0;
        uint32_t register_ = 0;
        uint32_t register_count = 0;
        uint32_t descriptor_set = 0;
        uint32_t binding_ = 0;
        ShaderVariableType variable_type = ShaderVariableType::FLOAT;
        ShaderVariable() = default;
        ShaderVariable(const ShaderVariable& other)
        {
            offset = other.offset;
            register_ = other.register_;
            register_count = other.register_count;
            descriptor_set = other.descriptor_set;
            variable_type = other.variable_type;
            binding_ = other.binding_;
            name = other.name;
            size = other.size;
        }

        ShaderVariable &operator=(const ShaderVariable &other) {
            offset = other.offset;
            register_ = other.register_;
            register_count = other.register_count;
            descriptor_set = other.descriptor_set;
            variable_type = other.variable_type;
            binding_ = other.binding_;
            name = other.name;
            size = other.size;
            return *this;
        }

        ShaderVariable(ShaderVariable &&rvalue) noexcept {
            offset = rvalue.offset;
            register_ = rvalue.register_;
            register_count = rvalue.register_count;
            descriptor_set = rvalue.descriptor_set;
            variable_type = rvalue.variable_type;
            binding_ = rvalue.binding_;
            name = std::move(rvalue.name);
            size = rvalue.size;
        }

        ShaderVariable &operator=(ShaderVariable &&rvalue) noexcept {
            offset = rvalue.offset;
            register_ = rvalue.register_;
            register_count = rvalue.register_count;
            descriptor_set = rvalue.descriptor_set;
            variable_type = rvalue.variable_type;
            binding_ = rvalue.binding_;
            name = std::move(rvalue.name);
            size = rvalue.size;
            return *this;
        }
    };

    struct UniformBuffer {
        std::string name;
        uint8_t binding = 0;
        uint8_t descriptor_set = 0;
        uint32_t size = 0;
        std::vector<ShaderVariable> shader_variables;
        UniformBuffer() = default;
        UniformBuffer(const UniformBuffer &other) {
            name = other.name;
            binding = other.binding;
            descriptor_set = other.descriptor_set;
            size = other.size;
            shader_variables = other.shader_variables;
        }

        UniformBuffer &operator=(const UniformBuffer &other) {
            name = other.name;
            binding = other.binding;
            descriptor_set = other.descriptor_set;
            size = other.size;
            shader_variables = other.shader_variables;
            return *this;
        }

        UniformBuffer(UniformBuffer &&rvalue) noexcept {
            name = std::move(rvalue.name);
            binding = rvalue.binding;
            descriptor_set = rvalue.descriptor_set;
            size = rvalue.size;
            shader_variables = std::move(rvalue.shader_variables);
        }

        UniformBuffer &operator=(UniformBuffer &&rvalue) noexcept {
            name = std::move(rvalue.name);
            binding = rvalue.binding;
            descriptor_set = rvalue.descriptor_set;
            size = rvalue.size;
            shader_variables = std::move(rvalue.shader_variables);
            return *this;
        }
    };

    std::vector<ShaderResource> shader_resources;
    std::vector<UniformBuffer> uniform_buffers;
    std::vector<UniformBuffer> push_constant_buffers;
    std::vector<ShaderResource> input_layouts;
    
};
}// namespace ocarina